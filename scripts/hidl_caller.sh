#!/system/bin/sh
# Direct HIDL Service Caller
# Attempt to interact with QSEE via various methods

echo "=== HIDL Service Direct Caller ==="
echo ""

# 1. Try hwservicemanager
echo "[1] HwServiceManager Status:"
ps -ef | grep hwservice
dumpsys hwservicemanager 2>&1 | head -30

echo ""
echo "[2] Attempting HIDL via lshal:"
lshal debug vendor.qti.hardware.qseecom@1.0::IQSEECom/default 2>&1

echo ""
echo "[3] Gatekeeper Service Debug:"
lshal debug android.hardware.gatekeeper@1.0::IGatekeeper/default 2>&1

echo ""
echo "[4] Keymaster via hwbinder:"
lshal debug android.hardware.keymaster@4.0::IKeymasterDevice/default 2>&1 | head -20

echo ""
echo "[5] Check TrustZone App Paths:"
ls -la /vendor/firmware/*.mbn 2>/dev/null | head -10
ls -la /vendor/firmware_mnt/*.mbn 2>/dev/null | head -10
ls -la /firmware/image/*.mbn 2>/dev/null | head -10
find /vendor -name "*.mdt" 2>/dev/null | head -10
find /vendor -name "gatekeeper*" 2>/dev/null

echo ""
echo "[6] QSEE Loaded TAs (via /d/tzdbg/):"
cat /d/tzdbg/log 2>/dev/null | head -30
cat /sys/kernel/debug/tzdbg/log 2>/dev/null | head -30

echo ""
echo "[7] Binder Processes with QSEE Access:"
cat /proc/554/status 2>/dev/null | grep -E "Name|Uid|Gid|Groups"
echo ""
cat /proc/612/status 2>/dev/null | grep -E "Name|Uid|Gid|Groups"

echo ""
echo "[8] GateServiced Process Details:"
cat /proc/17281/status 2>/dev/null | grep -E "Name|Uid|Gid|Groups"
cat /proc/17281/cmdline 2>/dev/null && echo ""

echo ""
echo "[9] SELinux Context of QSEE Processes:"
ps -eZ | grep -E "qsee|gate|keymaster"

echo ""
echo "[10] Attempting to Trigger TA via Keystore:"
# Using keystore_cli_v2 to trigger keymaster
keystore_cli_v2 generate --algorithm RSA --size 2048 --name test_key_trigger 2>&1 | head -10
keystore_cli_v2 delete test_key_trigger 2>/dev/null

echo ""
echo "[11] Checking for app_process hijack potential:"
ls -la /system/bin/app_process* 2>/dev/null
ls -la /data/local/tmp/*.dex 2>/dev/null

echo ""
echo "=== Complete ==="
