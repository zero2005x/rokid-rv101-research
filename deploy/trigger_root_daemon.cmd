#!/system/bin/sh
echo 0 > /sys/fs/selinux/enforce 2>/dev/null
# 只有當 root_daemon 未「活著」監聽時才啟動（stale socket 檔不算存活）。
if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
    rm -f /data/local/tmp/root_daemon.sock
    setsid /data/local/tmp/root_daemon >/data/local/tmp/root_daemon.log 2>&1 &
fi
for i in $(seq 1 20); do
    grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null && break
    sleep 0.2
done
id > /data/local/tmp/ghostlock_bridge/last.done
echo __GHOSTLOCK_PAYLOAD_DONE__ >> /data/local/tmp/ghostlock_bridge/last.done
