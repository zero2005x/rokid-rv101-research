package com.rokid.rc.phone.ring

import android.content.Context
import android.content.SharedPreferences
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.rokid.rc.phone.R
import com.rokid.rc.phone.Remote
import com.rokid.rc.shared.protocol.DisplayConfig
import com.rokid.rc.shared.ring.RingAction
import com.rokid.rc.shared.ring.RingGesture
import com.rokid.rc.shared.ring.RingGestureDetector
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Ring control (path A: phone-side interception).
 *
 * Once the ring connects to the phone as BLE HID, Android delivers its key/mouse events to the foreground window;
 * [MainActivity] feeds those events in, and this object is responsible for:
 *   1. gesture recognition ([RingGestureDetector])
 *   2. executing a [RingAction] per the mapping table (photo/video/silent/toggle display)
 *   3. providing status text to the UI (latest gesture, counts, errors)
 *
 * Default mapping: single tap=photo, double tap=video toggle, long press=silent toggle, wheel=toggle display.
 * (Changeable from the UI; the key-code learning feature will hook up to prefs later.)
 */
object RingController {

    private const val TAG = "RingController"
    private const val PREFS = "ring_ctrl"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_COUNT = "gesture_count"

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val handler = Handler(Looper.getMainLooper())

    private var prefs: SharedPreferences? = null
    private var recording = false
    private var lastSummary = ""
    private var gestureCount = 0
    private var lastGesture: RingGesture? = null
    private var lastAction: RingAction? = null

    /** For UI observation (RemoteFragment updates the screen after subscribing). */
    var onStateChanged: ((String) -> Unit)? = null

    /** Gesture learning: while non-null, the next key/mouse button only reports (key code, description) and runs no existing mapping. */
    private var learnSink: ((Int, String) -> Unit)? = null

    private const val KEY_MAP_PREFIX = "map_"

    /** Media-key migration flag (runs once). */
    private const val KEY_MEDIA_MIGRATION = "migrated_media_keys_v1"

    /** The device the user selected in the "pair ring" list (single source of truth; used for display and for binding later sources). */
    private const val KEY_APPROVED_MAC = "approved_mac"
    private const val KEY_APPROVED_NAME = "approved_name"

    /** Remember the ring device the user selected. */
    fun setApprovedDevice(context: Context, name: String?, mac: String) {
        ensureInit(context)
        prefs?.edit()?.putString(KEY_APPROVED_MAC, mac)?.putString(KEY_APPROVED_NAME, name)?.apply()
    }

    /** The already selected ring (mac, name); null when never selected. */
    fun approvedDevice(): Pair<String, String?>? {
        val mac = prefs?.getString(KEY_APPROVED_MAC, null) ?: return null
        return mac to prefs?.getString(KEY_APPROVED_NAME, null)
    }

    /** Description of the most recently learned key code (shown in the UI). */
    @Volatile var lastLearned: String? = null
        private set

    val enabled: Boolean get() = prefs?.getBoolean(KEY_ENABLED, true) ?: true
    val count: Int get() = gestureCount
    val summary: String get() = lastSummary

    private val detector = RingGestureDetector(
        onGesture = { g -> handleGesture(g) }
    )

    fun ensureInit(context: Context) {
        if (prefs == null) {
            appContext = context.applicationContext
            prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            gestureCount = prefs?.getInt(KEY_COUNT, 0) ?: 0
            migrateMediaKeyMappings()
        }
    }

    /**
     * One-shot migration (2026-09-13): remove the old custom "media key → video toggle" mappings.
     *
     * These mappings were created earlier by the user in "gesture learning" (e.g. `map_85 → VIDEO_TOGGLE`).
     * Keeping them means one stray swipe starts recording → when recording gets stuck the camera is locked up.
     * Only media-key mappings whose **action is VIDEO_TOGGLE** are removed; other custom ones (e.g. media key → photo) are untouched.
     */
    private fun migrateMediaKeyMappings() {
        val p = prefs ?: return
        if (p.getBoolean(KEY_MEDIA_MIGRATION, false)) return
        val edit = p.edit()
        var removed = 0
        MEDIA_KEYS_PASS_THROUGH.forEach { code ->
            if (p.getString("$KEY_MAP_PREFIX$code", null) == RingAction.VIDEO_TOGGLE.name) {
                edit.remove("$KEY_MAP_PREFIX$code")
                removed++
            }
            if (p.getString("${MOUSE_BUTTON_BASE + code}", null) == RingAction.VIDEO_TOGGLE.name) {
                // in theory this cannot happen (mouse buttons do not take this path), belt and braces
                edit.remove("${MOUSE_BUTTON_BASE + code}")
                removed++
            }
        }
        edit.putBoolean(KEY_MEDIA_MIGRATION, true).apply()
        Log.i(TAG, "媒體鍵遷移完成：移除 $removed 筆「媒體鍵→錄影切換」舊對應（媒體鍵已改為放行）")
    }

    fun setEnabled(context: Context, value: Boolean) {
        ensureInit(context)
        prefs?.edit()?.putBoolean(KEY_ENABLED, value)?.apply()
        // link the background media key: acquire media-key routing when enabled, release it when disabled (handing the media key back to the music app)
        if (value) RingMediaSession.ensureStarted(context) else RingMediaSession.stop()
        detector.reset()
        lastSummary = s(if (value) R.string.ring_state_enabled else R.string.ring_state_disabled)
        onStateChanged?.invoke(lastSummary)
    }

    /** Periodic tick: called by MainActivity every 80 ms, used for long-press and repeated-tap detection. */
    fun tick() {
        if (!enabled) return
        detector.tick()
    }

    /** Start gesture learning: the next key event only reports (key code, description) and takes no action. */
    fun startLearn(onResult: (Int, String) -> Unit) {
        learnSink = onResult
        onStateChanged?.invoke(s(R.string.ring_learn_waiting))
    }

    fun cancelLearn() {
        learnSink = null
    }

    /** Fetch a localized string (appContext is set in ensureInit; returns an empty string when uninitialized, to avoid an NPE). */
    private fun s(resId: Int, vararg args: Any): String =
        appContext?.getString(resId, *args) ?: ""

    private fun describeKey(code: Int): String =
        runCatching { android.view.KeyEvent.keyCodeToString(code) }.getOrDefault("KEYCODE_$code") + " ($code)"

    // ---------------- learnable key-code mappings ----------------

    /** Custom mapping: key code → action (stored in prefs, kept across restarts). */
    fun keyMappings(): Map<Int, RingAction> {
        val p = prefs ?: return emptyMap()
        return p.all.mapNotNull { (k, v) ->
            if (!k.startsWith(KEY_MAP_PREFIX)) return@mapNotNull null
            val code = k.removePrefix(KEY_MAP_PREFIX).toIntOrNull() ?: return@mapNotNull null
            val action = runCatching { RingAction.valueOf(v as String) }.getOrNull() ?: return@mapNotNull null
            code to action
        }.toMap()
    }

    fun setKeyMapping(code: Int, action: RingAction) {
        prefs?.edit()?.putString("$KEY_MAP_PREFIX$code", action.name)?.apply()
        onStateChanged?.invoke(s(R.string.ring_map_set, code.toString(), action.name))
    }

    fun clearKeyMappings() {
        val p = prefs ?: return
        val edit = p.edit()
        keyMappings().keys.forEach { edit.remove("$KEY_MAP_PREFIX$it") }
        edit.apply()
        onStateChanged?.invoke(s(R.string.ring_map_cleared_short))
    }

    /** Keyboard-type events (the ring may send ENTER / media keys…). Returning true means consumed (not passed further down). */
    /**
     * Keys that ring control will intercept (everything else is passed through, otherwise system keys such as BACK/HOME get eaten — hit this in practice).
     * Contents = keys common in ring HID descriptors: confirm, volume, home, F1-F12.
     *
     * ⚠ **Media keys are deliberately not here** (2026-09-13): the "swipe" of rings such as AIVELA sends `MEDIA_NEXT/PREVIOUS`,
     * and a light tap sends `PLAY_PAUSE`. Previously they went through the gesture defaults (double tap → `VIDEO_TOGGLE`), so **one swipe started recording**;
     * once recording got stuck the camera could no longer take photos (see `reports/aivela_ring_connection_failure_2026-09-13.md`).
     * Media keys are now always passed through to the system media controls and no longer take part in gesture resolution; the user can still
     * **explicitly** bind one to an action in "gesture learning" (through the `keyMappings()` route, not the gesture defaults).
     */
    /** Base of mouse buttons in the learnable key-code table (0x1000 + button), to avoid clashing with KeyEvent numbers. */
    const val MOUSE_BUTTON_BASE = 0x1000

    /** Media keys **not intercepted** by default (passed through to the system); also the cleanup list for old mappings. */
    private val MEDIA_KEYS_PASS_THROUGH = setOf(
        android.view.KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
        android.view.KeyEvent.KEYCODE_MEDIA_PLAY,
        android.view.KeyEvent.KEYCODE_MEDIA_PAUSE,
        android.view.KeyEvent.KEYCODE_MEDIA_STOP,
        android.view.KeyEvent.KEYCODE_MEDIA_NEXT,
        android.view.KeyEvent.KEYCODE_MEDIA_PREVIOUS,
        android.view.KeyEvent.KEYCODE_MEDIA_FAST_FORWARD,
        android.view.KeyEvent.KEYCODE_MEDIA_REWIND,
        android.view.KeyEvent.KEYCODE_HEADSETHOOK
    )

    private val INTERESTING_KEYS = setOf(
        android.view.KeyEvent.KEYCODE_ENTER,
        android.view.KeyEvent.KEYCODE_DPAD_CENTER,
        android.view.KeyEvent.KEYCODE_BUTTON_A,
        android.view.KeyEvent.KEYCODE_VOLUME_UP,
        android.view.KeyEvent.KEYCODE_VOLUME_DOWN,
        122,                                                        // KEYCODE_HOMEPAGE (an API 33+ constant; the numeric value is used here)
        131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142  // F1–F12
    )

    fun onKeyDown(code: Int): Boolean {
        if (!enabled) return false
        learnSink?.let { sink ->
            val desc = describeKey(code)
            lastLearned = desc
            learnSink = null
            sink(code, desc)
            return true
        }
        // custom key-code mappings take precedence over the gesture defaults (a learned key code runs the given action directly)
        keyMappings()[code]?.let { action ->
            onCustomKey(action, describeKey(code))
            return true
        }
        if (code !in INTERESTING_KEYS) return false   // pass through: BACK/HOME/other app shortcuts
        detector.onPress(code)
        return true
    }

    /** Run the action of a custom key-code mapping. */
    private fun onCustomKey(action: RingAction, desc: String) {
        gestureCount++
        prefs?.edit()?.putInt(KEY_COUNT, gestureCount)?.apply()
        Log.i(TAG, "自訂鍵碼 $desc → $action（累計 $gestureCount）")
        onStateChanged?.invoke("$desc → …")
        scope.launch {
            val result = runCatching { perform(action) }.getOrElse { s(R.string.ring_err_prefix, it.message ?: "") }
            publish("$desc → $result")
        }
    }

    fun onKeyUp(code: Int): Boolean {
        if (!enabled) return false
        if (code !in INTERESTING_KEYS && keyMappings()[code] == null) return false
        detector.onRelease(code)
        return true
    }

    /** Mouse-type events (the ring is mostly a mouse-style HID): left/right/middle button. */
    fun onMouseButton(action: Int, button: Int): Boolean {
        if (!enabled) return false
        learnSink?.let { sink ->
            if (action == android.view.MotionEvent.ACTION_DOWN) {
                val code = MOUSE_BUTTON_BASE + button   // mouse button offset, to avoid clashing with KeyEvent numbers
                val desc = "MOUSE_BUTTON_$button"
                lastLearned = desc
                learnSink = null
                sink(code, desc)
                return true
            }
            return false
        }
        keyMappings()[MOUSE_BUTTON_BASE + button]?.let { act ->
            if (action == android.view.MotionEvent.ACTION_DOWN) onCustomKey(act, "MOUSE_BUTTON_$button")
            return action == android.view.MotionEvent.ACTION_DOWN
        }
        return when (action) {
            android.view.MotionEvent.ACTION_DOWN -> {
                detector.onPress(button); true
            }
            android.view.MotionEvent.ACTION_UP -> {
                detector.onRelease(button); true
            }
            else -> false
        }
    }

    /** Wheel/swipe. */
    fun onScroll(amount: Int): Boolean {
        if (!enabled) return false
        detector.onScroll(amount)
        return true
    }

    // ---------------- gesture → action ----------------

    /** Gesture mapping table (hardcoded defaults for now; later overridden by the UI/learn mode). */
    private fun actionFor(g: RingGesture): RingAction = when (g) {
        RingGesture.TAP -> RingAction.PHOTO
        RingGesture.DOUBLE_TAP -> RingAction.VIDEO_TOGGLE
        RingGesture.LONG_PRESS -> RingAction.SILENT_TOGGLE
        RingGesture.SCROLL_UP -> RingAction.DISPLAY_NEXT
        RingGesture.SCROLL_DOWN -> RingAction.DISPLAY_PREV
        RingGesture.TRIPLE_TAP -> RingAction.NONE
    }

    private fun handleGesture(g: RingGesture) {
        val action = actionFor(g)
        gestureCount++
        prefs?.edit()?.putInt(KEY_COUNT, gestureCount)?.apply()
        lastGesture = g
        lastAction = action
        Log.i(TAG, "手勢 $g → 動作 $action（累計 $gestureCount）")
        onStateChanged?.invoke("$g → …")
        if (action == RingAction.NONE) {
            publish(s(R.string.ring_no_action, g.name))
            return
        }
        scope.launch {
            val result = runCatching { perform(action) }.getOrElse { s(R.string.ring_err_prefix, it.message ?: "") }
            publish("$g → $result")
        }
    }

    private suspend fun perform(action: RingAction): String {
        // Do not check the connection up front here: Remote's request() calls ensureConnected() internally to reconnect,
        // so blocking beforehand only makes ring gestures fail outright after an "idle disconnect" (hit this in practice).
        return when (action) {
            RingAction.PHOTO -> Remote.capturePhoto().fold(
                onSuccess = { m -> s(R.string.ring_photo_ok, m.fileName) },
                onFailure = { e -> s(R.string.ring_photo_fail, e.message ?: "") }
            )

            RingAction.VIDEO_TOGGLE -> {
                if (!recording) {
                    Remote.startVideo().fold(
                        onSuccess = { recording = true; s(R.string.ring_video_start) },
                        onFailure = { e -> s(R.string.ring_video_fail, e.message ?: "") }
                    )
                } else {
                    Remote.stopVideo().fold(
                        onSuccess = { m -> recording = false; s(R.string.ring_video_saved, m.fileName) },
                        onFailure = { e -> s(R.string.ring_stop_fail, e.message ?: "") }
                    )
                }
            }

            RingAction.SILENT_TOGGLE -> {
                val cur = withContext(Dispatchers.IO) { Remote.status().getOrNull()?.silentEnabled }
                if (cur == null) {
                    s(R.string.ring_status_read_fail)
                } else {
                    Remote.setSilent(!cur).fold(
                        onSuccess = { applied ->
                            if (!cur) s(if (applied) R.string.ring_silent_on else R.string.ring_silent_on_pending)
                            else s(R.string.ring_silent_off)
                        },
                        onFailure = { e -> s(R.string.ring_silent_fail, e.message ?: "") }
                    )
                }
            }

            RingAction.DISPLAY_NEXT, RingAction.DISPLAY_PREV -> {
                val cur = withContext(Dispatchers.IO) { Remote.fetchDisplayConfig().getOrNull() }
                    ?: DisplayConfig()
                val next = cycleDisplay(cur, if (action == RingAction.DISPLAY_NEXT) 1 else -1)
                Remote.setDisplayConfig(next).fold(
                    onSuccess = { s(R.string.ring_display_switched, describeDisplay(next)) },
                    onFailure = { e -> s(R.string.ring_display_fail, e.message ?: "") }
                )
            }

            RingAction.NONE -> "（未指定動作）"
        }
    }

    /** Display info cycle: all → connection+storage → camera+LED → all off. */
    private fun cycleDisplay(cur: DisplayConfig, dir: Int): DisplayConfig {
        val presets = listOf(
            DisplayConfig(),                                                   // all
            DisplayConfig(conn = true, root = false, camera = false, storage = true, led = false),
            DisplayConfig(conn = false, root = false, camera = true, storage = false, led = true),
            DisplayConfig(conn = false, root = false, camera = false, storage = false, led = false)
        )
        val idx = presets.indexOfFirst { it == cur }.let { if (it < 0) 0 else it }
        val next = ((idx + dir) % presets.size + presets.size) % presets.size
        return presets[next]
    }

    private fun describeDisplay(c: DisplayConfig): String {
        val on = buildList {
            if (c.conn) add("連線")
            if (c.root) add("root")
            if (c.camera) add("相機")
            if (c.storage) add("儲存")
            if (c.led) add("指示燈")
        }
        return if (on.isEmpty()) "全部隱藏" else on.joinToString("+")
    }

    private fun publish(msg: String) {
        lastSummary = msg
        Log.i(TAG, "結果：$msg")
        handler.post {
            onStateChanged?.invoke(msg)
            appContext?.let { ctx ->
                runCatching {
                    android.widget.Toast.makeText(ctx, "戒指：$msg", android.widget.Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private var appContext: Context? = null
}
