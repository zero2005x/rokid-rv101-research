#!/system/bin/sh
# QSEE HIDL and graphics_composer Exploitation Research
# Purpose: Investigate TA loading mechanism and qmcs exploitation

echo "=========================================="
echo "   QSEE & graphics_composer Research"
echo "=========================================="
echo ""

# Part 1: QSEE HIDL Interface Analysis
echo "[PHASE 1] QSEE HIDL Interface Analysis"
echo "======================================="

# Check QSEE service 
echo "[1.1] QSEE HIDL Service Status:"
lshal debug vendor.qti.hardware.qseecom@1.0::IQSEECom/default 2>&1 | head -50

echo ""
echo "[1.2] Available QSEE methods (from binary):"
strings /vendor/lib64/hw/vendor.qti.hardware.qseecom@1.0-impl.so 2>/dev/null | grep -E '^[a-z].*App|^[a-z].*Listener|^[a-z].*Command' | head -20

echo ""
echo "[1.3] QSEE Client Binder Check:"
cat /d/binder/transactions 2>/dev/null | grep -i qsee | head -10
cat /sys/kernel/debug/binder/transactions 2>/dev/null | grep -i qsee | head -10

# Part 2: graphics_composer qmcs Analysis
echo ""
echo "[PHASE 2] graphics_composer qmcs Analysis"
echo "=========================================="

echo "[2.1] Current qmcs contents:"
ls -la /mnt/vendor/qmcs/

echo ""
echo "[2.2] Create test config file:"
cat > /mnt/vendor/qmcs/display_config.ini << 'EOF'
[display]
brightness=100
mode=hdr
override=true
EOF
ls -la /mnt/vendor/qmcs/display_config.ini

echo ""
echo "[2.3] Create symlink test (if possible):"
ln -s /data/local/tmp/pwn.sh /mnt/vendor/qmcs/exec_me 2>&1
ls -la /mnt/vendor/qmcs/

echo ""
echo "[2.4] graphics_composer open files:"
ls -la /proc/801/fd 2>&1 | head -30

echo ""
echo "[2.5] graphics_composer memory maps (qmcs related):"
cat /proc/801/maps 2>/dev/null | grep -iE 'qmcs|mnt/vendor' | head -10

# Part 3: Try to trigger qmcs read
echo ""
echo "[PHASE 3] Trigger graphics_composer Reload"
echo "==========================================="

echo "[3.1] Sending SIGHUP to graphics_composer..."
# This might trigger config reload
kill -HUP 801 2>&1

echo ""
echo "[3.2] Check for qmcs access in kernel log:"
dmesg | grep -iE 'qmcs|composer' | tail -20

echo ""
echo "[3.3] graphics_composer logcat:"
logcat -d -b main -t 100 | grep -iE 'composer|qmcs|display' | tail -30

# Part 4: GateServiced Analysis
echo ""
echo "[PHASE 4] GateServiced TA Loading Research"
echo "==========================================="

echo "[4.1] GateServiced binder clients:"
cat /d/binder/proc/$(pidof GateServiced) 2>/dev/null | head -30
cat /sys/kernel/debug/binder/proc/$(pidof GateServiced) 2>/dev/null | head -30

echo ""
echo "[4.2] GateServiced open devices:"
ls -la /proc/$(pidof GateServiced)/fd 2>&1 | head -30

echo ""
echo "[4.3] Check TEE data directory:"
ls -la /data/vendor/qsee/ 2>/dev/null
ls -la /data/vendor/tzstorage/ 2>/dev/null

echo ""
echo "[4.4] QSEECOM device access:"
ls -la /dev/qseecom 2>&1
cat /dev/qseecom 2>&1 | head -5

echo ""
echo "[4.5] Kernel QSEE module params:"
cat /sys/module/qseecom/parameters/* 2>/dev/null

echo ""
echo "=========================================="
echo "   Research Complete"
echo "=========================================="
