#!/usr/bin/env bash
# boot_run_loop.sh — reboot -> gate on nr_threads -> run W2 (timeout-capped) -> grep success.
# Usage: boot_run_loop.sh <max_boots> <tag> <version> <success_grep> <env_vars> <binary_args...>
set -u
source "$(dirname "$0")/../deploy/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
REPO="$(cd "$(dirname "$0")/.." && pwd)"
MAX_BOOTS="$1"; TAG="$2"; VER="$3"; SUCCESS_GREP="$4"; ENV_VARS="$5"; shift 5
BINARGS="$*"
RUN_TIMEOUT=240

for boot in $(seq 1 "$MAX_BOOTS"); do
  echo "===== BOOT CYCLE $boot/$MAX_BOOTS $(date +%H:%M:%S) ====="
  "$ADB" -s "$SERIAL" reboot
  sleep 20
  BOOTED=0
  for i in $(seq 1 24); do
    sleep 10
    ST=$("$ADB" -s "$SERIAL" get-state 2>/dev/null | tr -d '\r')
    BC=$("$ADB" -s "$SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
    if [ "$ST" = "device" ] && [ "$BC" = "1" ]; then BOOTED=1; break; fi
  done
  if [ "$BOOTED" != "1" ]; then echo "boot did not complete, continuing"; continue; fi

  for i in $(seq 1 12); do
    LA=$("$ADB" -s "$SERIAL" shell cat /proc/loadavg 2>/dev/null | tr -d '\r')
    TOTAL=$(echo "$LA" | awk '{print $4}' | cut -d/ -f2)
    [ -z "$TOTAL" ] && TOTAL=99999
    echo "  [$boot] nr_threads=$TOTAL (la=$LA)"
    if [ "$TOTAL" -lt 2500 ]; then break; fi
    sleep 10
  done

  TS=$(date +%H%M%S)
  OUT="$REPO/reports/ghostlock_runlogs/run_${TAG}_${VER}_${TS}.log"
  # run capped by timeout (success hangs on the interactive root shell)
  timeout "$RUN_TIMEOUT" bash "$REPO/scripts/ghostlock_run.sh" "$TAG" "$VER" "$ENV_VARS" $BINARGS > /tmp/gl_run_stdout_$$.txt 2>&1
  # ghostlock_run.sh writes to its own timestamped log; find it
  LOG=$(grep '^LOGFILE=' /tmp/gl_run_stdout_$$.txt | tail -1 | cut -d= -f2)
  [ -z "$LOG" ] && LOG="$OUT"
  echo "  [$boot] log=$LOG"
  if [ -n "$LOG" ] && [ -f "$LOG" ] && grep -q "$SUCCESS_GREP" "$LOG"; then
    echo "===== SUCCESS on boot $boot: $LOG ====="
    # kill the hung adb shell (root shell) so the wrapper stops blocking
    "$ADB" -s "$SERIAL" shell "pkill -9 -f ghostlock_rv101" 2>/dev/null
    echo "SUCCESS_LOG=$LOG"
    exit 0
  fi
  echo "  [$boot] no success, rebooting..."
done
echo "===== FAILED after $MAX_BOOTS boots ====="
exit 1
