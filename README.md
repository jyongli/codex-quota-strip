# Codex Quota Strip

**中文** | [English](README.en.md) · [MIT 许可证](LICENSE) · [贡献指南](CONTRIBUTING.md) · [安全说明](SECURITY.md) · [变更记录](CHANGELOG.md)

Windows、Linux 和 macOS 上的轻量 Codex 配额工具，显示 **5 小时剩余配额** 和 **周剩余配额**，支持托盘／菜单栏及窗口跟随配额条。

使用 C++17 和原生系统接口，不依赖 Electron 或 WebView。它是独立的辅助程序，与 OpenAI 没有官方隶属关系。Windows 界面为中文，原生 Linux／macOS 预览界面为英文；英文说明见 [README.en.md](README.en.md)。

**[下载 v3.0.0-preview.1](https://github.com/jyongli/codex-quota-strip/releases/tag/v3.0.0-preview.1)** · [Linux／macOS 安装与构建](docs/native-desktop.zh-CN.md)

下文的命令及详细配置主要针对 Windows；Linux／macOS 请使用上面的独立原生指南。

![配置窗口](docs/images/configuration.png)

## 功能

- 两个数字托盘图标显示剩余百分比，缺失字段显示 `-`。
- 账号明确只提供周额度时（例如部分 Pro 账号），自动隐藏 5 小时图标／标签并缩短悬浮条。未知启动状态和已知窗口暂时过期仍显示 `-`，新数据提供 5 小时窗口后自动恢复。
- 顶部／左侧配额条仅在 Codex 处于前台时显示；切换应用仍保留托盘数字。
- 原生深色配置窗口，可设刷新间隔、旧数据提示时间、查询条件和显示位置。
- 默认每 120 秒在线查询，支持 30–3600 秒；旧数据提示默认 5 分钟，支持 1–120 分钟。
- 通过本机已登录的 `codex.exe app-server --stdio` 查询；本地会话额度事件用于启动及离线回退。
- Windows 文件事件驱动、增量读取，无录屏或截图监控；查询子进程在完成、超时或退出时清理。
- 同一数据目录重复启动复用一个实例。

## 环境支持

| 环境 | 配置与限制 |
| --- | --- |
| Windows x64 + Codex 桌面版 | 主要使用场景；先打开并登录 Codex，需可发现兼容的 `codex.exe`。 |
| Windows PowerShell 5.1 | 使用 `powershell.exe`，已验证构建入口。 |
| Windows PowerShell 7 | 使用 `pwsh.exe`，已验证构建及测试入口。 |
| CMD / Git Bash | 调用 `powershell.exe`，不直接在 Bash 中运行 `.ps1`。 |
| 原生 Windows Codex CLI | 条件支持；需运行中的兼容 `codex.exe` 或下文回退目录，通常使用仅托盘模式。 |
| Linux x64 | 原生 `.deb`／`.tar.gz`；GTK3／Ayatana 托盘，X11 窗口跟随，Wayland 托盘模式。 |
| macOS 14+ | 原生通用 `.app`，Apple Silicon／Intel；菜单栏和辅助功能权限下的窗口跟随。预览版尚未 Developer ID 签名／公证。 |
| WSL | Windows 程序不发现 WSL 内的 Codex；原生 Linux 界面需相应 Linux 桌面／托盘会话，未验证 WSLg。 |
| Windows ARM64 / 无桌面服务器 | 当前仅输出 x64；ARM64 模拟未验证，界面和集成测试需要交互式桌面。 |

## 安装与构建

以下命令默认在**仓库根目录**执行。ZIP 源码需先解压，并进入包含 `Build.ps1` 的目录。当前仓库提供源码，构建产物位于 `build\`。

### 1. 安装依赖

需要 Git、Windows PowerShell 5.1 或 PowerShell 7、Visual Studio 2022 C++ Build Tools／完整 Visual Studio 的 C++ 桌面开发组件，以及 Windows SDK。已有 Visual Studio 时，在 Installer 中“修改”并补齐组件。

支持 WinGet 的电脑可使用以下命令，安装器可能要求管理员权限。PowerShell 7 为可选项；安装完成后重新打开终端。

```powershell
winget install --id Git.Git --exact --source winget
# PowerShell 7 可选
winget install --id Microsoft.PowerShell --exact --source winget
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

没有 WinGet 时可按官方说明手动安装：[Git](https://git-scm.com/install/windows)、[PowerShell](https://learn.microsoft.com/en-us/powershell/scripting/install/install-powershell-on-windows)、[Microsoft C++](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170)。构建工具安装参数见 [Visual Studio 命令参考](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022)。

### 2. 获取源码

```powershell
git clone https://github.com/jyongli/codex-quota-strip.git
Set-Location .\codex-quota-strip
```

### 3. 按终端环境构建

Windows PowerShell 5.1：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1
```

PowerShell 7：

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1
```

CMD / Git Bash，在仓库根目录执行：

```sh
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./Build.ps1
```

脚本通过 `vswhere` 查找 C++ 工具并配置 MSVC／SDK 路径，普通 PowerShell 即可，无需 Developer Command Prompt。SDK 只选择头文件和 x64 库均完整的最新版本，跳过不完整安装；暂不提供工具链版本固定参数。输出 `build\CodexQuotaStrip.exe` 与 `build\Start-With-Codex.ps1`；运行时不需要编译器。

Linux／macOS 可按 [原生构建指南](docs/native-desktop.zh-CN.md) 使用 CMake 构建各自程序；也可按 [远程构建说明](docs/building-on-linux-macos.md) 使用 GitHub Actions 下载三平台产物。

### 4. 启动与配置

先打开 Codex 并登录，再运行。右键任意托盘图标也可打开配置、立即刷新或退出。

```powershell
.\build\CodexQuotaStrip.exe --settings
```

## 多环境配置

以下启动示例应在对应实例尚未运行时执行。若需改变已有实例的启动参数或环境，先通过托盘退出，或执行 `--quit` 并等待进程结束，再运行新的启动命令。

### A. 桌面版：商店安装／本地安装

1. 打开并登录 Codex，保持窗口打开。
2. 打开辅助程序配置，启用在线查询。
3. 选择仅托盘或托盘 + Codex 内显示，保存并应用。
4. 点击“立即刷新”，检查来源、数据时间和状态。

查询程序发现顺序：**运行中的 Windows `codex.exe` → `%LOCALAPPDATA%\OpenAI\Codex\bin\codex.exe` 或其一层版本子目录**。当前不单独搜索 `PATH`，没有 `--codex-path` 参数。商店安装或其他布局需要先运行可发现的进程；并非所有版本／布局都已验证。

建议从桌面快捷方式或普通 Windows 终端启动。某些打包应用的终端会重定向 `LOCALAPPDATA`，导致设置目录与普通桌面启动不同。

### B. 原生 Windows CLI

先启动并登录原生 Windows CLI，保持其 `codex.exe` 运行，再启动辅助程序。配置中取消“仅在 Codex 运行时定时查询”，保存并应用；此条件依据桌面窗口识别，只有 CLI 时可能阻止定时查询。

```powershell
.\build\CodexQuotaStrip.exe --taskbar --settings
```

CLI 没有可附着的 Codex 桌面窗口，使用仅托盘模式。CLI 退出后，如果不在上述回退目录，后续查询可能无法发现可执行文件。

### C. 自定义数据目录／账号环境

`--home` 优先于 `CODEX_HOME`；均未设置时使用 `%USERPROFILE%\.codex`。指定与要查询的账号一致、已由 Codex 初始化的数据目录。

```powershell
.\build\CodexQuotaStrip.exe --home "D:\CodexData\account-a" --settings
```

退出该实例时使用同一目录

```powershell
.\build\CodexQuotaStrip.exe --home "D:\CodexData\account-a" --quit
```

或者设置当前 PowerShell 会话环境

```powershell
$env:CODEX_HOME = 'D:\CodexData\account-a'
.\build\CodexQuotaStrip.exe --settings
```

`--home` 仅改变 Codex 数据目录，**不会隔离辅助程序设置**；同一 Windows 用户的实例共享 `%LOCALAPPDATA%\CodexQuotaStrip\settings.ini`。实例身份按数据目录区分。`--settings` 可打开已有配置，其他启动参数不会改变已运行实例；切换前先退出对应实例。

### D. 代理与受限网络

查询子进程继承辅助程序启动时的环境。以下仅设置当前会话；端口为示例，需替换为实际可用的 HTTP 代理。

```powershell
$env:HTTPS_PROXY = 'http://127.0.0.1:7890'
$env:HTTP_PROXY = $env:HTTPS_PROXY
.\build\CodexQuotaStrip.exe --settings
```

变量是否生效取决于所发现的 Codex 版本和网络配置；程序不设置系统代理。修改变量后需从该终端重新启动，已有实例不会继承新环境。

### E. 本地事件模式

```powershell
.\build\CodexQuotaStrip.exe --local-only --settings
```

此参数初始关闭在线查询；如需持续本地模式，请保持配置中的在线查询开关关闭。数据依赖新会话事件，无法保证闲置时仍是最新账号额度。

## 刷新与显示设置

推荐在配置窗口“保存并应用”。默认 INI 内容如下，字段与单位见表格。

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

| 字段 | 默认 | 含义与范围 |
| --- | --- | --- |
| `online` | `1` | `1` 在线查询，`0` 本地事件 |
| `only_with_codex` | `1` | 定时查询需识别到 Codex 桌面窗口；手动刷新可跳过 |
| `seconds` | `120` | 查询间隔 30–3600 秒 |
| `stale_minutes` | `5` | 旧快照阈值 1–120 分钟，不触发额外查询 |
| `surface` | `taskbar` | `taskbar` 或 `taskbar-and-codex` |
| `position` | `top` | `top` 或 `left`，仅配额条生效 |

手动编辑前退出对应实例并等待结束，再备份；保存后重启。可先在配置窗口保存一次生成文件。

```powershell
$quotaConfig = Join-Path $env:LOCALAPPDATA 'CodexQuotaStrip\settings.ini'
if (Test-Path -LiteralPath $quotaConfig) {
    Copy-Item -LiteralPath $quotaConfig -Destination ($quotaConfig + '.before-edit-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.bak')
    notepad.exe $quotaConfig
}
```

在记事本保存并关闭后重新启动

```powershell
.\build\CodexQuotaStrip.exe
```

在线失败退避最长 15 分钟；手动查询至少间隔 5 秒。数值表示**剩余配额**，按上游窗口时长区分 5 小时和周配额。缺失显示 `-`；灰色表示旧快照，失败保留最后快照，不保证逐秒同步。

## 启动方式

### 便携运行／启动器调用

将构建产物保存在固定目录。此入口遵循已保存设置，启动后退出 PowerShell，不留下脚本监控进程；只启动辅助程序，不负责启动 Codex 或修改通用 Codex 快捷方式。

```powershell
powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File .\build\Start-With-Codex.ps1
```

### 桌面快捷方式

使用 Windows 返回的桌面目录，支持桌面重定向。移动程序后需更新快捷方式。

```powershell
$quotaExe = (Resolve-Path .\build\CodexQuotaStrip.exe).Path
$quotaDesktop = [Environment]::GetFolderPath('Desktop')
$quotaShortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $quotaDesktop 'Codex Quota.lnk'))
$quotaShortcut.TargetPath = $quotaExe
$quotaShortcut.WorkingDirectory = Split-Path -Parent $quotaExe
$quotaShortcut.Save()
```

### Windows 登录自启

在当前用户启动目录创建快捷方式。此方式**随 Windows 登录启动**，不等同于只随 Codex 启动；默认 `only_with_codex=1` 限制桌面版未打开时的定时在线查询。

```powershell
$quotaExe = (Resolve-Path .\build\CodexQuotaStrip.exe).Path
$quotaStartup = [Environment]::GetFolderPath('Startup')
$quotaShortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $quotaStartup 'Codex Quota.lnk'))
$quotaShortcut.TargetPath = $quotaExe
$quotaShortcut.WorkingDirectory = Split-Path -Parent $quotaExe
$quotaShortcut.Save()
```

取消自启时，在上述 Startup 目录移除快捷方式。

### 特定自定义启动器

`scripts\Install-CodexStartup.ps1` 仅用于已有 `%LOCALAPPDATA%\OpenAI\Codex\launch-with-clash.ps1` 且符合启动锚点检查的环境。会备份启动器、安装至 `%LOCALAPPDATA%\CodexQuotaStrip` 并添加一个钩子；不是通用官方安装集成，不匹配时终止。

```powershell
# 仅用于上述启动器
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Install-CodexStartup.ps1 -SourceDirectory .\build
```

## 命令参考

| 参数／命令 | 用途 |
| --- | --- |
| `CodexQuotaStrip.exe` | 按保存设置启动 |
| `--settings` | 打开配置，适用于已有实例 |
| `--taskbar` | 本次启动仅托盘 |
| `--taskbar-and-codex --top` | 本次启动托盘 + 顶部条 |
| `--taskbar-and-codex --left` | 本次启动托盘 + 左侧条 |
| `--home "路径"` | 指定 Codex 数据目录 |
| `--local-only` | 本次启动初始关闭在线查询 |
| `--status "路径"` | 写诊断 JSON，父目录需存在 |
| `--quit` | 异步请求退出当前数据目录对应实例 |

切换到顶部条前先退出旧实例

```powershell
.\build\CodexQuotaStrip.exe --quit
```

等待旧实例退出后再执行

```powershell
.\build\CodexQuotaStrip.exe --taskbar-and-codex --top
```

自定义构建输出目录

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1 -OutputDirectory .\build\portable
```

## 测试与排错

默认测试覆盖 SDK 安装发现、解析、窗口映射、分片记录、设置持久化和图标绘制。`-Integration` 增加配置交互、窗口重建和真实文件事件检查，使用隔离数据目录，可能短暂显示测试窗口。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Test.ps1 -Integration
```

PowerShell 7 环境将 `powershell.exe` 换为 `pwsh.exe`。以下账号诊断需已登录且可发现的 Codex，会访问账号接口；默认测试不访问账号接口。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1 -Target ProviderProbe
.\build\ProviderProbe.exe success
```

| 现象 | 检查 |
| --- | --- |
| 找不到工具／SDK | 在 Visual Studio Installer 补齐 C++ 和 SDK，无需给系统 PATH 添加 cl.exe。 |
| 脚本被阻止 | 使用上面进程级 `-ExecutionPolicy Bypass` 命令，组织策略仍可能限制执行。 |
| 托盘图标隐藏 | 查看 Windows 隐藏图标区和任务栏设置。 |
| 配额条不显示 | 选择托盘 + Codex 内显示，需识别到前台未最小化的 Codex 窗口；CLI 无附着窗口。 |
| 长期 `-` 或刷新失败 | 检查登录、数据目录、可发现的 codex.exe、在线开关和网络，查看配置状态并手动刷新。 |
| 参数／环境不生效 | 使用相同 `--home` 退出再启动，重复启动通常复用旧实例。 |
| 启动位置影响设置 | 对照进程的 LOCALAPPDATA，从普通终端或桌面快捷方式重启。 |

## 数据与源码

程序读取会话日志提取额度事件，不直接读取 `auth.json` 内容，只监听其变化以清除旧账号快照。认证交由本机 Codex；不需要额外 API Key，不发起模型推理。默认不写诊断日志，不向额外分析服务上传数据。

| 文件 | 用途 |
| --- | --- |
| `source/QuotaStrip.cpp` | 托盘、配额条、配置和控制器 |
| `source/QuotaProvider.*` | app-server 查询及子进程清理 |
| `source/QuotaCore.h`, `QuotaFiles.h` | 有界解析、额度映射、增量读取 |
| `source/QuotaSettings.h`, `SettingsTheme.h` | 设置和原生深色界面 |
| `source/TrayIcons.h` | 数字图标 |
| `Build.ps1`, `Test.ps1` | 构建和测试入口 |

## 许可与贡献

本项目源码和文档采用 [MIT License](LICENSE)，允许使用、修改、分发和商用，需保留版权及许可声明；软件按原样提供，不作担保。第三方工具、运行库、Windows SDK 和 Codex 服务仍适用各自条款。

参与开发请查看 [贡献指南](CONTRIBUTING.md)；问题报告与数据边界见 [安全说明](SECURITY.md)；源码变更见 [变更记录](CHANGELOG.md)。
