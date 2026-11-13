# -*- coding: utf-8 -*-
"""
Что сейчас играет — из системной медиапанели Windows (GSMTC).

Туда публикуют название, исполнителя, обложку и состояние почти все плееры:
Яндекс Музыка, Spotify, браузеры (YouTube, ВК и т. п.), AIMP, «Музыка» Windows…
Через неё же работают кнопки «назад / плей-пауза / вперёд» для того плеера,
который сейчас играет. Нужны пакеты winrt-* (см. requirements.txt); без них
модуль просто сообщает available = False, и агент берёт старый способ.
"""
from __future__ import annotations

import asyncio
import io
import threading
import time
import zlib

COVER = 72   # обложка на экране, пикселей

APP_NAMES = {
    "spotify": "Spotify", "yandex.music": "Яндекс Музыка", "yandexmusic": "Яндекс Музыка", "yandex music": "Яндекс Музыка",
    "chrome": "Chrome", "msedge": "Edge", "firefox": "Firefox", "browser": "Яндекс Браузер", "yandex": "Яндекс Браузер",
    "opera": "Opera", "aimp": "AIMP", "foobar2000": "foobar2000", "vlc": "VLC", "zunemusic": "Музыка",
    "microsoft.zunemusic": "Музыка", "microsoft.media.player": "Медиаплеер", "telegram": "Telegram",
    "discord": "Discord", "itunes": "iTunes", "apple music": "Apple Music", "winamp": "Winamp",
    "vk": "VK Музыка", "potplayer": "PotPlayer",
}


def friendly_app(aumid: str) -> str:
    s = (aumid or "").lower()
    for key, name in APP_NAMES.items():
        if key in s:
            return name
    base = s.split("!")[0].split("\\")[-1].split("_")[0]
    base = base[:-4] if base.endswith(".exe") else base
    return base.capitalize()[:23] if base else ""


def cover_rgb565(data: bytes) -> bytes:
    """Картинка обложки → 72×72 RGB565 (little-endian), обрезка по центру до квадрата."""
    from PIL import Image

    im = Image.open(io.BytesIO(data)).convert("RGB")
    w, h = im.size
    side = min(w, h)
    im = im.crop(((w - side) // 2, (h - side) // 2, (w - side) // 2 + side, (h - side) // 2 + side))
    im = im.resize((COVER, COVER), Image.LANCZOS)
    px = im.tobytes()
    out = bytearray(COVER * COVER * 2)
    for i in range(COVER * COVER):
        r, g, b = px[i * 3], px[i * 3 + 1], px[i * 3 + 2]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[i * 2] = v & 0xFF
        out[i * 2 + 1] = v >> 8
    return bytes(out)


class MediaWatcher(threading.Thread):
    def __init__(self) -> None:
        super().__init__(daemon=True, name="media")
        self.lock = threading.Lock()
        self.available = False
        self.error = ""
        self.state = {"app": "", "title": "", "artist": "", "playing": False, "cover": 0}
        self.cover_png = ""          # для панели (data:image/png;base64,…)
        self.cover_raw = b""         # для экрана (RGB565)
        self._cmds: list[str] = []
        self._thumb_key = ""
        try:
            from winrt.windows.media.control import (  # type: ignore  # noqa: F401
                GlobalSystemMediaTransportControlsSessionManager,
            )

            self.available = True
        except Exception as exc:
            self.error = f"winrt: {exc}"[:80]

    # ---------------------------------------------------------------- API
    def snapshot(self) -> dict:
        with self.lock:
            return dict(self.state)

    def cover(self) -> tuple[int, bytes, str]:
        with self.lock:
            return self.state["cover"], self.cover_raw, self.cover_png

    def command(self, key: str) -> bool:
        """prev / play / next для текущего плеера. False — системная панель недоступна."""
        if not self.available or key not in ("prev", "play", "next"):
            return False
        with self.lock:
            self._cmds.append(key)
        return True

    # ---------------------------------------------------------------- поток
    def run(self) -> None:
        if not self.available:
            return
        try:
            asyncio.run(self._loop())
        except Exception as exc:
            self.available = False
            self.error = f"media: {exc}"[:80]

    async def _loop(self) -> None:
        from winrt.windows.media.control import (  # type: ignore
            GlobalSystemMediaTransportControlsSessionManager as Manager,
        )

        mgr = await Manager.request_async()
        last_poll = 0.0
        while True:
            with self.lock:
                cmds, self._cmds = self._cmds, []
            if cmds:
                sess = mgr.get_current_session()
                for key in cmds:
                    try:
                        if sess is None:
                            continue
                        if key == "play":
                            await sess.try_toggle_play_pause_async()
                        elif key == "next":
                            await sess.try_skip_next_async()
                        elif key == "prev":
                            await sess.try_skip_previous_async()
                    except Exception:
                        pass
                last_poll = 0.0
            if time.time() - last_poll >= 1.0:
                last_poll = time.time()
                try:
                    await self._poll(mgr)
                except Exception as exc:
                    self.error = f"poll: {exc}"[:80]
            await asyncio.sleep(0.1)

    async def _poll(self, mgr) -> None:
        sess = mgr.get_current_session()
        if sess is None:
            with self.lock:
                self.state = {"app": "", "title": "", "artist": "", "playing": False, "cover": 0}
                self.cover_raw, self.cover_png = b"", ""
            self._thumb_key = ""
            return
        app = friendly_app(sess.source_app_user_model_id)
        playing = False
        try:
            playing = int(sess.get_playback_info().playback_status) == 4   # PLAYING
        except Exception:
            pass
        props = await sess.try_get_media_properties_async()
        title = (props.title or "").strip()
        artist = (props.artist or props.album_artist or "").strip()
        key = f"{app}|{title}|{artist}|{props.album_title or ''}"
        cover_id, raw, png = 0, b"", ""
        if key != self._thumb_key:
            data = b""
            if props.thumbnail is not None:
                try:
                    data = await self._read_thumb(props.thumbnail)
                except Exception:
                    data = b""
            if data:
                try:
                    raw = cover_rgb565(data)
                    cover_id = zlib.crc32(raw) & 0x7FFFFFFF or 1
                    png = self._png(data)
                except Exception:
                    raw, cover_id, png = b"", 0, ""
            self._thumb_key = key
            with self.lock:
                self.cover_raw, self.cover_png = raw, png
                self.state["cover"] = cover_id
        with self.lock:
            self.state.update({"app": app, "title": title[:70], "artist": artist[:60], "playing": playing})

    @staticmethod
    async def _read_thumb(ref) -> bytes:
        from winrt.windows.storage.streams import Buffer, InputStreamOptions  # type: ignore

        stream = await ref.open_read_async()
        size = int(stream.size)
        if size <= 0 or size > 8 * 1024 * 1024:
            return b""
        buf = Buffer(size)
        await stream.read_async(buf, size, InputStreamOptions.READ_AHEAD)
        try:
            return bytes(memoryview(buf)[: buf.length])
        except TypeError:
            from winrt.windows.storage.streams import DataReader  # type: ignore

            reader = DataReader.from_buffer(buf)
            out = bytearray(buf.length)
            reader.read_bytes(out)
            return bytes(out)

    @staticmethod
    def _png(data: bytes) -> str:
        import base64

        from PIL import Image

        im = Image.open(io.BytesIO(data)).convert("RGB")
        im.thumbnail((160, 160), Image.LANCZOS)
        b = io.BytesIO()
        im.save(b, "JPEG", quality=85)
        return "data:image/jpeg;base64," + base64.b64encode(b.getvalue()).decode()
