# -*- coding: utf-8 -*-
"""
PC Monitor — программа-компаньон.

Запускает агент (сбор показателей, связь с экраном по USB и Wi-Fi), показывает
значок в трее и открывает панель управления отдельным окном.

Запуск: двойной щелчок по «PC Monitor.bat» в папке проекта.
Ключ --tray: стартовать свёрнутой в трей (так запускается из автозагрузки).
"""
import os
import shutil
import subprocess
import sys
import threading
import time
import urllib.request
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.chdir(HERE)

# Под pythonw.exe нет консоли: print() не должен ронять программу.
if sys.stdout is None:
    sys.stdout = open(os.devnull, "w", encoding="utf-8")
if sys.stderr is None:
    sys.stderr = open(os.path.join(HERE, "pcmon_app.log"), "a", encoding="utf-8")

import sysmon_agent as sa  # noqa: E402

PORT = 8123
URL = f"http://127.0.0.1:{PORT}/"


def already_running(app_only: bool = False) -> bool:
    """Отвечает ли агент на этом ПК; app_only — только если это сама программа, а не агент без окна."""
    try:
        import json

        with urllib.request.urlopen(URL + "api/health", timeout=1.5) as r:
            info = json.loads(r.read().decode("utf-8"))
            return bool(info.get("app")) if app_only else True
    except Exception:
        return False


def stop_other_agents() -> None:
    """Останавливает агент, запущенный отдельно (например, скриптом сборки)."""
    try:
        import psutil
    except Exception:
        return
    me = os.getpid()
    for p in psutil.process_iter(["pid", "name", "cmdline"]):
        try:
            cmd = " ".join(p.info.get("cmdline") or [])
            name = (p.info.get("name") or "").lower()
            if p.info["pid"] != me and "python" in name and ("sysmon_agent.py" in cmd or "pcmon_app.pyw" in cmd):
                p.kill()
        except Exception:
            continue
    time.sleep(1.0)


def find_edge() -> str:
    for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles"), os.environ.get("LocalAppData")):
        if not base:
            continue
        path = os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe")
        if os.path.exists(path):
            return path
    return shutil.which("msedge") or ""


def open_panel() -> None:
    """Панель — отдельное окно без адресной строки (режим приложения Edge)."""
    edge = find_edge()
    if edge:
        try:
            subprocess.Popen([edge, f"--app={URL}", "--window-size=920,760"])
            return
        except OSError:
            pass
    webbrowser.open(URL)


def tray_image():
    from PIL import Image, ImageDraw

    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((2, 2, 62, 62), 14, fill=(14, 16, 20, 255))
    d.arc((12, 12, 52, 52), 135, 405, fill=(58, 63, 71, 255), width=6)
    d.arc((12, 12, 52, 52), 135, 300, fill=(255, 159, 28, 255), width=6)
    d.ellipse((27, 27, 37, 37), fill=(255, 159, 28, 255))
    return img


def main() -> int:
    start_hidden = "--tray" in sys.argv
    if already_running(app_only=True):
        # вторая копия не нужна: просто показываем окно первой
        if not start_hidden:
            open_panel()
        return 0
    stop_other_agents()

    pyw = sys.executable
    if pyw.lower().endswith("python.exe"):
        cand = pyw[:-10] + "pythonw.exe"
        if os.path.exists(cand):
            pyw = cand
    sa.AUTOSTART_CMD = f'"{pyw}" "{os.path.abspath(__file__)}" --tray'

    args = sa.parse_args(["--quiet"])
    agent = sa.Agent(args)
    threading.Thread(target=agent.start, daemon=True, name="agent").start()

    for _ in range(40):
        if already_running():
            break
        time.sleep(0.25)
    if not start_hidden:
        open_panel()

    try:
        import pystray
    except Exception:
        # без значка в трее: программа работает, пока не завершён процесс
        while True:
            time.sleep(3600)

    def on_open(icon, item):
        open_panel()

    def on_exit(icon, item):
        icon.stop()

    def auto_checked(item):
        return sa.autostart_state()["autostart"]

    def on_auto(icon, item):
        sa.autostart_set(not sa.autostart_state()["autostart"])

    menu = pystray.Menu(
        pystray.MenuItem("Открыть панель", on_open, default=True),
        pystray.MenuItem("Запускать вместе с Windows", on_auto, checked=auto_checked),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Выход", on_exit),
    )
    icon = pystray.Icon("pcmon", tray_image(), "PC Monitor", menu)
    icon.run()
    return 0


if __name__ == "__main__":
    try:
        code = main()
    except Exception as exc:  # pragma: no cover
        with open(os.path.join(HERE, "pcmon_app.log"), "a", encoding="utf-8") as f:
            f.write("%s fatal: %r\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), exc))
        code = 1
    os._exit(code)
