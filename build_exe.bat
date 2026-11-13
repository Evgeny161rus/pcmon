@echo off
setlocal
cd /d "%~dp0"

where python >nul 2>nul || (echo Python 3.10+ was not found in PATH.& exit /b 1)

if not exist ".venv\Scripts\python.exe" python -m venv .venv
if errorlevel 1 goto :err

".venv\Scripts\python.exe" -m pip install --upgrade pip
if errorlevel 1 goto :err
".venv\Scripts\python.exe" -m pip install -r agent\requirements.txt pyinstaller
if errorlevel 1 goto :err

rem One-file EXE; Python is not required on the target PC.
".venv\Scripts\python.exe" -m PyInstaller --noconfirm --clean --onefile --windowed --noupx --name PCMonitor ^
  --collect-all pycaw --collect-all comtypes --collect-all pystray ^
  --collect-all webview --collect-all clr_loader --collect-all pythonnet --hidden-import clr --collect-all winrt ^
  --icon "%~dp0agent\pcmon.ico" --hidden-import pystray._win32 agent\pcmon_app.pyw
if errorlevel 1 goto :err

rem Folder build can be preferable when antivirus flags a one-file executable.
".venv\Scripts\python.exe" -m PyInstaller --noconfirm --clean --onedir --windowed --noupx --name PCMonitor ^
  --distpath dist-onedir --workpath build-onedir --specpath build-onedir ^
  --collect-all pycaw --collect-all comtypes --collect-all pystray ^
  --collect-all webview --collect-all clr_loader --collect-all pythonnet --hidden-import clr --collect-all winrt ^
  --icon "%~dp0agent\pcmon.ico" --hidden-import pystray._win32 agent\pcmon_app.pyw
if errorlevel 1 goto :err

if not exist dist mkdir dist
".venv\Scripts\python.exe" -c "import shutil; shutil.make_archive('dist/PCMonitor-folder', 'zip', 'dist-onedir', 'PCMonitor')"
if errorlevel 1 goto :err

echo.
echo Build complete:
echo   dist\PCMonitor.exe
echo   dist\PCMonitor-folder.zip
exit /b 0

:err
echo.
echo PC Monitor build failed.
exit /b 1


