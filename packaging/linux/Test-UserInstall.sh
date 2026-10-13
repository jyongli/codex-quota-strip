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
bash "$package_root/install.sh" install --prefix "$prefix"
cmp "$package_root/usr/bin/CodexQuotaStrip" "$prefix/lib/codex-quota-strip/CodexQuotaStrip"
test -L "$prefix/bin/CodexQuotaStrip"
test -f "$prefix/share/applications/codex-quota-strip.desktop"
grep -F "$prefix/lib/codex-quota-strip/CodexQuotaStrip" "$prefix/share/applications/codex-quota-strip.desktop"
printf 'user-created\n' > "$prefix/lib/codex-quota-strip/keep.txt"
bash "$package_root/install.sh" install --prefix "$prefix"
grep -q 'seconds=777' "$XDG_CONFIG_HOME/codex-quota-strip/settings.ini"
bash "$prefix/lib/codex-quota-strip/uninstall.sh"
test ! -e "$prefix/bin/CodexQuotaStrip"
test ! -e "$prefix/share/applications/codex-quota-strip.desktop"
test ! -e "$prefix/lib/codex-quota-strip/CodexQuotaStrip"
test -f "$prefix/lib/codex-quota-strip/keep.txt"
grep -q 'seconds=777' "$XDG_CONFIG_HOME/codex-quota-strip/settings.ini"
# 安装器不能覆盖没有本程序所有权标记的已有目录。
mkdir -p "$HOME/foreign/lib/codex-quota-strip"
if bash "$package_root/install.sh" install --prefix "$HOME/foreign"; then echo 'FAIL: unowned directory was overwritten'; exit 1; fi
echo 'PASS: Linux user install/upgrade/uninstall, spaces, shortcuts, settings and unowned-file preservation'
