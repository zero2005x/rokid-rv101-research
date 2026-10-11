#!/usr/bin/env bash
# test_keeper_lease_guard.sh — 驗證 App `RootDaemonKeeper` 的租約守衛（實機、端到端）。
#
# ## 為什麼要特地製造「root 失效」
#
# 守衛程式碼位於 `check()` 裡 `if (client.isRoot()) return` **之後** ——
# root 可用時 keeper 直接早退，**永遠不會**執行到租約判斷。
# 這正是 `docs/lease-and-keeper.md` 記錄的那次失敗：keeper 卡在 acquireRoot，
# 從頭到尾沒跑到租約檢查，所以「無法觀測」而非通過或失敗。
#
# ## 本測試做什麼
#
# 製造「裝置健康 + keeper 閒置 + root 失效」三者同時成立，然後觀察：
#   1. logcat 出現守衛日誌，且**指出正確的 owner**（`deploy`，不是寫死的 `desktop`）
#   2. stale socket **存活**（守衛必須在刪 socket 之前就讓開）
#
# **對照組**：不持有租約時，keeper **應該**刪掉 stale socket。
# 這證明 keeper 確實在跑、且「socket 存活」是租約造成的，而不是 keeper 根本沒動。
#
# ## 安全機制
#
# 需要暫時讓 root 失效，而 root daemon 一旦被殺**只有 root 能重建**。
# 因此本腳本會**先**經 root socket 啟動 `root_restore_delayed.sh`（detach 的 root 行程），
# 時間到自動把 daemon 與 keepalive 帶回來 —— **不需重跑 exploit**。
#
# 用法：
#   SERIAL=<序號> bash deploy/test_keeper_lease_guard.sh
#   ROOT_DELAY=360 可調整復原延遲（預設 300s；**必須大於兩個階段的總時間**，
#   否則復原行程會在測試中途把 root 帶回來，keeper 就早退而觀測不到守衛）
source "$(dirname "$0")/env.sh"
set -u
source "$(dirname "$0")/lease.sh"
source "$(dirname "$0")/proc_util.sh"

SOCK=/data/local/tmp/root_daemon.sock
KA_SH=/data/local/tmp/root_daemon_keepalive.sh
RESTORER=/data/local/tmp/root_restore_delayed.sh
RESTORER_LOG=/data/local/tmp/root_daemon.log
ROOT_DELAY="${ROOT_DELAY:-300}"
# 兩個階段最壞情況：控制 75s + 實驗 75s + 額外 35s ≈ 185s，再加 adb 往返 → 取 240s 為下限
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

# ---------- 前置清理與檢查 ----------
# 目的：確保「即使 root 失效，keeper 也**沒有**觸發來源可用」。
# 否則 keeper 會在對照組真的重跑一次 exploit（慢、且對核心狀態敏感）。
# ⚠ 前置檢查必須涵蓋 **心跳檔**：keeper 判斷 helper 是否常駐靠的是
# `ghostlock_bridge/helper.alive`（容忍 120s），**不是 ps** ——
# 只看 ps 會漏掉「行程已死但心跳還新鮮」的窗口。
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

# 把心跳檔寫成 0 → keeper 立刻視為「不常駐」，不必等 120 秒自然過期
timeout 25 "$ADB" -s "$SERIAL" shell "echo 0 > /data/local/tmp/ghostlock_bridge/helper.alive" >/dev/null 2>&1
sleep 1
HB=$(timeout 25 "$ADB" -s "$SERIAL" shell "cat /data/local/tmp/ghostlock_bridge/helper.alive 2>/dev/null" | tr -d '\r')
if [ "$HB" = "0" ]; then ok "helper 心跳已失效（keeper 會判定無觸發來源）"
else bad "無法讓 helper 心跳失效（值=$HB）—— keeper 可能真的重武裝"; exit 1; fi

if [ -n "$(timeout 25 "$ADB" -s "$SERIAL" shell 'pidof shizuku_server 2>/dev/null' | tr -d '\r')" ]; then
  echo "  ✗ Shizuku 在跑 → keeper 會走 Shizuku 重武裝，測試不安全"; exit 1
fi
ok "無 Shizuku"

# ---------- 先武裝復原行程 ----------
echo ""
echo "[1] 武裝延遲復原行程（daemon 死後才由它帶回來）"
timeout 60 "$ADB" -s "$SERIAL" push "$(dirname "$0")/root_restore_delayed.sh" /data/local/tmp/ >/dev/null 2>&1
timeout 25 "$ADB" -s "$SERIAL" shell "chmod 755 $RESTORER" >/dev/null 2>&1
root_exec "setsid sh $RESTORER $ROOT_DELAY >/dev/null 2>&1 &" >/dev/null
sleep 4
# **必須確認它是 root 身分**：非 root 的復原行程沒有重建 daemon 的權限，
# 那就等於把 root 丟掉且救不回來。
if timeout 25 "$ADB" -s "$SERIAL" shell "ps -A -o USER,ARGS 2>/dev/null | grep '[r]oot_restore_delayed' | grep -q '^root'" 2>/dev/null; then
  ok "復原行程已就緒且為 root 身分（${ROOT_DELAY}s 後恢復 root）"
else
  echo "  ✗ 復原行程沒起來或不是 root —— 中止，不要拿 root 冒險"; exit 1
fi

# ---------- 讓 root 失效 ----------
echo ""
echo "[2] 讓 root 對 App 失效（先殺 keepalive，再殺 daemon）"
KP=$(ka_pid); DP=$(daemon_pid)
[ -n "$KP" ] && ok "keepalive pid=$KP" || echo "  (keepalive 不在，略過)"
root_exec "kill -9 $KP $DP" >/dev/null 2>&1
sleep 3
if root_ok; then bad "root 仍然可用（daemon 沒被殺掉？）"; else ok "root 已失效（keeper 現在才會走到租約判斷）"; fi
# stale socket 應該還在（這正是守衛要保護的目標）
if [ "$(sock_exists)" = "yes" ]; then ok "stale socket 檔案存在（守衛的保護對象）"; else
  timeout 25 "$ADB" -s "$SERIAL" shell "touch $SOCK" >/dev/null 2>&1
  echo "  (socket 檔不在，已建立一個作為觀察對象)"
fi

# ---------- 對照組：不持有租約 ----------
# ⚠ **判準是日誌，不是 socket 檔存不存在。**
# 實測（2026-09-13）：`/data/local/tmp` 是 `drwxrwx--x shell shell`，App（uid 10053）
# 屬 "other"，**沒有目錄寫入權** → App 的 `delete()` 永遠失敗。
# 所以「socket 還在」**不能**證明守衛生效（不守衛時它也刪不掉）。
# 真正可觀測的差異是 keeper 有沒有**越過守衛**繼續往下走：
#   越過 → 記錄 stale socket 處置 ＋「root lost and no trigger available」
#   讓開 → 記錄 external operator(...) holds lease，**且不會**出現 no trigger
echo ""
echo "[3] 對照組：**不**持有租約 → keeper 應越過守衛（記錄 no trigger available）"
lease_cleanup deploy >/dev/null 2>&1   # 確保沒有租約
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

# ---------- 實驗組：持有新鮮 deploy 租約 ----------
echo ""
echo "[4] 實驗組：持有新鮮 **deploy** 租約 → keeper 應在守衛處讓開"
if lease_take deploy >/dev/null 2>&1; then ok "已取得租約（$(lease_status)）"; else
  bad "取不到租約（有別人在跑？）—— 測試無效"; fi
timeout 25 "$ADB" -s "$SERIAL" shell "logcat -c" >/dev/null 2>&1
# 先確認守衛週期真的跑過（日誌出現）
echo "  輪詢最多 75s，等守衛日誌出現..."
GUARD_SEEN=0; DEADLINE=$(( $(date +%s) + 75 ))
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
  if keeper_log | grep -q "external operator(deploy) holds lease"; then GUARD_SEEN=1; break; fi
  sleep 5
done
if [ "$GUARD_SEEN" = "1" ]; then ok "logcat：守衛日誌出現，owner 正確顯示為 **deploy**（寫死 desktop 的缺陷已修）"
else bad "logcat：等不到 external operator(deploy) 守衛日誌"; fi
# 再跨一個完整週期（35s > 30s），確認守衛**持續**讓開、沒有中途越過
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

# ---------- 等復原 ----------
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
