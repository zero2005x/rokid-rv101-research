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

/** 眼鏡端全域元件圖（單例，由 [App] 建立）。 */
class AppGraph(val app: Application) {
    val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    /**
     * ghostlock LPE 觸發層選擇：
     *  - SHIZUKU（預設）：Shizuku UserService（shell 身份）直跑 exploit；Shizuku
     *    不可用（server 未啟動/未授權）時自動回退 BRIDGE。
     *  - BRIDGE：既有 shell helper 迴圈（deploy/root_bridge.sh 常駐）。
     *  - DIRECT：App 直連（symlink trick；實測 exploit 以 untrusted_app 執行會
     *    kernel panic，不可用，僅保留供研究）。
     */
    /**
     * 提權引擎：**執行期自動探測**（P1）。
     *
     * 先前是編譯期常數 `USE_ENGINE`，換環境（先跑 `start_shizuku.sh` 或 `root_bridge.sh`）
     * 得改程式重建 APK；現在每次呼叫時探測 Shizuku／bridge helper 並委派，
     * 選錯也能自動換另一條管線重試（見 [AutoGhostlockEngine]）。
     */
    val ghostlock: GhostlockEngine = com.rokid.rc.glasses.ghost.AutoGhostlockEngine(app)
    val media: MediaRepository = MediaRepository(app)
    val led: LedSuppressor = LedSuppressor(app, ghostlock)

    /** WiFi 熱點管理（提權後啟動 SoftAP，供手機以 TCP 傳輸媒體）。 */
    val wifiHotspot = WifiHotspot()

    /** 儀表板顯示配置（手機可遠端設定，持久化）。 */
    val displayConfig = DisplayConfigStore(app)

    /** 靜音拍攝（強制壓制拍照/錄影提示音；需 root）。 */
    val silentMode = SilentMode(app)

    /** 無線 adb：加入開發者網段（A：WifiNetworkSuggestion／B：cmd wifi connect-network）。 */
    val wifiJoin by lazy { com.rokid.rc.glasses.net.WifiJoin(app) }

    /**
     * 開發模式：按需開啟無線 adb（加入網段 → 確認 listener → idle 逾時自動關 → 失敗回退）。
     * 預設 OFF，避免 adb TCP 常駐曝露。
     */
    val devMode by lazy { com.rokid.rc.glasses.net.DevModeController(app) }

    /**
     * 路徑 B：戒指／觸控板控制（root 直讀 evdev，背景也能觸發拍攝）。
     * lazy：真的要用時才建立（未取得 root 時不初始化）。
     */
    val ringControl: RingControlManager by lazy { RingControlManager(app, this, displayConfig) }

    /**
     * root daemon 存活看守（P0）：30 秒一輪檢查，失效時清 stale socket、
     * 在「還有人能觸發提權」的前提下自動重建，並回報需操作者介入的腳本。
     */
    val rootKeeper: com.rokid.rc.glasses.ghost.RootDaemonKeeper by lazy {
        com.rokid.rc.glasses.ghost.RootDaemonKeeper(this)
    }

    init {
        // ghostlock 長時操作的進度，推送到已連線手機
        ghostlock.onProgress = { msg -> pushProgress(msg) }
    }

    /**
     * 提權度量（P2）：由 [acquireRootMetered] 累計，隨 STATUS 回報手機。
     *
     * **持久化於 prefs**：Rokid 會頻繁殺掉本 App，若只放記憶體，計數每次重啟就歸零，
     * 累計成功率就失去意義。
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
     * 取得 root（**唯一入口**，含度量）。所有呼叫端都應走這裡，才能累計 P2 指標。
     */
    suspend fun acquireRootMetered(): ExecResult {
        val engine = (ghostlock as? com.rokid.rc.glasses.ghost.AutoGhostlockEngine)
            ?.lastEngineName ?: "unknown"
        val started = android.os.SystemClock.elapsedRealtime()
        // 快速路徑（root 已存在）不計入「首次即成功」統計，避免美化數字
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

    /** App 共用 prefs（keeper 的看守計數也存這裡）。 */
    val prefs = app.getSharedPreferences("rokid_glasses", Context.MODE_PRIVATE)

    /** root 取得模式：AUTO=開啟即自動跑 LPE；MANUAL=等手機指令（持久化，重啟後保留）。 */
    @Volatile
    var rootMode: RootMode = run {
        val saved = prefs.getString("root_mode", null)
        if (saved != null) RootMode.valueOf(saved) else RootMode.MANUAL
    }
        private set

    /** 設定 root 模式並持久化。 */
    fun setRootMode(mode: RootMode) {
        rootMode = mode
        prefs.edit().putString("root_mode", mode.name).apply()
    }

    /** 隱私指示燈狀態（由手機 SET_LED 設定）。 */
    @Volatile
    var ledOn: Boolean = true

    /** 連線成功後自動關閉隱私指示燈（預設開啟）。 */
    @Volatile
    var autoLedOffOnConnect: Boolean = true

    /** 由 [CaptureService] 建立並註冊。 */
    @Volatile
    var camera: CameraController? = null

    /** 目前連線中的手機（單一連線，藍牙控制通道或 WiFi 媒體通道皆可取代）。 */
    @Volatile
    var connection: ConnectionHandler? = null

    /** 藍牙控制通道是否已連線（儀表板顯示用）。 */
    @Volatile
    var btConnected: Boolean = false

    /** WiFi 媒體通道是否已連線（儀表板顯示用）。 */
    @Volatile
    var wifiConnected: Boolean = false

    /** 推送非請求回應（如錄影完成）給已連線手機。 */
    fun push(json: JSONObject) {
        val c = connection ?: return
        scope.launch { c.sendJson(json.toString()) }
    }

    /**
     * 推送結構化進度給已連線手機（只送代碼＋參數，手機端以自己語系顯示）。
     * 舊欄位 `message` 仍一併帶上（= `code|arg`），方便 logcat 與向後相容。
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
        /** ghostlock 觸發層模式。 */
        enum class Engine { SHIZUKU, BRIDGE, DIRECT }

        // 預設 SHIZUKU（Shizuku 不可用時自動回退 BRIDGE，見建構子）。
        // P1：引擎改為執行期探測（見 ghostlock 屬性）；此列保留僅作為文件說明。
        // private val USE_ENGINE = Engine.SHIZUKU
    }
}
