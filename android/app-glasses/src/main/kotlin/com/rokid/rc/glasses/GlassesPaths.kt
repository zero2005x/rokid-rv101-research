package com.rokid.rc.glasses

/**
 * Central management of the glasses-side fixed paths.
 *
 * These paths used to be hardcoded separately across 8 files (Ghostlock/LED/silent/ring and so on),
 * so a change in one place easily went out of sync (e.g. moving the socket but missing the payload script).
 * They live here now and are checked continuously by `scripts/audit_hardcode.py`.
 */
object GlassesPaths {
    /** Staging directory that is writable after escalation and reachable by both shell and root. */
    const val TMP = "/data/local/tmp"

    // ---- root daemon ----
    const val ROOT_DAEMON_SOCK = "$TMP/root_daemon.sock"
    const val ROOT_DAEMON_BIN = "$TMP/root_daemon"
    const val ROOT_DAEMON_PID = "$TMP/root_daemon.pid"
    const val ROOT_DAEMON_LOG = "$TMP/root_daemon.log"
    /** Resident root keep-alive loop: rebuilds the daemon automatically when it crashes (see P0). */
    const val ROOT_DAEMON_KEEPALIVE_SH = "$TMP/root_daemon_keepalive.sh"
    const val ROOT_DAEMON_KEEPALIVE_PID = "$TMP/root_daemon_keepalive.pid"

    // ---- ghostlock / LPE ----
    const val GHOSTLOCK_BIN = "$TMP/ghostlock_rv101"
    const val GHOSTLOCK_ROOT_SCRIPT = "$TMP/.ghostlock_root.sh"
    const val BRIDGE_DIR = "$TMP/ghostlock_bridge"
    /** Heartbeat file the helper updates every second (the app uses it to tell whether the trigger pipeline is resident; `ps` inside the app cannot see other processes). */
    const val BRIDGE_HELPER_ALIVE = "$BRIDGE_DIR/helper.alive"
    const val BRIDGE_HELPER_PID = "$BRIDGE_DIR/helper.pid"
    const val RUN_SHELL = "$TMP/run_shell"

    // ---- silent capture (shutter sound suppression) ----
    const val SILENT_OGG = "$TMP/silent_camera.ogg"

    // ---- ring (root reads evdev directly) ----
    const val RING_EVENTS_LOG = "$TMP/ring_events.log"
    const val RING_READER_PID = "$TMP/ring_reader.pid"
    const val RING_KEEPALIVE_SH = "$TMP/ring_keepalive.sh"
    const val RING_KEEPALIVE_PID = "$TMP/ring_keepalive.pid"

    // ---- privacy indicator LED sysfs ----
    const val LED_BASE = "/sys/class/leds"
    fun ledBrightness(color: String): String = "$LED_BASE/$color/brightness"
    const val LED_WHITE_CURRENT = "$LED_BASE/white/led_current"
}
