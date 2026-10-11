#!/usr/bin/env bash
set -u
source "$(dirname "$0")/../deploy/env.sh"   # shared ADB/SERIAL (see deploy/env.sh)
S="<DEVICE_SERIAL>"
ENV="GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1"
LOCAL_SCRIPT="/home/kali/rokid_root/led_test.sh"
MAX_RETRY_CURRENT="${1:-3}"
MAX_BOOTS="${2:-4}"

attempt_run() {
  local label="$1"
  echo "----- attempt: $label ($(date +%H:%M:%S)) -----"
  "$ADB" -s "$S" shell "rm -f /data/local/tmp/led_test.log /data/local/tmp/gl_run.log" >/dev/null 2>&1
  # start exploit, stdout to file (orphan child holds a file, not the adb pipe)
  "$ADB" -s "$S" shell "$ENV /data/local/tmp/ghostlock_rv101 > /data/local/tmp/gl_run.log 2>&1" &
  local EXPID=$!
  # wait for default script marker (proves write_root_script() ran)
  local ready=0
  for i in $(seq 1 40); do
    sleep 2
    if "$ADB" -s "$S" shell "cat /data/local/tmp/.ghostlock_root.sh 2>/dev/null" 2>/dev/null | grep -q "dropping to root shell"; then
      ready=1; break
    fi
  done
  if [ "$ready" != "1" ]; then
    echo "  [!] default script never observed; exploit may have failed early"
    kill "$EXPID" 2>/dev/null
    "$ADB" -s "$S" shell "pkill -9 -f ghostlock_rv101" 2>/dev/null
    return 1
  fi
  # overwrite with custom LED script (atomic rename)
  "$ADB" -s "$S" push "$LOCAL_SCRIPT" /data/local/tmp/.ghostlock_root.sh.new >/dev/null 2>&1
  "$ADB" -s "$S" shell "mv /data/local/tmp/.ghostlock_root.sh.new /data/local/tmp/.ghostlock_root.sh && chmod 755 /data/local/tmp/.ghostlock_root.sh" >/dev/null 2>&1
  echo "  [+] custom script installed into .ghostlock_root.sh"
  # wait for LED_TEST_DONE or explicit exploit failure
  local done=0
  for i in $(seq 1 100); do
    sleep 2
    if "$ADB" -s "$S" shell "cat /data/local/tmp/led_test.log 2>/dev/null" 2>/dev/null | grep -q "LED_TEST_DONE"; then
      done=1; break
    fi
    local fail
    fail=$("$ADB" -s "$S" shell "grep -E 'FOPS install failed|failed after' /data/local/tmp/gl_run.log 2>/dev/null" 2>/dev/null)
    if [ -n "$fail" ]; then
      echo "  [!] exploit failed: $fail"
      break
    fi
  done
  "$ADB" -s "$S" shell "pkill -9 -f ghostlock_rv101" 2>/dev/null
  if [ "$done" = "1" ]; then return 0; else return 1; fi
}

# Phase 1: retry on current (known-good) boot
for r in $(seq 1 "$MAX_RETRY_CURRENT"); do
  if attempt_run "boot=current retry=$r"; then
    echo "===== SUCCESS ====="
    exit 0
  fi
done

# Phase 2: reboot loop
for b in $(seq 1 "$MAX_BOOTS"); do
  echo "===== REBOOT CYCLE $b/$MAX_BOOTS $(date +%H:%M:%S) ====="
  "$ADB" -s "$S" reboot
  sleep 20
  booted=0
  for i in $(seq 1 30); do
    sleep 10
    ST=$("$ADB" -s "$S" get-state 2>/dev/null | tr -d '\r')
    BC=$("$ADB" -s "$S" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
    if [ "$ST" = "device" ] && [ "$BC" = "1" ]; then booted=1; break; fi
  done
  if [ "$booted" != "1" ]; then echo "  boot did not complete"; continue; fi
  for i in $(seq 1 12); do
    LA=$("$ADB" -s "$S" shell cat /proc/loadavg 2>/dev/null | tr -d '\r')
    TOTAL=$(echo "$LA" | awk '{print $4}' | cut -d/ -f2)
    [ -z "$TOTAL" ] && TOTAL=99999
    echo "  nr_threads=$TOTAL"
    if [ "$TOTAL" -lt 2500 ]; then break; fi
    sleep 10
  done
  for r in $(seq 1 2); do
    if attempt_run "boot=$b retry=$r"; then
      echo "===== SUCCESS ====="
      exit 0
    fi
  done
done

echo "===== FAILED after all retries ====="
exit 1
