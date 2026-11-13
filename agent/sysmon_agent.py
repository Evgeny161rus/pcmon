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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from launcher import ACTIONS, Launcher, item_image, pick_file, start_menu_apps, to_png_b64  # noqa: E402
from panel import PAGE as PANEL_PAGE  # noqa: E402
from skin_previews import PREVIEWS  # noqa: E402

APP_NAME = "PC Monitor Agent"
VERSION = "1.6.0"
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
              "Импульс", "Шина", "HUD", "Сканлайн", "Звук"]

# Команда автозапуска задаётся программой-оболочкой (pcmon_app.pyw); без неё автозапуск недоступен.
SHOW_HOOK = None   # программа-компаньон: показать своё окно
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


# --------------------------------------------------------------------------- история
class History:
    """Раз в минуту запоминает нагрузку, температуры и трафик; хранит сутки.
    Для экрана сворачивает последние 12 часов в 72 отрезка по 10 минут."""

    STEP = 600
    SLOTS = 72

    def __init__(self, path: str) -> None:
        self.path = path
        self.lock = threading.Lock()
        self.rows: list[list] = []      # [t, cpu, gpu, ct, gt, mb, top]
        self._saved = 0.0
        try:
            with open(path, "r", encoding="utf-8") as f:
                self.rows = [r for r in json.load(f) if isinstance(r, list) and len(r) == 7]
        except Exception:
            self.rows = []

    def add(self, cpu: float, gpu: float, ct: float, gt: float, mb: float, top: str) -> None:
        now = int(time.time())
        with self.lock:
            self.rows.append([now, round(cpu, 1), round(gpu, 1), round(ct, 1), round(gt, 1), round(mb, 2), top])
            self.rows = [r for r in self.rows if now - r[0] <= 86400]
            if now - self._saved > 600:
                self._saved = now
                try:
                    tmp = self.path + ".tmp"
                    with open(tmp, "w", encoding="utf-8") as f:
                        json.dump(self.rows, f, separators=(",", ":"))
                    os.replace(tmp, self.path)
                except Exception:
                    pass

    def summary(self) -> dict:
        end = int(time.time()) // self.STEP * self.STEP + self.STEP
        t0 = end - self.STEP * self.SLOTS
        cpu = [[] for _ in range(self.SLOTS)]
        gpu = [[] for _ in range(self.SLOTS)]
        ct = [-1.0] * self.SLOTS
        gt = [-1.0] * self.SLOTS
        net = [0.0] * self.SLOTS
        tops: dict[str, int] = {}
        hot = (-1.0, 0)
        with self.lock:
            rows = [r for r in self.rows if r[0] >= t0]
        for t, c, g, tc, tg, mb, top in rows:
            i = (t - t0) // self.STEP
            if not 0 <= i < self.SLOTS:
                continue
            cpu[i].append(c)
            gpu[i].append(g)
            ct[i] = max(ct[i], tc)
            gt[i] = max(gt[i], tg)
            net[i] += mb
            if top and c >= 10:
                tops[top] = tops.get(top, 0) + 1
            if tc > hot[0]:
                hot = (tc, t)
        avg = lambda v: int(round(sum(v) / len(v))) if v else -1  # noqa: E731
        on = sum(1 for v in cpu if v)
        top = max(tops, key=tops.get) if tops else ""
        return {
            "hist": 1, "t0": t0, "step": self.STEP,
            "cpu": [avg(v) for v in cpu], "gpu": [avg(v) for v in gpu],
            "ct": [int(round(v)) for v in ct], "gt": [int(round(v)) for v in gt],
            "net": [int(round(v)) for v in net],
            "hot": int(round(hot[0])), "hotT": hot[1], "top": top[:26],
            "onMin": on * self.STEP // 60,
        }


# --------------------------------------------------------------------------- медиа
MEDIA_VK = {"prev": 0xB1, "play": 0xB3, "next": 0xB0}
MEDIA_APPS = {
    "spotify.exe": "Spotify", "chrome.exe": "Chrome", "msedge.exe": "Edge", "firefox.exe": "Firefox",
    "browser.exe": "Яндекс Браузер", "opera.exe": "Opera", "vlc.exe": "VLC", "aimp.exe": "AIMP",
    "foobar2000.exe": "foobar2000", "wmplayer.exe": "Windows Media Player", "music.ui.exe": "Музыка",
    "microsoft.media.player.exe": "Медиаплеер", "telegram.exe": "Telegram", "discord.exe": "Discord",
    "yandexmusic.exe": "Яндекс Музыка", "potplayermini64.exe": "PotPlayer", "mpc-hc64.exe": "MPC-HC",
    "steam.exe": "Steam", "itunes.exe": "iTunes", "winamp.exe": "Winamp",
}
TITLE_TAILS = (" - Google Chrome", " — Яндекс Браузер", " - Яндекс Браузер", " - Microsoft​ Edge", " - Microsoft Edge",
               " — Mozilla Firefox", " - Mozilla Firefox", " - Opera", " - YouTube", " - VLC media player")


def press_media_key(key: str) -> bool:
    vk = MEDIA_VK.get(key)
    if vk is None or os.name != "nt":
        return False
    u = ctypes.windll.user32
    u.keybd_event(vk, 0, 1, 0)        # KEYEVENTF_EXTENDEDKEY
    u.keybd_event(vk, 0, 1 | 2, 0)    # + KEYEVENTF_KEYUP
    return True


def window_titles_by_exe(exe: str) -> list[str]:
    """Заголовки видимых окон всех процессов с таким именем exe."""
    if os.name != "nt":
        return []
    pids = set()
    for pr in psutil.process_iter(["name"]):
        if (pr.info.get("name") or "").lower() == exe:
            pids.add(pr.pid)
    if not pids:
        return []
    u = ctypes.windll.user32
    titles: list[str] = []
    proto = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def cb(hwnd, _):
        if not u.IsWindowVisible(hwnd):
            return True
        pid = ctypes.c_ulong()
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids:
            n = u.GetWindowTextLengthW(hwnd)
            if n > 0:
                buf = ctypes.create_unicode_buffer(n + 1)
                u.GetWindowTextW(hwnd, buf, n + 1)
                titles.append(buf.value)
        return True

    u.EnumWindows(proto(cb), 0)
    return titles


def clean_media_title(app: str, title: str) -> str:
    t = title.strip()
    for tail in TITLE_TAILS:
        if t.endswith(tail):
            t = t[: -len(tail)].strip()
    t = re.sub(r"^\(\d+\)\s*", "", t)          # «(3) Видео» — счётчик уведомлений во вкладке
    if t.lower() in ("spotify", "spotify premium", "spotify free", app.lower(), ""):
        return ""
    return t[:70]


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
        # что звучит: {"app", "title", "playing"}; последнее звучавшее помним 15 минут
        self.media = {"app": "", "title": "", "playing": False}
        self._media_seen = 0.0
        self._media_exe = ""

    def _scan_media(self) -> None:
        from pycaw.pycaw import AudioUtilities  # type: ignore

        active_exe = ""
        for sess in AudioUtilities.GetAllSessions():
            try:
                proc = sess.Process
                if proc is None:
                    continue
                exe = proc.name().lower()
                if exe.startswith("python") or exe in ("audiodg.exe", "svchost.exe", "pcmonitor.exe"):
                    continue
                if sess.State == 1:          # AudioSessionStateActive
                    active_exe = exe
                    if exe in MEDIA_APPS:
                        break
            except Exception:
                continue
        now = time.time()
        if active_exe:
            self._media_exe = active_exe
            self._media_seen = now
        exe = self._media_exe
        if not exe or now - self._media_seen > 900:
            m = {"app": "", "title": "", "playing": False}
        else:
            app = MEDIA_APPS.get(exe, exe[:-4] if exe.endswith(".exe") else exe)
            title = ""
            for t in window_titles_by_exe(exe):
                title = clean_media_title(app, t)
                if title:
                    break
            m = {"app": app[:23], "title": title, "playing": bool(active_exe)}
        with self.lock:
            self.media = m

    def media_state(self) -> dict:
        with self.lock:
            return dict(self.media)

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
        last_media = 0.0
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
            if self.available and now - last_media >= 1.5:
                last_media = now
                try:
                    self._scan_media()
                except Exception:
                    pass
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
            elif cmd == "media":
                press_media_key(str(msg.get("key", "")))
            elif cmd == "launch":
                ok, info = self.agent.launcher.launch(int(msg.get("i", -1)))
                if not ok:
                    self.error = f"launch: {info}"
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
            last_hist = 0.0
            apps_ver = None
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
                        if apps_ver != self.agent.launcher.ver:
                            apps_ver = self.agent.launcher.ver
                            for m in self.agent.launcher.board_messages():
                                s.write((json.dumps(m, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8"))
                        if now - last_hist > 300:
                            last_hist = now
                            hs = self.agent.history.summary()
                            s.write((json.dumps(hs, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8"))
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
        self.history = History(os.path.join(os.path.dirname(os.path.abspath(__file__)), "history.json"))
        self.launcher = Launcher(os.path.join(os.path.dirname(os.path.abspath(__file__)), "apps.json"))
        self.forecast_source = ""
        self.forecast_error = ""
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
        # Keep user-selected location persistent beside the EXE in PyInstaller builds.
        root = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))
        return os.path.join(root, "city.json")

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
                if isinstance(d.get("lat"), (int, float)) and isinstance(d.get("lon"), (int, float)):
                    self.city_cfg["lat"] = float(d["lat"])
                    self.city_cfg["lon"] = float(d["lon"])
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
               "fw": h.get("fw", ""), "ip": ip, "skin": h.get("skin"), "flip": h.get("flip"),
               "city": h.get("city") or self.city_cfg["city"],
               "gm": h.get("gm") or self.city_cfg["gm"],
               "mail": h.get("mail") or self.city_cfg["mail"],
               "skins": SKIN_NAMES}
        if not usb and wifi:
            try:
                st = requests.get(f"http://{ip}/api/status", timeout=1.5).json()
                out.update({"skin": st.get("skin"), "flip": st.get("flip"),
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
                self.city_cfg = {"city": cname, "gm": gm, "mail": mail}   # координаты найдутся заново
                self._save_city_cfg()
                cmd["city"] = cname
                cmd["gm"] = gm
                cmd["mail"] = mail
                self.forecast = {}
                self._wx_wake.set()
        show = str(data.get("show") or "")
        if show in ("fc", "media", "night", "launch", "idle", "home"):
            cmd["show"] = show
        if not cmd:
            return self.screen_state()
        if self.usb.fresh():
            self.usb.outbox.put({"set": cmd})
            self.usb.hello.update({k: v for k, v in cmd.items() if k != "show"})   # до следующего приветствия
        else:
            ip = self.screen_addr()
            if ip:
                params = {k: v for k, v in cmd.items() if k != "show"}
                if show:
                    params.update({"idle": 0, "fc": 0} if show == "home" else {show: 1})
                try:
                    requests.get(f"http://{ip}/api/set", params=params, timeout=3)
                except requests.RequestException:
                    pass
            else:
                st = self.screen_state()
                st["ok"] = False
                return st
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

    def _fetch_ru_current(self) -> list[int] | None:
        """Текущая погода с Gismeteo, резерв — Погода Mail.ru: [темп, ощущается, ветер, код]."""
        headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/124.0.0.0"}
        gm_slug = self.city_cfg.get("gm") or ""
        mail_slug = self.city_cfg.get("mail") or ""
        if gm_slug:
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
                        return [cur_t, int(m_f.group(1)) if m_f else cur_t, int(m_w.group(1)) if m_w else 0,
                                self._ru_weather_code(m_d.group(1) if m_d else "")]
            except Exception:
                pass
        if mail_slug:
            try:
                r_mail = requests.get(f"https://pogoda.mail.ru/prognoz/{mail_slug}/", headers=headers, timeout=8)
                m = re.search(r"window\.__PRELOADED_STATE__PAGE\s*=\s*(\{.*?\});?\s*</script>", r_mail.text, re.S)
                if m:
                    cur = json.loads(m.group(1)).get("data", {}).get("current") or {}
                    if "temperature" in cur:
                        t0_v = int(float(str(cur.get("temperature", 0)).replace("+", "")))
                        return [t0_v, int(float(str(cur.get("tempe_comf", t0_v)).replace("+", ""))),
                                int(float(str(cur.get("wind_speed", 0)))),
                                self._ru_weather_code(str(cur.get("description") or cur.get("descr") or ""))]
            except Exception:
                pass
        return None

    WX_DEBUG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wx_debug")

    def _wx_save(self, name: str, text: str) -> None:
        """Последняя скачанная страница погоды — для разбора, если сайт поменяет вёрстку."""
        try:
            os.makedirs(self.WX_DEBUG_DIR, exist_ok=True)
            with open(os.path.join(self.WX_DEBUG_DIR, name), "w", encoding="utf-8") as f:
                f.write(text)
        except Exception:
            pass

    @staticmethod
    def _gm_row(html: str, row: str) -> str:
        m = re.search(r'data-row="%s"(.*?)(?=data-row="|</section>|$)' % re.escape(row), html, re.S)
        return m.group(1) if m else ""

    @staticmethod
    def _gm_items(chunk: str) -> list[str]:
        parts = re.split(r'<div[^>]*class="[^"]*row-item[^"]*"', chunk)[1:]
        return parts

    def _parse_gismeteo(self, html: str) -> list[tuple[int, int, int, int, int]]:
        """Столбцы страницы Gismeteo «сегодня»/«завтра» -> [(unix, темп, ощущ, ветер, код)].
        У каждого столбца есть точное время (атрибут timestamp), по нему и привязываемся."""
        m = re.search(r'widget-row-datetime-time(.*?)widget-row-icon', html, re.S)
        times = [int(x) for x in re.findall(r'timestamp="(\d+)"', m.group(1))] if m else []
        air = [int(x) for x in re.findall(r'<temperature-value value="(-?\d+)"', self._gm_row(html, "temperature-air"))]
        feel = [int(x) for x in re.findall(r'<temperature-value value="(-?\d+)"', self._gm_row(html, "temperature-heat-index"))]
        wind = [int(x) for x in re.findall(r'<speed-value value="(\d+)"', self._gm_row(html, "wind"))]
        icons = re.findall(r'data-tooltip="([^"]+)"', self._gm_row(html, "icon-tooltip"))
        n = min(len(times), len(air))
        out = []
        for i in range(n):
            out.append((times[i], air[i], feel[i] if i < len(feel) else air[i],
                        wind[i] if i < len(wind) else 0,
                        self._ru_weather_code(icons[i]) if i < len(icons) else 2))
        return out

    @staticmethod
    def _hourly(cols: list[tuple[int, int, int, int, int]], start: int, hours: int = 48) -> list[list[int]]:
        """Из столбцов через 1–3 часа делает почасовой ряд: температура и ветер
        интерполируются между соседними столбцами, явление — от ближайшего прошедшего."""
        cols = sorted(cols)
        rows = []
        for k in range(hours):
            t = start + k * 3600
            if t < cols[0][0] or t > cols[-1][0]:
                if t < cols[0][0] and cols[0][0] - t <= 3 * 3600:
                    c = cols[0]
                    rows.append([c[1], c[2], c[3], c[4]])
                    continue
                break
            for j in range(len(cols) - 1):
                if cols[j][0] <= t <= cols[j + 1][0]:
                    a, b = cols[j], cols[j + 1]
                    f = (t - a[0]) / max(1, b[0] - a[0])
                    rows.append([round(a[1] + (b[1] - a[1]) * f), round(a[2] + (b[2] - a[2]) * f),
                                 round(a[3] + (b[3] - a[3]) * f), a[4]])
                    break
            else:
                c = cols[-1]
                rows.append([c[1], c[2], c[3], c[4]])
        return rows

    def _fetch_gismeteo(self) -> list[list[int]]:
        """Почасовой прогноз с Gismeteo (страницы «сегодня», «завтра», «3 дня»)."""
        gm = self.city_cfg.get("gm") or ""
        if not gm:
            return []
        headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
                   "Accept-Language": "ru-RU,ru;q=0.9"}
        cols: dict[int, tuple] = {}
        errors = []
        for page, name in (("", "gm_today.html"), ("tomorrow/", "gm_tomorrow.html")):
            try:
                r = requests.get(f"https://www.gismeteo.ru/{gm}/{page}", headers=headers, timeout=10)
                self._wx_save(name, r.text)
                if r.status_code != 200:
                    errors.append(f"{page or 'today'}: HTTP {r.status_code}")
                    continue
                parsed = self._parse_gismeteo(r.text)
                if not parsed:
                    errors.append(f"{page or 'today'}: не разобрано")
                for c in parsed:
                    cols[c[0]] = c
            except Exception as exc:
                errors.append(f"{page or 'today'}: {exc}"[:60])
        cur = self._fetch_ru_current()
        start = int(time.time() // 3600 * 3600)
        if cur:
            cols[start] = (start, *cur)
        if errors:
            self.forecast_error = "; ".join(errors)[:120]
        if len(cols) < 2:
            return [cur] if cur else []
        return self._hourly(list(cols.values()), start)

    def _forecast_loop(self) -> None:
        """Раз в полчаса берёт почасовой прогноз с Gismeteo и отдаёт экрану:
        когда ПК выключен, экран сам показывает погоду на текущий час.
        Каждая строка — свой час начиная с текущего (t0), без растягивания."""
        while True:
            delay = 1800
            try:
                self.forecast_error = ""
                rows = self._fetch_gismeteo()
                src = "Gismeteo"
                if len(rows) <= 1:
                    cur = rows[0] if rows else self._fetch_ru_current()
                    rows = [cur] if cur else []
                    src = "Gismeteo/Mail.ru (только сейчас)"
                    delay = 600
                if rows:
                    self.forecast = {"wx": 1, "t0": int(time.time() // 3600 * 3600), "h": rows}
                    self.forecast_source = src
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
            running = any("hwinfo" in (pr.info.get("name") or "").lower() for pr in psutil.process_iter(["name"]))
            self.hwinfo_diag = ("HWiNFO запущен, но Shared Memory Support выключен" if running
                                else "HWiNFO не запущен")
            return -1.0, -1.0
        ptr = k32.MapViewOfFile(h, 0x0004, 0, 0, 0)
        if not ptr:
            k32.CloseHandle(h)
            self.hwinfo_diag = "общая память HWiNFO не открывается"
            return -1.0, -1.0
        cpu_primary: list[float] = []
        cpu_other: list[float] = []
        gpu_values: list[float] = []
        try:
            # _HWiNFO_SENSORS_SHARED_MEM2 (pack 1): сигнатура, версия, ревизия,
            # время опроса (int64), затем смещение/размер/число для датчиков
            # и то же для показаний — всего 44 байта.
            hdr = ctypes.string_at(ptr, 44)
            magic, _, _, _, _, _, _, off_r, sz_r, num_r = struct.unpack("<4sIIqIIIIII", hdr)
            self.hwinfo_diag = f"SM2 magic={magic!r} readings={num_r} size={sz_r}"
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
                        cpu_primary.append(float(val))
                    elif "cpu" in lbl or "цп" in lbl:
                        cpu_other.append(float(val))
                    if "gpu" in lbl or "гп" in lbl:
                        gpu_values.append(float(val))
        except Exception as e:
            self.hwinfo_diag = f"ошибка разбора SM2: {e}"
        finally:
            k32.UnmapViewOfFile(ptr)
            k32.CloseHandle(h)
        # HWiNFO shared memory contains live sensor values. Prefer package/Tctl
        # sensors and report the hottest current one, not a historical minimum.
        cpu_values = cpu_primary or cpu_other
        return (max(cpu_values) if cpu_values else -1.0,
                max(gpu_values) if gpu_values else -1.0)

    @staticmethod
    def _current_temperatures(text: str) -> list[float]:
        """Extract live Celsius readings while excluding HWiNFO Min/Max/Avg rows."""
        values = []
        for line in str(text or "").splitlines():
            for match in re.finditer(r"(-?\d+(?:[.,]\d+)?)\s*°\s*C", line, re.I):
                prefix = line[: match.start()]
                if re.search(r"\b(?:min(?:imum)?|max(?:imum)?|avg|average)\s*[:=]?\s*$", prefix, re.I):
                    continue
                try:
                    value = float(match.group(1).replace(",", "."))
                except ValueError:
                    continue
                if 15.0 <= value <= 125.0:
                    values.append(value)
        return values

    def _hwinfo_reg_tray(self) -> float:
        """Read live HWiNFO CPU values from the Windows 11 notification area."""
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
                cpu_values = []
                for i in range(btns.Length):
                    name = str(btns.GetElement(i).CurrentName or "")
                    low = name.lower()
                    if "°c" in low and any(k in low for k in ("цп", "cpu", "tctl", "tdie", "core", "package", "кристалл")):
                        cpu_values.extend(self._current_temperatures(name))
                if cpu_values:
                    # If HWiNFO exposes several CPU sensors, use the hottest
                    # live reading for thermal monitoring.
                    return max(cpu_values)
        except Exception:
            self._uia = None

        # InitialTooltip from the registry is stale and may contain only Min/Max.
        # If no live tray value is available, do not report a historical value.
        return -1.0

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
            "media": self.volume.media_state(),
            "ping": self._ping,
            "top": self._top,
            "speed": speed,
        }

    def sample_loop(self) -> None:
        last_hist = 0.0
        net_prev = None
        while True:
            try:
                self.data = self._sample()
            except Exception as exc:
                self.data = {"ok": False, "error": str(exc)[:120]}
            now = time.time()
            if now - last_hist >= 60:
                last_hist = now
                try:
                    d = self.data
                    n = psutil.net_io_counters()
                    total = (n.bytes_recv + n.bytes_sent) if n else 0
                    mb = max(0.0, (total - net_prev) / 1e6) if net_prev is not None else 0.0
                    net_prev = total
                    top = (self._top[0]["name"] if self._top else "")
                    self.history.add(float(d["cpu"]["load"]), float(d.get("gpu", {}).get("load", 0) or 0),
                                     float(d["cpu"].get("temp", -1)), float(d.get("gpu", {}).get("temp", -1) or -1),
                                     mb, top)
                except Exception:
                    pass
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
# панель — в panel.py


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

        def _local(self) -> bool:
            return self.client_address[0] in ("127.0.0.1", "::1")

        def _json(self, obj, code: int = 200) -> None:
            self._send(code, json.dumps(obj, ensure_ascii=False).encode("utf-8"))

        def do_GET(self):  # noqa: N802
            path = urlparse(self.path).path
            if path in ("/", "/index.html"):
                self._send(200, PANEL_PAGE.encode("utf-8"), "text/html; charset=utf-8")
            elif path == "/api/previews":
                self._json({"ok": True, "previews": PREVIEWS})
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
            elif path == "/api/history":
                self._json(agent.history.summary())
            elif path == "/api/apps":
                cfg = agent.launcher.config()
                icons = []
                for i in range(6):
                    try:
                        icons.append(agent.launcher.icon(i)[1])
                    except Exception:
                        icons.append("")
                cfg["icons"] = icons
                cfg["actions"] = {k: v[0] for k, v in ACTIONS.items()}
                self._json({"ok": True, **cfg})
            elif path == "/api/apps/start":
                if not self._local():
                    self._json({"ok": False, "error": "только с этого ПК"}, 403)
                    return
                self._json({"ok": True, "apps": start_menu_apps()})
            elif path == "/api/speedtest":
                with agent.lock:
                    self._json({"ok": True, "speed": dict(agent.speed)})
            elif path == "/api/health":
                self._json({"ok": True, "name": APP_NAME, "version": VERSION, "time": int(time.time()), "app": bool(AUTOSTART_CMD), "usb": agent.usb.status(), "forecastHours": len(agent.forecast.get("h", [])), "forecastSource": agent.forecast_source, "forecastError": agent.forecast_error, "cpuTemp": agent._lhm.get("cpu", -1.0), "hwinfo": getattr(agent, "hwinfo_diag", "")})
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
            elif path in ("/api/apps", "/api/apps/pick", "/api/apps/preview") and not self._local():
                self._json({"ok": False, "error": "только с этого ПК"}, 403)
            elif path == "/api/apps":
                cfg = agent.launcher.set_config(data if isinstance(data, dict) else {})
                self._json({"ok": True, **cfg})
            elif path == "/api/apps/pick":
                self._json({"ok": True, "path": pick_file()})
            elif path == "/api/apps/preview":
                it = data if isinstance(data, dict) else {}
                try:
                    self._json({"ok": True, "icon": to_png_b64(item_image(it))})
                except Exception as exc:
                    self._json({"ok": False, "error": str(exc)[:80]})
            elif path == "/api/launch":
                if not (self._local() or self.client_address[0] == agent.screen_ip):
                    self._json({"ok": False, "error": "запуск — только с экрана или этого ПК"}, 403)
                    return
                ok, info = agent.launcher.launch(int(data.get("i", -1)) if isinstance(data, dict) else -1)
                self._json({"ok": ok, "info": info}, 200 if ok else 400)
            elif path == "/api/media":
                key = str(data.get("key", "")) if isinstance(data, dict) else ""
                if press_media_key(key):
                    self._json({"ok": True, "key": key})
                else:
                    self._json({"ok": False, "error": "key: prev | play | next"}, 400)
            elif path == "/api/screen/set":
                self._json(agent.screen_set(data if isinstance(data, dict) else {}))
            elif path == "/api/app" and isinstance(data, dict) and data.get("show"):
                if not self._local():
                    self._json({"ok": False}, 403)
                    return
                hook = globals().get("SHOW_HOOK")
                if hook:
                    threading.Thread(target=hook, daemon=True).start()
                self._json({"ok": bool(hook)})
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
