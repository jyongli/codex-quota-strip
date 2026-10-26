# Linux／macOS 原生预览版

**中文** | [English](native-desktop.en.md)

原生版使用 C++17、Linux GTK3／Ayatana 托盘和 macOS AppKit。提供剩余配额、配置窗口、按间隔查询和顶部／左侧悬浮条；没有 Electron、WebView 或录屏。

## 下载与平台

[下载原生预览版](https://github.com/jyongli/codex-quota-strip/releases/tag/v3.0.0-preview.7) · [完整选包、安装／升级／卸载及兼容说明](installers.zh-CN.md)

| 产物 | 适用环境 |
| --- | --- |
| `CodexQuotaStrip-linux-x86_64.deb` | Ubuntu 22.04／24.04、兼容的 Debian 系 x64 桌面 |
| `CodexQuotaStrip-linux-x86_64.rpm` | Fedora 43 x64；其他 RPM 系统需兼容依赖 |
| `CodexQuotaStrip-linux-x86_64.tar.gz` | Linux x64，需安装 GTK3、Ayatana AppIndicator、X11 与兼容的运行库 |
| `CodexQuotaStrip-macos-universal.zip` | macOS 14+，包含 Apple Silicon arm64 和 Intel x86_64 的 `.app` |
| `CodexQuotaStrip-macos-universal.dmg`／`.pkg` | 同一通用应用，分别用于拖放安装／安装向导 |

Linux 悬浮条需要 X11 会话。Wayland 提供托盘和配置窗口，但不能通用地读取其他应用的位置；宿主检测／悬浮设置会禁用，在线查询按间隔运行。托盘需要桌面支持 AppIndicator／StatusNotifier；GNOME 若没有显示图标，可安装并启用相应扩展。

macOS 悬浮条需要用户授予“辅助功能”权限。预览包使用 ad-hoc 完整性签名，没有 Apple Developer ID 签名或公证；首次打开可能被系统阻止。确认来源和校验值后，按 [Apple 的打开应用说明](https://support.apple.com/en-us/102445) 对此应用单独允许打开。

## 安装 Linux

从下载目录运行：

```bash
sudo apt install ./CodexQuotaStrip-linux-x86_64.deb
CodexQuotaStrip --settings
```

也可以使用 tar 包：

```bash
sudo apt install libgtk-3-0 libayatana-appindicator3-1 libx11-6
tar -xzf CodexQuotaStrip-linux-x86_64.tar.gz
./CodexQuotaStrip-linux-x86_64/usr/bin/CodexQuotaStrip --settings
```

TGZ 另提供 `bash CodexQuotaStrip-linux-x86_64/install.sh` 当前用户安装入口，会先检查架构和运行库；不需要 sudo，卸载和自定义路径见 [安装指南](installers.zh-CN.md)。

程序不依赖 Wine。可从应用菜单打开，或运行构建／解压得到的可执行文件。右键任一配额图标打开设置、刷新或退出。

## 安装 macOS

1. 解压 ZIP，将其中的 `CodexQuotaStrip.app` 放到“应用程序”。
2. 打开程序，菜单栏显示 `5h … W …`。点击菜单栏数字选择 `Settings…`。
3. 点击 `Allow window following…` 或配置中的 `Allow Accessibility…`，由用户在系统设置中为本程序授予辅助功能权限。
4. 返回 Codex／ChatGPT 桌面窗口，配额条会跟随它；切换到其他应用时隐藏。

也可从终端首次启动并打开配置：

```bash
open "/Applications/CodexQuotaStrip.app" --args --settings
```

无需录屏权限。更新程序后，如果系统要求重新确认辅助功能访问，请按系统提示处理。

## Codex 登录与可执行文件

先在本机 Codex CLI／桌面版中登录。程序通过 `codex app-server` 读取账号配额，不需要额外 API Key，也不发起模型推理。计划额度通常要求 ChatGPT 登录；缺失窗口显示 `-`。

自动发现会搜索 `PATH`、常见安装目录及部分桌面包路径。若找不到兼容的 Codex，在配置窗口填写其完整可执行文件路径。终端中可先检查：

```bash
command -v codex
codex --version
```

Codex CLI 的安装和登录方式见 [官方说明](https://learn.chatgpt.com/docs/codex/cli)。如果使用自定义 `CODEX_HOME`，辅助程序必须选择相同数据目录。

账号查询自动复用所选 `config.toml` 中 `[shell_environment_policy.set]` 的四项代理变量，支持配置文件选定的 profile。更改后在下次查询生效，不需重启；未配置项继承启动环境。读取范围、示例及错误处理见 [代理说明](../README.md#d-代理与受限网络)。

## 配置与命令

三平台支持中文／英文界面切换。原生版默认英文，在设置中将 `Language` 改为 `中文` 或 `English`，点击保存并应用后立即切换，重启后保留；`settings.ini` 使用 `language=zh-CN` 或 `language=en`。默认查询间隔 120 秒，范围 30–3600 秒；旧数据阈值默认 5 分钟，范围 1–120 分钟。

| 设置 | 含义 |
| --- | --- |
| `Query Codex account online` | 开启账号在线查询；关闭后保留当前快照，但不继续查询 |
| `Query only while the host … is open` | 检测到宿主桌面应用时定时查询；仅 CLI 环境可取消勾选，手动刷新可跳过宿主条件 |
| `Show window-following overlay` | 显示前台宿主窗口的配额条；macOS 要求辅助功能权限，Linux 要求 X11 |
| `Overlay position` | Top／Left |
| `Language` | 中文／English，保存后立即生效 |
| `Codex data directory` | 已登录账号的绝对数据目录 |
| `Codex executable` | 留空自动发现，或指定本机 Codex 的完整路径 |
| `Host … contains` | 宿主应用名／bundle ID（macOS）或窗口类（Linux）的匹配字符串 |

默认宿主 `Codex` 也匹配 `ChatGPT`，以兼容集成桌面版。仅当匹配的应用处于前台才显示悬浮条。若指定终端作为宿主，它的所有匹配窗口都可能显示，不能据此判断当前是否在执行 Codex CLI。

保存设置的位置：

- Linux：`$XDG_CONFIG_HOME/codex-quota-strip/settings.ini`，未设置时使用 `~/.config/codex-quota-strip/settings.ini`。
- macOS：`~/Library/Application Support/CodexQuotaStrip/settings.ini`。

一个用户配置目录只运行一个原生实例。更改数据目录不会启动第二份程序；已经运行时，通过托盘／菜单栏打开设置，或退出后使用新的启动参数。

Linux 命令示例，在当前实例退出后执行：

```bash
CodexQuotaStrip --settings
CodexQuotaStrip --home "$HOME/.codex" --codex-binary /usr/local/bin/codex --top
CodexQuotaStrip --left
CodexQuotaStrip --tray-only
CodexQuotaStrip --interval 60
```

macOS 可将命令中的 `CodexQuotaStrip` 替换为 `/Applications/CodexQuotaStrip.app/Contents/MacOS/CodexQuotaStrip`。原生版没有 Windows 的 `--taskbar`／`--quit` 等别名；退出使用托盘／菜单栏中的 `Quit`。

原生预览版的数据来自在线账号查询和运行中的最后快照，暂未移植 Windows 的会话日志离线回退。灰色／`*` 表示旧快照，重置过期的字段显示 `-`。认证文件元数据变化会清空旧值；程序不读取认证文件内容。

明确只提供周额度的账号（例如部分 Pro 账号），会自动隐藏 5 小时图标／标签并缩短悬浮条。暂未拿到数据或已知窗口过期仍保留 `-`；后续数据提供 5 小时窗口时自动恢复。不依赖猜测账号类型。

## 从源码构建

Ubuntu／Debian：

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libgtk-3-dev libayatana-appindicator3-dev libx11-dev
git clone https://github.com/jyongli/codex-quota-strip.git
cd codex-quota-strip
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
./build/native/CodexQuotaStrip --settings
```

macOS，需已安装 Xcode Command Line Tools 和 Homebrew：

```bash
brew install cmake
git clone https://github.com/jyongli/codex-quota-strip.git
cd codex-quota-strip
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
open build/native/CodexQuotaStrip.app
```

打包命令：

```bash
cd build/native
cpack
```

不想安装本机编译工具，可按 [远程构建说明](building-on-linux-macos.md) 使用 GitHub Actions，下载对应原生平台产物。

## 验证范围

云端测试包括 UTC／额度解析、真实管道协议、错误／超时／取消与子进程回收、配置、认证文件变化／移出、用户级单实例，以及 Linux X11 的前台隐藏／移动／恢复。macOS smoke 验证菜单栏、配置持久化和被动悬浮窗口生命周期。

macOS 的实际 Codex 窗口 AX 跟随、多显示器和真实账号查询尚未在用户设备验证；Wayland 的策略测试使用模拟会话能力，不代表所有 Wayland 桌面都已测试。首批产物因此作为预览版发布。

`ui-smoke.txt` 的闲置 CPU 数据在无账号、配置窗已关闭的云端测试中采集；`peak_rss_mib` 是进程历史峰值，不是当前私有内存。不同系统和主题的结果会变化。
