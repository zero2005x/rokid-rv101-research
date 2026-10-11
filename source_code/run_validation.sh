#!/bin/bash

echo "=== RV101 GhostLock Validation ==="
echo ""

# Check devices
echo "=== 1. Device Nodes ==="
ls -la /dev/kgsl-* /dev/adsprpc-* 2>/dev/null || echo "Devices check..."
echo ""

# Check kernel version
echo "=== 2. Kernel Version ==="
uname -r
echo ""

# Check SELinux
echo "=== 3. SELinux ==="
cat /sys/fs/selinux/enforce
echo ""

# Check uid/gid
echo "=== 4. Current Identity ==="
id
echo ""

# Check capabilities
echo "=== 5. Capabilities ==="
cat /proc/self/status | grep -E "Cap|Uid|Gid|Seccomp"
echo ""

# Try to open kgsl
echo "=== 6. KGSL Access Test ==="
cat /dev/kgsl-3d0 > /dev/null 2>&1 && echo "KGSL: OK" || echo "KGSL: FAIL"

# Check if adspRPC exists
echo "=== 7. ADSPRPC ==="
ls -la /dev/adsprpc-smd 2>/dev/null || echo "ADSPRPC: Check"

echo ""
echo "=== Validation Complete ==="
