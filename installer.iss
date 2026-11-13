; Установщик PC Monitor (Inno Setup 6). Собирается в GitHub Actions:
;   iscc /DAppVer=1.7.0 installer.iss
; Берёт папочную сборку PyInstaller из dist-onedir\PCMonitor — Python на ПК не нужен.
; Ставится без прав администратора в %LOCALAPPDATA%\Programs\PC Monitor.

#ifndef AppVer
  #define AppVer "0.0.0"
#endif

[Setup]
AppId={{3E4D6DF8-5CD4-4F3A-B809-DEF74D250A4D}
AppName=PC Monitor
AppVersion={#AppVer}
AppVerName=PC Monitor {#AppVer}
AppPublisher=PC Monitor
AppPublisherURL=https://github.com/Evgeny161rus/pcmon
DefaultDirName={localappdata}\Programs\PC Monitor
DefaultGroupName=PC Monitor
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=dist
OutputBaseFilename=PCMonitor-Setup
SetupIconFile=agent\pcmon.ico
UninstallDisplayIcon={app}\PCMonitor.exe
UninstallDisplayName=PC Monitor
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=force
RestartApplications=no

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "autostart"; Description: "Запускать вместе с Windows"
Name: "desktopicon"; Description: "Значок на рабочем столе"; Flags: unchecked

[Files]
Source: "dist-onedir\PCMonitor\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "build\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: NeedsWebView2

[Icons]
Name: "{group}\PC Monitor"; Filename: "{app}\PCMonitor.exe"
Name: "{group}\Удалить PC Monitor"; Filename: "{uninstallexe}"
Name: "{userdesktop}\PC Monitor"; Filename: "{app}\PCMonitor.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "PCMonitor"; ValueData: """{app}\PCMonitor.exe"" --tray"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; StatusMsg: "Устанавливаю Microsoft Edge WebView2…"; Check: NeedsWebView2; Flags: waituntilterminated
Filename: "{app}\PCMonitor.exe"; Description: "Запустить PC Monitor"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/IM PCMonitor.exe /F"; Flags: runhidden; RunOnceId: "StopPCMonitor"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
const
  WV2 = 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';
  WV2_32 = 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';

function HasVer(Root: Integer; Key: String): Boolean;
var
  V: String;
begin
  Result := RegQueryStringValue(Root, Key, 'pv', V) and (V <> '') and (V <> '0.0.0.0');
end;

// WebView2 (движок окна программы) есть почти на всех Windows 10/11; если нет — поставим.
function NeedsWebView2: Boolean;
begin
  Result := not (HasVer(HKLM, WV2_32) or HasVer(HKLM, WV2) or HasVer(HKCU, WV2));
end;

// Перед обновлением закрываем работающую копию, чтобы файлы не были заняты.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM PCMonitor.exe /F', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(800);
  Result := '';
end;
