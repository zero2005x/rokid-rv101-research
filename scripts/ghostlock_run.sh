#!/usr/bin/env bash
# ghostlock_run.sh — wrap a ghostlock_rv101 run with full metadata capture.
# Usage: ghostlock_run.sh <tag> <version> <command...>
set -u
source "$(dirname "$0")/../deploy/env.sh"   # 共用 ADB/SERIAL（見 deploy/env.sh）
REPO="$(cd "$(dirname "$0")/.." && pwd)"
TAG="$1"; VER="$2"; shift 2
TS=$(date +%H%M%S)
OUT="$REPO/reports/ghostlock_runlogs/run_${TAG}_${VER}_${TS}.log"

{
  echo "=== GHOSTLOCK RUNLOG ==="
  echo "tag=$TAG version=$VER ts=$TS"
  echo "--- host time ---"; date
  echo "--- uptime before ---"; "$ADB" -s "$SERIAL" shell uptime
  echo "--- loadavg before ---"; "$ADB" -s "$SERIAL" shell cat /proc/loadavg
  echo "--- nr_threads (field 4 total) ---"; "$ADB" -s "$SERIAL" shell cat /proc/loadavg | awk '{print "running/total="$4}'
  echo "--- meminfo head ---"; "$ADB" -s "$SERIAL" shell cat /proc/meminfo | head -4
  echo "--- getenforce before ---"; "$ADB" -s "$SERIAL" shell getenforce
  echo "--- device binary sha1 ---"; "$ADB" -s "$SERIAL" shell sha1sum /data/local/tmp/ghostlock_rv101
  echo "--- local binary sha1 ---"; sha1sum "$REPO/source_code/ghostlock_rv101/build/ghostlock_rv101"
   echo "--- environment ---"
   env | grep -E '^GL_|^W2_|^SKB_|^V9_|^SELFTEST' | sort || true
   echo "--- environment (command-prefix) ---"
   # GL_*/W2_* vars are passed inline as "VAR=VAL ... cmd", not exported;
   # capture leading VAR=VALUE tokens from the command string itself.
   for tok in $(printf '%s\n' "$@"); do
     case "$tok" in
       [A-Za-z_][A-Za-z0-9_]*=*) printf '  %s\n' "$tok" ;;
       *) break ;;
     esac
   done
  echo "--- command ---"; printf '%q ' "$@"; echo
  echo "=== RUN OUTPUT ==="
} > "$OUT"

# run the command on-device, tee stdout+stderr into the log
"$ADB" -s "$SERIAL" shell "$@" 2>&1 | tee -a "$OUT"
RC=${PIPESTATUS[0]}

{
  echo "=== RUN DONE (pipe rc=$RC) ==="
  echo "--- host time ---"; date
  echo "--- adb device state ---"; "$ADB" -s "$SERIAL" get-state 2>&1
  echo "--- uptime after ---"; "$ADB" -s "$SERIAL" shell uptime 2>&1
  echo "--- getenforce after ---"; "$ADB" -s "$SERIAL" shell getenforce 2>&1
  echo "--- log file: $OUT ---"
} >> "$OUT"
echo "LOGFILE=$OUT"
