# Linux／macOS 构建说明 / Building from Linux and macOS

## 平台状态 / Platform status

当前程序依赖 Win32／GDI、Windows 文件通知、托盘和进程接口，时间转换也使用 Windows CRT。Linux 的 GCC 和 macOS 的 Apple Clang 无法直接将当前源码构建为原生应用。安装 CMake 或 PowerShell 也不会补齐这些平台接口。

The application depends on Win32/GDI, Windows file notifications, tray/process APIs and Windows CRT time conversion. Linux GCC and macOS Apple Clang cannot directly build it as a native application. Installing CMake or PowerShell does not supply these platform APIs.

| 路线 / Route | 状态 / Status | 产物 / Output |
| --- | --- | --- |
| Windows + MSVC | 当前本机构建路线 / Current local build path | Windows x64 `.exe` |
| Linux／macOS → GitHub Actions | 可从本机发起，实际在 `windows-2022` runner 构建 / Initiate locally; build on a Windows runner | Windows x64 `.exe` |
| Linux／macOS → Windows 虚拟机／远程电脑 / VM or remote PC | 在 Windows 来宾系统内按 README 操作 / Follow README inside the Windows environment | Windows x64 `.exe` |
| MinGW 交叉编译 / Cross-compilation | 当前未提供受支持、已验证的构建入口 / No supported, verified build entry point | 不承诺可用产物 / No guaranteed artifact |
| Linux／macOS 原生移植 / Native port | 尚未实现 / Not implemented | 无原生二进制 / No native binary |

以下是远程构建 Windows 产物的具体步骤。下载后的 `.exe` 仍需在 Windows 上运行，不会变成 Linux 托盘程序或 macOS `.app`。

The steps below build Windows artifacts remotely. Downloaded `.exe` files still require Windows; they do not become Linux tray applications or macOS `.app` bundles.

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

## 3. 触发 Windows 构建 / Dispatch the Windows build

```bash
gh workflow run build-windows.yml --ref main
gh run list --workflow build-windows.yml --branch main --event workflow_dispatch --limit 5
```

工作流只手动触发，在 `windows-2022` 中执行 `Build.ps1` 和默认 `Test.ps1`。不需要 Codex 登录，不进行账号在线查询；也不验证真实 Codex 窗口或交互式集成测试。工作流配置见 [build-windows.yml](../.github/workflows/build-windows.yml)，runner 类型见 [GitHub 官方说明](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)。

The manual-only workflow runs `Build.ps1` and default `Test.ps1` on `windows-2022`, without a Codex login or online account queries. It does not verify real Codex windows or interactive integration tests. See [the workflow](../.github/workflows/build-windows.yml) and [GitHub's runner documentation](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

## 4. 等待并下载 / Wait and download

从上面的运行列表选择自己刚触发的记录，确认分支、提交和时间，并将数字 ID 替换到以下命令；不要原样输入 `RUN_ID`。

Select your dispatched run, checking its branch, commit and timestamp. Replace `RUN_ID` below with the numeric run ID; do not enter the placeholder literally.

```bash
gh run watch RUN_ID --exit-status
gh run download RUN_ID --name codex-quota-strip-windows-x64 --dir ./artifacts/windows-x64
```

命令参考 / Command references: [workflow run](https://cli.github.com/manual/gh_workflow_run), [run download](https://cli.github.com/manual/gh_run_download).

产物含 `CodexQuotaStrip.exe`、`Start-With-Codex.ps1`、`LICENSE`、`BUILD-INFO.txt` 和 `SHA256SUMS.txt`。构建标识记录源码提交；产物保留 14 天，过期可重新触发。产物目前没有代码签名。

The artifact contains `CodexQuotaStrip.exe`, `Start-With-Codex.ps1`, `LICENSE`, `BUILD-INFO.txt` and `SHA256SUMS.txt`. Build info identifies the source commit. Artifacts are retained for 14 days; dispatch a new run after expiry. Binaries are currently unsigned.

Linux 校验 / Linux checksum verification:

```bash
cd artifacts/windows-x64
sha256sum --check SHA256SUMS.txt
```

macOS 校验 / macOS checksum verification:

```bash
cd artifacts/windows-x64
shasum -a 256 --check SHA256SUMS.txt
```

将整个产物目录复制到 Windows，在该目录启动：

Copy the whole artifact directory to Windows and launch from that directory:

```powershell
.\CodexQuotaStrip.exe --settings
```

## 不使用 GitHub 的方式 / Without GitHub

在 Windows x64 虚拟机或远程 Windows 电脑中克隆源码，按 [README 构建步骤](../README.md) 安装 MSVC／SDK 并构建。界面功能需在 Windows 交互式桌面测试。Apple Silicon 上 Windows ARM 虚拟机的 x64 模拟未验证。

Clone the repository in an x64 Windows VM or remote Windows PC, install MSVC/SDK and follow the [README](../README.md). Test UI behavior in an interactive Windows desktop. x64 emulation in an ARM Windows VM on Apple Silicon is unverified.

原生移植需要替换托盘与窗口层、文件监听、进程生命周期、设置存储和时间转换，并重新验证平台上的 Codex 查询及认证路径；当前文档不宣称已完成这些适配。

A native port requires replacing tray/window integration, file notifications, process lifecycle, settings storage and time conversion, then validating Codex query/authentication paths on each platform. These adaptations are not implemented here.
