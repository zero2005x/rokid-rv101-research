#!/system/bin/sh
# DRM Service Analysis for drmrpc Group Access
# Purpose: Research DRM service to potentially gain drmrpc group access

echo "=============================================="
echo "   DRM Service Analysis for drmrpc Access"
echo "=============================================="
echo ""

# Part 1: DRM Service Information
echo "[1] DRM Service List:"
service list | grep -i drm

echo ""
echo "[2] DRM Manager Details:"
dumpsys drm.drmManager 2>&1 | head -50

echo ""
echo "[3] DRM Processes:"
ps -A | grep -iE 'drm|mediadrm|widevine'

echo ""
echo "[4] DRM Related HIDL Services:"
lshal | grep -i drm

# Part 2: DRM HAL Analysis
echo ""
echo "[5] DRM HAL Files:"
ls -la /vendor/lib64/hw/ | grep -i drm

echo ""
echo "[6] DRM Binary Strings (method hints):"
strings /vendor/bin/hw/android.hardware.drm@* 2>/dev/null | grep -iE 'qsee|tee|trustzone' | head -20

# Part 3: DRM Socket/Device Access
echo ""
echo "[7] DRM Related Devices:"
ls -la /dev/*drm* 2>/dev/null
ls -la /dev/dri/ 2>/dev/null

echo ""
echo "[8] DRM Socket Files:"
find /dev/socket -name '*drm*' 2>/dev/null
ls -la /dev/socket/*drm* 2>/dev/null

# Part 4: Test DRM Access
echo ""
echo "[9] Testing DRM Manager Call (via service):"
service call drm.drmManager 1 2>&1 | head -10

echo ""
echo "[10] DRM Init RC Configuration:"
cat /vendor/etc/init/*.drm*.rc 2>/dev/null | head -30
grep -rn 'drmrpc' /vendor/etc/init/ 2>/dev/null | head -20

# Part 5: Media DRM Analysis
echo ""
echo "[11] MediaDRM Service:"
dumpsys media.drm 2>&1 | head -50

echo ""
echo "[12] Available DRM Plugins:"
ls -la /vendor/lib64/mediadrm/ 2>/dev/null
ls -la /system/lib64/mediadrm/ 2>/dev/null

# Part 6: Content Protection
echo ""
echo "[13] HDCP Service:"
ps -A | grep -i hdcp
ls -la /vendor/bin/*hdcp* 2>/dev/null

echo ""
echo "[14] WFD (Wireless Display) Service with DRM:"
dumpsys media.wfd 2>&1 | head -20

# Part 7: App-level DRM access
echo ""
echo "[15] Apps with DRM Permissions:"
pm list packages -f 2>/dev/null | while read pkg; do
    pkg_name=$(echo $pkg | cut -d= -f2)
    if dumpsys package $pkg_name 2>/dev/null | grep -q "android.permission.ACCESS_DRM"; then
        echo "  - $pkg_name"
    fi
done

echo ""
echo "[16] Current User Groups (shell):"
id
groups

echo ""
echo "[17] drmrpc Group Members:"
cat /system/etc/group 2>/dev/null | grep drmrpc
cat /vendor/etc/group 2>/dev/null | grep drmrpc

echo ""
echo "=============================================="
echo "   Analysis Complete"
echo "=============================================="
