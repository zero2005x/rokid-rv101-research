#!/usr/bin/env bash
# lease.sh — deploy 路徑的共用租約 primitive（補上 deploy 側的租約缺口）。
#
# ## 為什麼需要
#
# exploit 本體的三個路徑**寫死在 `ghostlock_rv101` binary 內**，無法像 helper 與信箱
# 那樣一分為二：
#
#   /data/local/tmp/.ghostlock_root.sh   （payload；會被對方換掉）
#   /data/local/tmp/gl_run.log           （會被對方截斷）
#   /data/local/tmp/ghostlock_rv101      （會被對方 pkill）
#
# 所以「誰正在跑 exploit」必須全域序列化。目前有**三個**可能的操作者：
#
#   | 操作者                   | owner=    | 取租約的位置                              |
#   |--------------------------|-----------|-------------------------------------------|
#   | ZeroLightRokidDesktop    | `desktop` | `zero_light_desktop.c`（30 秒輪詢續約）    |
#   | 眼鏡 App `RootDaemonKeeper` | `app`  | `RootDaemonKeeper.kt`（`finally` 釋放）    |
#   | deploy 腳本（本檔）       | `deploy`  | 背景續約 + `EXIT` trap 釋放               |
#
# ## 契約（三處必須同步；改動時一起改）
#
#   檔案：`/data/local/tmp/ghostlock_op.lease`
#   內容：`owner=<desktop|app|deploy> pid=<pid> ts=<unix 秒>`
#   TTL ：900 秒；持有者需在工作期間持續續約
#
# ## 為什麼新增 `owner=deploy` 不必改動另外兩邊
#
# 兩邊的判斷都是 **deny-by-default** —— 只有 owner 等於「自己」才算自己人：
#
#   desktop: `mine = strstr(txt, "owner=desktop")`
#   app    : `mine = txt.contains("owner=app")`
#
# 因此任何未知的 owner（包含 `deploy`）都會被視為「別人持有」而正確讓開。
# 這點已在實機驗證（見下方 `lease_selftest`）。
#
# ## 用法
#
#   source "$(dirname "$0")/lease.sh"
#   lease_take deploy || exit 2      # 取不到就讓開
#   lease_autorenew_start deploy     # 背景續約（長流程必備）
#   trap 'lease_cleanup deploy' EXIT INT TERM
#   ...
#   lease_is_mine deploy || abort    # 迴圈中定期確認沒被搶走
#
# 需要 `env.sh` 已提供 `ADB`／`SERIAL`（本檔會自行 source，重複 source 無害）。
set -u

_LEASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
[ -n "${ADB:-}" ] && [ -n "${SERIAL:-}" ] || source "$_LEASE_DIR/env.sh"

# 租約檔路徑。**三處必須完全一致**（deploy/lease.sh、RootDaemonKeeper.kt、
# zero_light_desktop.c）；`deploy/test_lease.sh` 有一項跨檔一致性檢查會擋住漂移。
#
# ## 為什麼放在 `ghostlock_bridge/` 子目錄，而不是 `/data/local/tmp` 本身
#
# `/data/local/tmp` 是 `drwxrwx--x shell shell` → 眼鏡 App（uid 10053）屬 "other"，
# 只有 `--x`（可穿越）**沒有目錄寫入權**，實測**既不能建立也不能刪除**其中的檔案。
# 這讓 App 側的租約長期失效：`takeLease()` 在檔案不存在時必然失敗
# （→ App 沒持租約就跑 exploit）、`dropLease()` 永遠無效（→ 殘留租約卡住別人）。
#
# `ghostlock_bridge/` 是 **777**，實測 App 在其中 `CREATE_OK` / `RM_OK`。
# 把租約移到這裡就**從根本移除權限陷阱**，不必再依賴「只重設、不刪除」的約定。
LEASE_DIR="/data/local/tmp/ghostlock_bridge"
# 路徑**刻意寫成完整字面值**（不用 "$LEASE_DIR/..." 串接）：
# 這是一份三方契約，`deploy/test_lease.sh` 的 T9 會直接比對三個檔案裡的**字面值**
# 來擋住漂移。用變數串接會讓比對不到而失去保護作用。
LEASE_PATH="/data/local/tmp/ghostlock_bridge/ghostlock_op.lease"
# 舊路徑（在 /data/local/tmp 下）：僅用於遷移清理
LEASE_PATH_LEGACY="/data/local/tmp/ghostlock_op.lease"
LEASE_TTL_SEC="${LEASE_TTL_SEC:-900}"      # 與 desktop / app 一致，勿隨意改
LEASE_RENEW_S="${LEASE_RENEW_S:-30}"       # 續約間隔（desktop 為 30 秒）

# 續約子程序 pid 與「租約被搶走」旗標（主程序據此中止）
LEASE_RENEW_PID=""
LEASE_LOST_FLAG="${TMPDIR:-/tmp}/ghostlock_lease_lost.${SERIAL:-none}"

_adb() { "$ADB" -s "$SERIAL" "$@"; }

# 讀租約。回傳 1 = **adb 失敗**（無法判斷），0 = 讀到（可能為空 = 檔案不存在）。
# 用哨兵字串區分「adb 掛掉」與「檔案不存在」——這兩者對續約的意義完全不同：
# 前者是暫時性（例如 reboot 期間），後者代表租約已被釋放／搶走。
lease_read() {
  local raw
  raw=$(_adb shell "echo __LB__; cat $LEASE_PATH 2>/dev/null; echo __LE__" 2>/dev/null | tr -d '\r')
  case "$raw" in
    *__LB__*__LE__*) printf '%s\n' "$raw" | sed -n '/__LB__/,/__LE__/p' | sed '1d;$d' ;;
    *) return 1 ;;
  esac
}

# 目前持有者（空字串 = 無人或 adb 失敗；請自行用 lease_read 判別）
lease_owner() { lease_read 2>/dev/null | sed -n 's/.*owner=\([A-Za-z][A-Za-z]*\).*/\1/p' | head -1; }

# 租約年齡（秒）。-1 = 無法判斷／欄位不合法（與 desktop 的 lease_age_sec 同語意）
lease_age() {
  local ts
  ts=$(lease_read 2>/dev/null | sed -n 's/.*ts=\([0-9][0-9]*\).*/\1/p' | head -1)
  [ -n "${ts:-}" ] || { echo -1; return; }
  [ "$ts" -gt 1000000000 ] 2>/dev/null || { echo -1; return; }
  echo $(( $(date +%s) - ts ))
}

# 0 = 租約新鮮（有人有效持有）
lease_fresh() {
  local age; age=$(lease_age)
  [ "$age" -ge 0 ] 2>/dev/null && [ "$age" -le "$LEASE_TTL_SEC" ]
}

# 0 = 被「別人」有效持有（自己持有／過期／無人 → 1）
lease_held_by_other() {
  local me="${1:?lease_held_by_other <me>}" pid="${2:-}" owner
  lease_fresh || return 1
  owner=$(lease_owner)
  [ -n "$owner" ] || return 1
  lease_is_mine "$me" "$pid" && return 1
  return 0
}

# 租約的 pid 欄位
lease_pid() { lease_read 2>/dev/null | sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p' | head -1; }

# 0 = 租約是我的。
#
# **身分 = (owner, pid)**，不是只有 owner。原因：deploy 是唯一「同一種 owner
# 可能同時有多個實例」的操作者——`get_root_now.sh` 是命令列腳本，可以被同時跑兩次，
# 兩者都寫 `owner=deploy`，只比 owner 的話會互相認為是自己人而**完全不互斥**
# （本專案的 test_lease.sh T3 就是靠這點抓到這個缺口）。
# desktop（單一 GUI 行程，`g_busy` 守衛）與 app（單一 keeper，`rearming` 守衛）
# 本身是 singleton，不依賴 pid 就能互斥。
#
# pid 省略時只比 owner —— 供「模擬 desktop／app 視角」的跨側檢查使用。
lease_is_mine() {
  local me="${1:?lease_is_mine <me>}" pid="${2:-}" lpid
  [ "$(lease_owner 2>/dev/null)" = "$me" ] || return 1
  [ -z "$pid" ] && return 0
  lpid=$(lease_pid 2>/dev/null)
  [ "$lpid" = "$pid" ]
}

# 寫入租約。**先寫暫存檔再 mv**：`mv` 在同目錄為原子操作，
# 避免讀者看到半截內容（截斷的 `ts=` 會讓對方誤判為「過期」而搶進）。
# chmod 666 是必要的：App 是不同 uid，預設 umask 可能讓它讀不到。
_lease_write() {
  local me="$1" pid="$2"
  _adb shell "mkdir -p $LEASE_DIR 2>/dev/null; chmod 777 $LEASE_DIR 2>/dev/null; \
    printf 'owner=$me pid=$pid ts=%s\n' \"\$(date +%s)\" > $LEASE_PATH.tmp \
    && mv $LEASE_PATH.tmp $LEASE_PATH && chmod 666 $LEASE_PATH" >/dev/null 2>&1
}

# 取租約。0 = 取得；1 = 被別人（含**另一個同 owner 的實例**）有效持有。
lease_take() {
  local me="${1:?lease_take <me>}" pid="${2:-$$}"
  lease_held_by_other "$me" "$pid" && return 1
  _lease_write "$me" "$pid" || return 1
  # 預檢後仍可能被搶（TOCTOU）→ 回讀確認真的是我（owner 與 pid 都要相同）
  lease_is_mine "$me" "$pid"
}

# 等待租約空出來（最多 timeout 秒）。0 = 已取得；1 = 逾時仍被持有。
lease_wait_free() {
  local me="${1:?}" timeout="${2:-300}" waited=0 owner
  while [ "$waited" -lt "$timeout" ]; do
    lease_take "$me" && return 0
    owner=$(lease_owner 2>/dev/null)
    echo "[lease] 等待中：${waited}s/${timeout}s（目前持有者=${owner:-?}，age=$(lease_age)s）" >&2
    sleep 10; waited=$((waited + 10))
  done
  return 1
}

# 續約。**只在自己仍持有時續約，不搶別人的。**
# 回傳 0 = 續約成功；1 = 租約已非我（被搶走／被刪）；2 = 暫時性失敗（adb 不可用，例如 reboot 中）
#
# 注意：`case` 比對必須「owner 且 pid 都相同」才續約 —— 否則另一個 owner=deploy
# 的實例接手後，本實例會把它續約成自己（互相搶）。
lease_renew() {
  local me="${1:?lease_renew <me>}" pid="${2:-$$}" txt
  txt=$(lease_read) || return 2                 # adb 失敗 → 暫時性，不要誤判為被搶
  case "$txt" in
    "") return 1 ;;                             # 檔案不存在 → 視為失去
  esac
  case "$txt" in
    *"owner=$me"*"pid=$pid"*) _lease_write "$me" "$pid" && return 0 || return 2 ;;
  esac
  # owner 相同但 pid 不同 → 是另一個同類實例，不是我的
  return 1
}

# 重設為「已釋放」狀態：**寫入過期標記，不刪除檔案**。
#
# 為什麼仍然「不刪除」（即使新路徑已可寫）：
#   1. **一致性**：三方用同一套約定，不必各自判斷自己有沒有刪除權。
#   2. **避免空窗**：刪除與重建之間，讀者會看到「檔案不存在」＝無租約＝可搶，
#      那正是 TOCTOU 的溫床；寫標記則是單一原子 `mv`，沒有空窗。
#   3. 舊路徑（`/data/local/tmp`）本來就刪不掉，保留此約定讓遷移前後行為一致。
#   釋放＝寫 `owner=none pid=0 ts=0`（三方都視為無效）。
lease_reset() {
  _adb shell "mkdir -p $LEASE_DIR 2>/dev/null; chmod 777 $LEASE_DIR 2>/dev/null; \
    printf 'owner=none pid=0 ts=0\n' > $LEASE_PATH 2>/dev/null; \
    chmod 666 $LEASE_PATH 2>/dev/null; true" >/dev/null 2>&1
}

# 確保租約目錄與檔案存在、權限正確（目錄 777、檔案 666）。
# 目錄必須是 777，App 才能自行建立租約檔（否則又回到權限陷阱）。
# 已存在則只修權限，不動內容。
lease_ensure_file() {
  _adb shell "mkdir -p $LEASE_DIR 2>/dev/null; chmod 777 $LEASE_DIR 2>/dev/null; \
    if [ ! -e $LEASE_PATH ]; then \
      printf 'owner=none pid=0 ts=0\n' > $LEASE_PATH 2>/dev/null; \
    fi; chmod 666 $LEASE_PATH 2>/dev/null; true" >/dev/null 2>&1
}

# 遷移：清掉舊路徑（`/data/local/tmp/ghostlock_op.lease`）留下的租約檔，
# 避免「舊檔還新鮮」造成兩份租約並存的誤判。
lease_migrate_legacy() {
  _adb shell "rm -f $LEASE_PATH_LEGACY 2>/dev/null; true" >/dev/null 2>&1
}

# 釋放（只在仍持有時重設，避免清掉別人的租約）
lease_drop() {
  local me="${1:?lease_drop <me>}" pid="${2:-$$}"
  lease_is_mine "$me" "$pid" || return 0
  lease_reset
}

# 背景續約。失去租約時寫入旗標檔，主程序用 lease_lost 檢查。
lease_autorenew_start() {
  local me="${1:?lease_autorenew_start <me>}" pid="${2:-$$}" interval="${3:-$LEASE_RENEW_S}"
  rm -f "$LEASE_LOST_FLAG"
  (
    while true; do
      sleep "$interval"
      lease_renew "$me" "$pid"; rc=$?
      if [ "$rc" -eq 1 ]; then
        printf 'lost at %s (owner=%s)\n' "$(date '+%F %T')" "$(lease_owner 2>/dev/null)" > "$LEASE_LOST_FLAG"
        exit 0
      fi
      # rc=2（adb 暫時失敗）→ 繼續重試，不寫旗標
    done
  ) &
  LEASE_RENEW_PID=$!
}

lease_autorenew_stop() {
  [ -n "${LEASE_RENEW_PID:-}" ] && kill "$LEASE_RENEW_PID" 2>/dev/null
  LEASE_RENEW_PID=""
}

# 主程序在迴圈中定期呼叫：0 = 仍安全持有
lease_lost() { [ -f "$LEASE_LOST_FLAG" ]; }

# 一行狀態（給 log 用）
lease_status() {
  local owner age; owner=$(lease_owner 2>/dev/null); age=$(lease_age)
  echo "owner=${owner:-（無）} age=${age}s ttl=${LEASE_TTL_SEC}s"
}

# 收尾：停續約 → 釋放 → 清旗標。掛在 EXIT trap 上可重複呼叫。
lease_cleanup() {
  local me="${1:?lease_cleanup <me>}"
  lease_autorenew_stop
  lease_drop "$me"
  rm -f "$LEASE_LOST_FLAG"
}

# 自我測試：驗證 deny-by-default 契約（兩邊都把未知 owner 視為別人）
#   lease_selftest deploy
lease_selftest() {
  local me="${1:-deploy}" rc=0
  echo "[selftest] 目前租約：$(lease_status)"
  if lease_held_by_other "$me"; then
    echo "[selftest] 別的有效持有中 → 正確判定為『需讓開』"
  else
    echo "[selftest] 無人持有（或已過期）→ 正確判定為『可取得』"
  fi
  lease_take "$me"       || { echo "[selftest] ✗ 取租約失敗"; rc=1; }
  lease_is_mine "$me"    || { echo "[selftest] ✗ 取得後回讀不是自己"; rc=1; }
  lease_held_by_other "$me" && { echo "[selftest] ✗ 自己持有卻被判為別人"; rc=1; }
  # 換一個 owner 名稱模擬「對方程式的視角」：必須看到 deploy 是別人
  if lease_held_by_other desktop; then
    echo "[selftest] ✓ 以 desktop 視角看 deploy → 判定為『別人持有』（deny-by-default 成立）"
  else
    echo "[selftest] ✗ 以 desktop 視角竟看不到 deploy 的租約"; rc=1
  fi
  lease_drop "$me"
  echo "[selftest] 結束（rc=$rc）"
  return "$rc"
}

# 直接執行本檔時跑自我測試：bash deploy/lease.sh [owner]
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  case "${1:-selftest}" in
    selftest) lease_selftest "${2:-deploy}" ;;
    status)   echo "租約狀態：$(lease_status)" ;;
    *) echo "用法：bash deploy/lease.sh [selftest|status] [owner]"; exit 64 ;;
  esac
fi
