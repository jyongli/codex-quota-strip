# Codex Quota Strip

Windows 上的轻量 Codex 配额显示工具。在系统托盘显示 **5 小时剩余配额** 和 **周剩余配额**，也可选择在 Codex 窗口顶部或左侧显示。

使用 C++17、Win32 和 GDI 构建，不依赖 Electron、WebView 或浏览器。它是独立的桌面辅助程序，与 OpenAI 没有官方隶属关系。

![深色配置窗口](docs/images/configuration.png)

## 功能

- 两个数字托盘图标显示剩余百分比；缺失字段显示 `-`。
- 可选 Codex 顶部／左侧配额条。切换到其他应用时隐藏配额条，托盘数字仍保留。
- 深色配置窗口，可修改查询间隔、旧数据提示时间、运行条件和显示位置。
- 默认每 120 秒查询一次账号配额；支持 30–3600 秒。旧数据提示默认 5 分钟，支持 1–120 分钟。
- 在线查询使用本机已登录的 `codex.exe app-server --stdio`；本地会话事件用于启动及离线回退。
- 文件变化由 Windows 事件通知驱动，增量读取；没有屏幕录制、截图监控或常驻查询子进程。
- 查询完成、超时或退出时清理子进程；重复启动复用同一个实例。

## 构建

需要 Windows x64、PowerShell、Visual Studio 2022 的“使用 C++ 的桌面开发”／C++ Build Tools，以及 Windows SDK。

在仓库根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Build.ps1
```

生成文件位于 `build\`。使用静态 C++ 运行库，运行时不需要编译器。

```powershell
.\build\CodexQuotaStrip.exe
.\build\CodexQuotaStrip.exe --settings
```

右键任意配额托盘图标可打开配置窗口、立即刷新或退出。Windows 可能默认将图标折叠到托盘的“显示隐藏的图标”中；可以在 Windows 任务栏设置中调整其显示。

## 数据与刷新

请先在 Codex 中登录。辅助程序发现本机 Codex 可执行文件后，通过其 app-server 查询账号配额，不需要另外提供 API Key，也不发起模型推理。

数字表示**剩余配额**，根据上游窗口时长区分 5 小时和周配额。上游未提供窗口时显示 `-`，不推算额度。灰色数字代表旧快照；查询失败会保留最后一次快照，并在配置窗口显示状态。因上游查询、刷新间隔和网络情况，数据不会逐秒保证同步。

默认仅读取 `%CODEX_HOME%`（未设置时为 `%USERPROFILE%\.codex`）中的会话额度事件。程序不直接读取 `auth.json` 内容，只监听其变化以清除旧账号快照；认证交由本机 Codex 处理。设置保存在 `%LOCALAPPDATA%\CodexQuotaStrip\settings.ini`。默认不写诊断日志，也不向额外的分析服务上传数据。

## 启动方式

可以为 `build\CodexQuotaStrip.exe` 创建快捷方式；启动时会使用已保存的设置。

`build\Start-With-Codex.ps1` 是供启动器调用的入口，不强制覆盖显示设置：

```powershell
powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File .\build\Start-With-Codex.ps1
```

`scripts\Install-CodexStartup.ps1` 是**特定自定义启动器**的可选安装工具：仅适用于已有 `%LOCALAPPDATA%\OpenAI\Codex\launch-with-clash.ps1`，且含脚本检查的启动锚点的环境。它会备份启动器、安装辅助程序并添加一个启动钩子。不适用于所有官方 Codex 安装；不存在或不匹配时会终止。

```powershell
# 仅在使用上述自定义启动器的环境运行
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Install-CodexStartup.ps1 -SourceDirectory .\build
```

## 测试

默认测试覆盖配额解析、窗口映射、分片数据、设置持久化和图标绘制：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Test.ps1
```

带 `-Integration` 会额外启动隔离的测试实例，检查配置交互、窗口重建和真实文件事件监听。请在有桌面会话的 Windows 环境运行；测试可能短暂显示测试窗口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Test.ps1 -Integration
```

`source\` 中另有启动、安装路径、前台可见性及层级检查。`ProviderProbe` 是手动账号查询诊断工具，需要可用的 Codex 登录状态；默认测试不访问账号查询接口。

## 源码结构

| 文件 | 用途 |
| --- | --- |
| `source/QuotaStrip.cpp` | 托盘、配额条、配置窗口和控制器 |
| `source/QuotaProvider.*` | 短生命周期 app-server 查询及进程清理 |
| `source/QuotaCore.h`、`QuotaFiles.h` | 有界 JSON 解析、额度映射、增量文件读取 |
| `source/QuotaSettings.h`、`SettingsTheme.h` | 设置与原生深色界面 |
| `source/TrayIcons.h` | 数字图标绘制 |
| `Build.ps1`、`Test.ps1` | 公开构建和测试入口 |

本仓库尚未指定软件许可证。
