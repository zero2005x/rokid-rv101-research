package com.rokid.rc.glasses.ghost

import android.content.Context
import com.rokid.rc.glasses.GlassesPaths
import java.io.File

/**
 * LED 壓制協調器（daemon 優先）。
 *
 * 提權後若 root daemon 常駐（socket 存在），所有 LED 指令直接走 socket（快）；
 * 否則回退到舊的「每次重跑 LPE」bridge 路徑。
 * marker 檔存在期間，daemon 的背景迴圈持續壓制白燈（對抗相機開啟事件）。
 */
class LedSuppressor(context: Context, private val engine: GhostlockEngine) {

    private val marker = File(context.filesDir, "led_suppress.flag")
    private val daemon = RootDaemonClient()

    suspend fun suppress() = setLed("white", false)

    suspend fun restore() = setLed("white", true)

    suspend fun setLed(on: Boolean) = setLed("white", on)

    /** 指定顏色 LED 開/關。color ∈ white/green/blue/red/all。 */
    suspend fun setLed(color: String, on: Boolean) {
        if (daemon.isAvailable()) {
            daemonSetLed(color, on)
        } else {
            bridgeSetLed(color, on)
        }
    }

    // ---- daemon 路徑 ----

    private suspend fun daemonSetLed(color: String, on: Boolean) {
        val value = if (on) "255" else "0"
        when (color) {
            "white" -> if (on) {
                marker.delete()
                daemon.exec(
                    "echo 255 > /sys/class/leds/white/brightness 2>/dev/null; " +
                        "echo 2 > /sys/class/leds/white/led_current 2>/dev/null; " +
                        // 重發 camera-open 事件：相機使用中時讓系統自行維護點燈狀態
                        "service call lights_ctrl 8 i32 2 i32 2014 >/dev/null 2>&1"
                )
            } else {
                marker.writeText("1")
                daemon.exec(
                    // 根治：取消 lights_ctrl 的 camera-open 事件（否則 system_server 會
                    // 強制重寫 brightness=255，與 hold-loop 互搶造成錄影期間閃爍——
                    // 實測取消後 15s/150 樣本全為 0，見 RV101_CAMERA_LED_FLASH_MECHANISM.md 更新節）
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

    // ---- 回退：LPE bridge ----

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
