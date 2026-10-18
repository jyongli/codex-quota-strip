# 安装包选择与兼容环境

**中文** | [English](installers.en.md) · [下载 v3.0.0-preview.6](https://github.com/jyongli/codex-quota-strip/releases/tag/v3.0.0-preview.6)

安装前先在本机 Codex 登录。以下最低系统要求指配额程序；Codex 本身还需满足其版本的系统要求。更新或卸载前，先从托盘／菜单栏退出配额程序。

## 下载哪个包

| 你的系统／使用方式 | 推荐文件 | 安装方式与兼容范围 |
| --- | --- | --- |
| Windows 10 1809+／Windows 11，Intel 或 AMD 64 位 | `CodexQuotaStrip-windows-x64-setup.exe` | 中英文安装向导，当前用户安装，不要求管理员；提供开始菜单、可选桌面快捷方式及卸载入口。 |
| Windows，希望免安装 | `CodexQuotaStrip-windows-x64.zip` | 解压运行 EXE，系统要求同上；无需安装编译器或 Visual C++ 运行库。 |
| Windows 11 ARM64 | 同一 Windows x64 安装包或 ZIP | 使用 Windows 的 x64 模拟，允许安装但尚未实机验证；不是原生 ARM64 包。 |
| Ubuntu 22.04／24.04、Debian 12，x86_64／amd64 | `CodexQuotaStrip-linux-x86_64.deb` | 用 apt 安装，自动解析系统依赖。 |
| Fedora 43，x86_64 | `CodexQuotaStrip-linux-x86_64.rpm` | 用 dnf 安装；其他 RPM 发行版需兼容的 GTK3、Ayatana、X11 和 glibc，尚未逐一验证。 |
| 其他使用 glibc 的 Linux x86_64，或没有 sudo 权限 | `CodexQuotaStrip-linux-x86_64.tar.gz` | 解压运行，或使用包内 `install.sh` 安装到当前用户；运行库需由系统提供，安装器会先检查缺失依赖。 |
| macOS 14+，Apple Silicon 或 Intel | `CodexQuotaStrip-macos-universal.dmg` | 推荐拖放安装：打开 DMG，把应用拖到 Applications。 |
| macOS 14+，希望使用安装向导／部署工具 | `CodexQuotaStrip-macos-universal.pkg` | 双击安装到 `/Applications`，系统可能要求管理员授权；包含 arm64 与 x86_64。 |
| macOS 14+，希望下载压缩包 | `CodexQuotaStrip-macos-universal.zip` | 解压 `.app`，复制到 Applications；与 DMG／PKG 是同一应用。 |

`Source code` 两项是源码，通常无需下载。`BUILD-INFO.txt` 记录源码提交和构建；`SHA256SUMS.txt` 校验发布附件；`LICENSE` 与 `THIRD_PARTY_NOTICES.md` 是许可文件。

## 安装与升级

### Windows

运行 `CodexQuotaStrip-windows-x64-setup.exe`，按向导操作。默认目录为 `%LOCALAPPDATA%\CodexQuotaStrip`，桌面快捷方式可选；安装器不自动启用登录自启，也不修改已有 Codex 启动器。升级时退出旧程序，重新运行新安装包，沿用相同目录。用户设置会保留。

便携版：将 ZIP 解压到固定目录，运行 `CodexQuotaStrip.exe`；配置可通过托盘菜单或以下命令打开：

```powershell
.\CodexQuotaStrip.exe --settings
```

卸载向导版使用“设置 → 应用 → 已安装的应用”或开始菜单中的卸载入口。卸载保留用户 `settings.ini`、其他自建文件和 Codex 账号数据。之前自行配置的启动钩子也会保留，如不再使用请按原配置方式移除。

### Linux DEB

在下载目录运行：

```bash
sudo apt install ./CodexQuotaStrip-linux-x86_64.deb
CodexQuotaStrip --settings
```

升级仍用同一 `apt install ./新包.deb` 命令。卸载：

```bash
sudo apt remove codex-quota-strip
```

### Linux RPM

```bash
sudo dnf install ./CodexQuotaStrip-linux-x86_64.rpm
CodexQuotaStrip --settings
```

升级：`sudo dnf upgrade ./CodexQuotaStrip-linux-x86_64.rpm`；卸载：

```bash
sudo dnf remove codex-quota-strip
```

### Linux TGZ：免管理员安装

此包不捆绑 GTK3 等系统库。Ubuntu／Debian 可先安装 `libgtk-3-0 libayatana-appindicator3-1 libx11-6`，Fedora 的包管理器可按所缺的 `.so` 文件查询提供者。

```bash
tar -xzf CodexQuotaStrip-linux-x86_64.tar.gz
cd CodexQuotaStrip-linux-x86_64
bash install.sh
~/.local/bin/CodexQuotaStrip --settings
```

默认在 `~/.local` 安装命令、应用菜单入口和程序文件，不更改 PATH。可用 `bash install.sh install --prefix "/绝对路径"` 指定目录。升级时退出旧程序后，从新解压目录再次执行相同安装命令。卸载：

```bash
bash ~/.local/lib/codex-quota-strip/uninstall.sh
```

卸载只删除本程序登记的文件，保留用户配置和额外文件。也可跳过安装，直接运行解压目录中的 `./usr/bin/CodexQuotaStrip --settings`。

### macOS

DMG 推荐拖放安装；PKG 推荐双击运行安装向导，也支持部署命令：

```bash
sudo installer -pkg ./CodexQuotaStrip-macos-universal.pkg -target /
open /Applications/CodexQuotaStrip.app --args --settings
```

更新前先退出程序，再用新 DMG／ZIP 替换应用，或重新安装新 PKG。卸载时退出后，从 Applications 将 `CodexQuotaStrip.app` 移到废纸篓；用户配置目录仍保留。PKG 收据本身不提供卸载器。

macOS 预览包只有 ad-hoc 完整性签名，尚未 Apple Developer ID 签名或公证。首次打开可能被系统阻止，确认来源和校验值后按 [Apple 的打开应用说明](https://support.apple.com/en-us/102445) 处理。窗口跟随需要用户自行授予辅助功能权限；安装器不修改权限，不请求录屏权限。

## 兼容性与验证范围

- Linux 产物基于 Ubuntu 22.04 构建；DEB／RPM 按实际二进制自动生成依赖约束。TGZ 有架构及动态库预检，不能使不兼容的系统库自动兼容。
- Linux 安装验证覆盖 Ubuntu 22.04／24.04、Debian 12 和 Fedora 43 的一次性容器：安装、依赖解析、已安装程序的 X11 UI smoke、重装、卸载和设置保留。容器测试不代表所有真实桌面主题、Wayland 组合均已验证。
- Windows 云端验证隔离安装、升级、快捷方式、注册项和卸载；macOS 云端验证 DMG 挂载、PKG 实际安装、应用签名／双架构及 UI smoke。旧版 Windows 和 Windows ARM64 尚未实机验证，macOS 14 以外版本未逐一测试。
- Linux 悬浮条需要 X11；Wayland 仅托盘／配置窗口，桌面需支持 AppIndicator。macOS 实际 Codex AX 跟随、多显示器仍待设备验证。
- 当前不提供 Linux ARM64、32 位 Windows、Alpine/musl、Android 或 iOS 的成品包。需要 GUI 桌面和可访问的本机 Codex；不是无桌面服务器服务。

配置与构建详见 [中文 README](../README.md) 和 [原生平台指南](native-desktop.zh-CN.md)。
