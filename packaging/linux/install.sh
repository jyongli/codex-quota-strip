#!/usr/bin/env bash
set -euo pipefail
source_root="$(cd "$(dirname "$0")" && pwd -P)"
action=install
prefix="${HOME:?HOME is required}/.local"
if [ "$(basename "$0")" = uninstall.sh ]; then action=uninstall; prefix="$(cd "$source_root/../.." && pwd -P)"; fi
if [ $# -gt 0 ] && { [ "$1" = install ] || [ "$1" = uninstall ]; }; then action=$1; shift; fi
while [ $# -gt 0 ]; do
  case "$1" in --prefix) [ $# -ge 2 ] || exit 2; prefix=$2; shift 2 ;; *) echo 'Usage: bash install.sh [install|uninstall] [--prefix absolute-path]' >&2; exit 2 ;; esac
done
case "$prefix" in /*) ;; *) echo 'Installation prefix must be absolute' >&2; exit 2 ;; esac
# Desktop Exec 的字段代码与转义会改变命令；允许空格，拒绝控制字符和这些特殊字符。
case "$prefix" in *$'\n'*|*$'\r'*|*$'\t'*|*'"'*|*'\'*|*'%'*|*'$'*|*'`'*) echo 'Unsupported characters in prefix' >&2; exit 2 ;; esac
prefix=${prefix%/}
[ -n "$prefix" ] || { echo 'Refusing root prefix' >&2; exit 2; }
app_dir="$prefix/lib/codex-quota-strip"
link="$prefix/bin/CodexQuotaStrip"
desktop="$prefix/share/applications/codex-quota-strip.desktop"
marker=io.github.jyongli.codex-quota-strip
[ ! -L "$app_dir" ] || { echo 'Application directory must not be a symlink' >&2; exit 1; }
if [ -e "$app_dir" ] && { [ ! -f "$app_dir/package-id" ] || [ "$(cat "$app_dir/package-id")" != "$marker" ]; }; then
  echo 'Existing application directory is not owned by this installer' >&2; exit 1
fi
if [ "$action" = uninstall ]; then
  [ -f "$app_dir/package-id" ] || { echo 'No user installation found'; exit 0; }
  # 逐项删除本程序登记的文件；用户创建文件、配置及 Codex 账号数据都不在清单内。
  if [ -L "$link" ] && [ "$(readlink "$link")" = "$app_dir/CodexQuotaStrip" ]; then rm -- "$link"; fi
  if [ -f "$desktop" ] && grep -q '^X-CodexQuotaStrip-Installer=1$' "$desktop"; then rm -- "$desktop"; fi
  for name in CodexQuotaStrip LICENSE THIRD_PARTY_NOTICES.md uninstall.sh; do rm -f -- "$app_dir/$name"; done
  # 额外用户文件使目录非空时保留所有权标记，后续可以重装；不误判为外部目录。
  if [ -z "$(find "$app_dir" -mindepth 1 -maxdepth 1 ! -name package-id -print -quit)" ]; then
    rm -- "$app_dir/package-id"
    rmdir -- "$app_dir"
  fi
  echo 'Uninstalled. User configuration and account data were preserved.'
  exit 0
fi
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || { echo 'This package requires Linux x86_64 (glibc desktop)' >&2; exit 1; }
binary="$source_root/usr/bin/CodexQuotaStrip"
[ -f "$binary" ] || { echo 'Run install.sh from the extracted release archive' >&2; exit 1; }
command -v ldd >/dev/null || { echo 'ldd is required to check runtime dependencies' >&2; exit 1; }
if ! libraries=$(ldd "$binary" 2>&1) || [[ "$libraries" == *'not found'* ]]; then
  printf '%s\n' "$libraries" >&2
  echo 'Missing/incompatible runtime libraries. Install GTK3, Ayatana AppIndicator and X11, or choose the DEB/RPM package.' >&2; exit 1
fi
if { [ -e "$link" ] || [ -L "$link" ]; } && { [ ! -L "$link" ] || [ "$(readlink "$link")" != "$app_dir/CodexQuotaStrip" ]; }; then echo 'Existing command is not owned by this installer' >&2; exit 1; fi
if [ -e "$desktop" ] && ! grep -q '^X-CodexQuotaStrip-Installer=1$' "$desktop"; then echo 'Existing desktop entry is not owned by this installer' >&2; exit 1; fi
mkdir -p -- "$app_dir" "$prefix/bin" "$prefix/share/applications"
install -m 755 -- "$binary" "$app_dir/CodexQuotaStrip"
for name in LICENSE THIRD_PARTY_NOTICES.md; do install -m 644 -- "$source_root/usr/share/doc/codex-quota-strip/$name" "$app_dir/$name"; done
install -m 755 -- "$source_root/install.sh" "$app_dir/uninstall.sh"
printf '%s\n' "$marker" > "$app_dir/package-id"
ln -sfn -- "$app_dir/CodexQuotaStrip" "$link"
cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Codex Quota Strip
Exec="$app_dir/CodexQuotaStrip"
Icon=utilities-system-monitor
Terminal=false
Categories=Utility;
X-CodexQuotaStrip-Installer=1
EOF
echo "Installed: $link"
echo "Uninstall: bash \"$app_dir/uninstall.sh\""
echo 'Restart your application launcher if its menu has not refreshed. Add the prefix/bin directory to PATH if needed.'
