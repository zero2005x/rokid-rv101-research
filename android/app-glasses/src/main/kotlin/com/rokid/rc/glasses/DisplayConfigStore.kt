package com.rokid.rc.glasses

import android.content.Context
import android.os.Handler
import android.os.Looper
import com.rokid.rc.shared.protocol.DisplayConfig
import java.util.concurrent.CopyOnWriteArrayList

/**
 * Glasses-side dashboard display configuration (settable remotely from the phone, persisted in SharedPreferences).
 * All five categories are shown by default; hiding everything is allowed (leaving only the collapsed bar).
 * Change notifications are always dispatched on the main thread (safe for UI redraw; the trigger may be the
 * IO thread of a BT command or a broadcast receiver thread).
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

    /** Update the configuration (persist and notify only when a field actually changes; notification is on the main thread). */
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

    /** Fired when the configuration changes (used by MainActivity to redraw). */
    fun addListener(l: () -> Unit) {
        listeners.add(l)
    }

    fun removeListener(l: () -> Unit) {
        listeners.remove(l)
    }
}
