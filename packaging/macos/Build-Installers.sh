#!/usr/bin/env bash
set -euo pipefail
build_dir="$(cd "$1" && pwd -P)"
app="$build_dir/CodexQuotaStrip.app"
output="$build_dir/artifact"
mkdir -p "$output"
codesign --verify --deep --strict "$app"
lipo "$app/Contents/MacOS/CodexQuotaStrip" -verify_arch arm64 x86_64
# 单个应用只依赖系统框架；磁盘映像保留签名，Applications 链接提供拖放安装入口。
stage=$(mktemp -d "${TMPDIR:-/tmp}/quota-dmg.XXXXXX")
trap 'case "$stage" in "${TMPDIR:-/tmp}"/quota-dmg.*) rm -rf -- "$stage" ;; esac' EXIT
ditto "$app" "$stage/CodexQuotaStrip.app"
ln -s /Applications "$stage/Applications"
cp "$app/Contents/Resources/LICENSE" "$app/Contents/Resources/THIRD_PARTY_NOTICES.md" "$stage/"
hdiutil create -volname 'Codex Quota Strip' -srcfolder "$stage" -ov -format UDZO "$output/CodexQuotaStrip-macos-universal.dmg"
# component PKG 不包含脚本，不修改登录项/权限/用户配置。应用目录由安装器按组件管理。
mkdir -p "$stage/pkgroot/Applications"
ditto "$app" "$stage/pkgroot/Applications/CodexQuotaStrip.app"
pkgbuild --analyze --root "$stage/pkgroot" "$stage/components.plist"
# 禁止组件自动定位到其他同 bundle ID 的副本，例如用户的 Downloads 或构建目录。
/usr/libexec/PlistBuddy -c 'Set :0:BundleIsRelocatable false' "$stage/components.plist"
pkgbuild --root "$stage/pkgroot" --component-plist "$stage/components.plist" --install-location / --identifier io.github.jyongli.codex-quota-strip --version 3.0.0.6 "$output/CodexQuotaStrip-macos-universal.pkg"
