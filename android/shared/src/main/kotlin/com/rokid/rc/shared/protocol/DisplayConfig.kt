package com.rokid.rc.shared.protocol

import org.json.JSONObject

/**
 * 眼鏡端儀表板「顯示內容」配置（五類資訊，手機端可遠端設定、眼鏡端持久化）。
 * 預設全顯示（全部 true）；允許全隱藏（只剩摺疊條）。
 */
data class DisplayConfig(
    val conn: Boolean = true,      // 連線狀態（BT/WiFi）
    val root: Boolean = true,      // root/ghostlock 狀態
    val camera: Boolean = true,    // 相機狀態
    val storage: Boolean = true,   // 儲存空間
    val led: Boolean = true        // LED 狀態
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
