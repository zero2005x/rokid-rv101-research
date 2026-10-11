#!/system/bin/sh
# Comprehensive Vulnerability Scanner for Kernel 5.10.209
# Target: Rokid AR Glasses

echo "=== Comprehensive Vulnerability Scan ==="
echo "Kernel: $(uname -r)"
echo "Date: $(date)"
echo ""

# 1. Check Dirty Pipe (CVE-2022-0847)
echo "[1] Dirty Pipe (CVE-2022-0847) Check:"
KVER=$(uname -r | cut -d'-' -f1)
echo "  Kernel version: $KVER"
# Affected: 5.8 - 5.16.11, 5.15.25, 5.10.102
# 5.10.209 should be patched
if [ -f /proc/sys/vm/unprivileged_userfaultfd ]; then
    echo "  userfaultfd: $(cat /proc/sys/vm/unprivileged_userfaultfd 2>/dev/null)"
fi
echo "  Status: Likely PATCHED (5.10.209 > 5.10.102)"

echo ""
echo "[2] io_uring Availability:"
# Check if io_uring syscalls work
cat /proc/kallsyms 2>/dev/null | grep -c io_uring
ls -la /dev/io_uring 2>/dev/null || echo "  /dev/io_uring not found (normal)"

echo ""
echo "[3] BPF Subsystem:"
echo "  unprivileged_bpf: $(cat /proc/sys/kernel/unprivileged_bpf_disabled)"
echo "  BPF JIT: $(cat /proc/sys/net/core/bpf_jit_enable 2>/dev/null)"
echo "  BPF JIT harden: $(cat /proc/sys/net/core/bpf_jit_harden 2>/dev/null)"

echo ""
echo "[4] Checking /proc/config.gz for vulnerabilities:"
if [ -f /proc/config.gz ]; then
    zcat /proc/config.gz 2>/dev/null | grep -E "CONFIG_IO_URING|CONFIG_BPF|CONFIG_USERFAULTFD|CONFIG_USER_NS|CONFIG_SLAB_FREELIST" | head -20
else
    echo "  /proc/config.gz not available"
fi

echo ""
echo "[5] KGSL GPU Driver (Qualcomm):"
ls -la /dev/kgsl* 2>/dev/null
cat /sys/class/kgsl/kgsl-3d0/gpu_model 2>/dev/null

echo ""
echo "[6] Binder Driver:"
ls -la /dev/binder* /dev/hwbinder* /dev/vndbinder* 2>/dev/null

echo ""
echo "[7] Ashmem:"
ls -la /dev/ashmem* 2>/dev/null

echo ""
echo "[8] DRM/Graphics:"
ls -la /dev/dri/* /dev/graphics/* 2>/dev/null

echo ""
echo "[9] Network Capabilities Check:"
# Check if we can create raw sockets
cat /proc/self/status | grep Cap
echo ""
echo "  Testing socket creation..."
# Try to see what network syscalls are available

echo ""
echo "[10] Keyring Subsystem:"
cat /proc/keys 2>/dev/null | head -5
keyctl show 2>/dev/null | head -5

echo ""
echo "[11] Checking for CVE-2024-0582 (io_uring mmap UAF):"
# This requires io_uring to be available
echo "  io_uring syscall test needed"

echo ""
echo "[12] Checking for CVE-2024-36971 (Route UAF):"
echo "  Route table access:"
ip route show 2>/dev/null | head -5
cat /proc/net/route 2>/dev/null | head -5

echo ""
echo "[13] Netfilter/nftables:"
cat /proc/net/nf_tables 2>/dev/null || echo "  nf_tables not available"
lsmod 2>/dev/null | grep -E "nf_|xt_" | head -10

echo ""
echo "[14] Filesystem Vulnerabilities:"
echo "  OverlayFS:"
cat /proc/filesystems | grep overlay
echo "  FUSE:"
cat /proc/filesystems | grep fuse

echo ""
echo "[15] SELinux Detailed:"
getenforce
cat /sys/fs/selinux/enforce
ls -la /sys/fs/selinux/

echo ""
echo "[16] Checking debugfs mount:"
mount | grep debugfs
ls -la /sys/kernel/debug/ 2>/dev/null | head -10

echo ""
echo "[17] procfs sensitive files:"
cat /proc/sys/kernel/randomize_va_space
cat /proc/sys/kernel/modules_disabled 2>/dev/null

echo ""
echo "[18] Potential Privilege Escalation Binaries:"
find /system /vendor -perm -4000 2>/dev/null | head -20

echo ""
echo "[19] World-Writable Device Nodes:"
find /dev -perm -0002 -type c 2>/dev/null | head -20

echo ""
echo "[20] Checking for ptrace restrictions:"
cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null

echo ""
echo "=== Scan Complete ==="
