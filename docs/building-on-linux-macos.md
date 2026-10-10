# Linux／macOS 构建说明 / Building from Linux and macOS

## 平台状态 / Platform status

项目现在提供 Windows、Linux 和 macOS 三套原生产物。本页说明如何从任意系统触发 GitHub Actions 并下载对应包；本机构建及安装见 [中文原生指南](native-desktop.zh-CN.md) / [English native guide](native-desktop.en.md)。

The project now provides Windows, Linux and macOS native artifacts. This page covers dispatching GitHub Actions and downloading the matching package from any OS; use the native guides above for local builds and installation.

| 路线 / Route | 状态 / Status | 产物 / Output |
| --- | --- | --- |
| Windows + MSVC | 当前本机构建路线 / Current local build path | Windows x64 `.exe` |
| Linux／macOS → GitHub Actions | 三套 runner 分别构建各自平台 / Separate runners build each platform | Windows ZIP、Linux DEB/TGZ、macOS universal ZIP |
| Linux／macOS → Windows 虚拟机／远程电脑 / VM or remote PC | 在 Windows 来宾系统内按 README 操作 / Follow README inside the Windows environment | Windows x64 `.exe` |
| MinGW 交叉编译 / Cross-compilation | 当前未提供受支持、已验证的构建入口 / No supported, verified build entry point | 不承诺可用产物 / No guaranteed artifact |
| Linux／macOS 本机构建 / Native build | CMake + 平台组件，见原生指南 / CMake + platform components; see native guides | Linux ELF、macOS `.app` |

以下命令只操作源码构建，不需要 Codex 登录。可直接从 [Releases](https://github.com/jyongli/codex-quota-strip/releases) 下载公开预览包，无需构建权限；自行触发工作流需要仓库写权限。

These build commands do not require a Codex login. Public previews can be downloaded from [Releases](https://github.com/jyongli/codex-quota-strip/releases) without build permission; workflow dispatch requires write access.

## 1. 安装 Git 和 GitHub CLI / Install Git and GitHub CLI

Ubuntu 24.04 / Debian 12，使用发行版软件源 / using distribution packages:

```bash
sudo apt update
sudo apt install git gh
```

其他 Linux 发行版，或软件源没有所需版本时，按 [GitHub CLI 官方 Linux 安装说明](https://github.com/cli/cli/blob/trunk/docs/install_linux.md) 安装。

For other distributions or an unsuitable package version, follow the [official Linux installation guide](https://github.com/cli/cli/blob/trunk/docs/install_linux.md).

macOS，需已安装 [Homebrew](https://brew.sh/) / with Homebrew already installed:

```bash
brew install git gh
```

GitHub CLI 的 Homebrew 包见 [官方公式页](https://formulae.brew.sh/formula/gh)。 / See the [Homebrew formula](https://formulae.brew.sh/formula/gh).

## 2. 登录和选择仓库 / Sign in and select a repository

```bash
gh auth login --hostname github.com --git-protocol https --web
git clone https://github.com/jyongli/codex-quota-strip.git
cd codex-quota-strip
```

触发工作流需要目标仓库的写入权限和 GitHub Actions 访问权限。仅查看／克隆公开仓库不授予构建触发权限。维护者可直接使用本仓库；其他用户先通过 GitHub 网页 Fork，再在自己的 Fork 中启用 Actions。Fork 完成后克隆它：

Dispatching a workflow requires write access to the target repository and access to GitHub Actions. Reading/cloning a public repository does not grant dispatch permission. Maintainers can use this repository; other users should fork it through GitHub and enable Actions in their fork, then clone it:

```bash
# 替换为自己的 GitHub 用户名 / Replace with your GitHub username.
git clone https://github.com/YOUR_USERNAME/codex-quota-strip.git
cd codex-quota-strip
```

上面两个克隆示例选择其一。以下命令都在选定仓库根目录执行，不带 `--repo` 时 GitHub CLI 使用当前仓库。

Choose one clone example. Run the following commands from that repository root; without `--repo`, GitHub CLI uses the current repository.

## 3. 触发三平台构建 / Dispatch the desktop build

```bash
gh workflow run build-windows.yml --ref main
gh run list --workflow build-windows.yml --branch main --event workflow_dispatch --limit 5
```

工作流名为 `Build Desktop`，文件名保留为 `build-windows.yml`。手动触发后，Windows 执行 MSVC 构建／默认测试；Ubuntu 22.04 执行原生编译、CTest、X11 UI 和单实例测试；macOS 14 构建 arm64/x86_64 通用程序并执行 CTest／UI smoke。全部使用合成数据，不进行真实账号查询。配置见 [工作流](../.github/workflows/build-windows.yml)，runner 类型见 [GitHub 官方说明](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)。

The workflow is named `Build Desktop`, retaining the filename `build-windows.yml`. It manually runs Windows MSVC/default tests, Ubuntu 22.04 native builds/CTest/X11 UI/single-instance tests, and macOS 14 universal arm64/x86_64 builds/CTest/UI smoke. Tests use synthetic data, not live account queries. See the [workflow](../.github/workflows/build-windows.yml) and [runner documentation](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

## 4. 等待并下载 / Wait and download

从上面的运行列表选择自己刚触发的记录，确认分支、提交和时间，并将数字 ID 替换到以下命令；不要原样输入 `RUN_ID`。

Select your dispatched run, checking its branch, commit and timestamp. Replace `RUN_ID` below with the numeric run ID; do not enter the placeholder literally.

```bash
gh run watch RUN_ID --exit-status
```

按平台选择一条下载命令 / Choose the download command for your platform:

```bash
gh run download RUN_ID --name codex-quota-strip-Linux-native --dir ./artifacts/linux
gh run download RUN_ID --name codex-quota-strip-macOS-native --dir ./artifacts/macos
gh run download RUN_ID --name codex-quota-strip-windows-x64 --dir ./artifacts/windows-x64
```

命令参考 / Command references: [workflow run](https://cli.github.com/manual/gh_workflow_run), [run download](https://cli.github.com/manual/gh_run_download).

每份产物含对应平台包、`LICENSE`、`BUILD-INFO.txt` 和 `SHA256SUMS.txt`。原生包还包含 UI smoke 输出。构建标识记录源码提交，Actions 产物保留 14 天，过期可重跑；Release 附件与 Actions 保留期无关。macOS 使用 ad-hoc 签名，尚未 Developer ID 签名／公证。

Each artifact contains its platform package, `LICENSE`, `BUILD-INFO.txt` and `SHA256SUMS.txt`. Native artifacts also include UI smoke output. Build info identifies the commit. Actions artifacts expire after 14 days; Release assets are independent of that retention. macOS uses ad-hoc signing without Developer ID/notarization.

Linux 校验 / Linux checksum verification:

```bash
cd artifacts/linux
sha256sum --check SHA256SUMS.txt
```

macOS 校验 / macOS checksum verification:

```bash
cd artifacts/macos
shasum -a 256 --check SHA256SUMS.txt
```

Windows 包在 Windows 启动；Linux／macOS 安装步骤见原生指南：

Run Windows artifacts on Windows; use the native guide for Linux/macOS installation:

```powershell
.\CodexQuotaStrip.exe --settings
```

## 不使用 GitHub 的方式 / Without GitHub

在 Windows x64 虚拟机或远程 Windows 电脑中克隆源码，按 [README 构建步骤](../README.md) 安装 MSVC／SDK 并构建。界面功能需在 Windows 交互式桌面测试。Apple Silicon 上 Windows ARM 虚拟机的 x64 模拟未验证。

Clone the repository in an x64 Windows VM or remote Windows PC, install MSVC/SDK and follow the [README](../README.md). Test UI behavior in an interactive Windows desktop. x64 emulation in an ARM Windows VM on Apple Silicon is unverified.

原生预览已实现各自的托盘／菜单栏、配置、查询和文件元数据事件。Linux Wayland 暂无窗口悬浮，macOS 的真实 Codex AX 跟随和账号查询尚待用户设备验证；完整限制见原生指南。

Native previews implement tray/menu bar, settings, queries and auth metadata events. Linux Wayland lacks window following; macOS real Codex AX following and account queries still require user-device validation. See the native guides for full limitations.
