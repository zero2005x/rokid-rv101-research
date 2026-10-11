package com.rokid.rc.phone.ring

import android.content.Context
import android.content.Intent
import android.media.session.MediaSession
import android.util.Log
import android.view.KeyEvent

/**
 * **Background** support for path A: MediaSession media-button interception.
 *
 * Why it is needed: while in the foreground the ring's key events are received by `MainActivity.dispatchKeyEvent`; but
 * when the phone is in a pocket / another app is in the foreground, Android delivers only **media keys** to the "active
 * MediaSession". The ring's (BLE HID) report descriptor already contains
 * `KEY_PLAYPAUSE / KEY_NEXTSONG / KEY_PREVIOUSSONG`, so keeping a resident MediaSession is enough to receive those keys
 * in the background and trigger photo/video/display toggle.
 *
 * **Measured limitation (important)**: Android delivers media keys to the "media button session", whose priority is
 * decided by PlaybackState; when a music app is present on the device (measured: YouTube Music wins even while paused,
 * `dumpsys media_session` showed `Media button session is yt.morphe.noname.exe`), our session does not receive the media
 * keys. Forcibly taking over (pretending STATE_PLAYING) would override the user's music controls, so this project
 * **does not preempt**: the MediaSession is only a best effort for "when no other media session exists".
 * The reliable solution for background control is **glasses-side path B** (root reads evdev directly, independent of the
 * phone being in the foreground or of media keys).
 *
 * Non-media keys (mouse clicks, ENTER) likewise cannot be intercepted while the phone is in the background — that needs
 * an AccessibilityService or root.
 */
object RingMediaSession {

    private const val TAG = "RingMediaSession"
    private var session: MediaSession? = null

    /** Create and enable (idempotent; called by MainActivity at startup). */
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
