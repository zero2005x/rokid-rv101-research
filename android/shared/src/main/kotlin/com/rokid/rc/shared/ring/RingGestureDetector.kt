package com.rokid.rc.shared.ring

import android.os.SystemClock

/** Gestures the ring can recognize. */
enum class RingGesture {
    TAP,            // single tap
    DOUBLE_TAP,     // double tap
    LONG_PRESS,     // long press
    TRIPLE_TAP,     // triple tap (some rings use it to wake up / switch mode)
    SCROLL_UP,      // wheel/swipe forward
    SCROLL_DOWN,    // wheel/swipe backward
}

/** The action a gesture performs. */
enum class RingAction {
    PHOTO,          // take a photo
    VIDEO_TOGGLE,   // start/stop recording
    SILENT_TOGGLE,  // silent capture switch
    DISPLAY_NEXT,   // switch the glasses display info (next group)
    DISPLAY_PREV,   // switch the glasses display info (previous group)
    NONE,           // no action (reserved/unspecified)
}

/**
 * Gesture recognizer: turns a sequence of "press/release/scroll" events into a [RingGesture].
 *
 * Design points:
 *  - The ring (BLE HID) may send a "tap" either as a **keyboard key** or as a **left mouse button**;
 *    both go through the same press/release flow.
 *  - The timing parameters are tunable; the defaults are adjusted to how ordinary touch rings feel.
 *  - Threading: every callback should be invoked on the same thread (the app main thread).
 */
class RingGestureDetector(
    private val onGesture: (RingGesture) -> Unit,
    private val tapMaxMs: Long = 400L,        // press-to-release < 400ms counts as a "tap"
    private val doubleGapMs: Long = 450L,     // interval between two taps < 450ms counts as a double tap
    private val tripleGapMs: Long = 450L,     // interval to the third tap (triple tap)
    private val longPressMs: Long = 700L      // holding longer than 700ms counts as a long press
) {

    private var pressAt = 0L
    private var pressed = false
    private var lastTapAt = 0L
    private var tapCount = 0
    private var longPressFired = false

    /** Key code currently held (used to ignore interference from other keys). */
    var activeCode: Int = 0
        private set

    /** Key/mouse button pressed. [atMs] can carry the event's own time (evdev events have timestamps, so batch parsing does not compress them). */
    fun onPress(code: Int, atMs: Long = SystemClock.uptimeMillis()) {
        if (pressed) return
        pressed = true
        activeCode = code
        pressAt = atMs
        longPressFired = false
    }

    /** Key/mouse button released. [atMs] has the same meaning as in [onPress]. */
    fun onRelease(code: Int, atMs: Long = SystemClock.uptimeMillis()) {
        if (!pressed || code != activeCode) return
        pressed = false
        activeCode = 0
        val now = atMs
        val held = now - pressAt
        if (longPressFired) return              // the long press already fired on the timer
        if (held > tapMaxMs) return             // held too long but not enough for the designated long press → ignore

        // consecutive-tap detection
        if (tapCount > 0 && now - lastTapAt <= if (tapCount >= 2) tripleGapMs else doubleGapMs) {
            tapCount++
        } else {
            tapCount = 1
        }
        lastTapAt = now
    }

    /**
     * Called periodically (every 50-100 ms is recommended): fires the long press while a key is held down,
     * and converts a settled tap count into TAP/DOUBLE_TAP/TRIPLE_TAP.
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

    /** Wheel/touchpad swipe. */
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
