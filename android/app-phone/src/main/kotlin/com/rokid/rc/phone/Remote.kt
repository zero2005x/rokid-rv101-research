package com.rokid.rc.phone

import com.rokid.rc.phone.net.BluetoothClient
import com.rokid.rc.phone.net.LinkClient
import com.rokid.rc.phone.net.TcpClient
import com.rokid.rc.phone.net.WifiLink
import com.rokid.rc.phone.util.LocalStore

import android.bluetooth.BluetoothDevice
import android.content.Context
import com.rokid.rc.shared.protocol.Album
import com.rokid.rc.shared.protocol.DisplayConfig
import com.rokid.rc.shared.protocol.MediaItem
import com.rokid.rc.shared.protocol.Msg
import com.rokid.rc.shared.protocol.MsgType
import com.rokid.rc.shared.protocol.RootMode
import com.rokid.rc.shared.protocol.Status
import com.rokid.rc.shared.protocol.WifiApInfo
import java.io.File
import java.io.IOException
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject

/**
 * WiFi 媒體通道建立失敗（帶原因代碼；UI 依 code 顯示在地化訊息，
 * 訊息本文僅供 logcat 除錯，不直接顯示給使用者）。
 */
class WifiSetupException(
    val code: Code,
    message: String? = null,
    cause: Throwable? = null
) : IOException(message, cause) {
    enum class Code { AP_TIMEOUT, AP_NOT_READY, AP_JOIN_MANUAL, TCP, NOT_CONNECTED }
}

/** 手機端遠端 API 封裝（單例）。 */
object Remote {
    lateinit var appContext: Context
        private set

    val client = BluetoothClient()
    var device: BluetoothDevice? = null
        private set

    /**
     * WiFi 通道（控制指令與媒體**都**優先走 WiFi、藍牙回退）。
     *
     * 注意：眼鏡端 BluetoothServer 與 WifiServer 在 accept 新連線時都會 `graph.connection?.close()`，
     * 兩條通道互斥、「後連上者勝」。因此只要有任何一般指令走藍牙重連，就會把 WiFi socket 踢掉，
     * 媒體便默默退回藍牙（實測 10 秒影片傳輸極慢的根因）。
     */
    val tcp = TcpClient()
    val wifiLink: WifiLink by lazy { WifiLink(appContext) }

    /** 任一通道可用即視為已連線（WiFi 連上時眼鏡會主動關閉藍牙，不可只看藍牙）。 */
    val isConnected: Boolean get() = client.connected || tcp.connected
    val wifiMode: Boolean get() = tcp.connected

    private const val TAG = "Remote"
    private const val PREF = "rc_phone"
    private const val KEY_AUTO_WIFI = "auto_wifi_media"

    /** 媒體前自動建立 WiFi 通道（預設開啟；可由設定頁「WiFi 傳輸」開關調整）。 */
    @Volatile
    var autoWifi: Boolean = true
        private set

    /** 自動建立失敗後的冷卻，避免每次操作都彈系統加入視窗（鎖內外都會存取）。 */
    @Volatile
    private var lastAutoWifiFailAt = 0L
    private const val AUTO_WIFI_COOLDOWN_MS = 60_000L

    /** 自動模式下熱點啟動等待次數（5s/次）；比手動模式短，避免 UI 卡住過久。 */
    private const val AUTO_AP_TRIES = 6

    private val wifiEnsureLock = Mutex()

    /**
     * 正在建立 WiFi 媒體通道（開熱點／加入 AP／連 TCP）。
     *
     * UI 用它在「還沒連上」的幾秒內顯示「WiFi 連線中…」，而不是顯示成「藍牙」——
     * 否則使用者下載影片時看到的是藍牙，但其實幾秒後就跑在 WiFi 上（傳輸很快、標示卻相反）。
     */
    @Volatile
    var wifiNegotiating: Boolean = false
        private set

    /** 傳輸通道狀態變化（開始協商／連上／失敗／斷線）→ UI 應重新整理。可能來自背景執行緒。 */
    @Volatile
    var onTransportChanged: (() -> Unit)? = null

    /**
     * 眼鏡主動推送的訊息（`PROGRESS` 等）。
     *
     * **必須兩條通道都註冊**：眼鏡端 `AppGraph.push()` 是用「當前連線」推送的，
     * 只註冊藍牙會讓 WiFi 成為當前通道時推播全部被丟棄（LPE 進度即失效）。
     */
    @Volatile
    var onPush: ((JSONObject) -> Unit)? = null
        set(value) {
            field = value
            client.onPush = value
            tcp.onPush = value
        }

    private fun notifyTransportChanged() {
        runCatching { onTransportChanged?.invoke() }
    }

    fun ensureInit(context: Context) {
        if (!::appContext.isInitialized) {
            appContext = context.applicationContext
            autoWifi = appContext.getSharedPreferences(PREF, Context.MODE_PRIVATE)
                .getBoolean(KEY_AUTO_WIFI, true)
            // 集中在此註冊：任何通道連上或斷線都要更新 UI（原本只有「斷線」會更新，
            // 導致 WiFi 自動建立後畫面仍停在「傳輸：藍牙」）
            client.onConnected = { notifyTransportChanged() }
            client.onDisconnected = { notifyTransportChanged() }
            tcp.onConnected = { notifyTransportChanged() }
            tcp.onDisconnected = { notifyTransportChanged() }
        }
    }

    /** 設定頁「WiFi 傳輸」開關：記錄偏好（下一次媒體操作即自動套用）。 */
    fun setAutoWifi(enabled: Boolean) {
        autoWifi = enabled
        if (::appContext.isInitialized) {
            appContext.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit()
                .putBoolean(KEY_AUTO_WIFI, enabled).apply()
        }
    }

    suspend fun connect(device: BluetoothDevice): Result<Unit> {
        this.device = device
        return client.connect(device)
    }

    fun disconnect() {
        client.disconnect()
        tcp.disconnect()
    }

    /**
     * 目前可用的控制通道：WiFi 優先、藍牙回退。
     *
     * 一般指令也要走 WiFi，否則藍牙重連會把 WiFi socket 關掉（見 [tcp] 說明）。
     */
    fun link(): LinkClient = if (tcp.connected) tcp else client

    /** 若連線已斷，嘗試用已知裝置重連（僅在 WiFi 不可用時才會有機會重連藍牙）。 */
    private suspend fun ensureConnected(): Boolean {
        if (client.connected) return true
        if (tcp.connected) return false
        val d = device ?: return false
        return client.connect(d).isSuccess
    }

    /** 長時指令（跑 ghostlock LPE）的請求逾時。 */
    private const val LONG_TIMEOUT = 600_000L

    private suspend fun request(msg: JSONObject, timeoutMs: Long = 30_000L): JSONObject {
        if (link() === tcp) {
            return try {
                tcp.request(msg, timeoutMs)
            } catch (e: IOException) {
                android.util.Log.w(TAG, "wifi control failed: ${e.message}")
                tcp.disconnect()
                // 長時指令不重送（避免重複觸發 LPE／擷取），直接讓呼叫端看到錯誤
                if (timeoutMs > 30_000L) throw e
                if (!client.connected && !ensureConnected()) throw e
                client.request(msg, timeoutMs)
            }
        }
        if (!client.connected) ensureConnected()
        return client.request(msg, timeoutMs)
    }

    /**
     * 媒體通道：WiFi 優先。
     *
     * 未連線且允許自動模式時，主動請眼鏡開熱點並加入；失敗（含使用者未授權加入 AP）則回退藍牙，
     * 並進入冷卻避免連續彈窗。
     */
    private suspend fun ensureMediaLink(): LinkClient {
        if (tcp.connected) return tcp
        if (autoWifi) {
            wifiEnsureLock.withLock {
                // 等鎖期間別人可能已經連上，或剛剛才失敗（冷卻中）
                if (tcp.connected) return tcp
                val cooling = System.currentTimeMillis() - lastAutoWifiFailAt < AUTO_WIFI_COOLDOWN_MS
                if (!cooling) {
                    appLog("媒體通道：建立 WiFi 中…")
                    wifiNegotiating = true
                    notifyTransportChanged()   // UI 立刻改顯示「WiFi 連線中…」
                    try {
                        val r = ensureWifiMedia(apTries = AUTO_AP_TRIES)
                        if (r.isSuccess && tcp.connected) {
                            lastAutoWifiFailAt = 0L
                            appLog("媒體通道：WiFi ${wifiLink.apInfo.ip}:${wifiLink.apInfo.port}")
                            return tcp
                        }
                        lastAutoWifiFailAt = System.currentTimeMillis()
                        val why = (r.exceptionOrNull() as? WifiSetupException)?.code?.name
                            ?: r.exceptionOrNull()?.message ?: "unknown"
                        android.util.Log.w(TAG, "auto wifi media failed: $why")
                        appLog("媒體通道：WiFi 建立失敗（$why），暫用藍牙（較慢）")
                    } finally {
                        wifiNegotiating = false
                        notifyTransportChanged()
                    }
                }
            }
        }
        if (!client.connected) ensureConnected()
        return client
    }

    /** 寫入 App 內共用操作紀錄（無 UI 時只留 logcat）。 */
    private fun appLog(msg: String) {
        android.util.Log.i(TAG, msg)
        runCatching { com.rokid.rc.phone.ui.LogStore.append(msg) }
    }

    private suspend fun requestFile(
        msg: JSONObject,
        dir: File,
        name: String,
        timeoutMs: Long = 120_000L,
        onProgress: ((doneBytes: Long, totalBytes: Long) -> Unit)? = null
    ): Result<File> {
        val link = ensureMediaLink()
        if (link.connected) {
            return link.requestFile(msg, dir, name, timeoutMs, onProgress)
        }
        return Result.failure(IOException("not connected"))
    }

    /**
     * 建立 WiFi 媒體通道：
     * 藍牙請眼鏡開熱點（ENSURE_WIFI_AP，輪詢至 AP up）→ 手機加入 AP（系統彈窗或手動）
     * → TCP 連 8555。失敗回傳可讀訊息（UI 可引導手動加入 AP）。
     *
     * @param apTries 熱點啟動輪詢次數（每次間隔 5 秒）；自動模式用較短的值。
     */
    suspend fun ensureWifiMedia(apTries: Int = 18): Result<WifiApInfo> {
        if (tcp.connected) return Result.success(wifiLink.apInfo)
        // 控制鏈路（藍牙）必須先通；否則 request() 會拋 IOException 造成當機（曾實際發生）
        if (!client.connected && !ensureConnected()) {
            return Result.failure(
                WifiSetupException(WifiSetupException.Code.NOT_CONNECTED, "glasses control link not connected")
            )
        }
        // 1) 請眼鏡確保 SoftAP；AP 啟動慢（5~90s），輪詢直到 up 或逾時
        var lastOk = false
        for (i in 1..apTries) {
            val r = runCatching { client.request(Msg.build(MsgType.ENSURE_WIFI_AP), 30_000L) }
                .getOrElse {
                    return Result.failure(
                        WifiSetupException(
                            WifiSetupException.Code.NOT_CONNECTED,
                            "ENSURE_WIFI_AP failed: ${it.message}"
                        )
                    )
                }
            wifiLink.apInfo = WifiApInfo.fromJson(r.getJSONObject("wifiAp"))
            lastOk = r.optBoolean("ok", false)
            if (wifiLink.apInfo.up) break
            kotlinx.coroutines.delay(5000)
        }
        if (!wifiLink.apInfo.up) {
            return Result.failure(
                if (lastOk) WifiSetupException(WifiSetupException.Code.AP_TIMEOUT, "hotspot start timeout")
                else WifiSetupException(WifiSetupException.Code.AP_NOT_READY, "hotspot not ready")
            )
        }
        // 2) 手機加入 AP
        if (!wifiLink.isOnAp()) {
            val joined = wifiLink.requestJoinAp()
            if (!joined) {
                return Result.failure(
                    WifiSetupException(
                        WifiSetupException.Code.AP_JOIN_MANUAL,
                        "manual join required: ${wifiLink.apInfo.ssid}"
                    )
                )
            }
        }
        // 3) TCP 連線（**必須綁定到剛加入的眼鏡 AP 網路**，否則會走行動網路而逾時）
        val apNet = wifiLink.joinedApNetwork()
        android.util.Log.i("Remote", "wifi media: apNet=${apNet?.toString() ?: "null"} localIp=${wifiLink.currentLocalIp()}")
        val res = tcp.connect(wifiLink.apInfo.ip, wifiLink.apInfo.port, network = apNet)
        return if (res.isSuccess) Result.success(wifiLink.apInfo) else Result.failure(
            WifiSetupException(
                WifiSetupException.Code.TCP,
                "tcp connect failed: ${res.exceptionOrNull()?.message}",
                res.exceptionOrNull()
            )
        )
    }

    /** 關閉 WiFi 通道，並把藍牙控制鏈路接回來（眼鏡端在 WiFi accept 時已關掉藍牙）。 */
    suspend fun closeWifiMedia() {
        tcp.disconnect()
        lastAutoWifiFailAt = 0L
        if (!client.connected) ensureConnected()
    }

    suspend fun status(): Result<Status> = runCatching {
        Status.fromJson(request(Msg.build(MsgType.GET_STATUS)))
    }

    suspend fun setGhostlock(enabled: Boolean): Result<Unit> = runCatching {
        val r = request(Msg.build(MsgType.SET_GHOSTLOCK, 0L, "enabled" to enabled))
        if (Msg.ok(r)) Unit else throw IOException(Msg.error(r) ?: "failed")
    }

    suspend fun setRootMode(mode: RootMode): Result<Unit> = runCatching {
        val r = request(Msg.build(MsgType.SET_ROOT_MODE, 0L, "mode" to mode.name))
        if (Msg.ok(r)) Unit else throw IOException(Msg.error(r) ?: "failed")
    }

    suspend fun acquireRoot(): Result<Boolean> = runCatching {
        val r = request(Msg.build(MsgType.ACQUIRE_ROOT), LONG_TIMEOUT)
        if (Msg.ok(r)) true else throw IOException(Msg.error(r) ?: "failed")
    }

    suspend fun setLed(on: Boolean, color: String = "white"): Result<Unit> = runCatching {
        val r = request(Msg.build(MsgType.SET_LED, 0L, "on" to on, "color" to color), LONG_TIMEOUT)
        if (Msg.ok(r)) Unit else throw IOException(Msg.error(r) ?: "failed")
    }

    /** 設定眼鏡端儀表板顯示內容（五類開關；眼鏡端持久化）。 */
    suspend fun setDisplayConfig(config: DisplayConfig): Result<Unit> = runCatching {
        val r = request(Msg.build(MsgType.SET_DISPLAY_CONFIG, 0L, "display" to config.toJson()))
        if (Msg.ok(r)) Unit else throw IOException(Msg.error(r) ?: "failed")
    }

    /** 讀回眼鏡端目前的儀表板顯示配置（連線時同步勾選狀態）。 */
    suspend fun fetchDisplayConfig(): Result<DisplayConfig> = runCatching {
        val r = request(Msg.build(MsgType.GET_STATUS))
        DisplayConfig.fromJson(r.getJSONObject("display"))
    }

    /** 切換眼鏡端「靜音拍攝」（強制壓制拍照/錄影提示音；需 root）。 */
    suspend fun setSilent(enabled: Boolean): Result<Boolean> = runCatching {
        val r = request(Msg.build(MsgType.SET_SILENT, 0L, "enabled" to enabled), LONG_TIMEOUT)
        if (Msg.ok(r)) Msg.data(r)?.optBoolean("applied", false) ?: false
        else throw IOException(Msg.error(r) ?: "failed")
    }

    /** 要求眼鏡開啟無線 adb（root 後可用；回傳可連線的 ip:port）。 */
    suspend fun enableWifiAdb(): Result<Pair<String, Int>> = runCatching {
        val r = request(Msg.build(MsgType.ENABLE_WIFI_ADB), 30_000L)
        if (!Msg.ok(r)) throw IOException(Msg.error(r) ?: "failed")
        Msg.data(r)?.getString("ip")?.let { ip -> ip to r.getJSONObject("data").optInt("port", 5555) }
            ?: throw IOException("no data")
    }


    // ---- 開發模式（無線 adb 按需開關；眼鏡端 DevModeController） ----

    /** 開始開發模式：眼鏡加入 ssid 網段並開啟 adb TCP listener。 */
    suspend fun devModeStart(ssid: String?, pass: String?, security: String = "wpa2",
                             idleMs: Long = 10 * 60_000L): Result<JSONObject> = runCatching {
        val m = Msg.build(MsgType.DEV_MODE_START)
            .put("ssid", ssid ?: "")
            .put("pass", pass ?: "")
            .put("security", security)
            .put("idleMs", idleMs)
        val r = request(m, 60_000L)
        if (!Msg.ok(r)) throw IOException(Msg.error(r) ?: "failed")
        Msg.data(r) ?: JSONObject()
    }

    /** 停止開發模式（清 persist.adb.tcp.port、必要時還原 SoftAP）。 */
    suspend fun devModeStop(): Result<JSONObject> = runCatching {
        val r = request(Msg.build(MsgType.DEV_MODE_STOP), 30_000L)
        if (!Msg.ok(r)) throw IOException(Msg.error(r) ?: "failed")
        Msg.data(r) ?: JSONObject()
    }

    /** 查詢開發模式狀態（state/ssid/ip/listenerTcp/listenerManaged/message）。 */
    suspend fun devModeStatus(): Result<JSONObject> = runCatching {
        val r = request(Msg.build(MsgType.DEV_MODE_STATUS), 20_000L)
        if (!Msg.ok(r)) throw IOException(Msg.error(r) ?: "failed")
        Msg.data(r) ?: JSONObject()
    }

    /** 查詢眼鏡端權限/SELinux/WiFi AP 狀況。 */
    suspend fun deviceInfo(): Result<JSONObject> = runCatching {
        val info = request(Msg.build(MsgType.GET_DEVICE_INFO)).getJSONObject("info")
        if (info.has("wifiAp")) {
            wifiLink.apInfo = WifiApInfo.fromJson(info.getJSONObject("wifiAp"))
        }
        info
    }

    /**
     * 檢查回覆是否為錯誤（眼鏡端失敗時回 `{ok:false,error:"..."}`，沒有 media 欄位）。
     * 沒有這層檢查時，失敗會變成難懂的 `No value for media`（實測踩到）。
     */
    private fun requireOk(r: JSONObject) {
        if (!r.optBoolean("ok", true)) {
            throw IOException(
                com.rokid.rc.phone.ui.GhostlockText.error(
                    appContext,
                    r.optString("error").ifBlank { appContext.getString(R.string.err_unknown) }
                )
            )
        }
        if (r.has("error") && !r.isNull("error")) {
            val err = r.optString("error")
            if (err.isNotBlank()) throw IOException(err)
        }
    }

    /** 路徑 B：遠端開關眼鏡端的戒指／觸控板控制。 */
    suspend fun setRing(enabled: Boolean, pattern: String? = null): Result<Boolean> = runCatching {
        val m = Msg.build(MsgType.SET_RING).put("enabled", enabled)
        if (!pattern.isNullOrBlank()) m.put("pattern", pattern)
        val r = request(m)
        requireOk(r)
        r.optBoolean("ringRunning", enabled)
    }

    suspend fun capturePhoto(): Result<MediaItem> = runCatching {
        val r = request(Msg.build(MsgType.CAPTURE_PHOTO), LONG_TIMEOUT)
        requireOk(r)
        MediaItem.fromJson(r.getJSONObject("media"))
    }

    suspend fun startVideo(): Result<Unit> = runCatching {
        requireOk(request(Msg.build(MsgType.START_VIDEO)))
        Unit
    }

    suspend fun stopVideo(): Result<MediaItem> = runCatching {
        val r = request(Msg.build(MsgType.STOP_VIDEO))
        requireOk(r)
        MediaItem.fromJson(r.getJSONObject("media"))
    }

    suspend fun listAlbums(): Result<List<Album>> = runCatching {
        val r = request(Msg.build(MsgType.LIST_ALBUMS))
        val arr = r.getJSONArray("albums")
        (0 until arr.length()).map { Album.fromJson(arr.getJSONObject(it)) }
    }

    suspend fun createAlbum(name: String): Result<Album> = runCatching {
        val r = request(Msg.build(MsgType.CREATE_ALBUM, 0L, "name" to name))
        if (!Msg.ok(r)) throw IOException(Msg.error(r) ?: "failed")
        Album.fromJson(Msg.data(r)!!.getJSONObject("album"))
    }

    suspend fun renameAlbum(albumId: String, name: String): Result<Unit> = runCatching {
        val r = request(Msg.build(MsgType.RENAME_ALBUM, 0L, "albumId" to albumId, "name" to name))
        if (Msg.ok(r)) Unit else throw IOException(Msg.error(r) ?: "failed")
    }

    suspend fun deleteAlbum(albumId: String): Result<Unit> = runCatching {
        val r = request(Msg.build(MsgType.DELETE_ALBUM, 0L, "albumId" to albumId))
        if (Msg.ok(r)) Unit else throw IOException(Msg.error(r) ?: "failed")
    }

    suspend fun listMedia(albumId: String): Result<List<MediaItem>> = runCatching {
        val r = request(Msg.build(MsgType.LIST_MEDIA, 0L, "albumId" to albumId))
        val arr = r.getJSONArray("items")
        (0 until arr.length()).map { MediaItem.fromJson(arr.getJSONObject(it)) }
    }

    suspend fun deleteMedia(mediaId: String): Result<Unit> = runCatching {
        val r = request(Msg.build(MsgType.DELETE_MEDIA, 0L, "mediaId" to mediaId))
        if (Msg.ok(r)) Unit else throw IOException(Msg.error(r) ?: "failed")
    }

    /**
     * 依檔案大小估算傳輸逾時。
     *
     * 速率取 **45 KB/s**：實測藍牙 RFCOMM 只有 **51.7 KB/s**（16.7 MB 檔 ＝ 324 秒），
     * 原本假設 60 KB/s 會讓同一個檔案**剛好逾時失敗**（位元組其實已到齊）。
     */
    private fun transferTimeout(sizeBytes: Long): Long =
        (sizeBytes * 1000 / 45_000).coerceAtLeast(30_000L) + 45_000L

    suspend fun getThumbnail(mediaId: String, maxW: Int = 320, maxH: Int = 320): Result<File> {
        val dir = File(appContext.cacheDir, "thumbs")
        dir.mkdirs()
        val target = File(dir, "$mediaId.jpg")
        if (target.exists() && target.length() > 0) return Result.success(target)
        return requestFile(
            Msg.build(MsgType.GET_THUMBNAIL, 0L, "mediaId" to mediaId, "maxW" to maxW, "maxH" to maxH),
            dir, "$mediaId.jpg",
            timeoutMs = 120_000L
        )
    }

    suspend fun downloadMedia(
        item: MediaItem,
        albumName: String? = null,
        onProgress: ((doneBytes: Long, totalBytes: Long) -> Unit)? = null
    ): Result<File> {
        val dir = LocalStore.albumDir(item.albumId)
        dir.mkdirs()
        if (albumName != null) LocalStore.rememberAlbumName(item.albumId, albumName)
        return requestFile(
            Msg.build(MsgType.DOWNLOAD_MEDIA, 0L, "mediaId" to item.id),
            dir, item.fileName,
            timeoutMs = transferTimeout(item.sizeBytes),
            onProgress = onProgress
        )
    }
}
