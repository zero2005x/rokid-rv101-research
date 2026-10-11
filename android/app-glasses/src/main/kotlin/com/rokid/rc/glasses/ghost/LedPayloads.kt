package com.rokid.rc.glasses.ghost

import java.io.File
import com.rokid.rc.glasses.GlassesPaths

/**
 * Root payload templates for suppressing/restoring the camera privacy LED.
 *
 * Based on the conclusion of the existing test script (led_test.log Path A): under root, writing
 * `/sys/class/leds/white/brightness` / `led_current` directly is enough to control the LED.
 * To stop the camera-open event (lights_ctrl `EVENT_WARN_ID_CAMERA_OPEN`) from lighting it again during capture,
 * suppress forks a background loop that holds the LED at 0 for as long as the marker file exists.
 */
object LedPayloads {

    private val LED_BRIGHT = GlassesPaths.ledBrightness("white")
    private const val LED_CURRENT = GlassesPaths.LED_WHITE_CURRENT

    private val LED_PATHS = mapOf(
        "white" to GlassesPaths.ledBrightness("white"),
        "green" to GlassesPaths.ledBrightness("green"),
        "blue" to GlassesPaths.ledBrightness("blue"),
        "red" to GlassesPaths.ledBrightness("red")
    )

    /** Start suppression: drive it to 0 immediately + cancel the lights_ctrl camera-open event (fixes flicker at the root) + background hold loop. */
    fun suppress(marker: File, payloadLog: File): RootScript {
        val content = """
            #!/system/bin/sh
            M=${marker.absolutePath}
            service call lights_ctrl 9 i32 2 i32 2014 >/dev/null 2>&1
            echo 0 > $LED_BRIGHT 2>/dev/null
            echo 0 > $LED_CURRENT 2>/dev/null
            ( while [ -f "${'$'}M" ]; do
                echo 0 > $LED_BRIGHT 2>/dev/null
                echo 0 > $LED_CURRENT 2>/dev/null
                sleep 0.2
              done ) >/dev/null 2>&1 &
            echo "suppressed brightness=$(cat $LED_BRIGHT 2>&1)" > ${payloadLog.absolutePath}
            echo ${PayloadMarkers.DONE} >> ${payloadLog.absolutePath}
        """.trimIndent()
        return RootScript("led_suppress", content)
    }

    /** Restore: put the LED back into the camera-open state (brightness=255, led_current=2, and re-emit the camera-open event). */
    fun restore(payloadLog: File): RootScript {
        val content = """
            #!/system/bin/sh
            echo 255 > $LED_BRIGHT 2>/dev/null
            echo 2 > $LED_CURRENT 2>/dev/null
            service call lights_ctrl 8 i32 2 i32 2014 >/dev/null 2>&1
            echo ${PayloadMarkers.DONE} > ${payloadLog.absolutePath}
        """.trimIndent()
        return RootScript("led_restore", content)
    }

    /** Verify/acquire root: write id to the log and mark DONE. */
    fun verifyRoot(doneLog: File): RootScript {
        val content = """
            #!/system/bin/sh
            id > ${doneLog.absolutePath}
            echo ${PayloadMarkers.DONE} >> ${doneLog.absolutePath}
        """.trimIndent()
        return RootScript("verify_root", content)
    }

    /** Start the resident root daemon after escalation (and verify root). */
    /**
     * Start the root daemon, **with liveness hardening (P0)**:
     *  1. write a pid file (so later health checks/cleanup can target it precisely)
     *  2. `oom_score_adj=-1000`: keeps lmkd from reclaiming it under memory pressure
     *  3. install and start a **resident root keep-alive loop**: rebuilds the daemon automatically when it crashes (no adb / no re-running the exploit)
     *
     * NOTE (a pitfall already hit): after a reboot `/data/local/tmp` keeps a `root_daemon.sock` "file" with no listener behind it,
     * so checking `[ -S ]` alone misreports -> always use `/proc/net/unix` (which lists only sockets that have a listener) to decide liveness.
     */
    fun startRootDaemon(doneLog: File, daemonPath: String = GlassesPaths.ROOT_DAEMON_BIN): RootScript {
        // NOTE: this is a Kotlin raw string, so shell's $ must be escaped as ${'$'}
        val content = """
            #!/system/bin/sh
            echo 0 > /sys/fs/selinux/enforce 2>/dev/null

            KA=${GlassesPaths.ROOT_DAEMON_KEEPALIVE_SH}
            PID=${GlassesPaths.ROOT_DAEMON_PID}
            SOCK=${GlassesPaths.ROOT_DAEMON_SOCK}
            LOG=${GlassesPaths.ROOT_DAEMON_LOG}

            # ---- 1) keep-alive loop (resident as root; rebuilds the daemon when it dies) ----
            cat > "${'$'}KA" <<'KEEPALIVE'
            #!/system/bin/sh
            # root daemon liveness watchdog (P0). Rebuilds it when it crashes/is reclaimed, and keeps its OOM score low.
            PID=${GlassesPaths.ROOT_DAEMON_PID}
            SOCK=${GlassesPaths.ROOT_DAEMON_SOCK}
            BIN=${GlassesPaths.ROOT_DAEMON_BIN}
            LOG=${GlassesPaths.ROOT_DAEMON_LOG}
            while true; do
              if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
                rm -f "${'$'}SOCK"
                setsid "${'$'}BIN" >>"${'$'}LOG" 2>&1 &
                echo ${'$'}! > "${'$'}PID"
                echo "${'$'}(date '+%F %T') keepalive: daemon restarted pid=${'$'}(cat ${'$'}PID 2>/dev/null)" >> "${'$'}LOG"
              fi
              # the pid file may hold a stale value -> look the real PID up via ps (keepalive runs as root)
              ACTUAL=${'$'}(ps -A -o PID,ARGS 2>/dev/null | grep '[r]oot_daemon${'$'}' | awk '{print ${'$'}1}' | head -1)
              [ -n "${'$'}ACTUAL" ] && echo "${'$'}ACTUAL" > "${'$'}PID"
              for p in ${'$'}(cat "${'$'}PID" 2>/dev/null) ${'$'}${'$'}; do
                echo -1000 > /proc/${'$'}p/oom_score_adj 2>/dev/null
              done
              sleep 20
            done
            KEEPALIVE
            chmod 755 "${'$'}KA"

            # ---- 2) the daemon itself ----
            if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
              rm -f "${'$'}SOCK"
              setsid ${daemonPath} >>"${'$'}LOG" 2>&1 &
              echo ${'$'}! > "${'$'}PID"
            fi
            for i in $(seq 1 20); do
              grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null && break
              sleep 0.2
            done

            # ---- 3) start keep-alive (do not duplicate it if already running) ----
            if ! ps -A -o ARGS 2>/dev/null | grep -q '[r]oot_daemon_keepalive.sh'; then
              setsid sh "${'$'}KA" >/dev/null 2>&1 &
              echo ${'$'}! > ${GlassesPaths.ROOT_DAEMON_KEEPALIVE_PID}
            fi
            # make both the daemon and keep-alive as hard to reclaim as possible (correct the pid file first)
            ACTUAL=${'$'}(ps -A -o PID,ARGS 2>/dev/null | grep '[r]oot_daemon${'$'}' | awk '{print ${'$'}1}' | head -1)
            [ -n "${'$'}ACTUAL" ] && echo "${'$'}ACTUAL" > "${'$'}PID"
            for p in $(cat "${'$'}PID" 2>/dev/null) $(cat ${GlassesPaths.ROOT_DAEMON_KEEPALIVE_PID} 2>/dev/null); do
              echo -1000 > /proc/${'$'}p/oom_score_adj 2>/dev/null
            done

            id > ${doneLog.absolutePath}
            echo ${PayloadMarkers.DONE} >> ${doneLog.absolutePath}
        """.trimIndent()
        return RootScript("start_root_daemon", content)
    }

    /** Turn the given colour LED on/off. color is one of white/green/blue/red/all. */
    fun ledColor(color: String, on: Boolean, marker: File?, payloadLog: File): RootScript {
        val value = if (on) "255" else "0"
        val targets = if (color == "all") {
            LED_PATHS.values.toList()
        } else {
            listOf(LED_PATHS[color] ?: LED_PATHS["white"]!!)
        }
        val writes = targets.joinToString("\n") { "echo $value > $it 2>/dev/null" }
        // when turning white (or all) off: cancel the camera-open event (fixes flicker at the root) + hold loop as a safety net
        val hold = if (!on && marker != null && (color == "white" || color == "all")) {
            """
            service call lights_ctrl 9 i32 2 i32 2014 >/dev/null 2>&1
            M=${marker.absolutePath}
            ( while [ -f "${'$'}M" ]; do
                echo 0 > $LED_BRIGHT 2>/dev/null
                sleep 0.2
              done ) >/dev/null 2>&1 &
            """.trimIndent()
        } else {
            ""
        }
        val content = """
            #!/system/bin/sh
            $writes
            $hold
            echo ${PayloadMarkers.DONE} > ${payloadLog.absolutePath}
        """.trimIndent()
        return RootScript("led_${color}_${if (on) "on" else "off"}", content)
    }
}
