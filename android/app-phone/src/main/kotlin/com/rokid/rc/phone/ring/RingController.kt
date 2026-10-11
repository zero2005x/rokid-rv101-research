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
 * 戒指控制（路徑 A：手機端攔截）。
 *
 * 戒指以 BLE HID 連到手機後，Android 會把它的按鍵／滑鼠事件送給前景視窗；
 * [MainActivity] 把這些事件餵進來，這裡負責：
 *   1. 手勢辨識（[RingGestureDetector]）
 *   2. 依對應表執行 [RingAction]（拍照／錄影／靜音／切換顯示）
 *   3. 提供狀態文字給 UI（最近手勢、次數、錯誤）
 *
 * 預設對應：單擊=拍照、雙擊=錄影切換、長按=靜音切換、滾輪=切換顯示。
 * （可由 UI 改；鍵碼學習功能後續接上 prefs。）
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

    /** UI 觀察用（RemoteFragment 訂閱後更新畫面）。 */
    var onStateChanged: ((String) -> Unit)? = null

    /** 手勢學習：非 null 時，下一個按鍵/滑鼠鍵只回報 (鍵碼, 描述)、不執行既有對應。 */
    private var learnSink: ((Int, String) -> Unit)? = null

    private const val KEY_MAP_PREFIX = "map_"

    /** 媒體鍵遷移旗標（只跑一次）。 */
    private const val KEY_MEDIA_MIGRATION = "migrated_media_keys_v1"

    /** 使用者在「配對戒指」清單中選定的裝置（唯一來源；供顯示與後續來源綁定使用）。 */
    private const val KEY_APPROVED_MAC = "approved_mac"
    private const val KEY_APPROVED_NAME = "approved_name"

    /** 記住使用者選定的戒指裝置。 */
    fun setApprovedDevice(context: Context, name: String?, mac: String) {
        ensureInit(context)
        prefs?.edit()?.putString(KEY_APPROVED_MAC, mac)?.putString(KEY_APPROVED_NAME, name)?.apply()
    }

    /** 已選定的戒指（mac, name）；未選過回 null。 */
    fun approvedDevice(): Pair<String, String?>? {
        val mac = prefs?.getString(KEY_APPROVED_MAC, null) ?: return null
        return mac to prefs?.getString(KEY_APPROVED_NAME, null)
    }

    /** 最近一次學到的鍵碼描述（給 UI 顯示）。 */
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
     * 一次性遷移（2026-09-13）：移除「媒體鍵 → 錄影切換」的舊自訂對應。
     *
     * 這些對應是使用者先前在「手勢學習」中建立的（例：`map_85 → VIDEO_TOGGLE`）。
     * 保留它們等於讓一次誤滑就開始錄影 → 錄影卡住時相機會被鎖死。
     * 只移除**動作是 VIDEO_TOGGLE** 的媒體鍵對應，其他自訂（例：媒體鍵 → 拍照）不動。
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
                // 理論上不會發生（滑鼠鍵不走這條），一併保險
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
        // 連動背景媒體鍵：啟用時取得媒體鍵路由，停用時釋放（把媒體鍵還給音樂 App）
        if (value) RingMediaSession.ensureStarted(context) else RingMediaSession.stop()
        detector.reset()
        lastSummary = s(if (value) R.string.ring_state_enabled else R.string.ring_state_disabled)
        onStateChanged?.invoke(lastSummary)
    }

    /** 週期性 tick：由 MainActivity 每 80ms 呼叫，用於長按與連續點擊判定。 */
    fun tick() {
        if (!enabled) return
        detector.tick()
    }

    /** 開始手勢學習：下一個按鍵事件只回報 (鍵碼, 描述)、不動作。 */
    fun startLearn(onResult: (Int, String) -> Unit) {
        learnSink = onResult
        onStateChanged?.invoke(s(R.string.ring_learn_waiting))
    }

    fun cancelLearn() {
        learnSink = null
    }

    /** 取在地化字串（appContext 於 ensureInit 設定；未初始化時回空字串避免 NPE）。 */
    private fun s(resId: Int, vararg args: Any): String =
        appContext?.getString(resId, *args) ?: ""

    private fun describeKey(code: Int): String =
        runCatching { android.view.KeyEvent.keyCodeToString(code) }.getOrDefault("KEYCODE_$code") + " ($code)"

    // ---------------- 可學習的鍵碼對應 ----------------

    /** 自訂對應：鍵碼 → 動作（存 prefs，重啟後保留）。 */
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

    /** 鍵盤類事件（戒指可能送 ENTER／媒體鍵…）。回傳 true 表示已消費（不再往下傳）。 */
    /**
     * 會被戒指控制攔截的鍵（其餘一律放行，否則 BACK／HOME 等系統鍵會被吃掉——實測踩到）。
     * 內容＝戒指 HID 描述元常見的鍵：確認鍵、音量鍵、首頁鍵、F1–F12。
     *
     * ⚠ **媒體鍵刻意不在這裡**（2026-09-13）：AIVELA 等戒指的「滑動」送 `MEDIA_NEXT/PREVIOUS`、
     * 輕觸送 `PLAY_PAUSE`。先前它們走手勢預設（雙擊 → `VIDEO_TOGGLE`），導致**滑一下就開始錄影**；
     * 一旦錄影卡住，相機就再也拍不了照（見 `reports/aivela_ring_connection_failure_2026-09-13.md`）。
     * 現在媒體鍵一律放行給系統媒體控制，不再參與手勢判定；使用者仍可在「手勢學習」中
     * **明確**把它綁到某個動作（走 `keyMappings()` 那條路，不經手勢預設）。
     */
    /** 滑鼠鍵在可學習鍵碼表的基底（0x1000 + button），避免與 KeyEvent 撞號。 */
    const val MOUSE_BUTTON_BASE = 0x1000

    /** 預設**不攔截**（放行給系統）的媒體鍵；同時是舊對應的清理清單。 */
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
        122,                                                        // KEYCODE_HOMEPAGE（API 33+ 常數，這裡用數值）
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
        // 自訂鍵碼對應優先於手勢預設（學習後的鍵碼直接執行指定動作）
        keyMappings()[code]?.let { action ->
            onCustomKey(action, describeKey(code))
            return true
        }
        if (code !in INTERESTING_KEYS) return false   // 放行：BACK／HOME／其他 App 快捷鍵
        detector.onPress(code)
        return true
    }

    /** 執行自訂鍵碼對應的動作。 */
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

    /** 滑鼠類事件（戒指多為滑鼠式 HID）：左／右／中鍵。 */
    fun onMouseButton(action: Int, button: Int): Boolean {
        if (!enabled) return false
        learnSink?.let { sink ->
            if (action == android.view.MotionEvent.ACTION_DOWN) {
                val code = MOUSE_BUTTON_BASE + button   // 滑鼠鍵位移，避免與 KeyEvent 撞號
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

    /** 滾輪／滑動。 */
    fun onScroll(amount: Int): Boolean {
        if (!enabled) return false
        detector.onScroll(amount)
        return true
    }

    // ---------------- 手勢 → 動作 ----------------

    /** 手勢對應表（先寫死預設；之後由 UI／學習模式覆寫）。 */
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
        // 不在這裡事先檢查連線：Remote 的 request() 內部會 ensureConnected() 自動重連，
        // 事前擋掉反而讓「閒置斷線」的戒指手勢直接失敗（實測踩到）。
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

    /** 顯示資訊循環：全部 → 連線+儲存 → 相機+指示燈 → 全部關閉。 */
    private fun cycleDisplay(cur: DisplayConfig, dir: Int): DisplayConfig {
        val presets = listOf(
            DisplayConfig(),                                                   // 全部
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
