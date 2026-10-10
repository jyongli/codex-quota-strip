#!/usr/bin/env bash
set -eu
case "$1" in /*) quota_binary="$1" ;; *) quota_binary="$PWD/$1" ;; esac
quota_temp_base="${TMPDIR:-/tmp}"
quota_test_root=$(mktemp -d "$quota_temp_base/quota-instance.XXXXXX")
quota_first_pid=''
quota_second_pid=''
cleanup() {
  for quota_pid in "$quota_first_pid" "$quota_second_pid"; do
    if [ -n "$quota_pid" ]; then kill "$quota_pid" 2>/dev/null || true; wait "$quota_pid" 2>/dev/null || true; fi
  done
  # 仅删除 mktemp 创建且已核实位于临时目录内的测试根目录。
  case "$quota_test_root" in "$quota_temp_base"/quota-instance.*) rm -rf -- "$quota_test_root" ;; *) exit 1 ;; esac
}
trap cleanup EXIT INT TERM
mkdir -p "$quota_test_root/home/Library/Application Support/CodexQuotaStrip" "$quota_test_root/config/codex-quota-strip"
for quota_ini in "$quota_test_root/home/Library/Application Support/CodexQuotaStrip/settings.ini" "$quota_test_root/config/codex-quota-strip/settings.ini"; do
  printf 'online=0\nonly_with_codex=0\noverlay=0\n' > "$quota_ini"
done
# 只改变测试子进程的 HOME/XDG 配置位置，宿主终端的环境保持不变。
env HOME="$quota_test_root/home" XDG_CONFIG_HOME="$quota_test_root/config" "$quota_binary" --home "$quota_test_root/account-a" --tray-only > "$quota_test_root/first.log" 2>&1 &
quota_first_pid=$!
sleep 1
kill -0 "$quota_first_pid"
env HOME="$quota_test_root/home" XDG_CONFIG_HOME="$quota_test_root/config" "$quota_binary" --home "$quota_test_root/account-b" --tray-only > "$quota_test_root/second.log" 2>&1 &
quota_second_pid=$!
sleep 1
if kill -0 "$quota_second_pid" 2>/dev/null; then
  printf 'FAIL: different account home created duplicate desktop instances\n'
  exit 1
fi
wait "$quota_second_pid"
quota_second_pid=''
printf 'PASS: per-user desktop instance remains unique across account-home changes\n'
