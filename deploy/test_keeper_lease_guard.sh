#!/usr/bin/env bash
# test_keeper_lease_guard.sh — verify the App `RootDaemonKeeper` lease guard (on-device, end to end).
#
# ## Why "root unavailable" has to be manufactured on purpose
#
# The guard code sits **after** `if (client.isRoot()) return` inside `check()` — while root is
# available the keeper returns early and **never** reaches the lease decision. That is exactly
# the failure recorded in `docs/lease-and-keeper.md`: the keeper was stuck in acquireRoot and
# never reached the lease check, so the outcome was "unobservable", not pass or fail.
#
# ## What this test does
#
# It makes "device healthy + keeper idle + root unavailable" hold at the same time, then
# observes:
#   1. the guard log appears in logcat and **names the right owner** (`deploy`, not a hardcoded
#      `desktop`)
#   2. the stale socket **survives** (the guard must yield before deleting the socket)
#
# **Control group**: without holding the lease the keeper **should** delete the stale socket.
# That proves the keeper is really running and that "the socket survives" is caused by the
# lease, not by the keeper not acting at all.
#
# ## Safety mechanism
#
# Root must be made unavailable temporarily, and once the root daemon is killed **only root can
# rebuild it**. So this script **first** starts `root_restore_delayed.sh` (a detached root
# process) through the root socket; when the time is up it brings the daemon and keepalive back
# automatically — **no exploit rerun needed**.
#
# Usage:
#   SERIAL=<serial> bash deploy/test_keeper_lease_guard.sh
#   ROOT_DELAY=360 adjusts the recovery delay (default 300 s; it **must exceed the total time
#   of both phases**, otherwise the restorer brings root back mid-test and the keeper returns
#   early, making the guard unobservable)
source "$(dirname "$0")/env.sh"
set -u
source "$(dirname "$0")/lease.sh"
source "$(dirname "$0")/proc_util.sh"

SOCK=/data/local/tmp/root_daemon.sock
KA_SH=/data/local/tmp/root_daemon_keepalive.sh
RESTORER=/data/local/tmp/root_restore_delayed.sh
RESTORER_LOG=/data/local/tmp/root_daemon.log
ROOT_DELAY="${ROOT_DELAY:-300}"
# Worst case for both phases: control 75 s + experiment 75 s + extra 35 s ~ 185 s, plus adb
# round trips -> 240 s is the floor
if [ "$ROOT_DELAY" -lt 240 ]; then
  echo "✗ ROOT_DELAY=${ROOT_DELAY}s 太短（<240s）—— 復原行程會在測試中途恢復 root，測試將無效"; exit 64
fi

PASS=0; FAIL=0
ok()  { echo "  ✓ $1"; PASS=$((PASS+1)); }
bad() { echo "  ✗ $1"; FAIL=$((FAIL+1)); }

ka_pid()    { timeout 25 "$ADB" -s "$SERIAL" shell "ps -A -o PID,ARGS 2>/dev/null | grep '[r]oot_daemon_keepalive.sh' | awk '{print \$1}'" 2>/dev/null | tr -d '\r' | head -1; }
daemon_pid(){ timeout 25 "$ADB" -s "$SERIAL" shell "ps -A -o PID,ARGS 2>/dev/null | grep '[r]oot_daemon$' | awk '{print \$1}'" 2>/dev/null | tr -d '\r' | head -1; }
sock_exists(){ timeout 25 "$ADB" -s "$SERIAL" shell "[ -e $SOCK ] && echo yes || echo no" 2>/dev/null | tr -d '\r'; }
keeper_log(){ timeout 25 "$ADB" -s "$SERIAL" shell "logcat -d -s RootDaemonKeeper:* 2>/dev/null" 2>/dev/null | tr -d '\r'; }

cleanup() {
  lease_cleanup deploy >/dev/null 2>&1
  echo ""
  echo "[cleanup] 租約已釋放。復原 daemon 由 root_restore_delayed.sh 負責（延遲 ${ROOT_DELAY}s）。"
  echo "[cleanup] 若 root 未回來，檢查 $RESTORER_LOG 的 restorer 行。"
}
trap cleanup EXIT

echo "===== RootDaemonKeeper 租約守衛測試 ====="
echo "裝置 $SERIAL ｜ 各階段以輪詢等待結果（keeper 週期 30s，逾時 75s）"
echo ""

# ---------- precondition cleanup and checks ----------
# Purpose: make sure the keeper has **no** trigger source available even with root gone.
# Otherwise the keeper would really rerun the exploit in the control group (slow, and sensitive
# to kernel state).
# ⚠ The precondition checks must cover the **heartbeat file**: the keeper decides whether the
# helper is resident from `ghostlock_bridge/helper.alive` (120 s tolerance), **not from ps** —
# checking ps alone would miss the window where "the process is dead but the heartbeat is still
# fresh".
echo "[0] 前置清理與檢查"
if ! root_ok; then
  echo "  ✗ 需要 root 才能進行（本測試要先把 root 關掉，再讓 keeper 觀察）"; exit 1
fi
ok "root 可用"

kill_all_helpers >/dev/null 2>&1
kill_all_exploits >/dev/null 2>&1
clear_trigger_mailboxes
sleep 2
if [ "$(helper_count)" != "0" ]; then
  echo "  ✗ 仍有 bridge helper 行程 → 測試不安全"; exit 1
fi
ok "無 bridge helper 行程"
if [ "$(exploit_count)" != "0" ]; then
  echo "  ✗ 仍有殘留 exploit 行程 → 測試不安全"; exit 1
fi
ok "無殘留 exploit 行程"

# write the heartbeat file to 0 -> the keeper immediately treats the helper as not resident,
# no need to wait 120 s for natural expiry
timeout 25 "$ADB" -s "$SERIAL" shell "echo 0 > /data/local/tmp/ghostlock_bridge/helper.alive" >/dev/null 2>&1
sleep 1
HB=$(timeout 25 "$ADB" -s "$SERIAL" shell "cat /data/local/tmp/ghostlock_bridge/helper.alive 2>/dev/null" | tr -d '\r')
if [ "$HB" = "0" ]; then ok "helper 心跳已失效（keeper 會判定無觸發來源）"
else bad "無法讓 helper 心跳失效（值=$HB）—— keeper 可能真的重武裝"; exit 1; fi

if [ -n "$(timeout 25 "$ADB" -s "$SERIAL" shell 'pidof shizuku_server 2>/dev/null' | tr -d '\r')" ]; then
  echo "  ✗ Shizuku 在跑 → keeper 會走 Shizuku 重武裝，測試不安全"; exit 1
fi
ok "無 Shizuku"

# ---------- arm the restorer first ----------
echo ""
echo "[1] 武裝延遲復原行程（daemon 死後才由它帶回來）"
timeout 60 "$ADB" -s "$SERIAL" push "$(dirname "$0")/root_restore_delayed.sh" /data/local/tmp/ >/dev/null 2>&1
timeout 25 "$ADB" -s "$SERIAL" shell "chmod 755 $RESTORER" >/dev/null 2>&1
root_exec "setsid sh $RESTORER $ROOT_DELAY >/dev/null 2>&1 &" >/dev/null
sleep 4
# **It must be confirmed to run as root**: a non-root restorer has no permission to rebuild the
# daemon, which would mean throwing root away with no way back.
if timeout 25 "$ADB" -s "$SERIAL" shell "ps -A -o USER,ARGS 2>/dev/null | grep '[r]oot_restore_delayed' | grep -q '^root'" 2>/dev/null; then
  ok "復原行程已就緒且為 root 身分（${ROOT_DELAY}s 後恢復 root）"
else
  echo "  ✗ 復原行程沒起來或不是 root —— 中止，不要拿 root 冒險"; exit 1
fi

# ---------- make root unavailable ----------
echo ""
echo "[2] 讓 root 對 App 失效（先殺 keepalive，再殺 daemon）"
KP=$(ka_pid); DP=$(daemon_pid)
[ -n "$KP" ] && ok "keepalive pid=$KP" || echo "  (keepalive 不在，略過)"
root_exec "kill -9 $KP $DP" >/dev/null 2>&1
sleep 3
if root_ok; then bad "root 仍然可用（daemon 沒被殺掉？）"; else ok "root 已失效（keeper 現在才會走到租約判斷）"; fi
# the stale socket should still be there (it is what the guard protects)
if [ "$(sock_exists)" = "yes" ]; then ok "stale socket 檔案存在（守衛的保護對象）"; else
  timeout 25 "$ADB" -s "$SERIAL" shell "touch $SOCK" >/dev/null 2>&1
  echo "  (socket 檔不在，已建立一個作為觀察對象)"
fi

# ---------- control group: not holding the lease ----------
# ⚠ **The criterion is the log, not whether the socket file exists.**
# Measured (2026-09-13): `/data/local/tmp` is `drwxrwx--x shell shell`, the App (uid 10053) is
# "other" and **has no directory write permission** -> the App's `delete()` always fails. So
# "the socket is still there" **cannot** prove the guard worked (without the guard it cannot be
# deleted either). The real observable difference is whether the keeper gets **past the guard**:
#   past it   -> logs stale-socket handling plus "root lost and no trigger available"
#   yields    -> logs external operator(...) holds lease, and no "no trigger" line appears
echo ""
echo "[3] 對照組：**不**持有租約 → keeper 應越過守衛（記錄 no trigger available）"
lease_cleanup deploy >/dev/null 2>&1   # make sure there is no lease
timeout 25 "$ADB" -s "$SERIAL" shell "logcat -c" >/dev/null 2>&1
echo "  輪詢最多 75s，等 keeper 越過守衛..."
CTRL_PASSED=0; DEADLINE=$(( $(date +%s) + 75 ))
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
  if keeper_log | grep -q "no trigger available"; then CTRL_PASSED=1; break; fi
  sleep 5
done
CTRL_LOG=$(keeper_log)
if [ "$CTRL_PASSED" = "1" ]; then ok "keeper 越過守衛（無租約時行為正確）"
else bad "等了 75s，keeper 仍未越過守衛 —— keeper 可能沒在跑，後續結果不可信"; fi
echo "$CTRL_LOG" | grep -q "stale socket" && ok "logcat：有 stale socket 處置記錄（keeper 確實在跑）" \
  || bad "logcat：未見 stale socket 處置記錄"

# ---------- experiment group: holding a fresh deploy lease ----------
echo ""
echo "[4] 實驗組：持有新鮮 **deploy** 租約 → keeper 應在守衛處讓開"
if lease_take deploy >/dev/null 2>&1; then ok "已取得租約（$(lease_status)）"; else
  bad "取不到租約（有別人在跑？）—— 測試無效"; fi
timeout 25 "$ADB" -s "$SERIAL" shell "logcat -c" >/dev/null 2>&1
# first confirm the guard cycle really ran (the log line appears)
echo "  輪詢最多 75s，等守衛日誌出現..."
GUARD_SEEN=0; DEADLINE=$(( $(date +%s) + 75 ))
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
  if keeper_log | grep -q "external operator(deploy) holds lease"; then GUARD_SEEN=1; break; fi
  sleep 5
done
if [ "$GUARD_SEEN" = "1" ]; then ok "logcat：守衛日誌出現，owner 正確顯示為 **deploy**（寫死 desktop 的缺陷已修）"
else bad "logcat：等不到 external operator(deploy) 守衛日誌"; fi
# then cross one more full cycle (35 s > 30 s) to confirm the guard **keeps** yielding and does
# not slip past midway
echo "  再等 35s（跨一個 keeper 週期）確認守衛持續讓開..."
sleep 35
GUARD_LOG=$(keeper_log)
if echo "$GUARD_LOG" | grep -q "no trigger available"; then
  bad "logcat：實驗組竟出現 no trigger available —— keeper 越過了守衛"
else
  ok "logcat：實驗組未越過守衛（無 no trigger available）"
fi
echo "  --- 本次 keeper 日誌 ---"
echo "$GUARD_LOG" | tail -8 | sed 's/^/    /'

lease_cleanup deploy >/dev/null 2>&1
echo ""
echo "  (租約已釋放；等復原行程把 root 帶回來)"

# ---------- wait for recovery ----------
echo ""
echo "[5] 等待延遲復原"
WAITED=0
while [ "$WAITED" -lt $((ROOT_DELAY + 60)) ]; do
  if root_ok; then break; fi
  sleep 10; WAITED=$((WAITED + 10))
done
if root_ok; then ok "root 已復原（未重跑 exploit，等了約 ${WAITED}s）"
else bad "root 未復原 —— 見 $RESTORER_LOG"; fi
echo "  --- restorer 日誌 ---"
timeout 25 "$ADB" -s "$SERIAL" shell "grep restorer $RESTORER_LOG 2>/dev/null | tail -4" 2>/dev/null | tr -d '\r' | sed 's/^/    /'

echo ""
echo "===== 結果：$PASS 通過 / $FAIL 失敗 ====="
[ "$FAIL" -eq 0 ] || exit 1
