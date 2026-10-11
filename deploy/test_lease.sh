#!/usr/bin/env bash
# test_lease.sh — rerunnable verification of the lease mechanism (**needs no root and no LPE**).
#
# Why this test matters: the lease is the only serialisation mechanism for three operators
# sharing one exploit, but its guard logic is spread over three languages (C / Kotlin / bash)
# and relies on hand-written string comparisons. If one side changes how it compares (for
# example by enumerating known owners), deny-by-default breaks. This test pins down every
# aspect of the contract.
#
# Usage:
#   SERIAL=<serial> bash deploy/test_lease.sh
#
# Prerequisite: only an adb connection; **it does not trigger the exploit**, so it is safe to run in any state.
source "$(dirname "$0")/env.sh"
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lease.sh"

PASS=0; FAIL=0
ok()   { echo "  ✓ $1"; PASS=$((PASS+1)); }
bad()  { echo "  ✗ $1"; FAIL=$((FAIL+1)); }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1（預期 $3，實得 $2）"; fi; }

# Reset the state to "released" before every test. **No file deletion**: the App has no
# directory write permission on /data/local/tmp, so deleting it would leave the App unable to
# take a lease ever again (measured Permission denied). Write an expired marker instead.
reset_lease() { lease_reset; }

echo "===== 租約測試（TTL=${LEASE_TTL_SEC}s）====="
echo "裝置：$SERIAL"
echo ""

# ---- T1: no lease -> can be taken ----
echo "[T1] 已釋放（過期標記）時應判定為『可取得』"
reset_lease
if lease_held_by_other deploy; then bad "已釋放的租約竟被判為別人持有"; else ok "已釋放 → 可取得"; fi

# ---- T2: holding it myself does not count as someone else ----
echo "[T2] 自己持有時不應被判為『別人』"
reset_lease
lease_take deploy >/dev/null 2>&1
if lease_held_by_other deploy "$$"; then bad "自己持有卻被判為別人"; else ok "自己持有 → 不阻擋自己"; fi

# ---- T3: mutual exclusion -- another deploy **instance** (different pid) must fail to take it ----
# This is a real gap: get_root_now.sh is a command-line script that can be started twice and
# both write owner=deploy. If identity compared owner only, the two instances would treat each
# other as their own -> no exclusion at all.
echo "[T3] 另一個 deploy 實例（不同 pid）應取不到租約"
T3_HOLDER=""; T3_OK=0; T3_FOREIGN=""
# Retrying is required: the desktop app / App may hold the lease right now (which is exactly
# exclusion working), so the child failing to take it is expected and must not fail the test.
for _try in 1 2 3; do
  reset_lease
  bash -c "source '$SCRIPT_DIR/lease.sh'; lease_take deploy >/dev/null 2>&1; sleep 40" &
  T3_HOLDER=$!
  sleep 6
  if lease_is_mine deploy "$T3_HOLDER"; then T3_OK=1; break; fi
  T3_FOREIGN=$(lease_owner 2>/dev/null)
  echo "  (前提未成立：目前持有者=${T3_FOREIGN:-無} → 重試 $_try/3)"
  kill $T3_HOLDER 2>/dev/null; wait $T3_HOLDER 2>/dev/null
  sleep 3
done
if [ "$T3_OK" != 1 ]; then
  bad "無法讓子行程取得租約（連續 3 次被外部操作者擋下${T3_FOREIGN:+，owner=$T3_FOREIGN}）"
else
  if lease_take deploy >/dev/null 2>&1; then
    bad "第二個 deploy 實例竟取得租約（同 owner 不互斥 → 會互搶共用檔案）"
  else
    ok "第二個 deploy 實例被正確擋下（身分含 pid）"
  fi
  # a same-owner different-pid lease must not be renewed/released as if it were mine
  if lease_drop deploy "$$" && lease_is_mine deploy "$T3_HOLDER"; then
    ok "不持有者無法刪掉持有者的租約"
  else
    bad "非持有者竟刪掉了別人的租約"
  fi
fi
kill $T3_HOLDER 2>/dev/null; wait $T3_HOLDER 2>/dev/null
reset_lease

# ---- T4/T5: deny-by-default (both the desktop and app views must see deploy as someone else) ----
# Precondition: a **fresh deploy lease** must exist, otherwise "can others see it" is untestable.
lease_take deploy >/dev/null 2>&1

echo "[T4] 以 desktop 的視角看 owner=deploy → 必須視為別人"
if lease_held_by_other desktop; then ok "desktop 會讓開（deny-by-default 成立）"; else bad "desktop 看不到 deploy 的租約（會被搶）"; fi

echo "[T5] 以 app 的視角看 owner=deploy → 必須視為別人"
if lease_held_by_other app; then ok "app 會讓開（deny-by-default 成立）"; else bad "app 看不到 deploy 的租約（會被搶）"; fi
reset_lease

# ---- T6: an expired lease no longer blocks (so a crashed operator cannot wedge things forever) ----
echo "[T6] 過期租約（ts 超過 TTL）應視為無效，不再阻擋"
reset_lease
OLD_TS=$(( $(date +%s) - LEASE_TTL_SEC - 60 ))
_adb shell "printf 'owner=desktop pid=1 ts=$OLD_TS\n' > $LEASE_PATH; chmod 666 $LEASE_PATH" >/dev/null 2>&1
if lease_held_by_other deploy; then bad "過期租約仍被判為有效持有"; else ok "過期租約 → 不阻擋"; fi
reset_lease

# ---- T7: renewal keeps the lease fresh ----
echo "[T7] 背景續約應讓租約持續新鮮（間隔 5s，觀察 12s）"
reset_lease
lease_take deploy >/dev/null 2>&1
TS0=$(lease_age)
lease_autorenew_start deploy "$$" 5
sleep 12
TS1=$(lease_age)
lease_autorenew_stop
if [ "$TS1" -ge 0 ] && [ "$TS1" -le 8 ] && lease_is_mine deploy "$$"; then
  ok "續約生效（age 由 ${TS0}s → ${TS1}s，仍為自己持有）"
else
  bad "續約未生效（age=$TS1，mine=$(lease_is_mine deploy "$$" && echo yes || echo no)）"
fi
reset_lease

# ---- T8: a TERMed process must release through the trap, leaving no residual lease ----
echo "[T8] 程序被 TERM 時，EXIT trap 應釋放租約（不留 15 分鐘殘留）"
reset_lease
# The child script lives in mktemp (**not in deploy/**): otherwise an interruption would leave
# a temp file in the project directory that could be mistaken for a real script. The library
# location is passed in through LEASE_LIB_DIR instead of being derived from $0.
CHILD_SCRIPT=$(mktemp "${TMPDIR:-/tmp}/gl_lease_trap.XXXXXX")
cat > "$CHILD_SCRIPT" <<'EOF'
source "$LEASE_LIB_DIR/env.sh"; source "$LEASE_LIB_DIR/lease.sh"
trap 'lease_cleanup deploy' EXIT
trap 'exit 143' TERM
lease_take deploy >/dev/null 2>&1
lease_autorenew_start deploy "$$" 5
echo "child-holding"; sleep 60
EOF
LEASE_LIB_DIR="$SCRIPT_DIR" bash "$CHILD_SCRIPT" >"${TMPDIR:-/tmp}/gl_lease_child.out" 2>&1 &
CHILD=$!
sleep 6
# the child's $$ is $! (it is that bash process itself), so the pid can be compared exactly
if ! lease_is_mine deploy "$CHILD"; then bad "子程序並未取得租約（測試前提失敗）"; else
  kill -TERM $CHILD 2>/dev/null; wait $CHILD 2>/dev/null
  sleep 2
  OWNER=$(lease_owner 2>/dev/null)
  if ! lease_fresh; then ok "TERM 後租約已失效（owner=${OWNER:-none}，不再阻擋他人）"
  else bad "TERM 後仍有新鮮租約（owner=$OWNER）—— 會擋住其他操作者"; fi
fi
rm -f "$CHILD_SCRIPT"

reset_lease

# ---- T9: the path must match in all three places (contract drift protection) ----
# The lease is a three-way agreement, but the path is written in three different languages and
# files. Historically it failed completely because "only the narrow string constant was changed
# while a dozen other places still hardcoded the old path". This check stops drift at test time
# instead of discovering on the device that the two sides cannot see each other.
echo ""
echo "[T9] 租約路徑在三處必須一致（契約漂移防護）"
KT="/home/kali/rokid_root/android/app-glasses/src/main/kotlin/com/rokid/rc/glasses/ghost/RootDaemonKeeper.kt"
CT="/home/kali/ZeroLightRokidDesktop/src/zero_light_desktop.c"
P_DEPLOY=$(grep -m1 '^LEASE_PATH=' lease.sh | sed 's/^LEASE_PATH="\([^"]*\)".*/\1/')
P_APP=$(grep -m1 'LEASE_PATH = "' "$KT" 2>/dev/null | sed 's/.*"\([^"]*\)".*/\1/')
P_DESK=$(grep -m1 '#define DEV_LEASE  *"' "$CT" 2>/dev/null | sed 's/.*"\([^"]*\)".*/\1/')
echo "    deploy : ${P_DEPLOY:-<找不到>}"
echo "    app    : ${P_APP:-<找不到，略過>}"
echo "    desktop: ${P_DESK:-<找不到，略過>}"
if [ -z "$P_DEPLOY" ]; then
  bad "deploy 側路徑解析失敗（test_lease.sh 的解析規則需配合更新）"
else
  ok "deploy 側路徑可解析：$P_DEPLOY"
fi
[ -z "$P_APP" ]  || { [ "$P_APP"  = "$P_DEPLOY" ] && ok "app 側一致" \
  || bad "app 側路徑不一致（$P_APP ≠ $P_DEPLOY）"; }
[ -z "$P_DESK" ] || { [ "$P_DESK" = "$P_DEPLOY" ] && ok "desktop 側一致" \
  || bad "desktop 側路徑不一致（$P_DESK ≠ $P_DEPLOY）"; }

# ---- T10: the App can create/delete files in the lease directory (root-cause fix verification) ----
# This is the **reason** the lease moved out of /data/local/tmp. Before the move the App had no
# write permission there and measured CREATE_FAIL / RM_FAIL; after the move to the 777
# ghostlock_bridge/ it must become OK. run-as (the App is debuggable) really acts with the App's
# uid instead of only inspecting permission bits.
echo ""
echo "[T10] App 必須能在租約目錄建立與刪除檔案（權限陷阱是否真的移除）"
if [ "${SKIP_APP_CHECK:-0}" = "1" ]; then
  echo "  (SKIP_APP_CHECK=1，略過)"
else
  PROBE="$LEASE_DIR/_appperm_probe"
  R=$(timeout 40 "$ADB" -s "$SERIAL" shell \
      "run-as com.rokid.rc.glasses sh -c 'echo x > $PROBE 2>/dev/null && echo C_OK || echo C_FAIL'" 2>/dev/null | tr -d '\r')
  case "$R" in
    *C_OK*)  ok "App 可在 $LEASE_DIR 建立檔案" ;;
    *)       bad "App 無法在 $LEASE_DIR 建立檔案（$R）—— 權限陷阱仍在" ;;
  esac
  R2=$(timeout 40 "$ADB" -s "$SERIAL" shell \
       "run-as com.rokid.rc.glasses sh -c 'rm -f $PROBE 2>/dev/null && echo D_OK || echo D_FAIL'" 2>/dev/null | tr -d '\r')
  case "$R2" in
    *D_OK*)  ok "App 可在 $LEASE_DIR 刪除檔案" ;;
    *)       bad "App 無法在 $LEASE_DIR 刪除檔案（$R2）" ;;
  esac
  # write the real lease file directly as the App uid to verify takeLease()'s core precondition
  R3=$(timeout 40 "$ADB" -s "$SERIAL" shell \
       "run-as com.rokid.rc.glasses sh -c 'echo \"owner=app pid=1 ts=1\" > $LEASE_PATH 2>/dev/null && echo W_OK || echo W_FAIL'" 2>/dev/null | tr -d '\r')
  case "$R3" in
    *W_OK*)  ok "App 可直接寫入租約檔（takeLease 的前提成立）" ;;
    *)       bad "App 無法寫入租約檔（$R3）—— App 仍會在沒持租約的情況下跑 exploit" ;;
  esac
  reset_lease
fi
echo ""
echo "===== 結果：$PASS 通過 / $FAIL 失敗 ====="
[ "$FAIL" -eq 0 ] || exit 1
