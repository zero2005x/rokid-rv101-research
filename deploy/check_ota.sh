#!/usr/bin/env bash
# =============================================================================
# check_ota.sh — one-stop Rokid glasses OTA check + GhostLock patch comparison
#
# Flow (default):
#   1. Read the glasses' current version/SPL + signing parameters
#      (ro.boot.key / secret / devicetypeid / serialno).
#   2. Call ota.rokid.com/v1/extended/ota/check with the cracked signature to get the latest
#      version.
#   3. Download the latest OTA and parse post-security-patch-level from
#      META-INF/com/android/metadata.
#   4. Compare the SPL with the GhostLock (CVE-2026-43499) patch release time and conclude
#      whether it is patched.
#
# Signature algorithm (reversed from com.rokid.glass.ota.component.CheckService.generateSignature):
#   sign = MD5("key=<key>&device_type_id=<dtid>&device_id=<did>&service=<service>&version=<v>&time=<ts>&secret=<secret>")
#   Authorization = "version=<v>;time=<ts>;sign=<sign>;key=<key>;device_type_id=<dtid>;device_id=<did>;service=<service>"
#
# Depends on: Windows adb, curl, unzip, md5sum, date +%s.
#
# Usage:
#   ./check_ota.sh                 # one-stop: query latest -> download -> compare SPL
#   ./check_ota.sh --api           # query the latest version only (no download)
#   ./check_ota.sh --url <URL>     # analyse the given OTA zip URL directly
#   ./check_ota.sh --prefs         # read the URL from ota_sp.xml and analyse it (no internet)
#   ./check_ota.sh --trigger       # trigger the device-side OTA check first, then read and analyse
#   GHOSTLOCK_SPL=2026-08-05 ./check_ota.sh   # override the SPL of the "patch threshold"
# =============================================================================
source "$(dirname "$0")/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
set -euo pipefail

# ============================ tunables ========================================
SERIAL="${SERIAL:-<DEVICE_SERIAL>}"
OTA_PREFS="/data/data/com.rokid.glass.ota/shared_prefs/ota_sp.xml"
SOCK="/data/local/tmp/root_daemon.sock"
API_BASE="${API_BASE:-https://ota.rokid.com}"
API_PATH="/v1/extended/ota/check"
# signing parameters: empty means read them from the device getprop; also overridable by environment
KEY="${KEY:-}"
SECRET="${SECRET:-}"
DTID="${DTID:-}"
DID="${DID:-}"
SERVICE="${SERVICE:-ota}"
SVER="${SVER:-1.0}"
# The GhostLock (CVE-2026-43499) rtmutex UAF patch shipped around 2026-07-09; Android SPLs
# conventionally use the 5th of the month, hence the default threshold 2026-07-05.
GHOSTLOCK_SPL="${GHOSTLOCK_SPL:-2026-07-05}"
CVE="CVE-2026-43499 (GhostLock, rtmutex UAF)"
WORKDIR="$(mktemp -d /tmp/check_ota.XXXXXX)"
# ==============================================================================

adb_s() { "$ADB" -s "$SERIAL" "$@"; }
log()  { echo "[*] $*"; }
ok()   { echo "[+] $*"; }
warn() { echo "[!] $*"; }
err()  { echo "[x] $*"; }

cleanup() { rm -rf "$WORKDIR"; }
trap cleanup EXIT

# ---- run a single command as root (prefer the root daemon socket, then adb root) ----
root_sh() {
    local cmd="$1"
    if adb_s shell "[ -S $SOCK ]" 2>/dev/null; then
        adb_s shell "printf '%s' '$cmd' | nc -U $SOCK" 2>/dev/null || true
    elif adb_s root 2>/dev/null; then
        sleep 1
        adb_s shell "$cmd" 2>/dev/null || true
    else
        return 1
    fi
}

# ---- read the device's current version / SPL ----
check_device() {
    log "讀取裝置目前版本..."
    if ! adb_s get-state >/dev/null 2>&1; then
        err "裝置 $SERIAL 不在線"
        exit 1
    fi
    CUR_INC="$(adb_s shell getprop ro.build.version.incremental | tr -d '\r')"
    CUR_SPL="$(adb_s shell getprop ro.build.version.security_patch | tr -d '\r')"
    CUR_KERNEL="$(adb_s shell uname -r 2>/dev/null | tr -d '\r')"
    ok "目前版本 incremental = $CUR_INC"
    ok "目前 SPL             = $CUR_SPL"
    ok "目前 kernel          = $CUR_KERNEL"
    echo
}

# ---- read the signing parameters (overridable by environment variables) ----
read_signing_props() {
    KEY="${KEY:-$(adb_s shell getprop ro.boot.key 2>/dev/null | tr -d '\r')}"
    SECRET="${SECRET:-$(adb_s shell getprop ro.boot.secret 2>/dev/null | tr -d '\r')}"
    DTID="${DTID:-$(adb_s shell getprop ro.boot.devicetypeid 2>/dev/null | tr -d '\r')}"
    DID="${DID:-$(adb_s shell getprop ro.serialno 2>/dev/null | tr -d '\r')}"
    if [ -z "$KEY" ] || [ -z "$SECRET" ] || [ -z "$DTID" ] || [ -z "$DID" ]; then
        warn "部分簽名參數為空（裝置離線？）。可用 KEY/SECRET/DTID/DID 環境變數覆寫。"
    fi
}

# ---- signature computation (reversed from CheckService.generateSignature) ----
sign_ota() {
    local ts="$1"
    printf '%s' "key=$KEY&device_type_id=$DTID&device_id=$DID&service=$SERVICE&version=$SVER&time=$ts&secret=$SECRET" \
        | md5sum | awk '{print $1}'
}

# ---- query the latest version (with the cracked signature) ----
query_latest() {
    log "查詢最新版本（已破解簽名）: $API_BASE$API_PATH"
    local ts sign auth body resp
    ts="$(date +%s)"
    sign="$(sign_ota "$ts")"
    auth="version=$SVER;time=$ts;sign=$sign;key=$KEY;device_type_id=$DTID;device_id=$DID;service=$SERVICE"
    body="{\"version\":\"${CUR_INC:-unknown}\",\"osType\":\"\",\"cpuType\":\"\"}"
    resp="$(curl -fsS -m 20 -X POST "$API_BASE$API_PATH" \
        -H 'Content-Type: application/json;charset=utf-8' \
        -H "Authorization: $auth" \
        -d "$body" 2>&1 || true)"
    if [ -z "$resp" ]; then
        err "API 無回應（主機需可連外網）"
        return 1
    fi
    LATEST_CODE="$(printf '%s' "$resp" | grep -o '"code":"[^"]*"' | head -1 | sed 's/.*":"//;s/"//')"
    LATEST_VER="$(printf '%s' "$resp" | grep -o '"version":"[^"]*"' | head -1 | sed 's/.*":"//;s/"//')"
    LATEST_URL="$(printf '%s' "$resp" | grep -o '"imageUrl":"[^"]*"' | sed 's/.*":"//;s/"//')"
    LATEST_SUM="$(printf '%s' "$resp" | grep -o '"checksum":"[^"]*"' | sed 's/.*":"//;s/"//')"
    ok "API code   = ${LATEST_CODE:-?}"
    ok "最新版本   = ${LATEST_VER:-?}"
    ok "imageUrl   = ${LATEST_URL:-?}"
    ok "checksum   = ${LATEST_SUM:-?}"
    if [ -n "${CUR_INC:-}" ] && [ -n "$LATEST_VER" ]; then
        if [ "$LATEST_VER" = "$CUR_INC" ]; then
            ok "版本判斷：已是最新（$CUR_INC）"
        else
            warn "版本判斷：有更新  $CUR_INC → $LATEST_VER"
        fi
    fi
    echo
}

# ---- read the OTA app shared_prefs ----
read_ota_prefs() {
    log "讀取 OTA app shared_prefs ($OTA_PREFS)..."
    local xml
    xml="$(root_sh "cat $OTA_PREFS" || true)"
    if [ -z "$xml" ]; then
        warn "無法讀取 ota_sp.xml（需 root daemon 或 adb root）。"
        return 1
    fi
    DL_URL="$(printf '%s' "$xml" | grep -o 'name="download_url">[^<]*' | sed 's/.*>//')"
    DL_VER="$(printf '%s' "$xml" | grep -o 'name="download_version">[^<]*' | sed 's/.*>//')"
    DL_SUM="$(printf '%s' "$xml" | grep -o 'name="check_sum">[^<]*' | sed 's/.*>//')"
    if [ -n "$DL_VER" ]; then ok "OTA app 最後已知下載版本 = $DL_VER"; fi
    if [ -n "$DL_URL" ]; then ok "OTA app 最後已知下載 URL  = $DL_URL"; fi
    echo
}

# ---- trigger the device-side OTA check (best effort) ----
trigger_ota_check() {
    warn "真正的 OTA 版本查詢需裝置認證（由 Rokid Glasses App / assist server 觸發）。"
    warn "建議：先在手機 Rokid Glasses App 檢查更新，再跑本腳本讀取 ota_sp.xml。"
    log "仍嘗試發送 OTA 檢查 broadcast（部分韌體版本可能回應）..."
    adb_s shell "am broadcast -a com.rokid.glass.ota.NORMAL_CMD_ACTION --es normal_cmd NORMAL_GET_UPDATE_INFO" 2>&1 | sed 's/^/    /' || true
    log "等待 15s ..."
    sleep 15
}

# ---- download and analyse the OTA ----
analyze_ota() {
    local url="$1"
    local expect_sum="${2:-}"
    log "下載 OTA: $url"
    local zip="$WORKDIR/ota.zip"
    if ! curl -fsSL -m 900 -o "$zip" "$url"; then
        err "下載失敗"
        return 1
    fi
    local size
    size="$(stat -c%s "$zip" 2>/dev/null || stat -f%z "$zip")"
    ok "下載完成：$(du -h "$zip" | cut -f1) ($size bytes)"

    if [ -n "$expect_sum" ]; then
        local sum
        sum="$(md5sum "$zip" | awk '{print $1}')"
        if [ "$sum" = "$expect_sum" ]; then
            ok "MD5 校驗通過 ($sum)"
        else
            warn "MD5 不符！API 宣告 $expect_sum，實際 $sum"
        fi
    fi

    log "解析 META-INF/com/android/metadata ..."
    local meta
    meta="$(unzip -p "$zip" META-INF/com/android/metadata 2>/dev/null || true)"
    if [ -z "$meta" ]; then
        err "找不到 metadata（不是標準 A/B OTA？）"
        return 1
    fi
    local post_inc pre_inc post_spl ota_type
    # NOTE: a full OTA may have no pre-build-incremental; grep returns 1 when it does not
    # match, so || true keeps set -e from killing the script
    post_inc="$(printf '%s' "$meta" | grep -o 'post-build-incremental=[^ ]*' | cut -d= -f2- || true)"
    pre_inc="$(printf '%s' "$meta" | grep -o 'pre-build-incremental=[^ ]*' | cut -d= -f2- || true)"
    post_spl="$(printf '%s' "$meta" | grep -o 'post-security-patch-level=[^ ]*' | cut -d= -f2- || true)"
    ota_type="$(printf '%s' "$meta" | grep -o 'ota-type=[^ ]*' | cut -d= -f2- || true)"
    echo
    ok "OTA 型態            = ${ota_type:-?}"
    ok "來源版本 (pre)      = ${pre_inc:-?}"
    ok "目標版本 (post)     = ${post_inc:-?}"
    ok "目標 SPL            = ${post_spl:-?}"
    echo

    # ---- SPL vs the GhostLock threshold ----
    if [ -z "$post_spl" ]; then
        warn "metadata 沒有 post-security-patch-level，無法判斷（可能非標準 OTA）"
    elif [ "$(printf '%s\n' "$post_spl" "$GHOSTLOCK_SPL" | sort | head -1)" = "$GHOSTLOCK_SPL" ]; then
        ok "SPL 判斷：目標 SPL ($post_spl) >= 門檻 ($GHOSTLOCK_SPL)"
        warn "結論：SPL 已進入補丁發布區間，**可能**包含 $CVE 修補；"
        warn "      需進一步比對 kernel (boot.img 的 rtmutex 程式碼) 才能確認。"
    else
        err "SPL 判斷：目標 SPL ($post_spl) < 門檻 ($GHOSTLOCK_SPL)"
        err "結論：❌ 此更新 **未修補** $CVE（SPL 早於補丁發布 ~2 年）。"
    fi
    warn "注意：SPL 僅供參考（Rokid 曾 backport CVE 修補卻未動 SPL），請用 ghostlock_patch_check.py 反組譯 remove_waiter 確認。"
    echo
}

# ================================ main =======================================
usage() {
    cat <<'EOF'
用法：
  ./check_ota.sh                 # 一條龍：查最新版本(已破解簽名) → 下載 → 比對 SPL
  ./check_ota.sh --api           # 只查最新版本（不下載）
  ./check_ota.sh --url <URL>     # 直接分析指定 OTA zip URL
  ./check_ota.sh --prefs         # 讀 ota_sp.xml 的 URL 並分析（不需外網）
  ./check_ota.sh --trigger       # 先觸發裝置端 OTA 檢查，再讀取並分析
環境變數：ADB、SERIAL、API_BASE、GHOSTLOCK_SPL、KEY、SECRET、DTID、DID 可覆寫。
EOF
    exit 0
}

MODE="check"
URL=""
while [ $# -gt 0 ]; do
    case "$1" in
        --url) MODE="url"; URL="${2:-}"; shift 2 ;;
        --api) MODE="api"; shift ;;
        --prefs) MODE="prefs"; shift ;;
        --trigger) MODE="trigger"; shift ;;
        -h|--help) usage ;;
        *) echo "未知參數: $1"; usage ;;
    esac
done

echo "=============================================================="
echo " Rokid 眼鏡 OTA 檢查 / SPL 比對  (GhostLock 門檻: $GHOSTLOCK_SPL)"
echo "=============================================================="
echo

case "$MODE" in
    api)
        check_device
        read_signing_props
        query_latest
        ;;
    url)
        analyze_ota "$URL" ""
        ;;
    prefs)
        check_device
        if read_ota_prefs && [ -n "${DL_URL:-}" ]; then
            analyze_ota "$DL_URL" "$DL_SUM"
        fi
        ;;
    trigger)
        check_device
        trigger_ota_check
        if read_ota_prefs && [ -n "${DL_URL:-}" ]; then
            analyze_ota "$DL_URL" "$DL_SUM"
        fi
        ;;
    check|*)
        check_device
        read_signing_props
        if query_latest; then
            if [ -n "$LATEST_URL" ]; then
                analyze_ota "$LATEST_URL" "$LATEST_SUM"
            else
                warn "API 未回傳 imageUrl（可能已是最新，無需下載）"
            fi
        fi
        ;;
esac

echo "=============================================================="
echo " 完成。GhostLock 修補門檻 SPL = $GHOSTLOCK_SPL（$CVE）"
echo "=============================================================="
