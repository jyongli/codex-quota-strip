#!/usr/bin/env bash
set -euo pipefail
package_root="$(cd "$1" && pwd -P)"
if [ ! -f "$package_root/install.sh" ]; then echo 'FAIL: portable package must provide its user installer'; exit 1; fi
test_root=$(mktemp -d "${TMPDIR:-/tmp}/quota-user-install.XXXXXX")
trap 'case "$test_root" in "${TMPDIR:-/tmp}"/quota-user-install.*) rm -rf -- "$test_root" ;; esac' EXIT
export HOME="$test_root/home" XDG_CONFIG_HOME="$test_root/config"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/codex-quota-strip"
printf 'online=0\nseconds=777\n' > "$XDG_CONFIG_HOME/codex-quota-strip/settings.ini"
prefix="$HOME/custom prefix"
# 已有主题图标不属于 TGZ 安装器，即使名称相同也应原样保留。
theme="$prefix/share/icons/hicolor/256x256/apps"
mkdir -p "$theme"
printf 'user theme icon\n' > "$theme/codex-quota-strip.png"
printf 'other application icon\n' > "$theme/foreign.png"
test -s "$package_root/usr/share/icons/hicolor/scalable/apps/codex-quota-strip.svg"
test -s "$package_root/usr/share/icons/hicolor/256x256/apps/codex-quota-strip.png"
bash "$package_root/install.sh" install --prefix "$prefix"
cmp "$package_root/usr/bin/CodexQuotaStrip" "$prefix/lib/codex-quota-strip/CodexQuotaStrip"
cmp "$package_root/usr/share/icons/hicolor/256x256/apps/codex-quota-strip.png" "$prefix/lib/codex-quota-strip/app.png"
test -L "$prefix/bin/CodexQuotaStrip"
test -f "$prefix/share/applications/codex-quota-strip.desktop"
grep -F "$prefix/lib/codex-quota-strip/CodexQuotaStrip" "$prefix/share/applications/codex-quota-strip.desktop"
grep -Fx "Icon=$prefix/lib/codex-quota-strip/app.png" "$prefix/share/applications/codex-quota-strip.desktop"
printf 'user-created\n' > "$prefix/lib/codex-quota-strip/keep.txt"
printf 'user-created icon\n' > "$prefix/lib/codex-quota-strip/keep-icon.png"
bash "$package_root/install.sh" install --prefix "$prefix"
grep -q 'seconds=777' "$XDG_CONFIG_HOME/codex-quota-strip/settings.ini"
bash "$prefix/lib/codex-quota-strip/uninstall.sh"
test ! -e "$prefix/bin/CodexQuotaStrip"
test ! -e "$prefix/share/applications/codex-quota-strip.desktop"
test ! -e "$prefix/lib/codex-quota-strip/CodexQuotaStrip"
test ! -e "$prefix/lib/codex-quota-strip/app.png"
test -f "$prefix/lib/codex-quota-strip/keep.txt"
test -f "$prefix/lib/codex-quota-strip/keep-icon.png"
grep -q 'user theme icon' "$theme/codex-quota-strip.png"
grep -q 'other application icon' "$theme/foreign.png"
grep -q 'seconds=777' "$XDG_CONFIG_HOME/codex-quota-strip/settings.ini"
bash "$package_root/install.sh" install --prefix "$prefix"
test -f "$prefix/lib/codex-quota-strip/keep.txt"
cmp "$package_root/usr/bin/CodexQuotaStrip" "$prefix/lib/codex-quota-strip/CodexQuotaStrip"
bash "$prefix/lib/codex-quota-strip/uninstall.sh"
# 安装器不能覆盖没有本程序所有权标记的已有目录。
mkdir -p "$HOME/foreign/lib/codex-quota-strip"
if bash "$package_root/install.sh" install --prefix "$HOME/foreign"; then echo 'FAIL: unowned directory was overwritten'; exit 1; fi
# 旧版目录虽有 package-id，也不能授权覆盖后来新增的、尚未登记的同名用户图标。
unowned_icon_app="$HOME/unowned icon/lib/codex-quota-strip"
mkdir -p "$unowned_icon_app"
printf '%s\n' io.github.jyongli.codex-quota-strip > "$unowned_icon_app/package-id"
printf 'unregistered user icon\n' > "$unowned_icon_app/app.png"
if bash "$package_root/install.sh" install --prefix "$HOME/unowned icon"; then echo 'FAIL: unregistered user icon was overwritten'; exit 1; fi
grep -q 'unregistered user icon' "$unowned_icon_app/app.png"
test ! -e "$unowned_icon_app/CodexQuotaStrip"
echo 'PASS: Linux user install/upgrade/uninstall, private icon, spaces, shortcuts, settings and unowned-file/icon preservation'
