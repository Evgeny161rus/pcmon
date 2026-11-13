# -*- coding: utf-8 -*-
"""
Быстрый запуск: 4–6 плиток на экране платы.

Что умеет:
  * хранит список плиток в apps.json рядом с агентом;
  * достаёт иконку из .exe / ярлыка / папки / браузера по умолчанию (Windows),
    уменьшает до 48×48 и кодирует для экрана (LVGL RGB565A8);
  * запускает выбранное по номеру плитки — программу, ярлык, папку, сайт
    или действие (блокировка, сон, перезагрузка, выключение).

Список меняется только с этого ПК (панель/компаньон); экран присылает лишь номер плитки.
"""
from __future__ import annotations

import base64
import ctypes
import io
import json
import os
import subprocess
import sys
import threading
import time

ICON = 48
KINDS = ("app", "folder", "url", "action")
ACTIONS = {
    "lock": ("Блокировка", "\ue72e", (31, 100, 242)),
    "sleep": ("Сон", "\ue708", (118, 58, 245)),
    "restart": ("Перезагрузка", "\ue777", (184, 83, 10)),
    "shutdown": ("Выключение", "\ue7e8", (229, 72, 77)),
}
CONFIRM = {"restart", "shutdown"}       # на экране — второе касание для подтверждения

WIN = os.name == "nt"
SYS32 = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32")


def default_items() -> list[dict]:
    home = os.path.expanduser("~")
    return [
        {"name": "Проводник", "kind": "folder", "target": home},
        {"name": "Браузер", "kind": "url", "target": "https://ya.ru"},
        {"name": "Диспетчер", "kind": "app", "target": os.path.join(SYS32, "Taskmgr.exe")},
        {"name": "Калькулятор", "kind": "app", "target": os.path.join(SYS32, "calc.exe")},
        {"name": "Блокировка", "kind": "action", "target": "lock"},
        {"name": "Сон", "kind": "action", "target": "sleep"},
    ]


# --------------------------------------------------------------------------- иконки (Windows)
def _hicon_to_image(hicon):
    """HICON → PIL.Image RGBA."""
    from PIL import Image

    u, g = ctypes.windll.user32, ctypes.windll.gdi32

    class ICONINFO(ctypes.Structure):
        _fields_ = [("fIcon", ctypes.c_bool), ("xHotspot", ctypes.c_ulong), ("yHotspot", ctypes.c_ulong),
                    ("hbmMask", ctypes.c_void_p), ("hbmColor", ctypes.c_void_p)]

    class BITMAP(ctypes.Structure):
        _fields_ = [("bmType", ctypes.c_long), ("bmWidth", ctypes.c_long), ("bmHeight", ctypes.c_long),
                    ("bmWidthBytes", ctypes.c_long), ("bmPlanes", ctypes.c_ushort), ("bmBitsPixel", ctypes.c_ushort),
                    ("bmBits", ctypes.c_void_p)]

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", ctypes.c_ulong), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long),
                    ("biPlanes", ctypes.c_ushort), ("biBitCount", ctypes.c_ushort), ("biCompression", ctypes.c_ulong),
                    ("biSizeImage", ctypes.c_ulong), ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", ctypes.c_ulong), ("biClrImportant", ctypes.c_ulong)]

    info = ICONINFO()
    if not u.GetIconInfo(ctypes.c_void_p(hicon), ctypes.byref(info)):
        return None
    try:
        bm = BITMAP()
        g.GetObjectW(ctypes.c_void_p(info.hbmColor or info.hbmMask), ctypes.sizeof(bm), ctypes.byref(bm))
        w, h = bm.bmWidth, bm.bmHeight if info.hbmColor else bm.bmHeight // 2
        if w <= 0 or h <= 0:
            return None

        def bits(hbm, hh):
            bih = BITMAPINFOHEADER()
            bih.biSize = ctypes.sizeof(bih)
            bih.biWidth, bih.biHeight, bih.biPlanes, bih.biBitCount = w, -hh, 1, 32
            buf = ctypes.create_string_buffer(w * hh * 4)
            dc = u.GetDC(None)
            g.GetDIBits(dc, ctypes.c_void_p(hbm), 0, hh, buf, ctypes.byref(bih), 0)
            u.ReleaseDC(None, dc)
            return buf.raw

        if info.hbmColor:
            raw = bits(info.hbmColor, h)
            img = Image.frombuffer("RGBA", (w, h), raw, "raw", "BGRA", 0, 1).copy()
            if not any(img.getchannel("A").getdata()):
                mask = bits(info.hbmMask, h)
                m = Image.frombuffer("RGBA", (w, h), mask, "raw", "BGRA", 0, 1).convert("L")
                img.putalpha(m.point(lambda v: 0 if v else 255))
            return img
        return None
    finally:
        if info.hbmColor:
            g.DeleteObject(ctypes.c_void_p(info.hbmColor))
        if info.hbmMask:
            g.DeleteObject(ctypes.c_void_p(info.hbmMask))


def _extract(path: str, index: int = 0, size: int = 64):
    """Крупная иконка из exe/dll/ico (PrivateExtractIcons отдаёт нужный размер)."""
    u = ctypes.windll.user32
    hicon = ctypes.c_void_p()
    iid = ctypes.c_uint()
    n = u.PrivateExtractIconsW(path, index, size, size, ctypes.byref(hicon), ctypes.byref(iid), 1, 0)
    if n <= 0 or not hicon.value:
        return None
    try:
        return _hicon_to_image(hicon.value)
    finally:
        u.DestroyIcon(hicon)


def _shell_icon(path: str):
    """Иконка, которую показывает Проводник (подходит для папок и любых файлов), 32×32."""

    class SHFILEINFOW(ctypes.Structure):
        _fields_ = [("hIcon", ctypes.c_void_p), ("iIcon", ctypes.c_int), ("dwAttributes", ctypes.c_ulong),
                    ("szDisplayName", ctypes.c_wchar * 260), ("szTypeName", ctypes.c_wchar * 80)]

    fi = SHFILEINFOW()
    if not ctypes.windll.shell32.SHGetFileInfoW(path, 0, ctypes.byref(fi), ctypes.sizeof(fi), 0x100):  # SHGFI_ICON
        return None
    try:
        return _hicon_to_image(fi.hIcon)
    finally:
        ctypes.windll.user32.DestroyIcon(ctypes.c_void_p(fi.hIcon))


def _resolve_lnk(path: str) -> tuple[str, str, int]:
    """Ярлык → (цель, файл иконки, номер иконки)."""
    try:
        import comtypes.client  # noqa: F401
        from comtypes.shelllink import ShellLink
        from comtypes.persist import IPersistFile

        link = ShellLink()
        link.QueryInterface(IPersistFile).Load(path, 0)
        target = link.GetPath(0)[0] or ""
        icon, idx = link.GetIconLocation()
        return target, os.path.expandvars(icon or ""), int(idx or 0)
    except Exception:
        return "", "", 0


def default_browser() -> str:
    try:
        buf = ctypes.create_unicode_buffer(520)
        n = ctypes.c_ulong(520)
        if ctypes.windll.shlwapi.AssocQueryStringW(0, 2, "http", "open", buf, ctypes.byref(n)) == 0:
            return buf.value
    except Exception:
        pass
    return ""


def _glyph_icon(glyph: str, color: tuple, letter: str = ""):
    """Значок-действие: цветной круг и символ из шрифта Segoe (или буква)."""
    from PIL import Image, ImageDraw, ImageFont

    s = 192
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((8, 8, s - 8, s - 8), fill=color + (255,))
    font = None
    text = glyph
    fonts = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "Fonts")
    for name in (("SegoeIcons.ttf", "segmdl2.ttf") if glyph else ("segoeuib.ttf", "arialbd.ttf")):
        try:
            font = ImageFont.truetype(os.path.join(fonts, name), 96)
            break
        except Exception:
            continue
    if font is None or not glyph:
        text = (letter or "?")[:1].upper()
        if font is None:
            font = ImageFont.load_default()
    box = d.textbbox((0, 0), text, font=font)
    d.text(((s - (box[2] - box[0])) / 2 - box[0], (s - (box[3] - box[1])) / 2 - box[1]), text, font=font,
           fill=(255, 255, 255, 255))
    return img


def item_image(item: dict):
    """PIL.Image RGBA для плитки (без масштабирования)."""
    kind, target = item.get("kind"), str(item.get("target") or "")
    custom = str(item.get("icon") or "")
    img = None
    if WIN:
        try:
            if custom and os.path.exists(custom):
                if custom.lower().endswith((".png", ".jpg", ".jpeg", ".bmp")):
                    from PIL import Image

                    img = Image.open(custom).convert("RGBA")
                else:
                    img = _extract(custom, 0, 64)
            elif kind == "action":
                name, glyph, color = ACTIONS.get(target, ("?", "", (90, 90, 90)))
                img = _glyph_icon(glyph, color)
            elif kind == "url":
                br = default_browser()
                img = _extract(br, 0, 64) if br else None
            elif kind == "app":
                path = target
                idx = 0
                if path.lower().endswith(".lnk"):
                    t, icon, idx = _resolve_lnk(path)
                    path = icon or t or path
                img = _extract(path, idx, 64) or _shell_icon(target)
            elif kind == "folder":
                img = _extract(os.path.join(SYS32, "imageres.dll"), 3, 64) or _shell_icon(target)
        except Exception:
            img = None
    if img is None:
        img = _glyph_icon("", (60, 66, 80), item.get("name") or "?")
    return img


def to_lvgl(img) -> bytes:
    """RGBA → 48×48 LVGL RGB565A8: сначала цвет (RGB565, little-endian), затем прозрачность."""
    from PIL import Image

    im = img.convert("RGBA")
    if im.size != (ICON, ICON):
        k = min(ICON / im.width, ICON / im.height)   # и уменьшаем, и увеличиваем мелкие
        im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
        canvas = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
        canvas.paste(im, ((ICON - im.width) // 2, (ICON - im.height) // 2), im)
        im = canvas
    px = im.tobytes()
    color = bytearray(ICON * ICON * 2)
    alpha = bytearray(ICON * ICON)
    for i in range(ICON * ICON):
        r, g, b, a = px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        color[i * 2] = v & 0xFF
        color[i * 2 + 1] = v >> 8
        alpha[i] = a
    return bytes(color + alpha)


def to_png_b64(img, size: int = 64) -> str:
    from PIL import Image

    im = img.convert("RGBA")
    im.thumbnail((size, size), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


# --------------------------------------------------------------------------- список и запуск
class Launcher:
    def __init__(self, path: str) -> None:
        self.path = path
        self.lock = threading.Lock()
        self.ver = int(time.time())
        self.count = 6
        self.items = default_items()
        self._cache: dict[str, tuple[bytes, str]] = {}   # ключ плитки → (lvgl, png)
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self._apply(data)
        except Exception:
            pass

    @staticmethod
    def _key(it: dict) -> str:
        return json.dumps([it.get("kind"), it.get("target"), it.get("icon"), it.get("name")], ensure_ascii=False)

    def _apply(self, data: dict) -> None:
        count = int(data.get("count", self.count))
        items = []
        for it in data.get("items", [])[:6]:
            kind = it.get("kind") if it.get("kind") in KINDS else "app"
            items.append({
                "name": str(it.get("name") or "")[:18],
                "kind": kind,
                "target": str(it.get("target") or "")[:400],
                "icon": str(it.get("icon") or "")[:400],
            })
        while len(items) < 6:
            items.append(default_items()[len(items)])
        self.count = max(4, min(6, count))
        self.items = items

    def config(self) -> dict:
        with self.lock:
            return {"count": self.count, "items": [dict(i) for i in self.items], "ver": self.ver}

    def set_config(self, data: dict) -> dict:
        with self.lock:
            self._apply(data)
            self.ver = int(time.time() * 1000) & 0x7FFFFFFF
            try:
                tmp = self.path + ".tmp"
                with open(tmp, "w", encoding="utf-8") as f:
                    json.dump({"count": self.count, "items": self.items}, f, ensure_ascii=False, indent=1)
                os.replace(tmp, self.path)
            except Exception:
                pass
        return self.config()

    def icon(self, i: int) -> tuple[bytes, str]:
        with self.lock:
            it = dict(self.items[i])
        k = self._key(it)
        if k not in self._cache:
            img = item_image(it)
            self._cache[k] = (to_lvgl(img), to_png_b64(img))
        return self._cache[k]

    def board_messages(self) -> list[dict]:
        """Сообщения для экрана: список плиток и иконки (по одной в сообщении)."""
        cfg = self.config()
        msgs = [{"apps": 1, "ver": cfg["ver"], "n": cfg["count"],
                 "items": [{"name": it["name"], "confirm": it["kind"] == "action" and it["target"] in CONFIRM}
                           for it in cfg["items"][: cfg["count"]]]}]
        for i in range(cfg["count"]):
            try:
                data, _ = self.icon(i)
                msgs.append({"icon": i, "ver": cfg["ver"], "d": base64.b64encode(data).decode()})
            except Exception:
                pass
        return msgs

    def launch(self, i: int) -> tuple[bool, str]:
        with self.lock:
            if not 0 <= i < self.count:
                return False, "нет такой плитки"
            it = dict(self.items[i])
        kind, target = it["kind"], it["target"]
        try:
            if kind == "action":
                if not WIN:
                    return False, "только Windows"
                if target == "lock":
                    ctypes.windll.user32.LockWorkStation()
                elif target == "sleep":
                    ctypes.windll.powrprof.SetSuspendState(0, 1, 0)
                elif target == "restart":
                    subprocess.Popen(["shutdown", "/r", "/t", "5"], creationflags=0x08000000)
                elif target == "shutdown":
                    subprocess.Popen(["shutdown", "/s", "/t", "5"], creationflags=0x08000000)
                else:
                    return False, "неизвестное действие"
            elif kind in ("app", "folder", "url"):
                if not target:
                    return False, "не задано"
                if kind == "url" and not target.lower().startswith(("http://", "https://")):
                    target = "https://" + target
                if WIN:
                    os.startfile(target)  # noqa: S606 — путь задаёт владелец ПК в панели
                else:
                    subprocess.Popen(["xdg-open", target])
            return True, it["name"]
        except Exception as exc:
            return False, str(exc)[:80]


# --------------------------------------------------------------------------- выбор в панели
def start_menu_apps(limit: int = 400) -> list[dict]:
    """Ярлыки из меню «Пуск» — для выбора программы в панели."""
    roots = [os.path.join(os.environ.get("ProgramData", r"C:\ProgramData"), r"Microsoft\Windows\Start Menu\Programs"),
             os.path.join(os.environ.get("APPDATA", ""), r"Microsoft\Windows\Start Menu\Programs")]
    out, seen = [], set()
    skip = ("uninstall", "удал", "readme", "help", "справк", "documentation", "website", "license")
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _, files in os.walk(root):
            for fn in files:
                if not fn.lower().endswith(".lnk"):
                    continue
                name = fn[:-4]
                if any(s in name.lower() for s in skip) or name.lower() in seen:
                    continue
                seen.add(name.lower())
                out.append({"name": name, "target": os.path.join(dirpath, fn)})
                if len(out) >= limit:
                    break
    out.sort(key=lambda x: x["name"].lower())
    return out


def pick_file() -> str:
    """Стандартное окно Windows «Открыть» (вызывается только с этого ПК)."""
    if not WIN:
        return ""

    class OFN(ctypes.Structure):
        _fields_ = [("lStructSize", ctypes.c_ulong), ("hwndOwner", ctypes.c_void_p), ("hInstance", ctypes.c_void_p),
                    ("lpstrFilter", ctypes.c_wchar_p), ("lpstrCustomFilter", ctypes.c_wchar_p),
                    ("nMaxCustFilter", ctypes.c_ulong), ("nFilterIndex", ctypes.c_ulong),
                    ("lpstrFile", ctypes.c_wchar_p), ("nMaxFile", ctypes.c_ulong), ("lpstrFileTitle", ctypes.c_wchar_p),
                    ("nMaxFileTitle", ctypes.c_ulong), ("lpstrInitialDir", ctypes.c_wchar_p),
                    ("lpstrTitle", ctypes.c_wchar_p), ("Flags", ctypes.c_ulong), ("nFileOffset", ctypes.c_ushort),
                    ("nFileExtension", ctypes.c_ushort), ("lpstrDefExt", ctypes.c_wchar_p), ("lCustData", ctypes.c_void_p),
                    ("lpfnHook", ctypes.c_void_p), ("lpTemplateName", ctypes.c_wchar_p), ("pvReserved", ctypes.c_void_p),
                    ("dwReserved", ctypes.c_ulong), ("FlagsEx", ctypes.c_ulong)]

    buf = ctypes.create_unicode_buffer(1024)
    ofn = OFN()
    ofn.lStructSize = ctypes.sizeof(OFN)
    flt = ctypes.create_unicode_buffer(
        "Программы и ярлыки\0*.exe;*.lnk;*.bat;*.cmd;*.url\0Иконки\0*.ico;*.png;*.exe;*.dll\0Все файлы\0*.*\0\0")
    ofn.lpstrFilter = ctypes.cast(flt, ctypes.c_wchar_p)
    ofn.lpstrFile = ctypes.cast(buf, ctypes.c_wchar_p)
    ofn.nMaxFile = 1024
    ofn.lpstrTitle = "PC Monitor — выберите программу"
    ofn.Flags = 0x00001000 | 0x00000800 | 0x00080000  # FILEMUSTEXIST | PATHMUSTEXIST | EXPLORER
    try:
        hwnd = ctypes.windll.user32.GetForegroundWindow()
        ofn.hwndOwner = hwnd
    except Exception:
        pass
    try:
        ctypes.oledll.ole32.CoInitialize(None)
    except OSError:
        pass
    if ctypes.windll.comdlg32.GetOpenFileNameW(ctypes.byref(ofn)):
        return buf.value
    return ""
