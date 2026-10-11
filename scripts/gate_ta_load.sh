#!/system/bin/sh
# GateServiced TA Loading Test via QSEE HIDL
# Purpose: Try to trigger TA load through QSEE service calls

echo "=============================================="
echo "   GateServiced TA Loading via QSEE HIDL"
echo "=============================================="
echo ""

# Check current service states
echo "[1] Current Service Status:"
echo "----------------------------"
ps -A | grep -E 'GateServiced|qseecom|qsee'
echo ""

# Monitor logcat for QSEE activity
echo "[2] Starting logcat monitor (5 seconds)..."
logcat -c
logcat -b main -b system -T 1 | grep -iE 'qsee|tee|gate|trustlet|ta|load' &
LOGCAT_PID=$!
sleep 2

# Try to restart GateServiced to trigger TA load
echo ""
echo "[3] Restarting GateServiced..."
setprop ctl.restart GateServiced
sleep 3

# Stop logcat monitor
kill $LOGCAT_PID 2>/dev/null

echo ""
echo "[4] GateServiced current state:"
ps -A | grep GateServiced
GATE_PID=$(pidof GateServiced)
echo "PID: $GATE_PID"

# Check if TA is loaded now
echo ""
echo "[5] Testing GateServiced connection after restart..."
{
    # Send test command
    printf 'gninp\x00\x00\x00\x00\x00\x00\x00\x00'
    sleep 1
} | nc -w 2 127.0.0.1 8341 2>&1 | xxd | head -5

echo ""
echo "[6] Check QSEE comm socket activity:"
ls -la /dev/socket/notify-topology 2>&1
stat /dev/socket/notify-topology 2>&1 | head -10

echo ""
echo "[7] Check what GateServiced links against:"
cat /proc/$GATE_PID/maps 2>/dev/null | grep -iE 'qsee|libQSEE' | head -10

echo ""
echo "[8] Check GateServiced environment:"
cat /proc/$GATE_PID/environ 2>/dev/null | tr '\0' '\n' | head -20

echo ""
echo "[9] QSEE HIDL call log (from dmesg):"
dmesg | grep -iE 'qseecom|startApp|load' | tail -20

echo ""
echo "[10] Try invoking QSEE HIDL directly (if available):"
# Check if there's a debug interface
cat /d/qseecom/apps 2>/dev/null
ls /sys/kernel/debug/qseecom/ 2>/dev/null
cat /sys/kernel/debug/qseecom/apps 2>/dev/null

echo ""
echo "=============================================="
echo "   Analysis Complete"
echo "=============================================="
