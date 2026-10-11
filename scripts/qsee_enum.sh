#!/system/bin/sh
# QSEE Service Enumeration Script
# Purpose: Enumerate QSEE/TEE services and attempt TA loading

echo "=== QSEE Service Enumeration ==="
echo "Date: $(date)"
echo ""

# 1. List HIDL services
echo "[1] HIDL Services (lshal):"
lshal 2>/dev/null | grep -E "qsee|tee|keymaster|gatekeeper|drm|crypto" | head -20

echo ""
echo "[2] Checking QSEECOM Service:"
service list 2>/dev/null | grep -iE "qsee|tee|drm"

echo ""
echo "[3] HwBinder Interfaces:"
cat /sys/kernel/debug/binder/state 2>/dev/null | grep -iE "qsee|vendor.qti" | head -10

echo ""
echo "[4] QSEE Device Access:"
ls -la /dev/qseecom 2>/dev/null
ls -la /dev/tee* 2>/dev/null
ls -la /dev/tzic 2>/dev/null

echo ""
echo "[5] Current Process Groups:"
id

echo ""
echo "[6] Testing QSEE via Keymaster:"
# Try to use keymaster to trigger TA
keystore_cli_v2 list 2>&1 | head -10

echo ""
echo "[7] GateServiced Status:"
ps -ef | grep -i gate
netstat -tlnp 2>/dev/null | grep 8341

echo ""
echo "[8] Testing GateServiced Connection:"
# Send test command to GateServiced
(echo -ne '\x00\x00\x00\x00\x00\x00\x00\x10test_cmd_qsee' | nc -w 2 127.0.0.1 8341 2>&1 | xxd | head -20) || echo "Connection failed"

echo ""
echo "[9] DRM Manager Service:"
dumpsys drm 2>&1 | head -30

echo ""
echo "[10] Attempting TA Query via Service Call:"
# Try calling qseecom service
service call vendor.qti.hardware.qseecom 1 2>&1 || echo "Service call failed"

echo ""
echo "=== Enumeration Complete ==="
