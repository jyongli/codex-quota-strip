# Codex Quota Strip

**中文 / English**

Windows 上的轻量 Codex 配额工具，在系统托盘显示 **5 小时剩余配额** 和 **周剩余配额**，可选在 Codex 窗口顶部或左侧显示配额条。

A lightweight Windows utility displaying **remaining five-hour quota** and **remaining weekly quota** in the system tray, with an optional strip at the top or left of the Codex window.

使用 C++17、Win32 和 GDI 构建，不依赖 Electron 或 WebView。它是独立的辅助程序，与 OpenAI 没有官方隶属关系。**当前程序界面为中文，文档提供中英双语。**

Built with C++17, Win32 and GDI, without Electron or WebView. This is an independent companion, not affiliated with OpenAI. **The application UI is currently Chinese; this guide is bilingual.**

![配置窗口 / Settings window](docs/images/configuration.png)

## 功能 / Features

- 两个数字托盘图标显示剩余百分比，缺失字段显示 `-`。 / Two numeric tray icons show remaining percentages; missing fields display `-`.
- 顶部／左侧配额条仅在 Codex 处于前台时显示；切换应用仍保留托盘数字。 / The optional top/left strip appears while Codex is in the foreground; tray icons remain when switching applications.
- 原生深色配置窗口，可设刷新间隔、旧数据提示时间、查询条件和显示位置。 / Native dark settings window for refresh interval, stale-data threshold, query conditions and display position.
- 默认每 120 秒在线查询，支持 30–3600 秒；旧数据提示默认 5 分钟，支持 1–120 分钟。 / Queries default to every 120 seconds, configurable from 30–3600 seconds; the stale threshold defaults to 5 minutes, configurable from 1–120 minutes.
- 通过本机已登录的 `codex.exe app-server --stdio` 查询；本地会话额度事件用于启动及离线回退。 / Queries use locally authenticated `codex.exe app-server --stdio`; local session events provide startup and offline snapshots.
- Windows 文件事件驱动、增量读取，无录屏或截图监控；查询子进程在完成、超时或退出时清理。 / Event-driven file notifications and incremental reads, without screen recording or screenshot monitoring; query subprocesses are cleaned up on completion, timeout or exit.
- 同一数据目录重复启动复用一个实例。 / Repeated launches with the same data directory reuse one instance.

## 环境支持 / Environments

| 环境 / Environment | 配置与限制 / Setup and limitations |
| --- | --- |
| Windows x64 + Codex 桌面版 / desktop | 主要使用场景；先打开并登录 Codex，需可发现兼容的 `codex.exe`。 / Primary use case; open Codex and sign in first. A compatible `codex.exe` must be discoverable. |
| Windows PowerShell 5.1 | 使用 `powershell.exe`，已验证构建入口。 / Use `powershell.exe`; the build entry point has been verified. |
| Windows PowerShell 7 | 使用 `pwsh.exe`，已验证构建及测试入口。 / Use `pwsh.exe`; build and test entry points have been verified. |
| CMD / Git Bash | 调用 `powershell.exe`，不直接在 Bash 中运行 `.ps1`。 / Invoke `powershell.exe`; do not execute `.ps1` directly in Bash. |
| 原生 Windows Codex CLI / Native Windows CLI | 条件支持；需运行中的兼容 `codex.exe` 或下文回退目录，通常使用仅托盘模式。 / Conditional support; requires a running compatible `codex.exe` or the fallback directory below. Usually use tray-only mode. |
| WSL / Linux / macOS | 无原生托盘版本；不能自动发现 WSL 内的 Codex 进程或 Linux 可执行文件。 / No native tray version; Codex processes and Linux executables inside WSL are not automatically discovered. |
| Windows ARM64 / 无桌面服务器 / headless servers | 当前仅输出 x64；ARM64 模拟未验证，界面和集成测试需要交互式桌面。 / Builds target x64; ARM64 emulation is unverified. UI and integration tests require an interactive desktop. |

## 安装与构建 / Install and build

以下命令默认在**仓库根目录**执行。ZIP 源码需先解压，并进入包含 `Build.ps1` 的目录。当前仓库提供源码，构建产物位于 `build\`。

Run commands from the **repository root** unless stated otherwise. For a source ZIP, extract it and open the directory containing `Build.ps1`. This repository provides source; artifacts go into `build\`.

### 1. 安装依赖 / Install prerequisites

需要 Git、Windows PowerShell 5.1 或 PowerShell 7、Visual Studio 2022 C++ Build Tools／完整 Visual Studio 的 C++ 桌面开发组件，以及 Windows SDK。已有 Visual Studio 时，在 Installer 中“修改”并补齐组件。

You need Git, Windows PowerShell 5.1 or PowerShell 7, Visual Studio 2022 C++ Build Tools or full Visual Studio with C++ desktop development, and Windows SDK. For existing Visual Studio installations, use **Modify** in the Installer to add the required components.

支持 WinGet 的电脑可使用以下命令，安装器可能要求管理员权限。PowerShell 7 为可选项；安装完成后重新打开终端。

If WinGet is available, use the commands below. Installers may request administrator privileges. PowerShell 7 is optional. Reopen the terminal after installation.

```powershell
winget install --id Git.Git --exact --source winget
# PowerShell 7 可选 / Optional; Windows PowerShell 5.1 also works.
winget install --id Microsoft.PowerShell --exact --source winget
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

没有 WinGet 时可手动安装。官方说明 / Without WinGet, install manually using the official guides: [Git](https://git-scm.com/install/windows), [PowerShell](https://learn.microsoft.com/en-us/powershell/scripting/install/install-powershell-on-windows), [Microsoft C++](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170). 构建工具安装参数 / Build Tools installation flags: [Visual Studio command reference](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022).

### 2. 获取源码 / Get the source

```powershell
git clone https://github.com/jyongli/codex-quota-strip.git
Set-Location .\codex-quota-strip
```

### 3. 按终端环境构建 / Build with your shell

Windows PowerShell 5.1：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1
```

PowerShell 7：

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1
```

CMD / Git Bash，在仓库根目录 / from the repository root：

```sh
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./Build.ps1
```

脚本通过 `vswhere` 查找 C++ 工具并配置 MSVC／SDK 路径，普通 PowerShell 即可，无需 Developer Command Prompt。会选择找到的最新安装／工具目录，暂不提供工具链版本固定参数。输出 `build\CodexQuotaStrip.exe` 与 `build\Start-With-Codex.ps1`；运行时不需要编译器。

The script uses `vswhere` to discover C++ tools and sets MSVC/SDK paths. A regular PowerShell session is sufficient; Developer Command Prompt is unnecessary. It selects the latest installation/tool directories it finds and has no toolchain-version pinning option. Outputs are `build\CodexQuotaStrip.exe` and `build\Start-With-Codex.ps1`; running the app does not require a compiler.

### 4. 启动与配置 / Launch and configure

先打开 Codex 并登录，再运行。右键任意托盘图标也可打开配置、立即刷新或退出。

Open Codex and sign in first, then run. Right-click either tray icon to open settings, refresh immediately or exit.

```powershell
.\build\CodexQuotaStrip.exe --settings
```

## 多环境配置 / Environment configuration

以下启动示例应在对应实例尚未运行时执行。若需改变已有实例的启动参数或环境，先通过托盘退出，或执行 `--quit` 并等待进程结束，再运行新的启动命令。

Run the launch examples when the matching instance is not already running. To change its arguments or environment, exit through the tray, or use `--quit` and wait for the process to finish before launching again.

### A. 桌面版：商店安装／本地安装 / Desktop: Store or local installation

1. 打开并登录 Codex，保持窗口打开。 / Open Codex, sign in and keep its window open.
2. 打开辅助程序配置，启用在线查询。 / Open companion settings and enable online queries.
3. 选择仅托盘或托盘 + Codex 内显示，保存并应用。 / Choose tray-only or tray plus in-Codex display, then save and apply.
4. 点击“立即刷新”，检查来源、数据时间和状态。 / Click **Refresh now** and inspect the source, timestamp and status.

查询程序发现顺序：**运行中的 Windows `codex.exe` → `%LOCALAPPDATA%\OpenAI\Codex\bin\codex.exe` 或其一层版本子目录**。当前不单独搜索 `PATH`，没有 `--codex-path` 参数。商店安装或其他布局需要先运行可发现的进程；并非所有版本／布局都已验证。

Discovery order: **running Windows `codex.exe` → `%LOCALAPPDATA%\OpenAI\Codex\bin\codex.exe` or its one-level version subdirectories**. There is no separate `PATH` search or `--codex-path` option. Store or other layouts require a discoverable process to be running; not all versions/layouts have been verified.

建议从桌面快捷方式或普通 Windows 终端启动。某些打包应用的终端会重定向 `LOCALAPPDATA`，导致设置目录与普通桌面启动不同。

Launch from a desktop shortcut or regular Windows terminal. Terminals in some packaged applications redirect `LOCALAPPDATA`, resulting in a different settings directory from a regular desktop launch.

### B. 原生 Windows CLI / Native Windows CLI

先启动并登录原生 Windows CLI，保持其 `codex.exe` 运行，再启动辅助程序。配置中取消“仅在 Codex 运行时定时查询”，保存并应用；此条件依据桌面窗口识别，只有 CLI 时可能阻止定时查询。

Start and sign in to the native Windows CLI, keep its `codex.exe` running, then start the companion. Disable **Query only while Codex is running** and save/apply; this condition detects the desktop window and may prevent scheduled queries with only the CLI running.

```powershell
.\build\CodexQuotaStrip.exe --taskbar --settings
```

CLI 没有可附着的 Codex 桌面窗口，使用仅托盘模式。CLI 退出后，如果不在上述回退目录，后续查询可能无法发现可执行文件。

Use tray-only mode: the CLI has no Codex desktop window to attach to. After the CLI exits, later queries may fail to find the executable unless it is in the fallback directory above.

### C. 自定义数据目录／账号环境 / Custom data directories or accounts

`--home` 优先于 `CODEX_HOME`；均未设置时使用 `%USERPROFILE%\.codex`。指定与要查询的账号一致、已由 Codex 初始化的数据目录。

`--home` takes precedence over `CODEX_HOME`; the default is `%USERPROFILE%\.codex`. Use the account's data directory, already initialized through Codex.

```powershell
.\build\CodexQuotaStrip.exe --home "D:\CodexData\account-a" --settings
```

退出该实例时使用同一目录 / To exit this instance, use the same directory：

```powershell
.\build\CodexQuotaStrip.exe --home "D:\CodexData\account-a" --quit
```

或者设置当前 PowerShell 会话环境 / Or set the current PowerShell session environment：

```powershell
$env:CODEX_HOME = 'D:\CodexData\account-a'
.\build\CodexQuotaStrip.exe --settings
```

`--home` 仅改变 Codex 数据目录，**不会隔离辅助程序设置**；同一 Windows 用户的实例共享 `%LOCALAPPDATA%\CodexQuotaStrip\settings.ini`。实例身份按数据目录区分。`--settings` 可打开已有配置，其他启动参数不会改变已运行实例；切换前先退出对应实例。

`--home` changes Codex data only; **companion settings are not isolated**. Instances under the same Windows user share `%LOCALAPPDATA%\CodexQuotaStrip\settings.ini`. Instance identity is based on the data directory. `--settings` opens an existing instance's settings; other arguments do not reconfigure a running instance. Exit the matching instance before changing arguments.

### D. 代理与受限网络 / Proxies and restricted networks

查询子进程继承辅助程序启动时的环境。以下仅设置当前会话；端口为示例，需替换为实际可用的 HTTP 代理。

Query subprocesses inherit the companion's launch environment. This configures the current session only; replace the example address/port with your working HTTP proxy.

```powershell
$env:HTTPS_PROXY = 'http://127.0.0.1:7890'
$env:HTTP_PROXY = $env:HTTPS_PROXY
.\build\CodexQuotaStrip.exe --settings
```

变量是否生效取决于所发现的 Codex 版本和网络配置；程序不设置系统代理。修改变量后需从该终端重新启动，已有实例不会继承新环境。

Whether these variables are honored depends on the discovered Codex version and network configuration. The companion does not configure the system proxy. Restart from this terminal after changes; existing instances do not inherit new variables.

### E. 本地事件模式 / Local-event mode

```powershell
.\build\CodexQuotaStrip.exe --local-only --settings
```

此参数初始关闭在线查询；如需持续本地模式，请保持配置中的在线查询开关关闭。数据依赖新会话事件，无法保证闲置时仍是最新账号额度。

This initially disables online queries. Keep the online-query checkbox disabled to remain in local mode. Data depends on new session events, so account quotas may not be current while idle.

## 刷新与显示设置 / Refresh and display settings

推荐在配置窗口“保存并应用”。默认 INI 内容如下，字段与单位见表格。

Use **Save and apply** in settings for immediate changes. Default INI contents and field meanings:

```ini
[refresh]
online=1
only_with_codex=1
seconds=120
stale_minutes=5

[display]
surface=taskbar
position=top
```

| 字段 / Field | 默认 / Default | 含义与范围 / Meaning and range |
| --- | --- | --- |
| `online` | `1` | `1` 在线查询，`0` 本地事件 / online queries or local events |
| `only_with_codex` | `1` | 定时查询需识别到 Codex 桌面窗口；手动刷新可跳过 / require a detected desktop window for scheduled queries; manual refresh bypasses this condition |
| `seconds` | `120` | 查询间隔 30–3600 秒 / query interval, 30–3600 seconds |
| `stale_minutes` | `5` | 旧快照阈值 1–120 分钟，不触发额外查询 / stale threshold, 1–120 minutes; does not trigger extra queries |
| `surface` | `taskbar` | `taskbar` 或 / or `taskbar-and-codex` |
| `position` | `top` | `top` 或 / or `left`，仅配额条生效 / applies only to the strip |

手动编辑前退出对应实例并等待结束，再备份；保存后重启。可先在配置窗口保存一次生成文件。

Before manual edits, exit the matching instance and wait for it to finish, then back up the file; restart after saving. Generate the file first by saving from the settings window.

```powershell
$quotaConfig = Join-Path $env:LOCALAPPDATA 'CodexQuotaStrip\settings.ini'
if (Test-Path -LiteralPath $quotaConfig) {
    Copy-Item -LiteralPath $quotaConfig -Destination ($quotaConfig + '.before-edit-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.bak')
    notepad.exe $quotaConfig
}
```

在记事本保存并关闭后重新启动 / After saving and closing Notepad, restart：

```powershell
.\build\CodexQuotaStrip.exe
```

在线失败退避最长 15 分钟；手动查询至少间隔 5 秒。数值表示**剩余配额**，按上游窗口时长区分 5 小时和周配额。缺失显示 `-`；灰色表示旧快照，失败保留最后快照，不保证逐秒同步。

Failures back off up to 15 minutes; manual queries are at least 5 seconds apart. Values represent **remaining quota**, identified by upstream window duration. Missing fields show `-`; gray values indicate stale snapshots. Failures retain the last snapshot, and updates are not guaranteed every second.

## 启动方式 / Startup options

### 便携运行／启动器调用 / Portable use or launcher integration

将构建产物保存在固定目录。此入口遵循已保存设置，启动后退出 PowerShell，不留下脚本监控进程；只启动辅助程序，不负责启动 Codex 或修改通用 Codex 快捷方式。

Keep artifacts in a fixed directory. This entry point respects saved preferences and exits PowerShell after launch, without a persistent script-monitor process. It starts the companion only, without launching Codex or modifying arbitrary Codex shortcuts.

```powershell
powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File .\build\Start-With-Codex.ps1
```

### 桌面快捷方式 / Desktop shortcut

使用 Windows 返回的桌面目录，支持桌面重定向。移动程序后需更新快捷方式。

Use the Windows-provided desktop directory, including redirected desktops. Update the shortcut if you move the executable.

```powershell
$quotaExe = (Resolve-Path .\build\CodexQuotaStrip.exe).Path
$quotaDesktop = [Environment]::GetFolderPath('Desktop')
$quotaShortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $quotaDesktop 'Codex Quota.lnk'))
$quotaShortcut.TargetPath = $quotaExe
$quotaShortcut.WorkingDirectory = Split-Path -Parent $quotaExe
$quotaShortcut.Save()
```

### Windows 登录自启 / Start at Windows sign-in

在当前用户启动目录创建快捷方式。此方式**随 Windows 登录启动**，不等同于只随 Codex 启动；默认 `only_with_codex=1` 限制桌面版未打开时的定时在线查询。

Create a shortcut in the current user's Startup folder. This starts **at Windows sign-in**, not exclusively with Codex; the default `only_with_codex=1` limits scheduled queries while the desktop app is not open.

```powershell
$quotaExe = (Resolve-Path .\build\CodexQuotaStrip.exe).Path
$quotaStartup = [Environment]::GetFolderPath('Startup')
$quotaShortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $quotaStartup 'Codex Quota.lnk'))
$quotaShortcut.TargetPath = $quotaExe
$quotaShortcut.WorkingDirectory = Split-Path -Parent $quotaExe
$quotaShortcut.Save()
```

取消自启时，在上述 Startup 目录移除快捷方式。 / To disable sign-in startup, remove this shortcut from that Startup directory.

### 特定自定义启动器 / Specific custom launcher

`scripts\Install-CodexStartup.ps1` 仅用于已有 `%LOCALAPPDATA%\OpenAI\Codex\launch-with-clash.ps1` 且符合启动锚点检查的环境。会备份启动器、安装至 `%LOCALAPPDATA%\CodexQuotaStrip` 并添加一个钩子；不是通用官方安装集成，不匹配时终止。

This installer applies only to an existing `%LOCALAPPDATA%\OpenAI\Codex\launch-with-clash.ps1` matching its checked startup anchor. It backs up the launcher, installs into `%LOCALAPPDATA%\CodexQuotaStrip` and adds one hook. It is not a generic official-installation integration; incompatible launchers cause it to stop.

```powershell
# 仅用于上述启动器 / Only for the custom launcher described above.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Install-CodexStartup.ps1 -SourceDirectory .\build
```

## 命令参考 / Command reference

| 参数／命令 / Argument or command | 用途 / Purpose |
| --- | --- |
| `CodexQuotaStrip.exe` | 按保存设置启动 / launch with saved settings |
| `--settings` | 打开配置，适用于已有实例 / open settings, including for an existing instance |
| `--taskbar` | 本次启动仅托盘 / launch with tray icons only |
| `--taskbar-and-codex --top` | 本次启动托盘 + 顶部条 / launch with tray icons and top strip |
| `--taskbar-and-codex --left` | 本次启动托盘 + 左侧条 / launch with tray icons and left strip |
| `--home "路径/path"` | 指定 Codex 数据目录 / select Codex data directory |
| `--local-only` | 本次启动初始关闭在线查询 / initially disable online queries |
| `--status "路径/path"` | 写诊断 JSON，父目录需存在 / write diagnostic JSON; parent directory must exist |
| `--quit` | 异步请求退出当前数据目录对应实例 / asynchronously request exit for the selected data directory |

切换到顶部条前先退出旧实例 / Exit the existing instance before switching to the top strip：

```powershell
.\build\CodexQuotaStrip.exe --quit
```

等待旧实例退出后再执行 / Wait for the previous instance to exit before running：

```powershell
.\build\CodexQuotaStrip.exe --taskbar-and-codex --top
```

自定义构建输出目录 / Custom build output directory：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1 -OutputDirectory .\build\portable
```

## 测试与排错 / Tests and troubleshooting

默认测试覆盖解析、窗口映射、分片记录、设置持久化和图标绘制。`-Integration` 增加配置交互、窗口重建和真实文件事件检查，使用隔离数据目录，可能短暂显示测试窗口。

Default tests cover parsing, window mapping, fragmented records, settings persistence and icon rendering. `-Integration` adds settings interactions, window recovery and real filesystem events using isolated data directories; test windows may briefly appear.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Test.ps1 -Integration
```

PowerShell 7 环境将 `powershell.exe` 换为 `pwsh.exe`。以下账号诊断需已登录且可发现的 Codex，会访问账号接口；默认测试不访问账号接口。

Replace `powershell.exe` with `pwsh.exe` for PowerShell 7. This account probe requires discoverable, signed-in Codex and contacts the account endpoint; default tests do not query the endpoint.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1 -Target ProviderProbe
.\build\ProviderProbe.exe success
```

| 现象 / Symptom | 检查 / Check |
| --- | --- |
| 找不到工具／SDK / missing tools or SDK | 在 Visual Studio Installer 补齐 C++ 和 SDK，无需给系统 PATH 添加 cl.exe。 / Add C++ tools and SDK in the Installer; no system PATH change for cl.exe is needed. |
| 脚本被阻止 / script blocked | 使用上面进程级 `-ExecutionPolicy Bypass` 命令，组织策略仍可能限制执行。 / Use the process-level command above; organizational policy may still restrict execution. |
| 托盘图标隐藏 / hidden tray icons | 查看 Windows 隐藏图标区和任务栏设置。 / Check the hidden-icons area and taskbar settings. |
| 配额条不显示 / strip missing | 选择托盘 + Codex 内显示，需识别到前台未最小化的 Codex 窗口；CLI 无附着窗口。 / Enable the strip; a recognized Codex window must be foreground and not minimized. CLI has no attachment window. |
| 长期 `-` 或刷新失败 / persistent `-` or failed refresh | 检查登录、数据目录、可发现的 codex.exe、在线开关和网络，查看配置状态并手动刷新。 / Check sign-in, data directory, executable discovery, online setting and network; inspect status and refresh manually. |
| 参数／环境不生效 / arguments or variables ignored | 使用相同 `--home` 退出再启动，重复启动通常复用旧实例。 / Exit with the same `--home` and relaunch; repeated launches usually reuse the old instance. |
| 启动位置影响设置 / different settings by launch location | 对照进程的 LOCALAPPDATA，从普通终端或桌面快捷方式重启。 / Compare LOCALAPPDATA; restart from a regular terminal or desktop shortcut. |

## 数据与源码 / Data and source

程序读取会话日志提取额度事件，不直接读取 `auth.json` 内容，只监听其变化以清除旧账号快照。认证交由本机 Codex；不需要额外 API Key，不发起模型推理。默认不写诊断日志，不向额外分析服务上传数据。

The utility reads session logs to extract quota events. It does not directly read `auth.json` contents, only watching changes to clear old snapshots. Local Codex handles authentication; no separate API key or model inference is used. Diagnostic logging is disabled by default, and no data is uploaded to an additional analytics service.

| 文件 / File | 用途 / Purpose |
| --- | --- |
| `source/QuotaStrip.cpp` | 托盘、配额条、配置和控制器 / tray, strip, settings and controller |
| `source/QuotaProvider.*` | app-server 查询及子进程清理 / app-server queries and subprocess cleanup |
| `source/QuotaCore.h`, `QuotaFiles.h` | 有界解析、额度映射、增量读取 / bounded parsing, quota mapping and incremental reads |
| `source/QuotaSettings.h`, `SettingsTheme.h` | 设置和原生深色界面 / settings and native dark UI |
| `source/TrayIcons.h` | 数字图标 / numeric icons |
| `Build.ps1`, `Test.ps1` | 构建和测试入口 / build and test entry points |

本仓库尚未指定软件许可证。 / No software license has been specified for this repository yet.
