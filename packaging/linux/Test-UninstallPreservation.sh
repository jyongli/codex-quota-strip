#!/usr/bin/env bash
set -euo pipefail
script="$(cd "$(dirname "$0")" && pwd -P)/install.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/quota-uninstall-check.XXXXXX")
trap 'case "$work" in "${TMPDIR:-/tmp}"/quota-uninstall-check.*) rm -rf -- "$work" ;; esac' EXIT
app="$work/prefix/lib/codex-quota-strip"
mkdir -p "$app"
printf '%s\n' io.github.jyongli.codex-quota-strip > "$app/package-id"
printf 'keep\n' > "$app/user-file.txt"
cp "$script" "$app/uninstall.sh"
bash "$app/uninstall.sh"
test -f "$app/user-file.txt"
test -f "$app/package-id" || { echo 'FAIL: preserved user files must retain directory ownership for later reinstall'; exit 1; }
echo 'PASS: uninstall retains ownership when user files remain'
