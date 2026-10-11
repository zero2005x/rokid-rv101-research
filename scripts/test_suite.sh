#!/system/bin/sh
# Session 14/15 Test Suite for Rokid AR
# Run this on device via: sh /data/local/tmp/test_suite.sh

echo "=== Rokid Priv Esc Test Suite ==="
date
echo ""

echo "=== 1. Device Info ==="
uname -a
getprop ro.build.fingerprint
id
echo ""

echo "=== 2. BPF 5.10 Exploit Test ==="
/data/local/tmp/bpf_5_10_exploit
echo ""

echo "=== 3. run-as test ==="
echo "Shell context:"
cat /proc/self/attr/current
echo ""

echo "Switching to debuggable app..."
run-as com.cooper.wheellog.debug sh -c '
echo "App context:"
id
cat /proc/self/attr/current
echo ""
echo "pwd: $(pwd)"
echo ""
echo "Checking accessible paths..."
ls -la /data/local/tmp 2>&1 | head -3
ls -la /data/data 2>&1 | head -3
echo ""
echo "Write test to app dir..."
echo test > /data/data/com.cooper.wheellog.debug/test_file 2>&1 && echo "App write: OK" || echo "App write: FAIL"
echo ""
echo "Binder access..."
ls -la /dev/binder 2>&1
'
echo ""

echo "=== 4. CVE-2024-0044 Sandbox Test ==="
if [ -f /data/local/tmp/cve_2024_0044_test ]; then
    run-as com.cooper.wheellog.debug /data/local/tmp/cve_2024_0044_test 2>&1 || echo "Cannot run from app context"
fi
echo ""

echo "=== 5. dmesg tail ==="
dmesg 2>&1 | tail -20
echo ""

echo "=== Test Complete ==="
