# Choose an installer and supported environment

[中文](installers.zh-CN.md) | **English** · [Download v3.0.0-preview.7](https://github.com/jyongli/codex-quota-strip/releases/tag/v3.0.0-preview.7)

Sign in through local Codex first. Minimum requirements below apply to the quota companion; your Codex version has its own system requirements. Quit the companion through its tray/menu-bar menu before updating or uninstalling.

## Which download?

| Environment or preference | Recommended file | Installation and compatibility |
| --- | --- | --- |
| Windows 10 1809+ / Windows 11 on Intel or AMD x64 | `CodexQuotaStrip-windows-x64-setup.exe` | English/Chinese per-user wizard, no administrator requirement; Start menu, optional desktop shortcut and uninstall entry. |
| Windows portable use | `CodexQuotaStrip-windows-x64.zip` | Extract and run; same requirements. No compiler or Visual C++ runtime installation is needed. |
| Windows 11 ARM64 | Same Windows x64 installer or ZIP | Uses Windows x64 emulation. Installation is allowed, but physical ARM64 hardware has not been tested. Not a native ARM64 build. |
| Ubuntu 22.04/24.04, Debian 12, x86_64/amd64 | `CodexQuotaStrip-linux-x86_64.deb` | Install with apt to resolve dependencies. |
| Fedora 43, x86_64 | `CodexQuotaStrip-linux-x86_64.rpm` | Install with dnf. Other RPM distributions require compatible GTK3, Ayatana, X11 and glibc; they have not all been tested. |
| Other glibc Linux x86_64 desktops or no sudo access | `CodexQuotaStrip-linux-x86_64.tar.gz` | Extract and run or use its per-user install.sh. System runtime libraries are required; the installer checks missing dependencies. |
| macOS 14+, Apple Silicon or Intel | `CodexQuotaStrip-macos-universal.dmg` | Recommended drag-and-drop installation into Applications. |
| macOS 14+, installation wizard or deployment | `CodexQuotaStrip-macos-universal.pkg` | Installs into /Applications; administrator authorization may be required. Contains arm64 and x86_64. |
| macOS 14+, archive preference | `CodexQuotaStrip-macos-universal.zip` | Extract the .app and copy it into Applications. Same app as DMG/PKG. |

The two `Source code` downloads are for developers. BUILD-INFO.txt identifies the source/build; SHA256SUMS.txt verifies release assets; LICENSE and THIRD_PARTY_NOTICES.md contain license notices.

## Installation and updates

### Windows

Run the setup EXE. It defaults to `%LOCALAPPDATA%\CodexQuotaStrip`; the desktop shortcut is optional. It does not enable login startup or change an existing Codex launcher. To update, quit the app and run the new installer using the same directory. User settings are preserved.

For portable use, extract the ZIP into a stable directory and run CodexQuotaStrip.exe. Open settings through the tray or:

```powershell
.\CodexQuotaStrip.exe --settings
```

Uninstall through Windows Settings → Apps or the Start menu entry. User settings.ini, additional user files and Codex account data are preserved. Previously configured custom startup hooks remain; remove them through their original setup if no longer needed.

### Linux DEB

```bash
sudo apt install ./CodexQuotaStrip-linux-x86_64.deb
CodexQuotaStrip --settings
```

Use the same apt install command with the new package to update. Remove with:

```bash
sudo apt remove codex-quota-strip
```

### Linux RPM

```bash
sudo dnf install ./CodexQuotaStrip-linux-x86_64.rpm
CodexQuotaStrip --settings
```

Update with `sudo dnf upgrade ./CodexQuotaStrip-linux-x86_64.rpm`. Remove with:

```bash
sudo dnf remove codex-quota-strip
```

### Linux TGZ: per-user installation

The archive does not bundle system libraries. Ubuntu/Debian can install libgtk-3-0, libayatana-appindicator3-1 and libx11-6; on Fedora, query the package manager for providers of missing .so files.

```bash
tar -xzf CodexQuotaStrip-linux-x86_64.tar.gz
cd CodexQuotaStrip-linux-x86_64
bash install.sh
~/.local/bin/CodexQuotaStrip --settings
```

Commands, application-menu entry and files default to ~/.local; PATH is unchanged. Specify an absolute prefix with `bash install.sh install --prefix "/absolute/path"`. To update, quit the app and rerun the same command from the new archive. Uninstall with:

```bash
bash ~/.local/lib/codex-quota-strip/uninstall.sh
```

Only registered application files are removed; settings and additional files remain. Alternatively run `./usr/bin/CodexQuotaStrip --settings` directly from the extracted archive.

### macOS

Use DMG for drag-and-drop installation or double-click PKG for the wizard. Deployment commands:

```bash
sudo installer -pkg ./CodexQuotaStrip-macos-universal.pkg -target /
open /Applications/CodexQuotaStrip.app --args --settings
```

Quit before updating, then replace the app from a new DMG/ZIP or install the new PKG. To uninstall, quit and move CodexQuotaStrip.app from Applications to Trash. User configuration remains; a PKG receipt is not an uninstaller.

Preview packages have an ad-hoc integrity signature without Apple Developer ID signing/notarization. macOS may block first launch; after verifying source/checksums, follow [Apple's app-opening guidance](https://support.apple.com/en-us/102445). Window following requires user-granted Accessibility access. Installers do not change permissions or request Screen Recording.

## Compatibility and verification scope

- Linux builds use Ubuntu 22.04 as the baseline. DEB/RPM dependencies are generated from the actual binary. TGZ checks architecture and dynamic libraries; it cannot make incompatible system libraries compatible.
- Disposable-container checks cover Ubuntu 22.04/24.04, Debian 12 and Fedora 43: installation, dependencies, installed-app X11 UI smoke, reinstall, removal and settings preservation. This does not validate every physical desktop theme or Wayland combination.
- Hosted Windows checks cover isolated install, upgrade, shortcuts, registry and uninstall. macOS checks cover DMG mounting, actual PKG installation, signatures, universal architectures and UI smoke. Older Windows and physical Windows ARM64 remain unverified; macOS versions beyond the macOS 14 runner are not individually tested.
- Linux overlays require X11. Wayland supports tray/settings only and requires desktop AppIndicator support. Real Codex AX following and multiple displays on macOS still need device validation.
- No prebuilt Linux ARM64, 32-bit Windows, Alpine/musl, Android or iOS packages are provided. A GUI desktop and accessible local Codex are needed; this is not a headless server service.

See [README](../README.en.md) and the [native guide](native-desktop.en.md) for settings and source builds.
