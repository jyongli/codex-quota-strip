#!/usr/bin/env bash
set -euo pipefail
package_dir="$(cd "$1" && pwd -P)"
mode=$2
work=$(mktemp -d /tmp/quota-package-check.XXXXXX)
trap 'case "$work" in /tmp/quota-package-check.*) rm -rf -- "$work" ;; esac' EXIT
export HOME="$work/home" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/codex-quota-strip"
printf 'online=0\nseconds=777\n' > "$XDG_CONFIG_HOME/codex-quota-strip/settings.ini"
if [ "$mode" = deb ]; then
  apt-get update -qq
  apt-get install -y "$package_dir/CodexQuotaStrip-linux-x86_64.deb" xvfb dbus-x11
  dpkg-query -W -f='${Version}\n' codex-quota-strip | grep -F '3.0.0~preview7'
else
  dnf install -y "$package_dir/CodexQuotaStrip-linux-x86_64.rpm" xorg-x11-server-Xvfb dbus-x11
  rpm -q codex-quota-strip
  # 系统 hicolor 目录由主题／桌面组件共享，RPM 文件清单不得把这些目录登记给本应用。
  if rpm -ql codex-quota-strip | grep -E '^/usr/share/icons(/hicolor(/(scalable|256x256)(/apps)?)?)?$'; then
    echo 'FAIL: RPM must not own shared hicolor directories' >&2; exit 1
  fi
fi
test -s /usr/share/icons/hicolor/scalable/apps/codex-quota-strip.svg
test -s /usr/share/icons/hicolor/256x256/apps/codex-quota-strip.png
grep -Fx 'Icon=codex-quota-strip' /usr/share/applications/codex-quota-strip.desktop
printf 'preserve unrelated icon\n' > /usr/share/icons/hicolor/256x256/apps/quota-package-preserved.png
ldd /usr/bin/CodexQuotaStrip | tee "$work/ldd.txt"
! grep -q 'not found' "$work/ldd.txt"
xvfb-run -a dbus-run-session -- env XDG_SESSION_TYPE=x11 /usr/bin/CodexQuotaStrip --smoke-test
# 原包覆盖安装/重装验证与已有设置兼容，不伪称已经测试历史版本的迁移。
if [ "$mode" = deb ]; then apt-get install --reinstall -y "$package_dir/CodexQuotaStrip-linux-x86_64.deb"; apt-get remove -y codex-quota-strip
else dnf reinstall -y "$package_dir/CodexQuotaStrip-linux-x86_64.rpm"; dnf remove -y codex-quota-strip; fi
test ! -e /usr/bin/CodexQuotaStrip
test ! -e /usr/share/icons/hicolor/scalable/apps/codex-quota-strip.svg
test ! -e /usr/share/icons/hicolor/256x256/apps/codex-quota-strip.png
grep -q 'preserve unrelated icon' /usr/share/icons/hicolor/256x256/apps/quota-package-preserved.png
rm -- /usr/share/icons/hicolor/256x256/apps/quota-package-preserved.png
grep -q 'seconds=777' "$XDG_CONFIG_HOME/codex-quota-strip/settings.ini"
echo "PASS: $(. /etc/os-release; echo "$PRETTY_NAME") $mode installation, dependencies, UI, reinstall, removal and settings preservation"
