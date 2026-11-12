#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Агент PC Monitor для ESP32-S3.

Отдаёт по локальной сети состояние Windows-ПК (CPU, ОЗУ, GPU, диск, сеть,
Wi-Fi, громкость), измеряет скорость интернета и принимает команды
регулятора громкости. Экран находит агент автоматически по UDP-маяку.

Запуск:
    python sysmon_agent.py

Зависимости:
    pip install -r requirements.txt
"""

from __future__ import annotations

import argparse
import ctypes
import ipaddress
import json
import os
import platform
import queue
import re
import socket
import statistics
import subprocess
import sys
import threading
import time
from ctypes import wintypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import psutil
import requests

APP_NAME = "PC Monitor Agent"
VERSION = "1.3.0"
UDP_MAGIC = b"PCMON?"
BEACON_PREFIX = "PCMON1"

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)

DEFAULT_DOWN_URLS = ",".join(
    [
        "https://speed.cloudflare.com/__down?bytes=25000000",
        "https://proof.ovh.net/files/10Mb.dat",
        "https://speedtest.tele2.net/10MB.zip",
    ]
)
DEFAULT_UP_URLS = ",".join(
    [
        "https://speed.cloudflare.com/__up",
        "https://httpbin.org/post",
        "https://postman-echo.com/post",
    ]
)
DEFAULT_PING_URL = "https://speed.cloudflare.com/__down?bytes=0"


SKIN_NAMES = ["Приборы", "Неон", "Минимализм", "Ретро-радио", "Часы", "Терминал", "Плитки", "Лампы",
              "Импульс", "Шина", "HUD", "Сканлайн"]

# Команда автозапуска задаётся программой-оболочкой (pcmon_app.pyw); без неё автозапуск недоступен.
AUTOSTART_CMD = ""
_RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
_RUN_NAME = "PCMonitor"


def autostart_state() -> dict:
    out = {"ok": True, "available": bool(AUTOSTART_CMD), "autostart": False}
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, _RUN_KEY) as k:
            winreg.QueryValueEx(k, _RUN_NAME)
            out["autostart"] = True
    except Exception:
        pass
    return out


def autostart_set(on: bool) -> None:
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, _RUN_KEY, 0, winreg.KEY_SET_VALUE) as k:
            if on and AUTOSTART_CMD:
                winreg.SetValueEx(k, _RUN_NAME, 0, winreg.REG_SZ, AUTOSTART_CMD)
            elif not on:
                try:
                    winreg.DeleteValue(k, _RUN_NAME)
                except FileNotFoundError:
                    pass
    except Exception:
        pass


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def r(value, digits: int = 1):
    try:
        if value is None:
            return 0
        return round(float(value), digits)
    except Exception:
        return 0


def windows_name() -> str:
    try:
        release = platform.release()
        build = int(platform.version().split(".")[-1])
        if release == "10" and build >= 22000:
            release = "11"
        edition = ""
        if hasattr(platform, "win32_edition"):
            edition = platform.win32_edition()
        return f"Windows {release} {edition}".strip()
    except Exception:
        return platform.platform()


# --------------------------------------------------------------------------- Wi-Fi
class DOT11_SSID(ctypes.Structure):
    _fields_ = [("uSSIDLength", ctypes.c_ulong), ("ucSSID", ctypes.c_ubyte * 32)]


class WLAN_ASSOCIATION_ATTRIBUTES(ctypes.Structure):
    _fields_ = [
        ("dot11Ssid", DOT11_SSID),
        ("dot11BssType", ctypes.c_uint),
        ("dot11Bssid", ctypes.c_ubyte * 6),
        ("dot11PhyType", ctypes.c_uint),
        ("uDot11PhyIndex", ctypes.c_ulong),
        ("wlanSignalQuality", ctypes.c_ulong),
        ("ulRxRate", ctypes.c_ulong),
        ("ulTxRate", ctypes.c_ulong),
    ]


class WLAN_SECURITY_ATTRIBUTES(ctypes.Structure):
    _fields_ = [
        ("bSecurityEnabled", wintypes.BOOL),
        ("bOneXEnabled", wintypes.BOOL),
        ("dot11AuthAlgorithm", ctypes.c_uint),
        ("dot11CipherAlgorithm", ctypes.c_uint),
    ]


class WLAN_CONNECTION_ATTRIBUTES(ctypes.Structure):
    _fields_ = [
        ("isState", ctypes.c_uint),
        ("wlanConnectionMode", ctypes.c_uint),
        ("strProfileName", wintypes.WCHAR * 256),
        ("wlanAssociationAttributes", WLAN_ASSOCIATION_ATTRIBUTES),
        ("wlanSecurityAttributes", WLAN_SECURITY_ATTRIBUTES),
    ]


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", ctypes.c_ubyte * 8),
    ]


class WLAN_INTERFACE_INFO(ctypes.Structure):
    _fields_ = [
        ("InterfaceGuid", GUID),
        ("strInterfaceDescription", wintypes.WCHAR * 256),
        ("isState", wintypes.DWORD),
    ]


class WLAN_INTERFACE_INFO_LIST(ctypes.Structure):
    _fields_ = [
        ("dwNumberOfItems", wintypes.DWORD),
        ("dwIndex", wintypes.DWORD),
        ("InterfaceInfo", WLAN_INTERFACE_INFO * 1),
    ]


class WifiReader:
    """Текущее Wi-Fi-подключение Windows через WLAN API (без netsh и локализации)."""

    OPCODE_CURRENT_CONNECTION = 7
    STATE_CONNECTED = 1

    def __init__(self) -> None:
        self.api = None
        self.handle = None
        self.error = ""
        try:
            api = ctypes.windll.wlanapi
            api.WlanOpenHandle.argtypes = [wintypes.DWORD, wintypes.LPVOID, ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.HANDLE)]
            api.WlanOpenHandle.restype = wintypes.DWORD
            api.WlanEnumInterfaces.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.POINTER(ctypes.POINTER(WLAN_INTERFACE_INFO_LIST))]
            api.WlanEnumInterfaces.restype = wintypes.DWORD
            api.WlanQueryInterface.argtypes = [
                wintypes.HANDLE,
                ctypes.POINTER(GUID),
                wintypes.DWORD,
                wintypes.LPVOID,
                ctypes.POINTER(wintypes.DWORD),
                ctypes.POINTER(ctypes.POINTER(WLAN_CONNECTION_ATTRIBUTES)),
                ctypes.POINTER(wintypes.DWORD),
            ]
            api.WlanQueryInterface.restype = wintypes.DWORD
            api.WlanFreeMemory.argtypes = [wintypes.LPVOID]
            api.WlanCloseHandle.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
            negotiated = wintypes.DWORD()
            handle = wintypes.HANDLE()
            if api.WlanOpenHandle(2, None, ctypes.byref(negotiated), ctypes.byref(handle)) != 0:
                raise OSError("WlanOpenHandle failed")
            self.api = api
            self.handle = handle
        except Exception as exc:  # pragma: no cover - зависит от системы
            self.error = str(exc)

    def read(self) -> dict:
        out = {"present": False, "ssid": "", "rssi": 0, "quality": 0, "link": 0}
        if not self.api:
            return out
        plist = ctypes.POINTER(WLAN_INTERFACE_INFO_LIST)()
        if self.api.WlanEnumInterfaces(self.handle, None, ctypes.byref(plist)) != 0:
            return out
        try:
            for i in range(plist.contents.dwNumberOfItems):
                info = plist.contents.InterfaceInfo[i]
                if info.isState != self.STATE_CONNECTED:
                    continue
                data = ctypes.POINTER(WLAN_CONNECTION_ATTRIBUTES)()
                size = wintypes.DWORD()
                opcode_type = wintypes.DWORD()
                res = self.api.WlanQueryInterface(
                    self.handle,
                    ctypes.byref(info.InterfaceGuid),
                    self.OPCODE_CURRENT_CONNECTION,
                    None,
                    ctypes.byref(size),
                    ctypes.byref(data),
                    ctypes.byref(opcode_type),
                )
                if res != 0 or not data:
                    continue
                try:
                    a = data.contents.wlanAssociationAttributes
                    length = min(int(a.dot11Ssid.uSSIDLength), 32)
                    ssid = bytes(a.dot11Ssid.ucSSID[:length]).decode("utf-8", "replace")
                    quality = int(clamp(a.wlanSignalQuality, 0, 100))
                    rate = max(int(a.ulRxRate), int(a.ulTxRate))
                    link = int(rate * 0.5 / 1000) if rate > 0 else 0
                    out.update(
                        {
                            "present": True,
                            "ssid": ssid,
                            "rssi": int((quality / 2) - 100),
                            "quality": quality,
                            "link": link,
                        }
                    )
                finally:
                    self.api.WlanFreeMemory(data)
                break
        finally:
            self.api.WlanFreeMemory(plist)
        return out


# --------------------------------------------------------------------------- громкость
class VolumeWorker(threading.Thread):
    """Отдельный поток для COM/WASAPI: команды приходят через очередь."""

    def __init__(self) -> None:
        super().__init__(daemon=True, name="volume")
        self.q: queue.Queue = queue.Queue()
        self.lock = threading.Lock()
        self.level = 0
        self.muted = False
        self.available = False
        self.error = ""
        self.vol = None

    def _init_com(self) -> None:
        try:
            ctypes.oledll.ole32.CoInitialize(None)
        except Exception:
            pass

    def _init_pycaw(self) -> None:
        from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume  # type: ignore
        from comtypes import CLSCTX_ALL  # type: ignore
        from ctypes import POINTER, cast

        speakers = AudioUtilities.GetSpeakers()
        if hasattr(speakers, "EndpointVolume") and speakers.EndpointVolume is not None:
            self.vol = speakers.EndpointVolume
        else:
            iface = speakers.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
            self.vol = cast(iface, POINTER(IAudioEndpointVolume))
        self._refresh_locked()
        self.available = True
        self.error = ""

    def _refresh_locked(self) -> None:
        level = int(round(self.vol.GetMasterVolumeLevelScalar() * 100))
        mute = bool(self.vol.GetMute())
        with self.lock:
            self.level = int(clamp(level, 0, 100))
            self.muted = mute

    def _apply(self, cmd: dict) -> None:
        if not self.available:
            self._init_pycaw()
        if "level" in cmd:
            self.vol.SetMasterVolumeLevelScalar(clamp(int(cmd["level"]), 0, 100) / 100.0, None)
        if "mute" in cmd:
            self.vol.SetMute(1 if cmd["mute"] else 0, None)
        self._refresh_locked()

    def run(self) -> None:
        self._init_com()
        try:
            self._init_pycaw()
        except Exception as exc:
            self.available = False
            self.error = f"pycaw: {exc}"
        last = 0.0
        while True:
            cmd = None
            try:
                cmd = self.q.get(timeout=0.25)
            except queue.Empty:
                pass
            if cmd:
                try:
                    self._apply(cmd)
                except Exception as exc:
                    self.available = False
                    self.error = f"volume: {exc}"
                    self.vol = None
                finally:
                    evt = cmd.get("event")
                    if evt:
                        evt.set()
            now = time.time()
            if self.available and now - last >= 0.5:
                last = now
                try:
                    self._refresh_locked()
                except Exception as exc:
                    self.available = False
                    self.error = f"volume: {exc}"
                    self.vol = None

    def state(self) -> dict:
        with self.lock:
            return {"level": self.level, "muted": self.muted, "available": self.available, "error": self.error}

    def _send(self, cmd: dict) -> dict:
        evt = threading.Event()
        cmd["event"] = evt
        self.q.put(cmd)
        evt.wait(1.5)
        return self.state()

    def set_level(self, level: int) -> dict:
        return self._send({"level": int(level)})

    def set_mute(self, mute: bool) -> dict:
        return self._send({"mute": bool(mute)})


# --------------------------------------------------------------------------- USB
class UsbLink(threading.Thread):
    """Связь с экраном по USB-кабелю: раз в секунду шлём кадр показателей одной
    JSON-строкой, обратно читаем команды — строки, начинающиеся с "@PCMON "."""

    # Встроенный USB-порт ESP32-S3 (USB Serial/JTAG)
    USB_IDS = {(0x303A, 0x1001)}

    def __init__(self, agent: "Agent", port: str = "") -> None:
        super().__init__(daemon=True, name="usb")
        self.agent = agent
        self.port_arg = port
        self.port = ""
        self.connected = False
        self.error = ""
        self.sent = 0
        self.hello: dict = {}
        self.hello_at = 0.0
        self.last_log = ""
        self.outbox: queue.Queue = queue.Queue()

    def status(self) -> dict:
        return {
            "port": self.port,
            "connected": self.connected,
            "error": self.error[:80],
            "sent": self.sent,
            "hello": self.hello,
            "helloAge": round(time.time() - self.hello_at, 1) if self.hello_at else -1,
            "lastLog": self.last_log[:80],
        }

    def fresh(self) -> bool:
        """Экран недавно присылал приветствие по кабелю."""
        return self.connected and self.hello_at > 0 and time.time() - self.hello_at < 15

    def _find(self, list_ports) -> str:
        if self.port_arg:
            return self.port_arg
        for p in list_ports.comports():
            if (p.vid, p.pid) in self.USB_IDS:
                return p.device
        return ""

    def _command(self, msg: dict) -> None:
        cmd = msg.get("cmd")
        try:
            if cmd == "volume":
                self.agent.volume.set_level(int(msg.get("level", 0)))
            elif cmd == "mute":
                self.agent.volume.set_mute(bool(msg.get("mute")))
            elif cmd == "speedtest":
                self.agent.start_speedtest()
        except Exception as exc:
            self.error = f"cmd: {exc}"

    def _on_line(self, raw: bytes) -> None:
        text = raw.decode("utf-8", "replace").strip()
        if not text:
            return
        if not text.startswith("@PCMON "):
            self.last_log = text
            return
        try:
            msg = json.loads(text[7:])
        except json.JSONDecodeError:
            return
        if "hello" in msg:
            self.hello = msg
            self.hello_at = time.time()
        if "cmd" in msg:
            threading.Thread(target=self._command, args=(msg,), daemon=True).start()

    def run(self) -> None:
        try:
            import serial  # type: ignore
            from serial.tools import list_ports  # type: ignore
        except Exception as exc:
            self.error = f"pyserial: {exc}"
            return
        while True:
            port = self._find(list_ports)
            if not port:
                self.port = ""
                time.sleep(2.0)
                continue
            s = serial.Serial()
            try:
                s.port = port
                s.baudrate = 115200
                s.timeout = 0.05
                s.write_timeout = 1.0
                # DTR=1 — «порт открыт»: без него плата ничего не присылает.
                # RTS=0 — иначе плата уйдёт в сброс.
                s.dtr = True
                s.rts = False
                s.open()
            except Exception as exc:
                self.error = str(exc)
                time.sleep(2.0)
                continue
            self.port = port
            self.connected = True
            self.error = ""
            buf = b""
            last = 0.0
            last_wx, last_wx_t0 = 0.0, None
            try:
                while True:
                    now = time.time()
                    if now - last >= self.agent.args.usb_interval:
                        last = now
                        with self.agent.lock:
                            data = dict(self.agent.data)
                        data["time"] = int(now)
                        line = json.dumps(data, ensure_ascii=False, separators=(",", ":")) + "\n"
                        s.write(line.encode("utf-8"))
                        self.sent += 1
                        wx = self.agent.forecast
                        if wx and (now - last_wx > 600 or wx.get("t0") != last_wx_t0):
                            last_wx, last_wx_t0 = now, wx.get("t0")
                            s.write((json.dumps(wx, separators=(",", ":")) + "\n").encode("utf-8"))
                    while not self.outbox.empty():
                        msg = self.outbox.get_nowait()
                        s.write((json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8"))
                    chunk = s.read(1024)
                    if chunk:
                        buf += chunk
                        while b"\n" in buf:
                            raw, buf = buf.split(b"\n", 1)
                            self._on_line(raw)
                        if len(buf) > 8192:
                            buf = b""
            except Exception as exc:
                self.error = str(exc)
            finally:
                self.connected = False
                try:
                    s.close()
                except Exception:
                    pass
            time.sleep(1.0)


# --------------------------------------------------------------------------- агент
class Agent:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.lock = threading.Lock()
        self.volume = VolumeWorker()
        self.wifi = WifiReader()
        self.usb = UsbLink(self, args.usb_port)
        self.forecast: dict = {}   # почасовой прогноз для заставки экрана
        self._wx_wake = threading.Event()
        self.city_cfg = {"city": "", "gm": "", "mail": ""}
        self._load_city_cfg()
        self.screen_ip = ""   # адрес экрана в сети (из USB-приветствия или по опросу)
        self.screen_seen = 0.0
        self.host = socket.gethostname()
        self.os_name = windows_name()
        self.speed = {
            "testing": False,
            "progress": 0.0,
            "down": 0.0,
            "up": 0.0,
            "ping": 0.0,
            "jitter": 0.0,
            "at": 0,
            "stage": "",
            "error": "",
        }
        self.data: dict = {}
        self._prev_net = None
        self._prev_disk = None
        self._prev_time = 0.0
        self._net_down = 0.0
        self._net_up = 0.0
        self._disk_read = 0.0
        self._disk_write = 0.0
        self._gpu_cache = {"present": False, "name": "", "load": 0.0, "temp": -1.0, "vram": 0.0, "vramUsed": 0.0, "vramTotal": 0.0}
        self._wifi_cache = {"present": False, "ssid": "", "rssi": 0, "quality": 0, "link": 0}
        self._ping = 0.0
        self._top: list[dict] = []
        self._lhm = {"cpu": -1.0, "gpu": -1.0}
        self._partitions: list[str] = []
        self._next = {"gpu": 0.0, "wifi": 0.0, "ping": 0.0, "top": 0.0, "lhm": 0.0, "parts": 0.0}
        psutil.cpu_percent(interval=None)
        psutil.cpu_percent(interval=None, percpu=True)

    def _city_file(self) -> str:
        return os.path.join(os.path.dirname(os.path.abspath(__file__)), "city.json")

    def _load_city_cfg(self) -> None:
        try:
            with open(self._city_file(), "r", encoding="utf-8") as f:
                d = json.load(f)
            if isinstance(d, dict) and d.get("city") and d.get("gm"):
                self.city_cfg = {
                    "city": str(d["city"])[:36],
                    "gm": str(d["gm"])[:64],
                    "mail": str(d.get("mail") or "")[:64],
                }
        except Exception:
            pass

    def _save_city_cfg(self) -> None:
        try:
            with open(self._city_file(), "w", encoding="utf-8") as f:
                json.dump(self.city_cfg, f, ensure_ascii=False)
        except Exception:
            pass

    @staticmethod
    def search_ru_cities(query: str) -> list[dict]:
        """Ищет города РФ через Gismeteo и Погоду Mail.ru."""
        q = (query or "").strip()
        if not q:
            return []
        headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/124.0.0.0"}
        out: list[dict] = []
        try:
            from urllib.parse import quote

            url = f"https://www.gismeteo.ru/mq/city/q/?q={quote(q)}&geo=ru&latitude=55.75&longitude=37.62&limit=10"
            r = requests.get(url, headers=headers, timeout=5)
            if r.status_code == 200:
                items = r.json().get("data") or []
                for it in items:
                    if (it.get("country") or {}).get("code") != "RU":
                        continue
                    cid = it.get("id")
                    slug = it.get("slug") or ""
                    tr = (it.get("translations") or {}).get("ru") or {}
                    cname = (tr.get("city") or {}).get("name") or slug
                    dname = (tr.get("district") or {}).get("name") or ""
                    if cid and slug and cname:
                        out.append(
                            {
                                "city": cname,
                                "region": dname,
                                "gm": f"weather-{slug}-{cid}",
                                "mail": slug.replace("-", "_"),
                            }
                        )
        except Exception:
            pass

        # Дополняем точным путём Погоды Mail.ru, если находится
        if out:
            try:
                from urllib.parse import quote

                rm = requests.get(f"https://pogoda.mail.ru/ajax/suggest/?q={quote(q)}", headers=headers, timeout=3)
                if rm.status_code == 200:
                    for grp in rm.json().get("items") or []:
                        for mit in grp.get("items") or []:
                            u = str(mit.get("url") or "").strip("/")
                            mname = str(mit.get("name") or "").lower()
                            if u.startswith("prognoz/"):
                                mslug = u.split("/", 1)[1].strip("/")
                                for c in out:
                                    if c["city"].lower() == mname:
                                        c["mail"] = mslug
            except Exception:
                pass
        return out

    def screen_state(self) -> dict:
        usb = self.usb.fresh()
        h = self.usb.hello if usb else {}
        ip = self.screen_addr()
        wifi = bool(self.screen_ip) and time.time() - self.screen_seen < 6
        out = {"ok": True, "link": "usb" if usb else ("wifi" if wifi else "none"), "port": self.usb.port,
               "fw": h.get("fw", ""), "ip": ip, "skin": h.get("skin"), "flip": h.get("flip"), "call": h.get("call"),
               "city": h.get("city") or self.city_cfg["city"],
               "gm": h.get("gm") or self.city_cfg["gm"],
               "mail": h.get("mail") or self.city_cfg["mail"],
               "skins": SKIN_NAMES}
        if not usb and wifi:
            try:
                st = requests.get(f"http://{ip}/api/status", timeout=1.5).json()
                out.update({"skin": st.get("skin"), "flip": st.get("flip"), "call": st.get("call"),
                            "city": st.get("city") or self.city_cfg["city"],
                            "gm": st.get("gm") or self.city_cfg["gm"],
                            "mail": st.get("mail") or self.city_cfg["mail"]})
            except (requests.RequestException, ValueError):
                pass
        return out

    def screen_set(self, data: dict) -> dict:
        cmd = {}
        if "skin" in data:
            cmd["skin"] = int(clamp(int(data["skin"]), 0, len(SKIN_NAMES) - 1))
        if "flip" in data:
            cmd["flip"] = 1 if data["flip"] else 0
        if "call" in data:
            cmd["call"] = "".join(ch for ch in str(data["call"]).upper() if ch.isalnum() or ch in "/-")[:8]
        if "city" in data or "gm" in data:
            cname = str(data.get("city") or "").strip()[:36]
            gm = str(data.get("gm") or "").strip()[:64]
            mail = str(data.get("mail") or "").strip()[:64]
            if cname and not gm:
                found = self.search_ru_cities(cname)
                if found:
                    cname = found[0]["city"]
                    gm = found[0]["gm"]
                    mail = found[0]["mail"]
            if cname and gm:
                if not mail:
                    m = re.match(r"weather-(.+)-\d+$", gm)
                    mail = m.group(1).replace("-", "_") if m else ""
                self.city_cfg = {"city": cname, "gm": gm, "mail": mail}
                self._save_city_cfg()
                cmd["city"] = cname
                cmd["gm"] = gm
                cmd["mail"] = mail
                self.forecast = {}
                self._wx_wake.set()
        if not cmd:
            return self.screen_state()
        if self.usb.fresh():
            self.usb.outbox.put({"set": cmd})
            self.usb.hello.update(cmd)   # до следующего приветствия показываем новое
        else:
            ip = self.screen_addr()
            if ip:
                try:
                    requests.get(f"http://{ip}/api/set", params=cmd, timeout=3)
                except requests.RequestException:
                    pass
        return self.screen_state()

    @staticmethod
    def _ru_weather_code(desc: str) -> int:
        s = (desc or "").lower()
        if "гроз" in s:
            return 95
        if "снегопад" in s or "метел" in s:
            return 85
        if "снег" in s:
            return 71
        if "ливен" in s or "сильный дождь" in s:
            return 80
        if "морос" in s:
            return 51
        if "дожд" in s or "осадк" in s:
            return 61
        if "туман" in s or "дымк" in s:
            return 45
        if "пасмурн" in s:
            return 3
        if "малооблач" in s or "небольшая облач" in s or "переменная облач" in s:
            return 1
        if "облач" in s:
            return 2
        if "ясно" in s:
            return 0
        return 2

    def _fetch_ru_forecast(self) -> list[list[int]]:
        """Загружает прогноз с российских серверов (Gismeteo.ru -> Pogoda.Mail.ru)."""
        headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/124.0.0.0"}
        rows: list[list[int]] = []
        gm_slug = self.city_cfg.get("gm") or ""
        mail_slug = self.city_cfg.get("mail") or ""
        if not gm_slug and not mail_slug:
            return []

        # 1. Gismeteo (текущая погода + прогноз по 3-часовым шагам, развёрнутый по часам)
        try:
            r_now = requests.get(f"https://www.gismeteo.ru/{gm_slug}/now/", headers=headers, timeout=8)
            if r_now.status_code == 200:
                t_html = r_now.text
                m_t = re.search(r'now-weather[^>]*>\s*<temperature-value value="(-?\d+)"', t_html)
                m_f = re.search(r'now-feel[^>]*>.*?<temperature-value value="(-?\d+)"', t_html, re.S)
                m_d = re.search(r'now-desc[^>]*>\s*([^<]+)', t_html)
                m_w = re.search(r'now-info-item[^>]*>.*?Ветер.*?<speed-value value="(\d+)"', t_html, re.S)
                if m_t:
                    cur_t = int(m_t.group(1))
                    cur_f = int(m_f.group(1)) if m_f else cur_t
                    cur_w = int(m_w.group(1)) if m_w else 0
                    cur_c = self._ru_weather_code(m_d.group(1) if m_d else "")
                    rows.append([cur_t, cur_f, cur_w, cur_c])

            r_3d = requests.get(f"https://www.gismeteo.ru/{gm_slug}/3-days/", headers=headers, timeout=8)
            if r_3d.status_code == 200:
                t3 = r_3d.text
                m_icon = re.search(r'data-row="icon-tooltip"(.*?)widget-row', t3, re.S)
                icons = re.findall(r'data-tooltip="([^"]+)"', m_icon.group(1)) if m_icon else []
                m_air = re.search(r'data-row="temperature-air"(.*?)data-row=', t3, re.S)
                air = [int(x) for x in re.findall(r'<temperature-value value="(-?\d+)"', m_air.group(1))] if m_air else []
                m_feel = re.search(r'data-row="temperature-heat-index"(.*?)data-row=', t3, re.S)
                feel = [int(x) for x in re.findall(r'<temperature-value value="(-?\d+)"', m_feel.group(1))] if m_feel else []
                m_wind = re.search(r'data-row="wind"(.*?)(?:data-row=|</section>)', t3, re.S)
                wind = [int(x) for x in re.findall(r'wind-speed.*?<speed-value value="(\d+)"', m_wind.group(1), re.S)] if m_wind else []
                n = min(len(air), len(feel), len(wind), len(icons))
                for i in range(n):
                    item = [air[i], feel[i], wind[i], self._ru_weather_code(icons[i])]
                    for _ in range(3):
                        if len(rows) < 48:
                            rows.append(list(item))
            if rows:
                while len(rows) < 48:
                    rows.append(list(rows[-1]))
                return rows[:48]
        except Exception:
            pass

        # 2. Резерв: Pogoda.Mail.ru
        try:
            r_mail = requests.get(f"https://pogoda.mail.ru/prognoz/{mail_slug}/", headers=headers, timeout=8)
            m = re.search(r"window\.__PRELOADED_STATE__PAGE\s*=\s*(\{.*?\});?\s*</script>", r_mail.text, re.S)
            if m:
                d = json.loads(m.group(1)).get("data", {})
                cur = d.get("current") or {}
                if "temperature" in cur:
                    t0_v = int(float(str(cur.get("temperature", 0)).replace("+", "")))
                    f0_v = int(float(str(cur.get("tempe_comf", t0_v)).replace("+", "")))
                    w0_v = int(float(str(cur.get("wind_speed", 0))))
                    c0_v = self._ru_weather_code(str(cur.get("description") or cur.get("descr") or ""))
                    rows.append([t0_v, f0_v, w0_v, c0_v])
                for h in d.get("hforecast") or []:
                    tv = int(float(str(h.get("temperature", 0)).replace("+", "")))
                    fv = int(float(str(h.get("tempe_comf", tv)).replace("+", "")))
                    wv = int(float(str(h.get("wind_speed", 0))))
                    cv = self._ru_weather_code(str(h.get("description") or h.get("descr") or ""))
                    if len(rows) < 48:
                        rows.append([tv, fv, wv, cv])
                if rows:
                    while len(rows) < 48:
                        rows.append(list(rows[-1]))
                    return rows[:48]
        except Exception:
            pass

        return []

    def _forecast_loop(self) -> None:
        """Раз в полчаса берёт прогноз с российских серверов (Gismeteo / Погода Mail.ru)
        и отдаёт экрану как кэш на двое суток."""
        while True:
            delay = 1800
            try:
                rows = self._fetch_ru_forecast()
                if rows:
                    t0 = int(time.time() // 3600 * 3600)
                    self.forecast = {"wx": 1, "t0": t0, "h": rows}
                else:
                    delay = 300
            except Exception as exc:
                self.forecast_error = str(exc)[:80]
                delay = 300
            self._wx_wake.wait(delay)
            self._wx_wake.clear()

    def screen_addr(self) -> str:
        ip = str(self.usb.hello.get("ip") or "")
        if not ip or ip == "0.0.0.0":
            ip = self.screen_ip
        return ip

    # -- фоновые замеры -----------------------------------------------------
    def _refresh_partitions(self) -> None:
        parts = []
        for p in psutil.disk_partitions(all=False):
            if not p.mountpoint or not p.fstype:
                continue
            if p.opts and "removable" in p.opts.lower():
                continue
            try:
                psutil.disk_usage(p.mountpoint)
                parts.append(p.mountpoint)
            except OSError:
                continue
        self._partitions = parts or [os.environ.get("SystemDrive", "C:") + "\\"]

    def _disk_totals(self) -> tuple[float, float, float]:
        total = used = 0
        for mount in self._partitions:
            try:
                u = psutil.disk_usage(mount)
                total += u.total
                used += u.used
            except OSError:
                continue
        percent = (used / total * 100.0) if total else 0.0
        return percent, used / 1024**3, total / 1024**3

    def _gpu_nvidia(self) -> None:
        try:
            out = subprocess.run(
                [
                    "nvidia-smi",
                    "--query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu",
                    "--format=csv,noheader,nounits",
                ],
                capture_output=True,
                text=True,
                timeout=3,
                creationflags=CREATE_NO_WINDOW,
            )
            if out.returncode != 0:
                return
            line = out.stdout.strip().splitlines()[0]
            name, load, used, total, temp = [x.strip() for x in line.split(",")]
            used_mb = float(used)
            total_mb = float(total)
            self._gpu_cache = {
                "present": True,
                "name": name[:38],
                "load": clamp(float(load), 0, 100),
                "temp": float(temp),
                "vram": (used_mb / total_mb * 100.0) if total_mb else 0.0,
                "vramUsed": used_mb / 1024.0,
                "vramTotal": total_mb / 1024.0,
            }
        except Exception:
            return

    def _hwinfo_sm2(self) -> tuple[float, float]:
        """Чтение общей памяти HWiNFO64 (если в настройках HWiNFO включена Shared Memory Support)."""
        import struct

        k32 = ctypes.windll.kernel32
        k32.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
        k32.OpenFileMappingW.restype = wintypes.HANDLE
        k32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
        k32.MapViewOfFile.restype = wintypes.LPVOID
        k32.UnmapViewOfFile.argtypes = [wintypes.LPCVOID]
        k32.CloseHandle.argtypes = [wintypes.HANDLE]

        h = None
        for name in ("Global\\HWiNFO_SENS_SM2", "HWiNFO_SENS_SM2", "Local\\HWiNFO_SENS_SM2"):
            h = k32.OpenFileMappingW(0x0004, False, name)
            if h:
                break
        if not h:
            return -1.0, -1.0
        ptr = k32.MapViewOfFile(h, 0x0004, 0, 0, 0)
        if not ptr:
            k32.CloseHandle(h)
            return -1.0, -1.0
        cpu_t, gpu_t = -1.0, -1.0
        try:
            hdr = ctypes.string_at(ptr, 44)
            magic, _, _, _, _, _, off_r, sz_r, num_r = struct.unpack("<4sIIqIIIII", hdr)
            if magic == b"HWiS" and 0 < num_r < 2000 and 64 <= sz_r <= 2048:
                raw = ctypes.string_at(ptr + off_r, sz_r * num_r)
                for i in range(num_r):
                    rec = raw[i * sz_r : (i + 1) * sz_r]
                    stype = struct.unpack_from("<I", rec, 0)[0]
                    if stype != 1:  # SENSOR_TYPE_TEMP = 1
                        continue
                    label_orig = rec[12:140].split(b"\x00", 1)[0].decode("cp1251", "ignore").lower()
                    label_user = rec[140:268].split(b"\x00", 1)[0].decode("cp1251", "ignore").lower()
                    val = struct.unpack_from("<d", rec, 284)[0]
                    if not (15.0 <= val <= 125.0):
                        continue
                    lbl = label_orig + " " + label_user
                    if any(k in lbl for k in ("tctl", "tdie", "cpu package", "цп (tctl", "кристалл цп")):
                        cpu_t = float(val)
                    elif cpu_t < 0 and ("cpu" in lbl or "цп" in lbl):
                        cpu_t = float(val)
                    if gpu_t < 0 and ("gpu" in lbl or "гп" in lbl):
                        gpu_t = float(val)
        except Exception:
            pass
        finally:
            k32.UnmapViewOfFile(ptr)
            k32.CloseHandle(h)
        return cpu_t, gpu_t

    def _hwinfo_reg_tray(self) -> float:
        """Чтение температуры ЦП из значка HWiNFO64 в трее Windows 11 (UI Automation + реестр)."""
        import winreg

        # 1. Сразу выдвигаем значок датчика HWiNFO64 в видимую область трея, чтобы Windows обновляла его текст
        fallback_val = -1.0
        try:
            base = r"Control Panel\NotifyIconSettings"
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, base) as root:
                idx = 0
                while True:
                    try:
                        sub = winreg.EnumKey(root, idx)
                        idx += 1
                    except OSError:
                        break
                    try:
                        with winreg.OpenKey(root, sub, 0, winreg.KEY_READ | winreg.KEY_SET_VALUE) as k:
                            exe, _ = winreg.QueryValueEx(k, "ExecutablePath")
                            if "hwinfo" not in str(exe).lower():
                                continue
                            tip, _ = winreg.QueryValueEx(k, "InitialTooltip")
                            m = re.search(r"(\d+(?:[.,]\d+)?)\s*°?\s*C", str(tip))
                            if m:
                                v = float(m.group(1).replace(",", "."))
                                if 15.0 <= v <= 125.0:
                                    fallback_val = v
                                try:
                                    prom, _ = winreg.QueryValueEx(k, "IsPromoted")
                                except OSError:
                                    prom = 0
                                if not prom:
                                    winreg.SetValueEx(k, "IsPromoted", 0, winreg.REG_DWORD, 1)
                    except OSError:
                        continue
        except Exception:
            pass

        # 2. Живое значение с кнопки значка на панели задач через UI Automation
        try:
            ctypes.oledll.ole32.CoInitialize(None)
        except Exception:
            pass
        try:
            import comtypes.client  # type: ignore

            if not getattr(self, "_uia", None):
                mod = comtypes.client.GetModule("UIAutomationCore.dll")
                self._uia = comtypes.client.CreateObject(
                    "{ff48dba4-60ef-4201-aa87-54103eef594e}", interface=mod.IUIAutomation
                )
                self._uia_cond = self._uia.CreatePropertyCondition(30012, "SystemTray.NormalButton")
            hwnd = ctypes.windll.user32.FindWindowW("Shell_TrayWnd", None)
            if hwnd:
                el = self._uia.ElementFromHandle(hwnd)
                btns = el.FindAll(4, self._uia_cond)
                for i in range(btns.Length):
                    name = str(btns.GetElement(i).CurrentName or "")
                    low = name.lower()
                    if ("°c" in low or "min:" in low) and any(k in low for k in ("цп", "cpu", "tctl", "tdie", "core", "package", "кристалл")):
                        m = re.search(r"(\d+(?:[.,]\d+)?)\s*°\s*c", low)
                        if m:
                            v = float(m.group(1).replace(",", "."))
                            if 15.0 <= v <= 125.0:
                                return v
                for i in range(btns.Length):
                    name = str(btns.GetElement(i).CurrentName or "")
                    low = name.lower()
                    if "min:" in low and "max:" in low and "°c" in low:
                        m = re.search(r"(\d+(?:[.,]\d+)?)\s*°\s*c", low)
                        if m:
                            v = float(m.group(1).replace(",", "."))
                            if 15.0 <= v <= 125.0:
                                return v
        except Exception:
            self._uia = None

        return fallback_val

    def _lhm_refresh(self) -> None:
        """Температуры из LibreHardwareMonitor, общей памяти HWiNFO64 или значка HWiNFO64 в трее."""
        url = self.args.lhm
        data = None
        if url:
            try:
                data = requests.get(url, timeout=0.6).json()
            except Exception:
                data = None
        if not data:
            c_sm, g_sm = self._hwinfo_sm2()
            if c_sm > 0 or g_sm > 0:
                self._lhm = {"cpu": c_sm, "gpu": g_sm}
                return
            c_tr = self._hwinfo_reg_tray()
            self._lhm = {"cpu": c_tr, "gpu": -1.0}
            return
        found: list[tuple[str, str, float]] = []  # (id датчика, имя, градусы)

        def num(value) -> float | None:
            if isinstance(value, (int, float)):
                return float(value)
            m = re.search(r"-?\d+(?:[.,]\d+)?", str(value or ""))
            return float(m.group(0).replace(",", ".")) if m else None

        def walk(node):
            if isinstance(node, dict):
                kind = node.get("Type") or node.get("SensorType") or ""
                sid = str(node.get("SensorId") or "").lower()
                if kind == "Temperature" or "/temperature/" in sid:
                    v = num(node.get("Value"))
                    if v is not None and 0 < v < 150:
                        found.append((sid, str(node.get("Text") or node.get("Name") or ""), v))
                for v in node.values():
                    walk(v)
            elif isinstance(node, list):
                for v in node:
                    walk(v)

        walk(data)
        cpu_keys = ["Core (Tctl/Tdie)", "CPU Package", "Package", "Core Average", "CPU Average", "Core Max", "CPU"]
        gpu_keys = ["GPU Core", "GPU Hot Spot", "GPU"]
        for keys, slot, tag in ((cpu_keys, "cpu", "cpu"), (gpu_keys, "gpu", "gpu")):
            mine = [f for f in found if tag in f[0]] or ([f for f in found if not f[0]] if tag == "cpu" else [])
            value = -1.0
            for key in keys:
                hit = [f for f in mine if f[1] == key]
                if hit:
                    value = hit[0][2]
                    break
            if value < 0 and mine and slot == "cpu":
                value = max(f[2] for f in mine)
            self._lhm[slot] = value

    def _wifi_refresh(self) -> None:
        self._wifi_cache = self.wifi.read()

    def _ping_refresh(self) -> None:
        targets = [("1.1.1.1", 443), ("8.8.8.8", 53), ("9.9.9.9", 53)]
        best = None
        for host, port in targets:
            try:
                t0 = time.perf_counter()
                with socket.create_connection((host, port), timeout=1.0):
                    dt = (time.perf_counter() - t0) * 1000.0
                best = dt if best is None else min(best, dt)
                break
            except OSError:
                continue
        self._ping = r(best, 1) if best is not None else 0.0

    def _top_refresh(self) -> None:
        skip = {"system idle process", "memory compression", "registry", "svchost.exe"}
        items = []
        for proc in psutil.process_iter(["name", "cpu_percent"]):
            try:
                info = proc.info
                name = (info.get("name") or "").strip()
                cpu = float(info.get("cpu_percent") or 0.0) / (psutil.cpu_count() or 1)
                if not name or name.lower() in skip or cpu <= 0:
                    continue
                items.append({"name": name[:26], "cpu": r(cpu, 1)})
            except (psutil.NoSuchProcess, psutil.AccessDenied, ValueError):
                continue
        items.sort(key=lambda x: x["cpu"], reverse=True)
        self._top = items[:3]

    def _sample(self) -> dict:
        now = time.time()
        cpu = psutil.cpu_percent(interval=None)
        freq = psutil.cpu_freq()
        mem = psutil.virtual_memory()

        if now >= self._next["parts"]:
            self._next["parts"] = now + 60
            self._refresh_partitions()
        disk_percent, disk_used, disk_total = self._disk_totals()

        net = psutil.net_io_counters()
        disk_io = psutil.disk_io_counters()
        if self._prev_time and now > self._prev_time:
            dt = now - self._prev_time
            if self._prev_net and net:
                down = max(0.0, (net.bytes_recv - self._prev_net.bytes_recv) * 8 / dt / 1e6)
                up = max(0.0, (net.bytes_sent - self._prev_net.bytes_sent) * 8 / dt / 1e6)
                self._net_down = self._net_down * 0.5 + down * 0.5 if self._net_down else down
                self._net_up = self._net_up * 0.5 + up * 0.5 if self._net_up else up
            if self._prev_disk and disk_io:
                read = max(0.0, (disk_io.read_bytes - self._prev_disk.read_bytes) / dt / 1024**2)
                write = max(0.0, (disk_io.write_bytes - self._prev_disk.write_bytes) / dt / 1024**2)
                self._disk_read = self._disk_read * 0.5 + read * 0.5 if self._disk_read else read
                self._disk_write = self._disk_write * 0.5 + write * 0.5 if self._disk_write else write
        self._prev_net = net
        self._prev_disk = disk_io
        self._prev_time = now

        if now >= self._next["gpu"]:
            self._next["gpu"] = now + 3
            self._gpu_nvidia()
        if now >= self._next["lhm"]:
            self._next["lhm"] = now + 3
            self._lhm_refresh()
        if now >= self._next["wifi"]:
            self._next["wifi"] = now + 3
            self._wifi_refresh()
        if now >= self._next["ping"]:
            self._next["ping"] = now + 5
            threading.Thread(target=self._ping_refresh, daemon=True).start()
        if now >= self._next["top"]:
            self._next["top"] = now + 3
            self._top_refresh()

        vol = self.volume.state()
        gpu = dict(self._gpu_cache)
        if gpu["present"] and self._lhm["gpu"] > 0:
            gpu["temp"] = self._lhm["gpu"]
        cpu_temp = self._lhm["cpu"]

        with self.lock:
            speed = dict(self.speed)

        return {
            "ok": True,
            "v": VERSION,
            "host": self.host[:32],
            "os": self.os_name[:46],
            "uptime": int(max(0, now - psutil.boot_time())),
            "cpu": {
                "load": r(cpu, 1),
                "freq": r(freq.current / 1000.0, 2) if freq and freq.current else 0.0,
                "temp": r(cpu_temp, 1) if cpu_temp > 0 else -1.0,
            },
            "ram": {
                "percent": r(mem.percent, 1),
                "used": r(mem.used / 1024**3, 2),
                "total": r(mem.total / 1024**3, 2),
            },
            "gpu": gpu,
            "disk": {
                "percent": r(disk_percent, 1),
                "used": r(disk_used, 1),
                "total": r(disk_total, 1),
                "read": r(self._disk_read, 1),
                "write": r(self._disk_write, 1),
            },
            "net": {
                "down": r(self._net_down, 2),
                "up": r(self._net_up, 2),
                "downTotal": int(net.bytes_recv) if net else 0,
                "upTotal": int(net.bytes_sent) if net else 0,
            },
            "wifi": self._wifi_cache,
            "volumeLevel": vol["level"] if vol["available"] else 0,
            "muted": vol["muted"] if vol["available"] else False,
            "volumeAvailable": vol["available"],
            "volumeError": vol["error"][:40],
            "ping": self._ping,
            "top": self._top,
            "speed": speed,
        }

    def sample_loop(self) -> None:
        while True:
            try:
                self.data = self._sample()
            except Exception as exc:
                self.data = {"ok": False, "error": str(exc)[:120]}
            time.sleep(self.args.interval)

    # -- speedtest ----------------------------------------------------------
    def start_speedtest(self) -> bool:
        with self.lock:
            if self.speed["testing"]:
                return False
            self.speed.update(
                {"testing": True, "progress": 0.0, "down": 0.0, "up": 0.0, "ping": 0.0, "jitter": 0.0, "stage": "подготовка", "error": ""}
            )
        threading.Thread(target=self._run_speedtest, daemon=True).start()
        return True

    def _set_speed(self, **kwargs) -> None:
        with self.lock:
            self.speed.update(kwargs)

    def _run_speedtest(self) -> None:
        session = requests.Session()
        session.headers.update({"User-Agent": f"{APP_NAME}/{VERSION}"})
        duration = max(2.0, float(self.args.speed_duration))
        down_urls = [u.strip() for u in self.args.download_urls.split(",") if u.strip()]
        up_urls = [u.strip() for u in self.args.upload_urls.split(",") if u.strip()]
        try:
            ping, jitter = self._speed_ping(session)
            self._set_speed(stage="Загрузка", progress=0.18, ping=r(ping, 1), jitter=r(jitter, 1))
            down = self._speed_download(session, down_urls, duration)
            self._set_speed(stage="Отдача", progress=0.68, down=r(down, 2))
            up = self._speed_upload(session, up_urls, duration)
            self._set_speed(
                testing=False,
                progress=1.0,
                down=r(down, 2),
                up=r(up, 2),
                ping=r(ping, 1),
                jitter=r(jitter, 1),
                at=int(time.time()),
                stage="завершено",
                error="",
            )
        except Exception as exc:
            self._set_speed(testing=False, progress=0.0, stage="ошибка", error=str(exc)[:46])
        finally:
            session.close()

    def _speed_ping(self, session: requests.Session) -> tuple[float, float]:
        self._set_speed(stage="Пинг", progress=0.03)
        pings: list[float] = []
        for i in range(4):
            try:
                t0 = time.perf_counter()
                resp = session.get(self.args.ping_url, timeout=6)
                resp.raise_for_status()
                pings.append((time.perf_counter() - t0) * 1000.0)
            except requests.RequestException:
                break
            self._set_speed(progress=0.03 + 0.12 * (i + 1) / 4.0)
        if not pings:
            for host, port in (("1.1.1.1", 443), ("8.8.8.8", 53)):
                try:
                    t0 = time.perf_counter()
                    with socket.create_connection((host, port), timeout=2):
                        pings.append((time.perf_counter() - t0) * 1000.0)
                except OSError:
                    continue
        if not pings:
            raise RuntimeError("пинг недоступен")
        return min(pings), (statistics.pstdev(pings) if len(pings) > 1 else 0.0)

    def _speed_download(self, session: requests.Session, urls: list[str], duration: float) -> float:
        last_error = ""
        for idx, url in enumerate(urls):
            try:
                self._set_speed(stage=f"Загрузка {idx + 1}/{len(urls)}")
                total = 0
                t0 = time.perf_counter()
                with session.get(url, stream=True, timeout=(5, duration + 20)) as resp:
                    resp.raise_for_status()
                    for chunk in resp.iter_content(262_144):
                        if not chunk:
                            continue
                        total += len(chunk)
                        elapsed = time.perf_counter() - t0
                        self._set_speed(progress=0.18 + 0.48 * min(1.0, elapsed / duration))
                        if elapsed >= duration:
                            break
                elapsed = max(0.001, time.perf_counter() - t0)
                if total < 65_536:
                    last_error = "мало данных"
                    continue
                return total * 8 / elapsed / 1e6
            except requests.RequestException as exc:
                last_error = str(exc)
                continue
        raise RuntimeError(f"загрузка не удалась: {last_error[:24]}")

    def _speed_upload(self, session: requests.Session, urls: list[str], duration: float) -> float:
        sizes = [2 * 1024 * 1024, 1024 * 1024, 256 * 1024]
        payload = b"\0" * sizes[0]
        total = 0
        elapsed = 0.0
        attempts = 0
        last_error = ""
        start = time.perf_counter()
        for round_idx in range(3):
            for size in sizes:
                if attempts >= 6 or time.perf_counter() - start > duration * 1.5:
                    break
                for url in urls:
                    attempts += 1
                    try:
                        self._set_speed(stage=f"Отдача {attempts}", progress=0.68 + 0.28 * min(1.0, attempts / 6.0))
                        t0 = time.perf_counter()
                        resp = session.post(url, data=payload[:size], timeout=(5, max(15, duration * 3)))
                        dt = time.perf_counter() - t0
                        if resp.status_code >= 400:
                            last_error = f"HTTP {resp.status_code}"
                            continue
                        total += size
                        elapsed += dt
                        break
                    except requests.RequestException as exc:
                        last_error = str(exc)
                        continue
            if total > 0 and time.perf_counter() - start > duration:
                break
        if total == 0 or elapsed <= 0:
            raise RuntimeError(f"отдача не удалась: {last_error[:24]}")
        return total * 8 / elapsed / 1e6

    # -- сеть ---------------------------------------------------------------
    def local_ipv4(self) -> list[tuple[str, str]]:
        out = []
        for name, addrs in psutil.net_if_addrs().items():
            for addr in addrs:
                if addr.family != socket.AF_INET:
                    continue
                if addr.address.startswith("127.") or addr.address.startswith("169.254."):
                    continue
                out.append((addr.address, addr.netmask or "255.255.255.0"))
        return out

    def primary_ip(self) -> str:
        ips = self.local_ipv4()
        if not ips:
            return "127.0.0.1"
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.settimeout(0.2)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            for candidate, _ in ips:
                if candidate == ip:
                    return ip
        except OSError:
            pass
        return ips[0][0]

    def beacon_payload(self, ip: str) -> bytes:
        return f"{BEACON_PREFIX}|{ip}|{self.args.http_port}|{self.host}".encode("utf-8")

    def beacon_loop(self) -> None:
        if self.args.no_beacon:
            return
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        targets: set[str] = {"255.255.255.255"}
        while True:
            try:
                current = {"255.255.255.255"}
                for ip, mask in self.local_ipv4():
                    try:
                        net = ipaddress.IPv4Network(f"{ip}/{mask}", strict=False)
                        current.add(str(net.broadcast_address))
                    except ValueError:
                        continue
                if current != targets:
                    targets = current
                payload = self.beacon_payload(self.primary_ip())
                for target in targets:
                    try:
                        sock.sendto(payload, (target, self.args.udp_port))
                    except OSError:
                        continue
            except Exception:
                pass
            time.sleep(self.args.beacon_interval)

    def udp_loop(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("", self.args.udp_port))
        except OSError as exc:
            print(f"[udp] не удалось занять порт {self.args.udp_port}: {exc}")
            return
        sock.settimeout(1.0)
        while True:
            try:
                data, addr = sock.recvfrom(64)
            except socket.timeout:
                continue
            except OSError:
                break
            if data.strip() == UDP_MAGIC:
                try:
                    reply = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    reply.connect(addr)
                    reply.send(self.beacon_payload(reply.getsockname()[0]))
                    reply.close()
                except OSError:
                    pass

    def start(self) -> None:
        self._refresh_partitions()
        self.data = self._sample()
        self.volume.start()
        threading.Thread(target=self._forecast_loop, daemon=True, name="forecast").start()
        if not self.args.no_usb:
            self.usb.start()
        threading.Thread(target=self.sample_loop, daemon=True, name="stats").start()
        threading.Thread(target=self.beacon_loop, daemon=True, name="beacon").start()
        threading.Thread(target=self.udp_loop, daemon=True, name="udp").start()

        handler = make_handler(self)
        server = ThreadingHTTPServer((self.args.host, self.args.http_port), handler)
        server.daemon_threads = True
        ip = self.primary_ip()
        vol = self.volume.state()
        print(f"{APP_NAME} {VERSION}")
        print(f"  HTTP: http://{ip}:{self.args.http_port}/")
        print(f"  UDP : {self.args.udp_port} (автопоиск экраном)")
        if not self.args.no_usb:
            print("  USB : экран ищется сам по кабелю (ESP32-S3)")
        if not vol["available"]:
            print(f"  Громкость недоступна: {vol['error']}")
        if not self.args.quiet:
            print("  Ctrl+C — остановить")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\n[agent] остановка")
            server.shutdown()


# --------------------------------------------------------------------------- HTTP
PAGE = """<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Monitor</title><style>
:root{--bg:#0e1014;--card:#171a20;--card2:#1f232b;--line:#2b303a;--text:#f3f5f8;--mut:#a3acb9;--acc:#ff9f1c;--ok:#46e06a;--bad:#ff5a4f}
*{box-sizing:border-box}html,body{margin:0}
body{background:var(--bg);color:var(--text);font:14px/1.4 "Segoe UI",system-ui,sans-serif;padding:18px}
.wrap{max-width:860px;margin:0 auto;display:flex;flex-direction:column;gap:14px}
header{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
h1{font-size:20px;margin:0;font-weight:700;letter-spacing:.02em}
.status{margin-left:auto;display:flex;align-items:center;gap:8px;color:var(--mut);font-size:13px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);flex:none}.dot.on{background:var(--ok)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.k{color:var(--mut);font-size:11px;text-transform:uppercase;letter-spacing:.08em}
.v{font-size:26px;font-weight:700;margin:4px 0 2px;font-variant-numeric:tabular-nums}
.s{color:var(--mut);font-size:12px;min-height:17px}
.bar{height:4px;background:var(--card2);border-radius:99px;overflow:hidden;margin-top:8px}.bar>i{display:block;height:100%;width:0;background:var(--acc);border-radius:99px;transition:width .4s}
h2{font-size:13px;margin:0 0 12px;font-weight:600;color:var(--mut);text-transform:uppercase;letter-spacing:.08em}
.cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px}
.skins{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.skin{display:flex;align-items:center;gap:10px;padding:10px;border-radius:10px;border:2px solid var(--line);background:var(--card2);color:var(--text);font:inherit;font-weight:600;cursor:pointer;text-align:left}
.skin[aria-pressed=true]{border-color:var(--acc)}
.sw{width:44px;height:33px;border-radius:6px;flex:none;position:relative;overflow:hidden;border:1px solid #0006}
.sw i{position:absolute;border-radius:50%}
.row{display:flex;align-items:center;gap:10px;margin-top:12px;flex-wrap:wrap}
.row label{color:var(--text)}
input[type=text],select{background:var(--card2);border:1px solid var(--line);border-radius:8px;color:var(--text);font:inherit;padding:8px 10px}
#call{width:120px;text-transform:uppercase}
#city{width:170px}
input[type=range]{flex:1;min-width:120px;accent-color:var(--acc)}
input[type=checkbox]{width:18px;height:18px;accent-color:var(--acc)}
button.b{padding:8px 14px;border-radius:8px;border:1px solid var(--line);background:var(--card2);color:var(--text);font:inherit;font-weight:600;cursor:pointer}
button.b.p{background:var(--acc);border-color:var(--acc);color:#1a1205}
button:focus-visible,input:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
.note{color:var(--mut);font-size:12px;margin-top:10px}
</style></head><body><div class="wrap">
<header><h1>PC Monitor</h1><span class="s" id="host"></span>
<div class="status"><span class="dot" id="dot"></span><span id="link">ищу экран…</span></div></header>
<div class="grid">
<div class="card"><div class="k">Процессор</div><div class="v" id="cpu">—</div><div class="s" id="cpus"></div><div class="bar"><i id="cpub"></i></div></div>
<div class="card"><div class="k">Видеокарта</div><div class="v" id="gpu">—</div><div class="s" id="gpus"></div><div class="bar"><i id="gpub"></i></div></div>
<div class="card"><div class="k">Память</div><div class="v" id="ram">—</div><div class="s" id="rams"></div><div class="bar"><i id="ramb"></i></div></div>
<div class="card"><div class="k">Диск</div><div class="v" id="disk">—</div><div class="s" id="disks"></div><div class="bar"><i id="diskb"></i></div></div>
<div class="card"><div class="k">Сеть</div><div class="v" id="net">—</div><div class="s" id="nets"></div></div>
</div>
<div class="cols">
<div class="card"><h2>Экран</h2>
<div class="skins" id="skins"></div>
<div class="row"><input type="checkbox" id="flip"><label for="flip">Перевернуть на 180°</label></div>
<div class="row"><label for="call">Позывной</label><input type="text" id="call" maxlength="8" autocomplete="off"><button class="b" id="callb">Сохранить</button></div>
<div class="row"><label for="city">Погода (город РФ)</label><input type="text" id="city" list="citylist" placeholder="Город РФ" autocomplete="off"><datalist id="citylist"></datalist><button class="b" id="cityb">Выбрать</button></div>
<div class="note" id="scrnote"></div></div>
<div class="card"><h2>Компьютер</h2>
<div class="row" style="margin-top:0"><label for="vol">Громкость</label><input type="range" id="vol" min="0" max="100"><span id="volv" style="width:44px;text-align:right">—</span><button class="b" id="mute">Звук</button></div>
<div class="row"><button class="b p" id="test">Тест скорости</button><span id="speed" class="s"></span></div>
<div class="bar"><i id="spb"></i></div>
<div class="row"><input type="checkbox" id="auto"><label for="auto">Запускать вместе с Windows</label></div>
<div class="note" id="appnote"></div></div>
</div></div>
<script>
const $=id=>document.getElementById(id);
const SK=[["Приборы","#0c0e11","#ff9f1c","#4cc9f0"],["Неон","#07060d","#22e3ff","#ff4df5"],["Минимализм","#f4f2ee","#16161a","#c2410c"],["Ретро-радио","#1c1a17","#f3e6c4","#5ef2d6"],["Часы","#000000","#ffa51e","#ff3b6b"],["Терминал","#020a04","#35e85a","#28c048"],["Плитки","#0b0c10","#1f64f2","#763af5"],["Лампы","#0e0905","#ff8a2b","#e6d7c3"],["Импульс","#07111d","#24d8ff","#8b6cff"],["Шина","#081018","#38bfd5","#45e0b2"],["HUD","#0b0d12","#ff4e9b","#27d9f2"],["Сканлайн","#080d12","#42d8ff","#ff4e9b"]];
async function j(u,o){const r=await fetch(u,o);return r.json()}
function post(u,b){return j(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})}
SK.forEach((s,i)=>{const b=document.createElement('button');b.className='skin';b.type='button';b.setAttribute('aria-pressed','false');
b.innerHTML=`<span class="sw" style="background:${s[1]}"><i style="width:22px;height:22px;left:5px;top:5px;border:3px solid ${s[2]}"></i><i style="width:8px;height:8px;right:5px;bottom:5px;background:${s[3]}"></i></span><span></span>`;
b.lastChild.textContent=s[0];b.onclick=async()=>{mark(i);show(await post('/api/screen/set',{skin:i}))};$('skins').appendChild(b)});
function mark(i){[...$('skins').children].forEach((b,k)=>b.setAttribute('aria-pressed',k===i?'true':'false'))}
function pct(id,v,on){$(id).textContent=on?v.toFixed(0)+'%':'—';$(id+'b').style.width=(on?v:0)+'%'}
let callEdit=false,cityEdit=false,volDrag=false;
const PRESET_CITIES=[
  ["Геленджик","Краснодарский край","weather-gelendzhik-5213","gelendzhik"],
  ["Краснодар","Краснодарский край","weather-krasnodar-5136","krasnodar"],
  ["Новороссийск","Краснодарский край","weather-novorossiysk-5214","novorossiysk"],
  ["Сочи","Краснодарский край","weather-sochi-5233","sochi"],
  ["Анапа","Краснодарский край","weather-anapa-5211","anapa"],
  ["Туапсе","Краснодарский край","weather-tuapse-5219","tuapse"],
  ["Ростов-на-Дону","Ростовская область","weather-rostov-na-donu-5110","rostov_na_donu"],
  ["Ставрополь","Ставропольский край","weather-stavropol-5141","stavropol"],
  ["Волгоград","Волгоградская область","weather-volgograd-5089","volgograd"],
  ["Симферополь","Республика Крым","weather-simferopol-4995","simferopol"],
  ["Севастополь","Крым","weather-sevastopol-5003","sevastopol"],
  ["Москва","Москва","weather-moscow-4368","moskva"],
  ["Санкт-Петербург","Санкт-Петербург","weather-sankt-peterburg-4079","sankt_peterburg"],
  ["Воронеж","Воронежская область","weather-voronezh-5026","voronezh"],
  ["Нижний Новгород","Нижегородская область","weather-nizhny-novgorod-4355","nizhniy_novgorod"],
  ["Казань","Республика Татарстан","weather-kazan-4364","kazan"],
  ["Самара","Самарская область","weather-samara-4618","samara"],
  ["Уфа","Республика Башкортостан","weather-ufa-4588","ufa"],
  ["Екатеринбург","Свердловская область","weather-yekaterinburg-4517","ekaterinburg"],
  ["Челябинск","Челябинская область","weather-chelyabinsk-4565","chelyabinsk"],
  ["Пермь","Пермский край","weather-perm-4476","perm"],
  ["Тюмень","Тюменская область","weather-tyumen-4501","tyumen"],
  ["Омск","Омская область","weather-omsk-4578","omsk"],
  ["Новосибирск","Новосибирская область","weather-novosibirsk-4690","novosibirsk"],
  ["Красноярск","Красноярский край","weather-krasnoyarsk-4674","krasnoyarsk"],
  ["Иркутск","Иркутская область","weather-irkutsk-4787","irkutsk"],
  ["Хабаровск","Хабаровский край","weather-khabarovsk-4862","habarovsk"],
  ["Владивосток","Приморский край","weather-vladivostok-4877","vladivostok"],
  ["Калининград","Калининградская область","weather-kaliningrad-4225","kaliningrad"],
  ["Мурманск","Мурманская область","weather-murmansk-3903","murmansk"]
];
const cityMap=new Map();
function fillCities(list){const dl=$('citylist');dl.innerHTML='';list.forEach(c=>{cityMap.set(c[0].toLowerCase(),{city:c[0],gm:c[2],mail:c[3]});const o=document.createElement('option');o.value=c[0];o.label=c[1];dl.appendChild(o)})}
fillCities(PRESET_CITIES);
function show(s){if(!s||!s.ok)return;const on=s.link!=='none';$('dot').className='dot'+(on?' on':'');
$('link').textContent=s.link==='usb'?`Экран подключён по USB (${s.port}), прошивка ${s.fw||'?'}`:s.link==='wifi'?`Экран на связи по Wi-Fi (${s.ip})`:'Экран не найден — проверьте кабель';
if(s.skin!=null)mark(s.skin);if(s.flip!=null)$('flip').checked=!!s.flip;if(s.call!=null&&!callEdit)$('call').value=s.call;if(s.city!=null&&!cityEdit)$('city').value=s.city;
$('scrnote').textContent=on?'Свайп по экрану тоже меняет скин. Касание открывает меню.':'Настройки применятся, когда экран появится на связи.'}
async function refresh(){try{const d=await j('/api/stats');
$('host').textContent=`${d.host} · ${d.os}`;
pct('cpu',d.cpu.load,true);$('cpus').textContent=(d.cpu.temp>0?d.cpu.temp.toFixed(0)+'°C · ':'')+d.cpu.freq.toFixed(1)+' ГГц';
pct('gpu',d.gpu.load,d.gpu.present);$('gpus').textContent=d.gpu.present?`${d.gpu.temp>0?d.gpu.temp.toFixed(0)+'°C · ':''}VRAM ${d.gpu.vram.toFixed(0)}%`:'нет видеокарты';
pct('ram',d.ram.percent,true);$('rams').textContent=`${d.ram.used.toFixed(1)} из ${d.ram.total.toFixed(0)} ГБ`;
pct('disk',d.disk.percent,true);$('disks').textContent=`${d.disk.used.toFixed(0)} из ${d.disk.total.toFixed(0)} ГБ`;
$('net').textContent=`↓ ${d.net.down.toFixed(1)}`;$('nets').textContent=`↑ ${d.net.up.toFixed(1)} Мбит/с · пинг ${d.ping||'—'} мс`;
if(!volDrag){$('vol').value=d.volumeLevel;$('volv').textContent=d.volumeAvailable?d.volumeLevel+'%':'—'}
$('mute').textContent=d.muted?'Включить звук':'Выключить звук';
const s=d.speed;$('spb').style.width=(s.testing?s.progress*100:0)+'%';
$('speed').textContent=s.testing?s.stage:(s.error||(s.down?`↓ ${s.down.toFixed(1)} · ↑ ${s.up.toFixed(1)} Мбит/с · ${s.ping.toFixed(0)} мс`:''));
}catch(e){$('host').textContent='агент не отвечает'}}
async function refreshScreen(){try{show(await j('/api/screen/state'))}catch(e){}}
$('flip').onchange=async e=>show(await post('/api/screen/set',{flip:e.target.checked?1:0}));
$('call').oninput=()=>callEdit=true;
$('callb').onclick=async()=>{callEdit=false;show(await post('/api/screen/set',{call:$('call').value.trim().toUpperCase()}))};
let cityTimer=null;
$('city').oninput=e=>{cityEdit=true;const q=e.target.value.trim();clearTimeout(cityTimer);if(q.length<2){fillCities(PRESET_CITIES);return}
cityTimer=setTimeout(async()=>{try{const r=await j('/api/weather/cities?q='+encodeURIComponent(q));if(r&&r.cities&&r.cities.length)fillCities(r.cities.map(c=>[c.city,c.region,c.gm,c.mail]))}catch(err){}},250)};
$('cityb').onclick=async()=>{const name=$('city').value.trim();if(!name)return;cityEdit=false;const hit=cityMap.get(name.toLowerCase());
const res=await post('/api/screen/set',hit||{city:name});show(res);$('scrnote').textContent='Город погоды: '+(res.city||name)};
$('vol').addEventListener('pointerdown',()=>volDrag=true);
$('vol').addEventListener('input',e=>$('volv').textContent=e.target.value+'%');
$('vol').addEventListener('change',async e=>{volDrag=false;await post('/api/volume',{level:+e.target.value})});
$('mute').onclick=async()=>{await post('/api/volume',{toggle:true});refresh()};
$('test').onclick=async()=>{await post('/api/speedtest',{});refresh()};
$('auto').onchange=async e=>app(await post('/api/app',{autostart:e.target.checked}));
function app(a){if(!a)return;$('auto').checked=!!a.autostart;$('auto').disabled=!a.available;
$('appnote').textContent=a.available?'Программа живёт в трее: закрытие окна её не останавливает.':'Автозапуск доступен, когда программа запущена через «PC Monitor.bat».'}
refresh();refreshScreen();j('/api/app').then(app).catch(()=>{});setInterval(refresh,1000);setInterval(refreshScreen,2000);
</script></body></html>"""


def make_handler(agent: Agent):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = f"{APP_NAME}/{VERSION}"

        def log_message(self, fmt, *args):  # noqa: N802
            if not agent.args.quiet:
                sys.stderr.write("[http] %s - %s\n" % (self.address_string(), fmt % args))

        def _send(self, code: int, body: bytes, ctype: str = "application/json; charset=utf-8") -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def _json(self, obj, code: int = 200) -> None:
            self._send(code, json.dumps(obj, ensure_ascii=False).encode("utf-8"))

        def do_GET(self):  # noqa: N802
            path = urlparse(self.path).path
            if path in ("/", "/index.html"):
                self._send(200, PAGE.encode("utf-8"), "text/html; charset=utf-8")
            elif path == "/api/screen/state":
                self._json(agent.screen_state())
            elif path == "/api/app":
                self._json(autostart_state())
            elif path == "/api/weather/cities":
                qs = parse_qs(urlparse(self.path).query)
                q = (qs.get("q") or [""])[0]
                self._json({"ok": True, "cities": agent.search_ru_cities(q)})
            elif path in ("/api/screen/set", "/api/screen/status", "/api/screen/skins.zip"):
                ip = agent.screen_addr()
                if not ip:
                    self._json({"ok": False, "error": "адрес экрана неизвестен: экран не в Wi-Fi"}, 503)
                    return
                query = urlparse(self.path).query
                try:
                    if path == "/api/screen/skins.zip":
                        import io
                        import zipfile

                        st = requests.get(f"http://{ip}/api/status", timeout=5).json()
                        buf = io.BytesIO()
                        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
                            if "themes" in query:
                                # меню «Обзор» в палитре каждого скина и страница настроек
                                for i in range(int(st.get("skins", 4))):
                                    requests.get(f"http://{ip}/api/set?skin={i}&menu=0", timeout=5)
                                    time.sleep(1.4)
                                    z.writestr(f"theme{i}.bmp", requests.get(f"http://{ip}/shot.bmp", timeout=10).content)
                                requests.get(f"http://{ip}/api/set?menu=3", timeout=5)
                                time.sleep(1.4)
                                z.writestr("settings.bmp", requests.get(f"http://{ip}/shot.bmp", timeout=10).content)
                                query = "done"
                            menus = "menu" in query
                            for i in range(0 if query == "done" else 4 if menus else int(st.get("skins", 4))):
                                key = "menu" if menus else "skin"
                                requests.get(f"http://{ip}/api/set?{key}={i}", timeout=5)
                                time.sleep(1.4)
                                z.writestr(f"{key}{i}.bmp", requests.get(f"http://{ip}/shot.bmp", timeout=10).content)
                        requests.get(f"http://{ip}/api/set?skin={int(st.get('skin', 0))}", timeout=5)
                        self._send(200, buf.getvalue(), "application/zip")
                    elif path == "/api/screen/set":
                        r2 = requests.get(f"http://{ip}/api/set?{query}", timeout=5)
                        self._send(r2.status_code, r2.content)
                    else:
                        r2 = requests.get(f"http://{ip}/api/status", timeout=5)
                        self._send(r2.status_code, r2.content)
                except (requests.RequestException, ValueError) as exc:
                    self._json({"ok": False, "error": str(exc)[:120]}, 502)
            elif path == "/api/screen.bmp":
                ip = str(agent.usb.hello.get("ip") or "")
                if not ip or ip == "0.0.0.0":
                    ip = agent.screen_ip
                if not ip:
                    self._json({"ok": False, "error": "адрес экрана неизвестен: экран не в Wi-Fi"}, 503)
                    return
                try:
                    data = requests.get(f"http://{ip}/shot.bmp", timeout=10).content
                except requests.RequestException as exc:
                    self._json({"ok": False, "error": str(exc)[:120]}, 502)
                    return
                self._send(200, data, "image/bmp")
            elif path == "/api/stats":
                if not self.client_address[0].startswith("127."):
                    agent.screen_ip = self.client_address[0]
                    agent.screen_seen = time.time()
                with agent.lock:
                    data = dict(agent.data)
                self._json(data)
            elif path == "/api/volume":
                self._json({"ok": True, **agent.volume.state()})
            elif path == "/api/speedtest":
                with agent.lock:
                    self._json({"ok": True, "speed": dict(agent.speed)})
            elif path == "/api/health":
                self._json({"ok": True, "name": APP_NAME, "version": VERSION, "time": int(time.time()), "app": bool(AUTOSTART_CMD), "usb": agent.usb.status(), "forecastHours": len(agent.forecast.get("h", []))})
            else:
                self._json({"ok": False, "error": "not found"}, 404)

        def do_POST(self):  # noqa: N802
            path = urlparse(self.path).path
            length = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(length) if length else b""
            try:
                data = json.loads(raw.decode("utf-8")) if raw else {}
            except json.JSONDecodeError:
                data = {}

            if path == "/api/volume":
                state = agent.volume.state()
                if not state["available"]:
                    self._json({"ok": False, "error": state["error"] or "volume unavailable"}, 503)
                    return
                if "level" in data:
                    state = agent.volume.set_level(int(data["level"]))
                elif "delta" in data:
                    state = agent.volume.set_level(int(state["level"] + int(data["delta"])))
                elif "mute" in data:
                    state = agent.volume.set_mute(bool(data["mute"]))
                elif data.get("toggle"):
                    state = agent.volume.set_mute(not state["muted"])
                else:
                    self._json({"ok": False, "error": "bad request"}, 400)
                    return
                self._json({"ok": True, "level": state["level"], "muted": state["muted"]})
            elif path == "/api/screen/set":
                self._json(agent.screen_set(data if isinstance(data, dict) else {}))
            elif path == "/api/app":
                if isinstance(data, dict) and "autostart" in data:
                    autostart_set(bool(data["autostart"]))
                self._json(autostart_state())
            elif path == "/api/speedtest":
                started = agent.start_speedtest()
                with agent.lock:
                    speed = dict(agent.speed)
                self._json({"ok": True, "started": started, "speed": speed})
            else:
                self._json({"ok": False, "error": "not found"}, 404)

        def do_OPTIONS(self):  # noqa: N802
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()

    return Handler


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=APP_NAME)
    p.add_argument("--host", default="0.0.0.0")
    p.add_argument("--http-port", type=int, default=8123)
    p.add_argument("--udp-port", type=int, default=4210)
    p.add_argument("--interval", type=float, default=0.7)
    p.add_argument("--beacon-interval", type=float, default=2.0)
    p.add_argument("--no-beacon", action="store_true")
    p.add_argument("--no-usb", action="store_true", help="не искать экран по USB-кабелю")
    p.add_argument("--usb-port", default=os.environ.get("PCMON_USB_PORT", ""), help="COM-порт экрана; пусто — найти автоматически")
    p.add_argument("--usb-interval", type=float, default=1.0)
    p.add_argument("--lat", type=float, default=0.0, help="широта для прогноза (если используется источником погоды)")
    p.add_argument("--lon", type=float, default=0.0, help="долгота для прогноза (если используется источником погоды)")
    p.add_argument("--lhm", default=os.environ.get("PCMON_LHM", "http://127.0.0.1:8085/data.json"), help="LibreHardwareMonitor data.json, например http://localhost:8085/data.json")
    p.add_argument("--download-urls", default=os.environ.get("PCMON_DOWN_URLS", DEFAULT_DOWN_URLS), help="список URL через запятую")
    p.add_argument("--upload-urls", default=os.environ.get("PCMON_UP_URLS", DEFAULT_UP_URLS), help="список URL через запятую")
    p.add_argument("--ping-url", default=os.environ.get("PCMON_PING_URL", DEFAULT_PING_URL))
    p.add_argument("--speed-duration", type=float, default=6.0)
    p.add_argument("--quiet", action="store_true")
    return p.parse_args(argv)


def main() -> int:
    args = parse_args()
    try:
        Agent(args).start()
    except OSError as exc:
        print(f"[agent] ошибка: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
