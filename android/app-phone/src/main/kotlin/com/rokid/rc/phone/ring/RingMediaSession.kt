package com.rokid.rc.phone.ring

import android.content.Context
import android.content.Intent
import android.media.session.MediaSession
import android.util.Log
import android.view.KeyEvent

/**
 * 路徑 A 的**背景**支援：MediaSession 媒體鍵攔截。
 *
 * 為什麼需要：戒指的按鍵事件在前景時由 `MainActivity.dispatchKeyEvent` 收到；但手機放口袋／
 * 其他 App 在前景時，Android 只會把**媒體鍵**派送給「作用中的 MediaSession」。
 * 戒指（BLE HID）的報告描述元本來就含 `KEY_PLAYPAUSE / KEY_NEXTSONG / KEY_PREVIOUSSONG`，
 * 因此建立一個常駐 MediaSession 就能在背景收到這些鍵並觸發拍照／錄影／顯示切換。
 *
 * **實測限制（重要）**：Android 會把媒體鍵派送給「media button session」，其優先序由
 * PlaybackState 決定；裝置上有音樂 App 時（實測 YouTube Music 即使暫停仍勝出，
 * `dumpsys media_session` 顯示 `Media button session is yt.morphe.noname.exe`），
 * 我們的 session 收不到媒體鍵。硬搶（假裝 STATE_PLAYING）會蓋掉使用者的音樂控制，
 * 因此本專案**不做搶佔**：MediaSession 只是「沒有其他媒體工作階段時」的盡力而為。
 * 背景控制的可靠解是**眼鏡端路徑 B**（root 直讀 evdev，不依賴手機前景/媒體鍵）。
 *
 * 非媒體鍵（滑鼠點擊、ENTER）在手機背景同樣無法攔截——需要 AccessibilityService 或 root。
 */
object RingMediaSession {

    private const val TAG = "RingMediaSession"
    private var session: MediaSession? = null

    /** 建立並啟用（冪等；由 MainActivity 在啟動時呼叫）。 */
    fun ensureStarted(context: Context) {
        if (session != null) return
        val ctx = context.applicationContext
        val s = MediaSession(ctx, "RokidRingControl")
        s.setCallback(object : MediaSession.Callback() {
            override fun onMediaButtonEvent(mediaButtonIntent: Intent): Boolean {
                val e = mediaButtonIntent.getParcelableExtra<KeyEvent>(Intent.EXTRA_KEY_EVENT)
                if (e == null) {
                    Log.w(TAG, "onMediaButtonEvent 無 KeyEvent")
                    return super.onMediaButtonEvent(mediaButtonIntent)
                }
                val code = e.keyCode
                val handled = when (e.action) {
                    KeyEvent.ACTION_DOWN -> RingController.onKeyDown(code)
                    KeyEvent.ACTION_UP -> RingController.onKeyUp(code)
                    else -> false
                }
                Log.i(TAG, "媒體鍵 $code action=${e.action} handled=$handled")
                return handled || super.onMediaButtonEvent(mediaButtonIntent)
            }
        })
        s.setFlags(MediaSession.FLAG_HANDLES_MEDIA_BUTTONS)
        s.isActive = true
        session = s
        Log.i(TAG, "MediaSession 已啟用（背景媒體鍵：盡力而為，見類別註解）")
    }


    fun stop() {
        runCatching { session?.isActive = false }
        runCatching { session?.release() }
        session = null
    }
}
