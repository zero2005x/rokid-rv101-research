#!/usr/bin/env bash
# proc_util.sh — deploy 腳本共用的「安全殺程序」工具。
#
# ## 為什麼不能用 `pkill -f`
#
# 兩個已實測的坑：
#
#  1. **自我匹配**：`pkill -f <pattern>` 的 pattern 會匹配到**執行它的 shell 本身**
#     （`sh -c "pkill -f ghostlock_rv101"` 的 cmdline 就含該字串）→ shell 自殺、
#     退出碼 137、清理中斷。（見 `docs/lease-and-keeper.md`）
#
#  2. **pattern 太窄會漏殺**：實測（2026-09-13）在裝置上以 `grep -E` 驗證：
#
#       pattern="ghostlock_bridge_helper.sh"    → 中 App helper、**不中** _desktop 變體
#       pattern="ghostlock_bridge_helper.s[h]"  → 中 App helper、**不中** _desktop 變體
#       pattern="ghostlock_bridge_helpe[r]"     → 兩者都中
#
#     原因是 `helper.sh` 與 `helper_desktop.sh` 在 "helper" 之後分岔。
#     這個漏殺讓桌面板 helper 在整場測試中存活，並在裝置自行重開機後被重新拉起、
#     看到殘留的 `last.cmd` 就再次觸發 exploit → **994 個子程序、load 75.8**。
#
# ## 本檔的做法
#
# **在 host 端過濾、只把 PID 送到裝置端 kill**：
#
#   ps（裝置） → grep/awk（host） → kill -9 <PID>（裝置）
#
# 裝置端執行的命令只有 `ps -A -o PID,ARGS`，**pattern 從不出現在裝置的 cmdline 上**，
# 因此結構上不可能自我匹配。搭配 `ps` 取 PID 再逐一 kill（本專案既有教訓）。
set -u

_PROC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
[ -n "${ADB:-}" ] && [ -n "${SERIAL:-}" ] || source "$_PROC_DIR/env.sh"

# 兩個 helper 變體的共同前綴：用 `helpe` 結尾可同時涵蓋
#   ghostlock_bridge_helper.sh
#   ghostlock_bridge_helper_desktop.sh
HELPER_PAT='ghostlock_bridge_helpe'
EXPLOIT_PAT='ghostlock_rv101'
ROOT_SOCK='/data/local/tmp/root_daemon.sock'

_padb() { "$ADB" -s "$SERIAL" "$@"; }

# ---- root 通道 ----
# exploit 提權成功後，它的行程變成 **root 所有**；adb shell（uid 2000）
# 殺不掉 root 行程（`kill` 回 EPERM）。所以清理殘留 exploit 必須能走 root 通道。
# 這也是 `root_restore_delayed.sh` 的同一個道理：只有 root 能收拾 root。
# 注意：必須直接呼叫 "$ADB"，**不能**寫成 `timeout 25 _padb ...` ——
# `timeout` 只能執行外部程式；餵 shell 函式給它會 exec 失敗，
# 並且**靜默**回傳空字串，於是 root_ok 誤判為 false、整段升級清理失效（實測踩過）。
root_exec() {
  printf '%s\n' "${1:?root_exec <cmd>}" | timeout 25 "$ADB" -s "$SERIAL" shell "nc -U $ROOT_SOCK" 2>/dev/null | tr -d '\r'
}
root_ok() { root_exec 'id' | grep -q 'uid=0'; }

# 列出符合 regex 的裝置 PID（host 端過濾；不影響裝置端 cmdline）
dev_pids_matching() {
  local pat="${1:?dev_pids_matching <regex>}"
  _padb shell "ps -A -o PID,ARGS" 2>/dev/null | tr -d '\r' \
    | grep -E "$pat" | grep -v '[g]rep' | awk '{print $1}' | grep -E '^[0-9]+$'
}

# 殺掉符合 regex 的程序，**先用 shell、殺不掉的再用 root 通道**。
# root 通道不可用時就把殘留數量回報出來（呼叫端可自行決定是否中止）。
kill_matching_escalating() {
  local pat="${1:?}" label="${2:-$1}" pids left
  pids=$(dev_pids_matching "$pat")
  [ -n "$pids" ] || return 0
  _padb shell "kill -9 $pids" >/dev/null 2>&1
  sleep 1
  left=$(dev_pids_matching "$pat")
  if [ -n "$left" ] && root_ok; then
    local batch="" n=0 pid
    for pid in $left; do
      batch="$batch $pid"; n=$((n + 1))
      if [ "$n" -ge 200 ]; then
        root_exec "kill -9$batch" >/dev/null 2>&1; batch=""; n=0
      fi
    done
    [ -n "$batch" ] && root_exec "kill -9$batch" >/dev/null 2>&1
    sleep 1
    left=$(dev_pids_matching "$pat")
  fi
  local total; total=$(echo "$pids" | wc -w | tr -d ' ')
  if [ -n "$left" ]; then
    echo "  [kill] $label：送出 $total 個 kill，仍有 $(echo "$left" | wc -w | tr -d ' ') 個殘留（可能需要 root 通道）" >&2
  else
    echo "  [kill] $label：已清除 $total 個程序" >&2
  fi
  return 0
}

# 殺掉符合 regex 的程序。回傳被送出的 kill 數。
#
# 效能注意：不要「一個 PID 一次 adb」——實測殺 515 個程序會超過 60 秒而讓呼叫端
# 逾時中斷（清理只做一半）。改成**分批**：host 端收齊 PID，再一次 adb 送整批 kill。
kill_matching() {
  local pat="${1:?kill_matching <regex>}" label="${2:-$1}" pids pid n=0 batch_n=0
  pids=$(dev_pids_matching "$pat")
  [ -n "$pids" ] || return 0

  local batch=""
  for pid in $pids; do
    batch="$batch $pid"
    batch_n=$((batch_n + 1))
    # 每 200 個送一批，避免命令列過長
    if [ "$batch_n" -ge 200 ]; then
      _padb shell "kill -9$batch" >/dev/null 2>&1
      batch=""; batch_n=0
    fi
  done
  [ -n "$batch" ] && _padb shell "kill -9$batch" >/dev/null 2>&1

  n=$(echo "$pids" | wc -w | tr -d ' ')
  sleep 1   # kill 是非同步的，稍等再讓呼叫端數殘留
  echo "  [kill] $label：送出 $n 個 kill -9" >&2
  return 0
}

# 收斂用：先殺 trigger 來源（helper），再殺被生出來的 exploit 子程序。
kill_all_helpers()  { kill_matching "$HELPER_PAT"  "bridge helper（含 _desktop）"; }
# exploit 提權後變 root 所有 → 用會升級到 root 通道的版本
kill_all_exploits() { kill_matching_escalating "$EXPLOIT_PAT" "ghostlock_rv101"; }

# 清掉兩個信箱的觸發檔——**這是失控重生迴圈的根源**：
# 裝置重開機後 helper 被重新拉起，若看到殘留的 last.cmd 就會立刻再跑一次 exploit。
clear_trigger_mailboxes() {
  _padb shell "rm -f \
    /data/local/tmp/ghostlock_bridge/last.cmd \
    /data/local/tmp/ghostlock_bridge_desktop/last.cmd \
    /data/local/tmp/ghostlock_bridge_desktop/last.progress" >/dev/null 2>&1
}

# 目前程序數（診斷用）
helper_count()  { dev_pids_matching "$HELPER_PAT"  | wc -l | tr -d ' '; }
exploit_count() { dev_pids_matching "$EXPLOIT_PAT" | wc -l | tr -d ' '; }

# 直接執行：顯示現況
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  case "${1:-status}" in
    status)
      echo "helpers=$(helper_count)  exploits=$(exploit_count)"
      for p in $(dev_pids_matching "$HELPER_PAT"); do
        echo "  helper pid=$p"
      done
      ;;
    kill)
      kill_all_helpers; kill_all_exploits; clear_trigger_mailboxes
      echo "after: helpers=$(helper_count)  exploits=$(exploit_count)"
      ;;
    *) echo "用法：bash deploy/proc_util.sh [status|kill]"; exit 64 ;;
  esac
fi
