package com.rokid.rc.glasses.ring

import android.content.Context
import com.rokid.rc.glasses.GlassesPaths
import android.util.Log
import com.rokid.rc.glasses.AppGraph
import com.rokid.rc.glasses.DisplayConfigStore
import com.rokid.rc.glasses.ghost.RootDaemonClient
import com.rokid.rc.shared.protocol.DisplayConfig
import com.rokid.rc.shared.ring.RingGesture
import com.rokid.rc.shared.ring.RingIdentity
import com.rokid.rc.shared.ring.RingGestureDetector
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Ring action result: **code + arguments** (not a translated string). The phone maps it to its own strings in `ui/RingText.kt`.
 */
data class RingOutcome(val key: String, val arg: String? = null)

/** SharedPreferences name for ring control (shared by RingControlManager and RingEvdevReader). */
object RingPrefs {
    const val NAME = "ring_ctrl"
    const val KEY_READER_OFFSET = "reader_offset"
    const val KEY_PATTERN = "pattern"
}

/**
 * Path B: glasses-side ring/touchpad control (**root reads evdev directly, so it works in the background too**).
 *
 * Flow: the root daemon runs `getevent` in the background → [RingEvdevReader] polls the increments → gesture recognition →
 * triggers photo/video/silent/display toggle locally (without going through the phone).
 *
 * The gesture defaults match the phone side: single tap=photo, double tap=video toggle, long press=silent toggle, wheel=display cycle.
 */
class RingControlManager(
    private val context: Context,
    private val graph: AppGraph,
    private val display: DisplayConfigStore
) {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val reader = RingEvdevReader(RootDaemonClient())
    private var pollJob: Job? = null
    private var watchdogJob: Job? = null
    @Volatile private var running = false
    private var recording = false

    private val prefs = context.getSharedPreferences(RingPrefs.NAME, Context.MODE_PRIVATE)

    init {
        reader.attach(context)
    }

    var enabled: Boolean
        get() = prefs.getBoolean(KEY_ENABLED, true)
        set(v) {
            prefs.edit().putBoolean(KEY_ENABLED, v).apply()
        }

    var devicePath: String?
        get() = prefs.getString(KEY_DEVICE, null)
        set(v) {
            prefs.edit().putString(KEY_DEVICE, v).apply()
        }

    /**
     * Structured result of the latest action (code + argument), so the phone can compose the text in **its own locale**.
     * The glasses side no longer composes Chinese sentences to send out (see shared `Status.ringLastKey`).
     */
    @Volatile var lastKey: String? = null
    @Volatile var lastArg: String? = null

    /** Build a structured result. */
    private fun outcome(key: String, arg: String? = null): RingOutcome = RingOutcome(key, arg)

    private val detector = RingGestureDetector(onGesture = { g -> onGesture(g) })

    /** Start: pick a device (the configured one takes priority, otherwise auto-detect) → background getevent → poll. */
    fun start(
        namePattern: String = prefs.getString(KEY_PATTERN, RingIdentity.DEFAULT_NAME_PATTERN)
            ?: RingIdentity.DEFAULT_NAME_PATTERN,
        skipExisting: Boolean = false
    ): Boolean {
        prefs.edit().putString(KEY_PATTERN, namePattern).apply()
        if (pollJob?.isActive == true) {
            Log.i(TAG, "已在執行中")
            return true
        }
        ensureCaptureService()
        var ok = false
        kotlinx.coroutines.runBlocking {
            /*
             * If the configured device **does not exist**, fall back to auto-detection.
             *
             * Why this is needed (measured 2026-09-13): once `devicePath` is written to prefs it is reused forever;
             * it had been set to `/dev/input/event4` in order to test an RGR06, and after the ring was unpaired /
             * powered off that path became **a pointer to a nonexistent device** — yet the code kept using it
             * regardless, so "no matter how the glasses touchpad was pressed, no photo was taken", with **no error
             * message at all**.
             */
            val stored = devicePath?.takeIf { it.isNotBlank() }
            val storedOk = stored != null && reader.deviceExists(stored)
            if (stored != null && !storedOk) {
                Log.w(TAG, "已設定的裝置不存在（$stored）→ 改為自動偵測（pattern=$namePattern）")
                devicePath = null
            }
            val dev = (if (storedOk) stored else null)
                ?: reader.autoDetect(namePattern)?.first
            if (dev == null) {
                lastKey = "no_device"
                lastArg = namePattern
                Log.w(TAG, "no input device found (pattern=$namePattern)")
                return@runBlocking
            }
            devicePath = dev
            ok = reader.start(dev, skipExisting)
            lastKey = if (ok) "running" else "start_failed"
            lastArg = dev
        }
        if (!ok) return false
        running = true
        pollJob = scope.launch { pollLoop() }
        startWatchdog()
        scope.launch { startKeepAlive() }
        return true
    }

    /**
     * root-side keep-alive: Rokid aggressively reclaims background apps (measured: the process was killed after
     * switching to the Launcher), and once the process dies the evdev "consumer" is gone → background gestures stop working.
     * Here the root daemon starts a shell loop that, when the app process is missing, pulls it back with
     * `am start-foreground-service` (as root, which is not subject to background-start restrictions),
     * and the app's `App.onCreate` then restores ring control.
     */
    private suspend fun startKeepAlive() {
        val script = KEEPALIVE_PATH
        val body = "#!/system/bin/sh\n" +
            "while true; do\n" +
            "  if ! pgrep -f com.rokid.rc.glasses >/dev/null 2>&1; then\n" +
            "    am start-foreground-service -n com.rokid.rc.glasses/.CaptureService >/dev/null 2>&1\n" +
            "  fi\n" +
            "  sleep 15\n" +
            "done\n"
        val client = RootDaemonClient()
        // stop the old one (a separate command, so pkill does not match itself) → write the script → start it in the background
        client.exec("kill \$(cat $KEEPALIVE_PID) 2>/dev/null ; pkill -f '[r]ing_keepalive' ; rm -f $KEEPALIVE_PID", 10_000L)
        client.exec("printf '$body' > $script ; chmod 755 $script", 10_000L)
        client.exec("setsid sh $script >/dev/null 2>&1 & echo \$! > $KEEPALIVE_PID", 10_000L)
        kotlinx.coroutines.delay(300)
        val alive = client.exec("pgrep -f '[r]ing_keepalive' | wc -l", 8_000L).trim()
        Log.i(TAG, "keep-alive 啟動：$alive")
    }

    private suspend fun stopKeepAlive() {
        RootDaemonClient().exec(
            "kill \$(cat $KEEPALIVE_PID) 2>/dev/null ; pkill -f '[r]ing_keepalive' ; rm -f $KEEPALIVE_PID",
            10_000L
        )
    }

    /**
     * Watchdog: recovers automatically when the app is restarted by the system, getevent is killed, or the poll
     * loop ends unexpectedly. (Rokid reclaims resources quite aggressively when the foreground app changes; without
     * a watchdog, background control would stop working.)
     */
    private fun startWatchdog() {
        if (watchdogJob?.isActive == true) return
        watchdogJob = scope.launch {
            while (true) {
                delay(WATCHDOG_MS)
                if (!enabled) continue
                val alive = runCatching { reader.isAlive() }.getOrDefault(false)
                if (!running || pollJob?.isActive != true || !alive) {
                    Log.w(TAG, "看門狗：恢復戒指控制（running=$running poll=${pollJob?.isActive} getevent=$alive）")
                    running = false
                    pollJob?.cancel()
                    val dev = devicePath ?: reader.autoDetect()?.first
                    if (dev != null && reader.start(dev, skipExisting = false)) {
                        devicePath = dev
                        running = true
                        pollJob = scope.launch { pollLoop() }
                        Log.i(TAG, "看門狗：已恢復（$dev）")
                    } else {
                        Log.w(TAG, "看門狗：恢復失敗（device=$dev）")
                    }
                }
            }
        }
    }

    /** Called by CaptureService: make sure ring control is running (start it if it is not). */
    fun ensureRunning(skipExisting: Boolean = true) {
        if (!enabled) return
        if (isRunning() && watchdogJob?.isActive == true) return
        Log.i(TAG, "ensureRunning：啟動戒指控制（skipExisting=$skipExisting）")
        start(skipExisting = skipExisting)
    }

    /**
     * Make sure CaptureService (the owner of the camera) is running.
     * While the display is asleep Android 12 blocks a "background FGS start", in which case this falls back to starting it as root.
     */
    private fun ensureCaptureService() {
        if (graph.camera != null) return
        val intent = android.content.Intent(context, com.rokid.rc.glasses.CaptureService::class.java)
        val okDirect = runCatching {
            androidx.core.content.ContextCompat.startForegroundService(context, intent); true
        }.getOrDefault(false)
        if (okDirect) {
            Log.i(TAG, "已請求啟動 CaptureService")
            return
        }
        scope.launch {
            val out = RootDaemonClient().exec(
                "am start-foreground-service -n com.rokid.rc.glasses/.CaptureService", 15_000L
            )
            Log.i(TAG, "以 root 啟動 CaptureService：${out.trim().replace('\n', '|')}")
        }
    }

    fun stop() {
        running = false
        pollJob?.cancel()
        pollJob = null
        watchdogJob?.cancel()
        watchdogJob = null
        kotlinx.coroutines.runBlocking {
            stopKeepAlive()
            reader.stop()
        }
        lastKey = "stopped"
    }

    fun isRunning(): Boolean = running

    private suspend fun pollLoop() {
        Log.i(TAG, "開始輪詢 evdev 事件（每 ${POLL_MS}ms）")
        while (running) {
            val lines = runCatching { reader.pollNew() }.getOrElse { emptyList() }
            if (lines.isNotEmpty()) lines.forEach { parseLine(it) }
            // tick every round: a single tap only settles after the "double-tap window" has passed (ticking only on events would wait forever)
            detector.tick()
            delay(POLL_MS)
        }
        Log.i(TAG, "輪詢結束")
    }

    /** Parse one line of `getevent -lt` output and feed it to gesture recognition (including the **event timestamp**, so batch parsing does not compress the timing). */
    private fun parseLine(line: String) {
        if (!line.contains("EV_")) return
        val label = line.substringAfter(":").trim().split(Regex("\\s+")).getOrNull(1) ?: return
        val value = line.trim().split(Regex("\\s+")).lastOrNull() ?: ""
        // guard: if the event timestamp is beyond now (inconsistent clock source / manual injection), clamp it to now+1s,
        // otherwise lastTapAt ends up in the future → tick() never settles (hit this in practice).
        val atMs = eventTimeMs(line).coerceAtMost(android.os.SystemClock.uptimeMillis() + 1000L)
        when {
            line.contains("EV_KEY") -> {
                if (label in RingEvdevReader.TAP_LIKE) {
                    when (value) {
                        "DOWN" -> detector.onPress(label.hashCode(), atMs)
                        "UP" -> detector.onRelease(label.hashCode(), atMs)
                    }
                }
            }
            line.contains("EV_REL") && (label == "REL_WHEEL" || label == "REL_HWHEEL") -> {
                val v = value.toIntOrNull() ?: 0
                if (v != 0) detector.onScroll(v)
            }
        }
    }

    /** Extract the event time (ms) from `[   12345.678901] …`; when it cannot be parsed, use the current uptime. */
    private fun eventTimeMs(line: String): Long {
        val m = Regex("\\[\\s*([0-9]+)\\.([0-9]+)\\]").find(line) ?: return android.os.SystemClock.uptimeMillis()
        val sec = m.groupValues[1].toLongOrNull() ?: return android.os.SystemClock.uptimeMillis()
        val frac = m.groupValues[2].padEnd(3, '0').take(3).toLongOrNull() ?: 0L
        return sec * 1000L + frac
    }

    private fun onGesture(g: RingGesture) {
        scope.launch {
            val r = when (g) {
                RingGesture.TAP -> takePhoto()
                RingGesture.DOUBLE_TAP -> toggleVideo()
                RingGesture.LONG_PRESS -> toggleSilent()
                RingGesture.SCROLL_UP -> cycleDisplay(1)
                RingGesture.SCROLL_DOWN -> cycleDisplay(-1)
                RingGesture.TRIPLE_TAP -> outcome("no_action", g.name)
            }
            lastKey = r.key
            lastArg = r.arg
            val argText = if (r.arg != null) " (" + r.arg + ")" else ""
            Log.i(TAG, "gesture " + g.name + " -> " + r.key + argText)
        }
    }

    private suspend fun takePhoto(): RingOutcome {
        val cam = graph.camera ?: return outcome("camera_not_ready")
        return cam.takePhoto(graph.ghostlock.isEnabled).fold(
            onSuccess = { outcome("photo_ok", it.fileName) },
            onFailure = { e -> outcome("photo_fail", e.message) }
        )
    }

    private suspend fun toggleVideo(): RingOutcome {
        val cam = graph.camera ?: return outcome("camera_not_ready")
        return if (!recording) {
            cam.startVideo(graph.ghostlock.isEnabled).fold(
                onSuccess = { recording = true; outcome("video_start") },
                onFailure = { e -> outcome("video_fail", e.message) }
            )
        } else {
            runCatching {
                val item = cam.stopVideo()
                recording = false
                outcome("video_saved", item.fileName)
            }.getOrElse { e ->
                recording = false
                outcome("stop_fail", e.message)
            }
        }
    }

    private suspend fun toggleSilent(): RingOutcome {
        val next = !graph.silentMode.enabled
        val ok = graph.silentMode.setEnabled(next)
        return when {
            next && ok -> outcome("silent_on")
            next -> outcome("silent_on_pending")
            else -> outcome("silent_off")
        }
    }

    private fun cycleDisplay(dir: Int): RingOutcome {
        val presets = listOf(
            DisplayConfig(),
            DisplayConfig(conn = true, root = false, camera = false, storage = true, led = false),
            DisplayConfig(conn = false, root = false, camera = true, storage = false, led = true),
            DisplayConfig(conn = false, root = false, camera = false, storage = false, led = false)
        )
        val cur = display.config
        val idx = presets.indexOfFirst { it == cur }.let { if (it < 0) 0 else it }
        val next = presets[((idx + dir) % presets.size + presets.size) % presets.size]
        display.update(next)
        // the argument is a **machine-readable** flag list (not translated strings); the phone renders it into its own language
        val flags = buildList {
            if (next.conn) add("conn")
            if (next.root) add("root")
            if (next.camera) add("camera")
            if (next.storage) add("storage")
            if (next.led) add("led")
        }
        return outcome("display_switched", flags.joinToString(","))
    }

    companion object {
        private const val TAG = "RingControlManager"
        private const val KEY_ENABLED = "enabled"
        private const val KEY_DEVICE = "device"
        private const val KEY_PATTERN = "pattern"
        private const val POLL_MS = 400L
        private const val WATCHDOG_MS = 30_000L
        private const val KEEPALIVE_PATH = GlassesPaths.RING_KEEPALIVE_SH
        private const val KEEPALIVE_PID = GlassesPaths.RING_KEEPALIVE_PID
    }
}
