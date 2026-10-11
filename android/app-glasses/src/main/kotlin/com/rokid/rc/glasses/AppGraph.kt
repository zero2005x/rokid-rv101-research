package com.rokid.rc.glasses

import android.app.Application
import android.content.Context
import com.rokid.rc.glasses.camera.CameraController
import com.rokid.rc.glasses.ghost.BridgeGhostlockEngine
import com.rokid.rc.glasses.ghost.ExecResult
import com.rokid.rc.glasses.ghost.GhostlockEngine
import com.rokid.rc.glasses.ghost.LedSuppressor
import com.rokid.rc.glasses.ghost.ShellGhostlockEngine
import com.rokid.rc.glasses.ghost.ShizukuGhostlockEngine
import com.rokid.rc.glasses.ghost.SilentMode
import com.rokid.rc.glasses.media.MediaRepository
import com.rokid.rc.glasses.net.ConnectionHandler
import com.rokid.rc.glasses.net.WifiHotspot
import com.rokid.rc.glasses.ring.RingControlManager
import com.rokid.rc.shared.protocol.Msg
import com.rokid.rc.shared.protocol.MsgType
import com.rokid.rc.shared.protocol.RootMode
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import org.json.JSONObject

/** Glasses-side global component graph (singleton, created by [App]). */
class AppGraph(val app: Application) {
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    /**
     * ghostlock LPE trigger layer choice:
     *  - SHIZUKU (default): the Shizuku UserService (shell identity) runs the exploit directly; when Shizuku is
     *    unavailable (server not started / not authorized) it falls back to BRIDGE automatically.
     *  - BRIDGE: the existing shell helper loop (deploy/root_bridge.sh resident).
     *  - DIRECT: the app connects directly (symlink trick; measured: running the exploit as untrusted_app
     *    kernel-panics, so it is unusable and is kept for research only).
     */
    /**
     * Escalation engine: **runtime auto-detection** (P1).
     *
     * It used to be the compile-time constant `USE_ENGINE`, so changing the environment (running `start_shizuku.sh`
     * or `root_bridge.sh` first) meant editing code and rebuilding the APK; now every call probes Shizuku/the bridge
     * helper and delegates, and a wrong pick automatically retries the other pipeline (see [AutoGhostlockEngine]).
     */
    val ghostlock: GhostlockEngine = com.rokid.rc.glasses.ghost.AutoGhostlockEngine(app)
    val media: MediaRepository = MediaRepository(app)
    val led: LedSuppressor = LedSuppressor(app, ghostlock)

    /** WiFi hotspot management (starts the SoftAP after escalation so the phone can transfer media over TCP). */
    val wifiHotspot = WifiHotspot()

    /** Dashboard display configuration (settable remotely from the phone, persisted). */
    val displayConfig = DisplayConfigStore(app)

    /** Silent capture (force-suppress the photo/video notification sound; requires root). */
    val silentMode = SilentMode(app)

    /** Wireless adb: join the developer subnet (A: WifiNetworkSuggestion / B: cmd wifi connect-network). */
    val wifiJoin by lazy { com.rokid.rc.glasses.net.WifiJoin(app) }

    /**
     * Dev mode: enable wireless adb on demand (join the subnet → confirm the listener → auto-off on idle timeout → fallback on failure).
     * OFF by default, to avoid leaving adb TCP exposed at all times.
     */
    val devMode by lazy { com.rokid.rc.glasses.net.DevModeController(app) }

    /**
     * Path B: ring/touchpad control (root reads evdev directly; it can also trigger capture in the background).
     * lazy: created only when actually used (not initialized before root has been acquired).
     */
    val ringControl: RingControlManager by lazy { RingControlManager(app, this, displayConfig) }

    /**
     * root daemon liveness watchdog (P0): checks every 30 seconds, clears the stale socket when it has died,
     * auto-rebuilds as long as "someone can still trigger escalation", and reports the script the operator must run.
     */
    val rootKeeper: com.rokid.rc.glasses.ghost.RootDaemonKeeper by lazy {
        com.rokid.rc.glasses.ghost.RootDaemonKeeper(this)
    }

    init {
        // progress of long ghostlock operations, pushed to the connected phone
        ghostlock.onProgress = { msg -> pushProgress(msg) }
    }

    /**
     * Escalation metrics (P2): accumulated by [acquireRootMetered] and reported to the phone with STATUS.
     *
     * **Persisted in prefs**: Rokid kills this app frequently, so if it lived only in memory the counter would
     * reset on every restart and the cumulative success rate would be meaningless.
     */
    @Volatile
    var rootMetrics: com.rokid.rc.shared.protocol.RootMetrics = loadRootMetrics()
        private set

    private fun loadRootMetrics(): com.rokid.rc.shared.protocol.RootMetrics {
        val p = app.getSharedPreferences("rokid_glasses", Context.MODE_PRIVATE)
        return com.rokid.rc.shared.protocol.RootMetrics(
            lastEngine = p.getString("su_engine", "none") ?: "none",
            lastOk = p.getBoolean("su_last_ok", false),
            lastMs = p.getLong("su_last_ms", 0L),
            attempts = p.getInt("su_attempts", 0),
            successes = p.getInt("su_successes", 0),
            firstTryOk = p.getInt("su_first_try_ok", 0)
        )
    }

    private fun saveRootMetrics(m: com.rokid.rc.shared.protocol.RootMetrics) {
        app.getSharedPreferences("rokid_glasses", Context.MODE_PRIVATE).edit()
            .putString("su_engine", m.lastEngine)
            .putBoolean("su_last_ok", m.lastOk)
            .putLong("su_last_ms", m.lastMs)
            .putInt("su_attempts", m.attempts)
            .putInt("su_successes", m.successes)
            .putInt("su_first_try_ok", m.firstTryOk)
            .apply()
    }

    /**
     * Acquire root (**the only entry point**, metrics included). Every caller should go through here so the P2
     * metrics can accumulate.
     */
    suspend fun acquireRootMetered(): ExecResult {
        val engine = (ghostlock as? com.rokid.rc.glasses.ghost.AutoGhostlockEngine)
            ?.lastEngineName ?: "unknown"
        val started = android.os.SystemClock.elapsedRealtime()
        // the fast path (root already present) is not counted in the "succeeded on the first try" statistic, to avoid flattering the numbers
        val preRooted = com.rokid.rc.glasses.ghost.RootDaemonClient().isRoot()
        val first = rootMetrics.attempts == 0
        val result = ghostlock.acquireRoot()
        val took = android.os.SystemClock.elapsedRealtime() - started
        val ok = result.success || ghostlock.rootAcquired
        val updated = rootMetrics.copy(
            lastEngine = engine,
            lastOk = ok,
            lastMs = took,
            attempts = rootMetrics.attempts + 1,
            successes = rootMetrics.successes + if (ok) 1 else 0,
            firstTryOk = rootMetrics.firstTryOk + if (ok && first && !preRooted) 1 else 0
        )
        rootMetrics = updated
        saveRootMetrics(updated)
        return result
    }

    /** Shared app prefs (the keeper's watchdog counters are stored here too). */
    val prefs = app.getSharedPreferences("rokid_glasses", Context.MODE_PRIVATE)

    /** root acquisition mode: AUTO=run the LPE automatically on start; MANUAL=wait for a phone command (persisted, kept across restarts). */
    @Volatile
    var rootMode: RootMode = run {
        val saved = prefs.getString("root_mode", null)
        if (saved != null) RootMode.valueOf(saved) else RootMode.MANUAL
    }
        private set

    /** Set and persist the root mode. */
    fun setRootMode(mode: RootMode) {
        rootMode = mode
        prefs.edit().putString("root_mode", mode.name).apply()
    }

    /** Privacy indicator LED state (set by the phone through SET_LED). */
    @Volatile
    var ledOn: Boolean = true

    /** Turn the privacy indicator LED off automatically after a successful connection (enabled by default). */
    @Volatile
    var autoLedOffOnConnect: Boolean = true

    /** Created and registered by [CaptureService]. */
    @Volatile
    var camera: CameraController? = null

    /** The currently connected phone (single connection; either the Bluetooth control channel or the WiFi media channel may replace it). */
    @Volatile
    var connection: ConnectionHandler? = null

    /** Whether the Bluetooth control channel is connected (for the dashboard display). */
    @Volatile
    var btConnected: Boolean = false

    /** Whether the WiFi media channel is connected (for the dashboard display). */
    @Volatile
    var wifiConnected: Boolean = false

    /** Push a non-response message (such as recording finished) to the connected phone. */
    fun push(json: JSONObject) {
        val c = connection ?: return
        scope.launch { c.sendJson(json.toString()) }
    }

    /**
     * Push structured progress to the connected phone (codes and arguments only; the phone renders them in its own locale).
     * The old `message` field is still included (= `code|arg`), for logcat and backward compatibility.
     */
    fun pushProgress(event: com.rokid.rc.shared.protocol.ProgressEvent) {
        val encoded = com.rokid.rc.shared.protocol.ProgressCodec.encode(event.code, event.arg)
        push(
            Msg.build(
                MsgType.PROGRESS, 0L,
                "code" to event.code,
                "arg" to (event.arg ?: ""),
                "message" to encoded
            )
        )
    }

    companion object {
        /** ghostlock trigger layer mode. */
        enum class Engine { SHIZUKU, BRIDGE, DIRECT }

        // SHIZUKU by default (it falls back to BRIDGE automatically when Shizuku is unavailable, see the constructor).
        // P1: the engine is now probed at runtime (see the ghostlock property); this line is kept as documentation only.
        // private val USE_ENGINE = Engine.SHIZUKU
    }
}
