#ifndef RepoRoot
  #error RepoRoot must be supplied by Build-WindowsInstaller.ps1
#endif
#ifndef PayloadRoot
  #error PayloadRoot must be supplied by Build-WindowsInstaller.ps1
#endif
#ifndef AppVersion
  #define AppVersion "3.0.0-preview.7"
#endif
#ifndef NumericVersion
  #define NumericVersion "3.0.0.7"
#endif
#ifndef OutputRoot
  #define OutputRoot RepoRoot + "\dist"
#endif
#ifdef TestBuild
  #ifndef TestRoot
    #error TestRoot is required for an isolated test build
  #endif
  #ifndef TestAppId
    #error TestAppId is required for an isolated test build
  #endif
  #define InstallerAppId TestAppId
  #define InstallerName "CodexQuotaStrip Installer Test"
  #define OutputName "CodexQuotaStrip-windows-x64-test-setup"
  #define StartMenuRoot TestRoot + "\start-menu"
  #define DesktopRoot TestRoot + "\desktop"
#else
  ; 正式版标识固定，升级沿用同一卸载日志；测试版必须使用独立标识。
  #define InstallerAppId "CodexQuotaStrip.Windows.x64"
  #define InstallerName "CodexQuotaStrip"
  #define OutputName "CodexQuotaStrip-windows-x64-setup"
  #define StartMenuRoot "{group}"
  #define DesktopRoot "{userdesktop}"
#endif

[Setup]
AppId={#InstallerAppId}
AppName={#InstallerName}
AppVersion={#AppVersion}
VersionInfoVersion={#NumericVersion}
AppPublisher=CodexQuotaStrip contributors
AppCopyright=CodexQuotaStrip contributors
#ifdef TestBuild
DefaultDirName={#TestRoot}\app
#else
DefaultDirName={localappdata}\CodexQuotaStrip
#endif
DefaultGroupName=CodexQuotaStrip
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputRoot}
OutputBaseFilename={#OutputName}
LicenseFile={#RepoRoot}\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableDirPage=no
DisableProgramGroupPage=no
AllowNoIcons=yes
UninstallDisplayIcon={app}\CodexQuotaStrip.exe
SetupIconFile={#RepoRoot}\assets\icons\app.ico
UninstallDisplayName={#InstallerName}
; 不使用 Restart Manager 关闭程序，更不安装登录自启或修改 Codex 启动 hook。
CloseApplications=no
RestartApplications=no
UninstallLogMode=append
SetupMutex=CodexQuotaStripInstaller-{#InstallerAppId}
#ifdef TestBuild
UsePreviousAppDir=no
UsePreviousGroup=no
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"

[CustomMessages]
english.SettingsShortcut=CodexQuotaStrip Settings
chinesesimplified.SettingsShortcut=Codex 配额设置
english.UninstallShortcut=Uninstall CodexQuotaStrip
chinesesimplified.UninstallShortcut=卸载 Codex 配额
english.DesktopTask=Create a desktop shortcut
chinesesimplified.DesktopTask=创建桌面快捷方式
english.LaunchApp=Launch CodexQuotaStrip
chinesesimplified.LaunchApp=启动 Codex 配额
english.CloseApp=CodexQuotaStrip from this installation folder is still running. Exit it from its tray menu, then retry, or cancel. No application will be closed automatically.
chinesesimplified.CloseApp=此安装目录中的 Codex 配额仍在运行。请通过托盘菜单退出后重试，或取消操作。安装程序不会自动关闭任何程序。
english.ProcessCheckFailed=Unable to check running processes. Cancel and retry after Windows Management Instrumentation is available.
chinesesimplified.ProcessCheckFailed=无法检查运行中的进程。请取消操作，并在 Windows Management Instrumentation 可用后重试。
english.InstallInfo=Windows 10 1809 or later is required. This is an x64 application. Windows 11 ARM64 may run it through x64 emulation; ARM64 has not been tested on physical hardware.%n%nNo compiler or Visual C++ runtime installation is required. Setup does not enable login startup or change your Codex launcher.%n%nUpgrades and uninstall preserve settings.ini and other user-created files. Codex account data and existing custom startup hooks are untouched.
chinesesimplified.InstallInfo=需要 Windows 10 1809 或更新版本。本程序为 x64 应用。Windows 11 ARM64 可以通过 x64 模拟运行，但 ARM64 尚未经过实机验证。%n%n无需安装编译器或 Visual C++ 运行库。安装程序不会启用登录自启或修改 Codex 启动器。%n%n升级和卸载保留 settings.ini 及其他用户自建文件，不修改 Codex 账号数据和已有自定义启动 hook。

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopTask}"; Flags: unchecked

[Files]
; 逐项登记安装文件；设置文件不进入安装/卸载清单，也不使用通配符删除。
Source: "{#PayloadRoot}\CodexQuotaStrip.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadRoot}\Start-With-Codex.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadRoot}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{#StartMenuRoot}\CodexQuotaStrip"; Filename: "{app}\CodexQuotaStrip.exe"; WorkingDir: "{app}"
Name: "{#StartMenuRoot}\{cm:SettingsShortcut}"; Filename: "{app}\CodexQuotaStrip.exe"; Parameters: "--settings"; WorkingDir: "{app}"
Name: "{#StartMenuRoot}\{cm:UninstallShortcut}"; Filename: "{uninstallexe}"
Name: "{#DesktopRoot}\CodexQuotaStrip"; Filename: "{app}\CodexQuotaStrip.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\CodexQuotaStrip.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent unchecked

#ifdef TestBuild
[Registry]
Root: HKCU64; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\{#InstallerAppId}_is1"; ValueType: string; ValueName: "QuotaStripInstallerTestRoot"; ValueData: "{#TestRoot}"; Flags: uninsdeletevalue
#endif

[Code]
#ifdef TestBuild
procedure CurStepChanged(CurStep: TSetupStep);
begin
  // Inno 在 [Registry] 后删除重建自己的卸载键；归属标记必须等注册完成后写回。
  // [Registry] 仍登记其 64 位卸载动作，标记与真实卸载项保持同一视图和生命周期。
  if CurStep = ssPostInstall then
    if not RegWriteStringValue(HKCU64,
      'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#InstallerAppId}_is1',
      'QuotaStripInstallerTestRoot', '{#TestRoot}') then
      RaiseException('Unable to record isolated test installation ownership.');
end;
#endif

// home 决定程序 mutex 名称，因此按完整 exe 路径检查，不调用可能影响其他实例的 --quit。
function RunningAppMessage: String;
var
  Locator, Services, Processes, Process, ExecutablePath: Variant;
  Index: Integer;
  Target: String;
begin
  Result := '';
  Target := ExpandConstant('{app}\CodexQuotaStrip.exe');
  if not FileExists(Target) then exit;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Services := Locator.ConnectServer('', 'root\CIMV2');
    Processes := Services.ExecQuery('SELECT ExecutablePath FROM Win32_Process WHERE Name = ''CodexQuotaStrip.exe''');
    for Index := 0 to Processes.Count - 1 do begin
      Process := Processes.ItemIndex(Index);
      ExecutablePath := Process.ExecutablePath;
      if not VarIsNull(ExecutablePath) then
        if CompareText(ExecutablePath, Target) = 0 then begin
          Result := CustomMessage('CloseApp');
          exit;
        end;
    end;
  except
    Log('Process check failed: ' + GetExceptionMessage);
    Result := CustomMessage('ProcessCheckFailed');
  end;
end;

procedure InitializeWizard;
var
  InfoPage: TOutputMsgWizardPage;
  InfoText: String;
begin
  InfoText := CustomMessage('InstallInfo');
  StringChangeEx(InfoText, '%n', #13#10, True);
  InfoPage := CreateOutputMsgPage(wpWelcome, SetupMessage(msgWizardInfoBefore),
    '{#InstallerName}', InfoText);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
#ifdef TestBuild
  // 测试包拒绝跳出预先编译的隔离目录；不能复用正式安装项或桌面目录。
  if CompareText(RemoveBackslashUnlessRoot(ExpandConstant('{app}')),
      '{#TestRoot}\app') <> 0 then begin
    Result := 'Test installer requires its compiled isolated installation folder.';
    exit;
  end;
#endif
  Result := RunningAppMessage;
end;

function InitializeUninstall: Boolean;
var
  MessageText: String;
begin
  Result := False;
  // 静默模式下直接失败，交互模式保留重试/取消，不强制终止进程。
  while True do begin
    MessageText := RunningAppMessage;
    if MessageText = '' then begin
      Result := True;
      exit;
    end;
    if UninstallSilent then begin
      Log(MessageText);
      exit;
    end;
    if SuppressibleMsgBox(MessageText, mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then exit;
  end;
end;
