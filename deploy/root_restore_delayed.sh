#!/system/bin/sh
# root_restore_delayed.sh — bring the root daemon and keepalive back up after N seconds.
#
# ## Why this exists
#
# Some tests need root to be unavailable to the App (for example verifying RootDaemonKeeper's
# lease guard: the guard code only runs **after** `if (client.isRoot()) return`).
#
# But once the root daemon is killed, **only root can bring it back** — so without this script,
# finishing a test means rerunning the exploit (slow, and sensitive to kernel state).
#
# Approach: **before** killing the daemon, start this script as root through the root socket.
# It is a detached process that outlives the daemon and brings the daemon and keepalive back
# once the time is up.
#
# ## Usage (must be started through the root socket, otherwise there is no permission to rebuild the daemon)
#
#   printf 'setsid sh /data/local/tmp/root_restore_delayed.sh 150 >/dev/null 2>&1 &\n' \
#     | adb shell "nc -U /data/local/tmp/root_daemon.sock"
#
# First argument = delay in seconds (default 90). Leave enough time for the test, and check
# that it is really running (`ps -A -o ARGS | grep '[r]oot_restore_delayed'`) before killing
# the daemon.
set -u

DELAY="${1:-90}"
DAEMON=/data/local/tmp/root_daemon
KA=/data/local/tmp/root_daemon_keepalive.sh
SOCK=/data/local/tmp/root_daemon.sock
LOG=/data/local/tmp/root_daemon.log

echo "$(date '+%F %T') restorer: armed (delay=${DELAY}s pid=$$)" >> "$LOG"
sleep "$DELAY"
echo "$(date '+%F %T') restorer: waking up" >> "$LOG"

# only rebuild when there really is no daemon, so an instance restored by another path during the test is not clobbered
if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
  rm -f "$SOCK"
  setsid "$DAEMON" >>"$LOG" 2>&1 &
  echo "$(date '+%F %T') restorer: daemon restarted pid=$!" >> "$LOG"
else
  echo "$(date '+%F %T') restorer: daemon already present, skip" >> "$LOG"
fi

sleep 3

# restart the keepalive if it is absent (again it must be root to keep rebuilding the daemon)
if ! ps -A -o ARGS 2>/dev/null | grep -q '[r]oot_daemon_keepalive.sh'; then
  setsid sh "$KA" >>"$LOG" 2>&1 &
  echo "$(date '+%F %T') restorer: keepalive restarted" >> "$LOG"
fi

echo "$(date '+%F %T') restorer: done" >> "$LOG"
