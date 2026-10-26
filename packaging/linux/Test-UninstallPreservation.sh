#!/usr/bin/env bash
set -euo pipefail
script="$(cd "$(dirname "$0")" && pwd -P)/install.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/quota-uninstall-check.XXXXXX")
trap 'case "$work" in "${TMPDIR:-/tmp}"/quota-uninstall-check.*) rm -rf -- "$work" ;; esac' EXIT
app="$work/prefix/lib/codex-quota-strip"
mkdir -p "$app"
printf '%s\n' io.github.jyongli.codex-quota-strip > "$app/package-id"
printf 'keep\n' > "$app/user-file.txt"
printf 'owned icon fixture\n' > "$app/app.png"
printf '%s\n' io.github.jyongli.codex-quota-strip > "$app/app-icon-id"
printf 'user icon fixture\n' > "$app/user-icon.png"
cp "$script" "$app/uninstall.sh"
bash "$app/uninstall.sh"
test -f "$app/user-file.txt"
test ! -e "$app/app.png" || { echo 'FAIL: uninstall must remove its own private app icon'; exit 1; }
test -f "$app/user-icon.png" || { echo 'FAIL: uninstall must preserve user-created icons'; exit 1; }
test -f "$app/package-id" || { echo 'FAIL: preserved user files must retain directory ownership for later reinstall'; exit 1; }
rm -f -- "$app/app-icon-id"
printf 'unowned icon fixture\n' > "$app/app.png"
bash "$script" uninstall --prefix "$work/prefix"
test -f "$app/app.png" || { echo 'FAIL: an unregistered icon in an older installation must be preserved'; exit 1; }
echo 'PASS: uninstall retains ownership when user files remain'
