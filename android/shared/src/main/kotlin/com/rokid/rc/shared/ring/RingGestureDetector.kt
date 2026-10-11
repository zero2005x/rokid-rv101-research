package com.rokid.rc.shared.ring

import android.os.SystemClock

/** 戒指可辨識的手勢。 */
enum class RingGesture {
    TAP,            // 單擊
    DOUBLE_TAP,     // 雙擊
    LONG_PRESS,     // 長按
    TRIPLE_TAP,     // 三擊（部分戒指用於喚醒／切換模式）
    SCROLL_UP,      // 滾輪／滑動向前
    SCROLL_DOWN,    // 滾輪／滑動向後
}

/** 手勢要執行的動作。 */
enum class RingAction {
    PHOTO,          // 拍照
    VIDEO_TOGGLE,   // 錄影開始／停止
    SILENT_TOGGLE,  // 靜音拍攝開關
    DISPLAY_NEXT,   // 切換眼鏡顯示資訊（下一組）
    DISPLAY_PREV,   // 切換眼鏡顯示資訊（上一組）
    NONE,           // 不動作（保留／未指定）
}

/**
 * 手勢辨識器：把「按下／放開／滾輪」事件序列變成 [RingGesture]。
 *
 * 設計要點：
 *  - 戒指（BLE HID）可能以**鍵盤按鍵**或**滑鼠左鍵**的形式送出「點一下」，
 *    兩者都走同一套 press/release 流程。
 *  - 時間參數可調；預設值依一般觸控戒指的體感調整。
 *  - 執行緒：所有回呼都應在同一執行緒（App 主執行緒）呼叫。
 */
class RingGestureDetector(
    private val onGesture: (RingGesture) -> Unit,
    private val tapMaxMs: Long = 400L,        // 按下到放開 < 400ms 視為「點」
    private val doubleGapMs: Long = 450L,     // 兩次點擊間隔 < 450ms 視為雙擊
    private val tripleGapMs: Long = 450L,     // 第三次點擊間隔（三擊）
    private val longPressMs: Long = 700L      // 按住超過 700ms 視為長按
) {

    private var pressAt = 0L
    private var pressed = false
    private var lastTapAt = 0L
    private var tapCount = 0
    private var longPressFired = false

    /** 目前按住的鍵碼（用於忽略其他按鍵的干擾）。 */
    var activeCode: Int = 0
        private set

    /** 按鍵／滑鼠鍵按下。[atMs] 可傳入事件本身的時間（evdev 事件有時間戳，批次解析時才不會被壓縮）。 */
    fun onPress(code: Int, atMs: Long = SystemClock.uptimeMillis()) {
        if (pressed) return
        pressed = true
        activeCode = code
        pressAt = atMs
        longPressFired = false
    }

    /** 按鍵／滑鼠鍵放開。[atMs] 語意同 [onPress]。 */
    fun onRelease(code: Int, atMs: Long = SystemClock.uptimeMillis()) {
        if (!pressed || code != activeCode) return
        pressed = false
        activeCode = 0
        val now = atMs
        val held = now - pressAt
        if (longPressFired) return              // 長按已在計時器觸發
        if (held > tapMaxMs) return             // 按太久但不構成指定長按 → 忽略

        // 連續點擊判定
        if (tapCount > 0 && now - lastTapAt <= if (tapCount >= 2) tripleGapMs else doubleGapMs) {
            tapCount++
        } else {
            tapCount = 1
        }
        lastTapAt = now
    }

    /**
     * 週期性呼叫（建議每 50~100ms）：負責在「按住不放」時觸發長按，
     * 以及把已穩定的點擊數轉成 TAP／DOUBLE_TAP／TRIPLE_TAP。
     */
    fun tick() {
        val now = SystemClock.uptimeMillis()
        if (pressed && !longPressFired && now - pressAt >= longPressMs) {
            longPressFired = true
            onGesture(RingGesture.LONG_PRESS)
            return
        }
        if (tapCount > 0 && !pressed && now - lastTapAt > tripleGapMs) {
            val n = tapCount
            tapCount = 0
            when (n) {
                1 -> onGesture(RingGesture.TAP)
                2 -> onGesture(RingGesture.DOUBLE_TAP)
                else -> onGesture(RingGesture.TRIPLE_TAP)
            }
        }
    }

    /** 滾輪／觸控板滑動。 */
    fun onScroll(amount: Int) {
        if (amount > 0) onGesture(RingGesture.SCROLL_UP) else onGesture(RingGesture.SCROLL_DOWN)
    }

    fun reset() {
        pressed = false
        activeCode = 0
        tapCount = 0
        longPressFired = false
    }
}
