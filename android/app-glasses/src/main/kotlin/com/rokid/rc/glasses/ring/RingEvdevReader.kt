package com.rokid.rc.glasses.ring

import android.util.Log
import com.rokid.rc.glasses.GlassesPaths
import com.rokid.rc.glasses.ghost.RootDaemonClient
import com.rokid.rc.shared.ring.RingIdentity

/**
 * Path B: the glasses obtain gestures by having **root read evdev directly** (`getevent`).
 *
 * Why getevent instead of a custom native binary:
 *   - the app is untrusted_app and cannot read dev/input nodes, but the root daemon can.
 *   - `getevent` is the device's built-in evdev reader, which avoids shipping an aarch64 binary and its build chain with the APK.
 *   - the daemon's exec returns only when the command finishes, so the approach is: start getevent **in the background**
 *     appending events to `/data/local/tmp/ring_events.log`, and let the app fetch the increment with
 *     `tail -c +offset` at a fixed interval (each exec is short, so event latency ≈ the poll interval).
 *
 * Event format (`getevent -lt`):
 *   `[   12345.678901] /dev/input/event1: EV_KEY       BTN_TOUCH            DOWN`
 */
class RingEvdevReader(
    private val root: RootDaemonClient = RootDaemonClient(),
    private val logPath: String = GlassesPaths.RING_EVENTS_LOG,
    private val pidPath: String = GlassesPaths.RING_READER_PID
) {

    private var offset = 0L
    private var device: String? = null

    /** List the available input devices: returns (path, name). */
    suspend fun listDevices(): List<Pair<String, String>> {
        val out = root.exec("getevent -pl 2>/dev/null | grep -E 'add device|name:'", 20_000L)
        val list = mutableListOf<Pair<String, String>>()
        var pending: String? = null
        out.lineSequence().forEach { line ->
            val l = line.trim()
            when {
                l.startsWith("add device") -> pending = l.substringAfter(":").trim()
                l.startsWith("name:") && pending != null -> {
                    list += pending!! to l.substringAfter("name:").trim().trim('"')
                    pending = null
                }
            }
        }
        return list
    }

    /**
     * Check whether an evdev path **really exists** (the device may have been unpaired/removed).
     *
     * ⚠ `listDevices()` goes through the root daemon (`getevent -pl`); **without root it always returns an empty list**.
     * Treating that "empty list" as "the device does not exist" would **wrongly delete the device the user configured**
     * while there is no root (measured: a rootless start at 19:40 misjudged `/dev/input/event1` as nonexistent and cleared it).
     * So when enumeration fails (empty list) this always returns true — if it cannot be confirmed, leave it alone.
     */
    suspend fun deviceExists(path: String): Boolean {
        val all = listDevices()
        if (all.isEmpty()) return true
        return all.any { it.first == path }
    }

    /** Auto-pick a device by name substring (case-insensitive); returns null when none is found. */
    suspend fun autoDetect(namePattern: String = RingIdentity.DEFAULT_NAME_PATTERN): Pair<String, String>? {
        val all = listDevices()
        return all.firstOrNull { it.second.contains(namePattern, true) }
            ?: all.firstOrNull { it.second.contains(RingIdentity.FALLBACK_NAME_PATTERN, true) }
    }

    /**
     * Start the background getevent (root). Returns true on success.
     *
     * NOTE: **pkill and starting getevent must not be in the same command line** — `pkill -f` would match the
     * command line itself (whose second half contains the literal `getevent -lt`) and kill itself, so everything
     * after it never runs (hit this in practice). Hence: stop with a separate command (the `[g]etevent` bracket trick
     * plus a PID file), then start with another exec.
     */
    /**
     * Start reading.
     *
     * The meaning of [skipExisting] (important when recovering after the app was reclaimed by the system):
     *  - first look at the log file's **last modification time**: if it is within [freshSec] seconds (meaning there
     *    really were gestures just now), continue from **the previously read offset** → a gesture from the moment
     *    before waking up is not lost;
     *  - if the file has been idle for more than [freshSec] seconds, start from the end of the file, to avoid
     *    replaying very old gestures.
     */
    suspend fun start(devicePath: String, skipExisting: Boolean = false, freshSec: Long = 10L): Boolean {
        stop()
        device = devicePath
        val size = root.exec("stat -c %s $logPath 2>/dev/null", 8_000L).trim().toLongOrNull() ?: 0L
        val mtime = root.exec("stat -c %Y $logPath 2>/dev/null", 8_000L).trim().toLongOrNull() ?: 0L
        val now = root.exec("date +%s", 8_000L).trim().toLongOrNull() ?: 0L
        val fresh = mtime > 0 && now > 0 && (now - mtime) <= freshSec
        offset = when {
            !skipExisting -> 0L
            fresh -> savedOffset().coerceAtMost(size)   // continue reading: may pick up a gesture that was just missed
            else -> size                                // too old: skip
        }
        Log.i(TAG, "start($devicePath) skipExisting=$skipExisting fresh=$fresh size=$size offset=$offset")
        val launch = "rm -f $logPath ; " +
            "setsid sh -c 'getevent -lt $devicePath >> $logPath 2>&1' >/dev/null 2>&1 & " +
            "echo \$! > $pidPath"
        root.exec(launch, 15_000L)
        kotlinx.coroutines.delay(500)
        val running = root.exec("pgrep -f '[g]etevent' | wc -l", 10_000L).trim().toIntOrNull() ?: 0
        val ok = running > 0
        Log.i(TAG, "start($devicePath) getevent 執行中=$ok")
        return ok
    }

    suspend fun stop() {
        root.exec("kill \$(cat $pidPath) 2>/dev/null ; pkill -f '[g]etevent' ; rm -f $pidPath", 10_000L)
        device = null
    }

    fun isRunning(): Boolean = device != null

    private var prefs: android.content.SharedPreferences? = null

    fun attach(context: android.content.Context) {
        prefs = context.applicationContext.getSharedPreferences(com.rokid.rc.glasses.ring.RingPrefs.NAME, android.content.Context.MODE_PRIVATE)
    }

    private fun savedOffset(): Long = prefs?.getLong(KEY_OFFSET, 0L) ?: 0L

    private fun saveOffset(v: Long) {
        prefs?.edit()?.putLong(KEY_OFFSET, v)?.apply()
    }

    /** Ask the root daemon directly: is the background getevent still alive (used by the watchdog). */
    suspend fun isAlive(): Boolean =
        (root.exec("pgrep -f '[g]etevent' | wc -l", 8_000L).trim().toIntOrNull() ?: 0) > 0

    /**
     * Fetch the event lines new since the previous call (at most [maxBytes]).
     * Returns an empty list when nothing can be read.
     */
    suspend fun pollNew(maxBytes: Int = 8192): List<String> {
        // guard: if the log file was recreated (a new start) while the offset still holds the old value, tail would never read anything.
        // Compare the file size every round; if the offset is out of range, reset it to zero and re-read.
        val size = root.exec("stat -c %s $logPath 2>/dev/null", 8_000L).trim().toLongOrNull() ?: 0L
        if (offset > size) {
            Log.w(TAG, "位移越界（offset=$offset > size=$size）→ 歸零重讀")
            offset = 0L
        }
        val out = root.exec("tail -c +${offset + 1} $logPath 2>/dev/null | head -c $maxBytes", 10_000L)
        if (out.isEmpty()) return emptyList()
        offset += out.toByteArray().size
        saveOffset(offset)
        return out.lineSequence().filter { it.isNotBlank() }.toList()
    }

    companion object {
        private const val TAG = "RingEvdevReader"
        private const val KEY_OFFSET = "reader_offset"

        /** The reader hands only the labels that would count as a tap to the gesture machine; it reports the rest as raw labels. */
        val TAP_LIKE = setOf(
            "BTN_TOUCH", "BTN_LEFT", "BTN_MOUSE", "BTN_RIGHT", "BTN_MIDDLE",
            "BTN_0", "BTN_1", "BTN_TRIGGER",
            "KEY_ENTER", "KEY_KPENTER", "KEY_SELECT", "KEY_OK",
            "KEY_PLAYPAUSE", "KEY_HOMEPAGE", "KEY_BACK", "KEY_FORWARD",
            "KEY_VOLUMEUP", "KEY_VOLUMEDOWN"
        )
    }
}
