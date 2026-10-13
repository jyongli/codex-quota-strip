#!/usr/bin/env bash
set -euo pipefail
build_dir="$(cd "$1" && pwd -P)"
output="$build_dir/artifact"
work=$(mktemp -d "${TMPDIR:-/tmp}/quota-mac-package.XXXXXX")
mounted=false
cleanup(){
  if $mounted; then hdiutil detach "$work/mount"; fi
  case "$work" in "${TMPDIR:-/tmp}"/quota-mac-package.*) rm -rf -- "$work" ;; esac
}
trap cleanup EXIT
mkdir "$work/mount"
hdiutil attach -nobrowse -readonly -mountpoint "$work/mount" "$output/CodexQuotaStrip-macos-universal.dmg"
mounted=true
test -L "$work/mount/Applications"
codesign --verify --deep --strict "$work/mount/CodexQuotaStrip.app"
lipo "$work/mount/CodexQuotaStrip.app/Contents/MacOS/CodexQuotaStrip" -verify_arch arm64 x86_64
hdiutil detach "$work/mount"; mounted=false
# 只在一次性 CI runner 进行系统级 PKG 安装，拒绝覆盖预先存在的应用。
test "${CI:-}" = true
test ! -e /Applications/CodexQuotaStrip.app
pkgutil --expand "$output/CodexQuotaStrip-macos-universal.pkg" "$work/expanded"
test ! -d "$work/expanded/Scripts"
sudo installer -pkg "$output/CodexQuotaStrip-macos-universal.pkg" -target /
codesign --verify --deep --strict /Applications/CodexQuotaStrip.app
cmp "$build_dir/CodexQuotaStrip.app/Contents/MacOS/CodexQuotaStrip" /Applications/CodexQuotaStrip.app/Contents/MacOS/CodexQuotaStrip
test "$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' /Applications/CodexQuotaStrip.app/Contents/Info.plist)" = 14.0
/Applications/CodexQuotaStrip.app/Contents/MacOS/CodexQuotaStrip --smoke-test
echo 'PASS: macOS DMG mount, Applications link, PKG install, packaged signatures/architectures and installed-app UI'
