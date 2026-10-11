#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# GateServiced Logcat Monitor — Run on RPi4 while testing EXEC
# ═══════════════════════════════════════════════════════════════
#
# Usage (on RPi4):
#   chmod +x gate_logcat_monitor.sh
#   ./gate_logcat_monitor.sh
#
# Then on PC, run:
#   python3 gate_exec_breakthrough.py version
#
# The logcat output will show WHERE run_exec_cmd_ta fails:
#   "cmd_id = %x, input_len = %zu"     → EXEC entered (good!)
#   "TA is not already loaded."         → g_loaded_flag=0 (bad)
#   "appObj is NULL!"                   → TA crashed post-load
#   "Failed to create DMA buffer"       → DMA fd invalid
#   "dma_alloc failed"                  → DMA heap exhausted
#   "ISMCIGate_sharedMemoryExample failed" → TZ IPC error
#   "received magicNumTA failed"        → TA processed but wrong magic
#   "Returned data had length of zero"  → TA returned empty
#   "ta version = %s"                   → version query succeeded!
# ═══════════════════════════════════════════════════════════════

echo "═══════════════════════════════════════════════════════"
echo " GateServiced Logcat Monitor"
echo " Watching for: GATE CA, GateServiced, QSEE, TZ"
echo "═══════════════════════════════════════════════════════"
echo ""
echo "Waiting for EXEC commands..."
echo "Press Ctrl+C to stop."
echo ""

# Clear buffer first
adb logcat -c 2>/dev/null

# Monitor with multiple relevant tags
adb logcat -v time \
    -s "GATE CA:*" \
    "GateServiced:*" \
    "QSEE:*" \
    "QSEEComAPI:*" \
    "TZ:*" \
    "qseecom:*" \
    "DMA:*" \
    "dma_buf:*" \
    "tee:*" \
    "SELinux:*" 2>&1 | while read -r line; do
    # Highlight important messages
    case "$line" in
        *"cmd_id"*)
            echo "★ $line"
            ;;
        *"magicNumTA"*)
            echo "★★★ $line"
            ;;
        *"ta version"*)
            echo "★★★★★ $line"
            ;;
        *"succeed"*)
            echo "✓ $line"
            ;;
        *"ERRO"*|*"fail"*|*"error"*|*"NULL"*|*"denied"*)
            echo "✗ $line"
            ;;
        *)
            echo "  $line"
            ;;
    esac
done
