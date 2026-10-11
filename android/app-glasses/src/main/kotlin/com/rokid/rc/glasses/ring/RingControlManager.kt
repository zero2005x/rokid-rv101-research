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
 * 戒指動作結果：**代碼 + 參數**（不是翻譯後的字串）。手機端以 `ui/RingText.kt` 對到自己的字串。
 */
data class RingOutcome(val key: String, val arg: String? = null)

/** 戒指控制相關的 SharedPreferences 名稱（RingControlManager 與 RingEvdevReader 共用）。 */
object RingPrefs {
    const val NAME = "ring_ctrl"
    const val KEY_READER_OFFSET = "reader_offset"
    const val KEY_PATTERN = "pattern"
}

/**
 * 路徑 B：眼鏡端戒指/觸控板控制（**root 直讀 evdev，背景也能用**）。
 *
 * 流程：root daemon 背景跑 `getevent` → [RingEvdevReader] 輪詢增量 → 手勢辨識 →
 * 直接在本機觸發拍照／錄影／靜音／顯示切換（不經手機）。
 *
 * 手勢預設與手機端一致：單擊=拍照、雙擊=錄影切換、長按=靜音切換、滾輪=顯示循環。
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
     * 最近一次動作的結構化結果（代碼＋參數），供手機端以**自己的語系**組字。
     * 眼鏡端不再組中文句子送出去（見 shared `Status.ringLastKey`）。
     */
    @Volatile var lastKey: String? = null
    @Volatile var lastArg: String? = null

    /** 產生結構化結果。 */
    private fun outcome(key: String, arg: String? = null): RingOutcome = RingOutcome(key, arg)

    private val detector = RingGestureDetector(onGesture = { g -> onGesture(g) })

    /** 啟動：挑裝置（已設定優先，否則自動偵測）→ 背景 getevent → 輪詢。 */
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
             * 已設定的裝置若**不存在**就回退到自動偵測。
             *
             * 為什麼需要（2026-09-13 實測踩到）：`devicePath` 一旦寫入 prefs 就會一直被沿用，
             * 先前為了測 RGR06 把它設成 `/dev/input/event4`，之後戒指解除配對／關機，
             * 這個路徑就變成**指向不存在的裝置**——但程式照用不誤，
             * 結果「眼鏡觸控板怎麼按都不會拍照」，而且**沒有任何錯誤訊息**。
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
     * root 端 keep-alive：Rokid 會積極回收背景 App（實測切到 Launcher 後程序被殺），
     * 程序一死，evdev 的「消費者」就沒了 → 背景手勢失效。
     * 這裡由 root daemon 起一個 shell 迴圈，發現 App 程序不在就以
     * `am start-foreground-service`（root 身份不受背景啟動限制）把它拉回來，
     * App 的 `App.onCreate` 再恢復戒指控制。
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
        // 停止舊的（獨立指令，避免 pkill 自我比對）→ 寫入腳本 → 背景啟動
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
     * 看門狗：App 被系統重啟、getevent 被殺、輪詢意外結束時自動恢復。
     * （Rokid 在切換前景 App 時相當積極回收資源，沒有看門狗會讓背景控制失效。）
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

    /** 由 CaptureService 呼叫：確保戒指控制有在跑（未啟動就啟動）。 */
    fun ensureRunning(skipExisting: Boolean = true) {
        if (!enabled) return
        if (isRunning() && watchdogJob?.isActive == true) return
        Log.i(TAG, "ensureRunning：啟動戒指控制（skipExisting=$skipExisting）")
        start(skipExisting = skipExisting)
    }

    /**
     * 確保 CaptureService（相機的擁有者）在跑。
     * 顯示器休眠時 Android 12 會擋「背景啟動 FGS」，此時退回用 root 啟動。
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
            // 每輪都要 tick：單擊需等「雙擊視窗」過後才成立（只在有事件時 tick 會永遠等不到）
            detector.tick()
            delay(POLL_MS)
        }
        Log.i(TAG, "輪詢結束")
    }

    /** 解析一行 `getevent -lt` 輸出並餵進手勢辨識（含**事件時間戳**，避免批次解析壓縮時序）。 */
    private fun parseLine(line: String) {
        if (!line.contains("EV_")) return
        val label = line.substringAfter(":").trim().split(Regex("\\s+")).getOrNull(1) ?: return
        val value = line.trim().split(Regex("\\s+")).lastOrNull() ?: ""
        // 防護：事件時間戳若超過現在（時鐘來源不一致/人為注入），夾到 now+1s，
        // 否則 lastTapAt 會在未來 → tick() 永遠不成立（實測踩到）。
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

    /** 從 `[   12345.678901] …` 取出事件時間（毫秒）；解析不到時用目前 uptime。 */
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
        // 參數用**機器可讀**的旗標清單（非翻譯字串），由手機端翻成自己的語言
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
