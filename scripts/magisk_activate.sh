#!/system/bin/sh
# Magisk Root Activation Attempt
# Bootloader is unlocked, try to activate Magisk root

echo "=== Magisk Root Activation ==="
echo ""

# 1. Check Magisk status
echo "[1] Magisk Package Info:"
pm dump com.topjohnwu.magisk 2>&1 | grep -E "version|flags|uid"

echo ""
echo "[2] Checking /data/adb:"
ls -la /data/adb 2>&1

echo ""
echo "[3] Checking for Magisk modules:"
ls -la /data/adb/modules 2>&1

echo ""
echo "[4] Checking Magisk binary locations:"
ls -la /sbin/magisk /sbin/su 2>&1
ls -la /data/adb/magisk 2>&1
ls -la /system/xbin/su 2>&1

echo ""
echo "[5] Trying to find su binary:"
find /data -name "su" 2>/dev/null | head -5
find /sbin -name "*" 2>/dev/null | head -10

echo ""
echo "[6] Check init.d scripts:"
ls -la /data/adb/post-fs-data.d 2>&1
ls -la /data/adb/service.d 2>&1

echo ""
echo "[7] Magisk Database:"
sqlite3 /data/adb/magisk.db "SELECT * FROM policies" 2>&1

echo ""
echo "[8] Boot Image Info:"
ls -la /dev/block/by-name/boot* 2>&1

echo ""
echo "[9] Trying Magisk Manager API:"
am start -a android.intent.action.VIEW -d "magisk://root" 2>&1

echo ""
echo "[10] Check superuser mode:"
getprop ro.build.selinux
getprop persist.sys.root_access

echo ""
echo "=== Complete ==="
