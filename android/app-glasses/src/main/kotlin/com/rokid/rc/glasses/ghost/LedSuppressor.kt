package com.rokid.rc.glasses.ghost

import android.content.Context
import com.rokid.rc.glasses.GlassesPaths
import java.io.File

/**
 * LED suppression coordinator (daemon first).
 *
 * After escalation, if the root daemon is resident (socket exists) every LED command goes straight over the socket (fast);
 * otherwise it falls back to the old "re-run the LPE every time" bridge path.
 * While the marker file exists, the daemon's background loop keeps suppressing the white LED (countering camera-open events).
 */
class LedSuppressor(context: Context, private val engine: GhostlockEngine) {

    private val marker = File(context.filesDir, "led_suppress.flag")
    private val daemon = RootDaemonClient()

    suspend fun suppress() = setLed("white", false)

    suspend fun restore() = setLed("white", true)

    suspend fun setLed(on: Boolean) = setLed("white", on)

    /** Turn the LED of the given colour on/off. color ∈ white/green/blue/red/all. */
    suspend fun setLed(color: String, on: Boolean) {
        if (daemon.isAvailable()) {
            daemonSetLed(color, on)
        } else {
            bridgeSetLed(color, on)
        }
    }

    // ---- daemon path ----

    private suspend fun daemonSetLed(color: String, on: Boolean) {
        val value = if (on) "255" else "0"
        when (color) {
            "white" -> if (on) {
                marker.delete()
                daemon.exec(
                    "echo 255 > /sys/class/leds/white/brightness 2>/dev/null; " +
                        "echo 2 > /sys/class/leds/white/led_current 2>/dev/null; " +
                        // re-send the camera-open event: while the camera is in use, let the system maintain the lighting state itself
                        "service call lights_ctrl 8 i32 2 i32 2014 >/dev/null 2>&1"
                )
            } else {
                marker.writeText("1")
                daemon.exec(
                    // fundamental fix: cancel lights_ctrl's camera-open event (otherwise system_server
                    // force-rewrites brightness=255 and fights the hold-loop, causing flicker during recording —
                    // measured: after cancelling, 15 s/150 samples were all 0, see the update section of RV101_CAMERA_LED_FLASH_MECHANISM.md)
                    "service call lights_ctrl 9 i32 2 i32 2014 >/dev/null 2>&1; " +
                        "echo 0 > /sys/class/leds/white/brightness 2>/dev/null; " +
                        "echo 0 > /sys/class/leds/white/led_current 2>/dev/null; " +
                        "( while [ -f ${marker.absolutePath} ]; do echo 0 > /sys/class/leds/white/brightness 2>/dev/null; sleep 0.2; done ) >/dev/null 2>&1 &"
                )
            }
            "all" -> {
                val colors = "white green blue red"
                val writes = colors.split(" ")
                    .joinToString("; ") { "echo $value > /sys/class/leds/$it/brightness 2>/dev/null" }
                if (on) {
                    marker.delete()
                    daemon.exec(writes)
                } else {
                    marker.writeText("1")
                    daemon.exec(
                        "service call lights_ctrl 9 i32 2 i32 2014 >/dev/null 2>&1; " +
                            "$writes; " +
                            "( while [ -f ${marker.absolutePath} ]; do echo 0 > /sys/class/leds/white/brightness 2>/dev/null; sleep 0.2; done ) >/dev/null 2>&1 &"
                    )
                }
            }
            else -> {
                val path = when (color) {
                    "green" -> GlassesPaths.ledBrightness("green")
                    "blue" -> GlassesPaths.ledBrightness("blue")
                    "red" -> GlassesPaths.ledBrightness("red")
                    else -> GlassesPaths.ledBrightness("white")
                }
                daemon.exec("echo $value > $path 2>/dev/null")
            }
        }
    }

    // ---- fallback: LPE bridge ----

    private suspend fun bridgeSetLed(color: String, on: Boolean) {
        when {
            color == "white" && on -> {
                marker.delete()
                engine.exec(LedPayloads.restore(engine.doneLog()))
            }
            color == "white" && !on -> {
                marker.writeText("1")
                engine.exec(LedPayloads.suppress(marker, engine.doneLog()))
            }
            color == "all" && !on -> {
                marker.writeText("1")
                engine.exec(LedPayloads.ledColor("all", false, marker, engine.doneLog()))
            }
            else -> engine.exec(LedPayloads.ledColor(color, on, null, engine.doneLog()))
        }
    }
}
