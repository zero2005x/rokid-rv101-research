package com.rokid.rc.shared.protocol

import org.json.JSONObject

/**
 * Glasses-side dashboard "displayed content" configuration (five kinds of information; settable remotely from the
 * phone, persisted on the glasses). All shown by default (all true); hiding everything is allowed (leaving only the
 * collapsed bar).
 */
data class DisplayConfig(
    val conn: Boolean = true,      // connection state (BT/WiFi)
    val root: Boolean = true,      // root/ghostlock state
    val camera: Boolean = true,    // camera state
    val storage: Boolean = true,   // storage space
    val led: Boolean = true        // LED state
) {
    fun toJson(): JSONObject = JSONObject()
        .put("conn", conn)
        .put("root", root)
        .put("camera", camera)
        .put("storage", storage)
        .put("led", led)

    companion object {
        fun fromJson(o: JSONObject): DisplayConfig = DisplayConfig(
            conn = o.optBoolean("conn", true),
            root = o.optBoolean("root", true),
            camera = o.optBoolean("camera", true),
            storage = o.optBoolean("storage", true),
            led = o.optBoolean("led", true)
        )
    }
}
