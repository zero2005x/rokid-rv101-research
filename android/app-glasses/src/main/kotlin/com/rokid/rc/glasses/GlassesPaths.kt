package com.rokid.rc.glasses

/**
 * 眼鏡端固定路徑集中管理。
 *
 * 這些路徑原本散落在 Ghostlock／LED／靜音／戒指等 8 個檔案中各自寫死，
 * 只要有一處改動就容易不同步（例如改 socket 位置卻漏改 payload 腳本）。
 * 統一放這裡，並由 `scripts/audit_hardcode.py` 持續檢查。
 */
object GlassesPaths {
    /** 提權後可寫、且 shell/root 皆可存取的中介目錄。 */
    const val TMP = "/data/local/tmp"

    // ---- root daemon ----
    const val ROOT_DAEMON_SOCK = "$TMP/root_daemon.sock"
    const val ROOT_DAEMON_BIN = "$TMP/root_daemon"
    const val ROOT_DAEMON_PID = "$TMP/root_daemon.pid"
    const val ROOT_DAEMON_LOG = "$TMP/root_daemon.log"
    /** root 常駐 keep-alive 迴圈：daemon 崩潰時自動重建（見 P0）。 */
    const val ROOT_DAEMON_KEEPALIVE_SH = "$TMP/root_daemon_keepalive.sh"
    const val ROOT_DAEMON_KEEPALIVE_PID = "$TMP/root_daemon_keepalive.pid"

    // ---- ghostlock / LPE ----
    const val GHOSTLOCK_BIN = "$TMP/ghostlock_rv101"
    const val GHOSTLOCK_ROOT_SCRIPT = "$TMP/.ghostlock_root.sh"
    const val BRIDGE_DIR = "$TMP/ghostlock_bridge"
    /** helper 每秒更新的心跳檔（App 端判斷觸發管線是否常駐；`ps` 在 App 內看不到別的程序）。 */
    const val BRIDGE_HELPER_ALIVE = "$BRIDGE_DIR/helper.alive"
    const val BRIDGE_HELPER_PID = "$BRIDGE_DIR/helper.pid"
    const val RUN_SHELL = "$TMP/run_shell"

    // ---- 靜音拍攝（提示音壓制）----
    const val SILENT_OGG = "$TMP/silent_camera.ogg"

    // ---- 戒指（root 直讀 evdev）----
    const val RING_EVENTS_LOG = "$TMP/ring_events.log"
    const val RING_READER_PID = "$TMP/ring_reader.pid"
    const val RING_KEEPALIVE_SH = "$TMP/ring_keepalive.sh"
    const val RING_KEEPALIVE_PID = "$TMP/ring_keepalive.pid"

    // ---- 隱私指示燈 sysfs ----
    const val LED_BASE = "/sys/class/leds"
    fun ledBrightness(color: String): String = "$LED_BASE/$color/brightness"
    const val LED_WHITE_CURRENT = "$LED_BASE/white/led_current"
}
