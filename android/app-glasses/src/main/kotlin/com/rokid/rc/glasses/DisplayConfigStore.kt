package com.rokid.rc.glasses

import android.content.Context
import android.os.Handler
import android.os.Looper
import com.rokid.rc.shared.protocol.DisplayConfig
import java.util.concurrent.CopyOnWriteArrayList

/**
 * 眼鏡端儀表板顯示配置（手機可遠端設定，持久化於 SharedPreferences）。
 * 預設五類全顯示；允許全隱藏（僅剩摺疊條）。
 * 變更通知一律在主執行緒派發（UI 重繪安全；觸發源可能是 BT 指令的 IO 執行緒
 * 或廣播接收執行緒）。
 */
class DisplayConfigStore(context: Context) {

    private val prefs = context.getSharedPreferences("glasses_display", Context.MODE_PRIVATE)
    private val listeners = CopyOnWriteArrayList<() -> Unit>()
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    var config: DisplayConfig = load()
        private set

    private fun load(): DisplayConfig = DisplayConfig(
        conn = prefs.getBoolean("conn", true),
        root = prefs.getBoolean("root", true),
        camera = prefs.getBoolean("camera", true),
        storage = prefs.getBoolean("storage", true),
        led = prefs.getBoolean("led", true)
    )

    /** 更新配置（任一欄位變化才持久化並通知；通知在主執行緒）。 */
    fun update(new: DisplayConfig) {
        if (new == config) return
        config = new
        prefs.edit()
            .putBoolean("conn", new.conn)
            .putBoolean("root", new.root)
            .putBoolean("camera", new.camera)
            .putBoolean("storage", new.storage)
            .putBoolean("led", new.led)
            .apply()
        listeners.forEach { l ->
            mainHandler.post { runCatching { l() } }
        }
    }

    /** 配置變更時觸發（MainActivity 重繪用）。 */
    fun addListener(l: () -> Unit) {
        listeners.add(l)
    }

    fun removeListener(l: () -> Unit) {
        listeners.remove(l)
    }
}
