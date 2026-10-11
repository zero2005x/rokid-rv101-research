#!/system/bin/sh
# Comprehensive Privilege Escalation Enumeration
# For Rokid AR Glasses

echo "=== Rokid Privilege Escalation Enumeration ==="
echo "Date: $(date)"
echo ""

# 1. System Info
echo "[1] System Information:"
uname -a
getprop ro.build.version.release
getprop ro.build.version.security_patch
getprop ro.product.model

echo ""
echo "[2] SELinux Status:"
getenforce
cat /sys/fs/selinux/enforce

echo ""
echo "[3] Current User Context:"
id
cat /proc/self/attr/current 2>/dev/null

echo ""
echo "[4] Sudo-like Binaries (SUID):"
find /system /vendor /data -perm -4000 2>/dev/null | head -20

echo ""
echo "[5] Writable Directories:"
find /data /vendor /system -writable -type d 2>/dev/null | grep -v "Permission denied" | head -20

echo ""
echo "[6] Interesting Capabilities:"
cat /proc/self/status | grep Cap

echo ""
echo "[7] Root Processes Accessible:"
ps -ef | grep -E "^root" | grep -v "\[" | head -20

echo ""
echo "[8] Network Services (Potential Attack Surface):"
netstat -tlnp 2>/dev/null | grep -E "LISTEN"

echo ""
echo "[9] Interesting Init Scripts:"
grep -l "setuid\|setgid\|seclabel.*:s0" /vendor/etc/init/*.rc 2>/dev/null | head -10

echo ""
echo "[10] Debuggable/Permissive Services:"
for svc in $(getprop | grep -oE 'init\.svc\.[a-zA-Z0-9_]+' | cut -d. -f3); do
    ctx=$(cat /proc/$(pidof $svc 2>/dev/null)/attr/current 2>/dev/null)
    if echo "$ctx" | grep -qE "permissive|:s0$"; then
        echo "  $svc: $ctx"
    fi
done 2>/dev/null | head -10

echo ""
echo "[11] Binder Services as Root:"
dumpsys activity services 2>/dev/null | grep -B5 "uid=0" | head -20

echo ""
echo "[12] Vendor Properties (Potential Misconfig):"
getprop | grep -iE "debug|test|root|admin|password|key" | head -15

echo ""
echo "[13] ADB Root Status:"
getprop ro.debuggable
getprop ro.secure
getprop service.adb.root

echo ""
echo "[14] Boot Loader Status:"
getprop ro.boot.flash.locked
getprop ro.boot.verifiedbootstate

echo ""
echo "[15] Kernel Modules:"
cat /proc/modules 2>/dev/null | head -10
ls /vendor/lib/modules/*.ko 2>/dev/null | head -10

echo ""
echo "[16] /dev Writable Devices:"
ls -la /dev 2>/dev/null | grep -E "^crw.*[0-9]+ +[0-9]+.*shell" | head -10

echo ""
echo "[17] Potential Credential Files:"
find /data /vendor -name "*.key" -o -name "*.pem" -o -name "*password*" -o -name "*credential*" 2>/dev/null | head -10

echo ""
echo "[18] Magisk/Root Detection:"
ls -la /sbin/su /system/xbin/su /system/bin/su 2>/dev/null
getprop init.svc.magisk
pm list packages | grep -iE "magisk|supersu|kingroot"

echo ""
echo "[19] GateServiced Analysis:"
cat /vendor/etc/init/GateServiced.rc 2>/dev/null

echo ""
echo "[20] QSEE/TEE Available Commands:"
ls -la /vendor/bin/*qsee* /vendor/bin/*tee* 2>/dev/null

echo ""
echo "=== Enumeration Complete ==="
