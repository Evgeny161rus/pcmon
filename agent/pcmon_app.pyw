# -*- coding: utf-8 -*-
"""
PC Monitor — программа-компаньон.

Запускает агент (сбор показателей, связь с экраном по USB и Wi-Fi), показывает
значок в трее и собственное окно с панелью (WebView2 через pywebview; если его
нет — панель открывается окном Edge без адресной строки).

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

FROZEN = bool(getattr(sys, "frozen", False))
HERE = os.path.dirname(os.path.abspath(sys.executable if FROZEN else __file__))
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


def ask_first_copy_to_show() -> None:
    try:
        req = urllib.request.Request(URL + "api/app", data=b'{"show":true}', method="POST",
                                     headers={"Content-Type": "application/json"})
        urllib.request.urlopen(req, timeout=2).read()
    except Exception:
        open_panel()


def make_tray(on_open, on_exit):
    import pystray

    def auto_checked(item):
        return sa.autostart_state()["autostart"]

    def on_auto(icon, item):
        sa.autostart_set(not sa.autostart_state()["autostart"])

    menu = pystray.Menu(
        pystray.MenuItem("Открыть PC Monitor", lambda icon, item: on_open(), default=True),
        pystray.MenuItem("Запускать вместе с Windows", on_auto, checked=auto_checked),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Выход", lambda icon, item: on_exit(icon)),
    )
    return pystray.Icon("pcmon", tray_image(), "PC Monitor", menu)


def run_window(start_hidden: bool) -> bool:
    """Собственное окно программы (WebView2). False — если pywebview недоступен."""
    try:
        import webview  # type: ignore
    except Exception:
        return False

    state = {"quit": False}
    win = webview.create_window("PC Monitor", URL, width=1060, height=740, min_size=(860, 580),
                                hidden=start_hidden, background_color="#0D0F13", text_select=True)

    def show():
        try:
            win.show()
            win.restore()
        except Exception:
            pass

    def on_closing():
        if state["quit"]:
            return True
        win.hide()          # крестик прячет окно в трей, программа работает дальше
        return False

    win.events.closing += on_closing
    sa.SHOW_HOOK = show

    def on_exit(icon):
        state["quit"] = True
        icon.stop()
        try:
            win.destroy()
        except Exception:
            os._exit(0)

    try:
        tray = make_tray(show, on_exit)
        tray.run_detached()
    except Exception:
        pass
    webview.start(private_mode=False, storage_path=os.path.join(HERE, ".webview"))
    return True


def main() -> int:
    start_hidden = "--tray" in sys.argv
    if already_running(app_only=True):
        # вторая копия не нужна: просим первую показать окно
        if not start_hidden:
            ask_first_copy_to_show()
        return 0
    stop_other_agents()

    if FROZEN:
        sa.AUTOSTART_CMD = f'"{sys.executable}" --tray'
    else:
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

    if run_window(start_hidden):
        return 0

    # запасной вариант без pywebview: панель в окне Edge, значок в трее
    sa.SHOW_HOOK = open_panel
    if not start_hidden:
        open_panel()
    try:
        icon = make_tray(open_panel, lambda ic: ic.stop())
    except Exception:
        while True:          # без значка в трее: работаем, пока не завершён процесс
            time.sleep(3600)
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
