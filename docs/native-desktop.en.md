# Native Linux and macOS preview

[中文](native-desktop.zh-CN.md) | **English**

The native version uses C++17, GTK3/Ayatana indicators on Linux and AppKit on macOS. It provides quota values, settings, interval-based queries and a top/left window-following strip, without Electron, WebView or screen capture.

## Downloads and platforms

[Download the native preview](https://github.com/jyongli/codex-quota-strip/releases/tag/v3.0.0-preview.4)

| Artifact | Environment |
| --- | --- |
| `CodexQuotaStrip-linux-x86_64.deb` | Ubuntu 22.04/24.04 and compatible Debian-based x64 desktops |
| `CodexQuotaStrip-linux-x86_64.tar.gz` | Linux x64 with GTK3, Ayatana AppIndicator, X11 and compatible runtime libraries |
| `CodexQuotaStrip-macos-universal.zip` | macOS 14+, with an Apple Silicon arm64 / Intel x86_64 `.app` |

Linux window following requires an X11 session. Wayland supports tray/settings, but does not expose a general way to locate other applications. Host detection and overlay controls are disabled there; online queries run at the configured interval. Your desktop must support AppIndicator/StatusNotifier. GNOME may need an indicator extension installed and enabled.

macOS window following requires the user's Accessibility permission. Preview bundles have an ad-hoc integrity signature, without Apple Developer ID signing or notarization; macOS may block first launch. After checking the source/checksum, follow [Apple's instructions](https://support.apple.com/en-us/102445) to allow this specific app.

## Install on Linux

From the download directory:

```bash
sudo apt install ./CodexQuotaStrip-linux-x86_64.deb
CodexQuotaStrip --settings
```

Alternatively, use the tar archive:

```bash
sudo apt install libgtk-3-0 libayatana-appindicator3-1 libx11-6
tar -xzf CodexQuotaStrip-linux-x86_64.tar.gz
./CodexQuotaStrip-linux-x86_64/usr/bin/CodexQuotaStrip --settings
```

Wine is not required. Launch from your application menu or run the extracted/built executable. Right-click either quota indicator to open settings, refresh or quit.

## Install on macOS

1. Extract the ZIP and move `CodexQuotaStrip.app` into Applications.
2. Open it; `5h … W …` appears in the menu bar. Choose `Settings…` from its menu.
3. Use `Allow window following…` or `Allow Accessibility…`, then grant this app Accessibility access in System Settings yourself.
4. Return to the Codex/ChatGPT desktop window. The strip follows it and hides when another application becomes foreground.

To launch with settings from a terminal:

```bash
open "/Applications/CodexQuotaStrip.app" --args --settings
```

Screen Recording permission is not required. Updates may require renewed Accessibility approval; follow the system prompt.

## Codex sign-in and executable

Sign in through local Codex CLI/desktop first. The utility reads account quotas through `codex app-server`, without another API key or model inference. Plan quotas generally require ChatGPT sign-in; missing windows display `-`.

Discovery searches `PATH`, common installation directories and some desktop bundle paths. If a compatible executable is not found, enter its full path in settings. Check it from a terminal:

```bash
command -v codex
codex --version
```

See the [official CLI guide](https://learn.chatgpt.com/docs/codex/cli) for installation/sign-in. Select the same data directory if you use a custom `CODEX_HOME`.

Account queries reuse the four proxy variables in `[shell_environment_policy.set]` from the selected `config.toml`, including its selected profile. Changes apply at the next query without restarting; unconfigured variables inherit the launch environment. See [proxy configuration](../README.en.md#proxies-and-restricted-networks) for scope, examples and error handling.

## Settings and commands

All three platforms support Chinese/English UI switching. Native previews default to English. Select `中文` or `English` under `Language`, then save and apply; changes take effect immediately and persist across restarts (`language=zh-CN` or `language=en` in settings.ini). Queries default to every 120 seconds (30–3600); the stale threshold defaults to 5 minutes (1–120).

| Setting | Meaning |
| --- | --- |
| `Query Codex account online` | Enable account queries; disabling preserves the current snapshot without additional queries |
| `Query only while the host … is open` | Schedule queries only when a host desktop app is detected; disable for CLI-only use. Manual refresh bypasses the host condition |
| `Show window-following overlay` | Show the foreground host's strip; requires macOS Accessibility or Linux X11 |
| `Overlay position` | Top or Left |
| `Language` | Chinese or English; applies immediately after saving |
| `Codex data directory` | Absolute directory for the signed-in account |
| `Codex executable` | Blank for discovery, or an explicit local executable path |
| `Host … contains` | Match an app name/bundle ID on macOS or window class on Linux |

The default `Codex` host also matches `ChatGPT` for the integrated desktop app. The strip appears only when a matching app is foreground. If you select a terminal as host, all matching terminal windows may show the strip; this does not detect whether Codex CLI is currently running inside them.

Settings locations:

- Linux: `$XDG_CONFIG_HOME/codex-quota-strip/settings.ini`, or `~/.config/codex-quota-strip/settings.ini` by default.
- macOS: `~/Library/Application Support/CodexQuotaStrip/settings.ini`.

One native instance runs per user configuration directory, independent of the selected account home. For an existing instance, open settings through the tray/menu bar, or quit before launching with different arguments.

Linux examples, after quitting the existing instance:

```bash
CodexQuotaStrip --settings
CodexQuotaStrip --home "$HOME/.codex" --codex-binary /usr/local/bin/codex --top
CodexQuotaStrip --left
CodexQuotaStrip --tray-only
CodexQuotaStrip --interval 60
```

On macOS, substitute `/Applications/CodexQuotaStrip.app/Contents/MacOS/CodexQuotaStrip` for `CodexQuotaStrip`. The native version does not implement Windows aliases such as `--taskbar`/`--quit`; exit through `Quit` in the tray/menu bar.

Data comes from online account queries and the last in-memory snapshot. Windows session-log offline fallback is not ported yet. Gray/`*` values indicate stale data; expired windows display `-`. Auth-file metadata changes clear old values without reading authentication-file contents.

Accounts explicitly providing only weekly quota (including some Pro accounts) automatically hide the five-hour icon/label and compact the strip. Unknown startup data or expired known windows retain `-`; a later five-hour window restores its display. No account-type guessing is used.

## Build from source

Ubuntu/Debian:

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

macOS, with Xcode Command Line Tools and Homebrew already installed:

```bash
brew install cmake
git clone https://github.com/jyongli/codex-quota-strip.git
cd codex-quota-strip
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
open build/native/CodexQuotaStrip.app
```

To package:

```bash
cd build/native
cpack
```

Without local compiler tools, use [GitHub Actions](building-on-linux-macos.md) and download the corresponding native artifact.

## Validation scope

Hosted tests cover UTC/quotas, real pipes, errors/timeouts/cancellation and child cleanup, settings, changed/moved auth files, per-user single instances, and X11 focus hiding/movement/recovery. macOS smoke covers menu-bar setup, settings persistence and passive overlay lifecycle.

Actual Codex AX following, multiple displays and live account queries have not been validated on users' Macs. Wayland policy tests simulate session capabilities rather than every real compositor. These first artifacts are therefore released as a preview.

Idle CPU measurements in `ui-smoke.txt` use a hosted session with no account and the settings window closed. `peak_rss_mib` is historical process peak memory, not current private memory; results vary across systems/themes.
