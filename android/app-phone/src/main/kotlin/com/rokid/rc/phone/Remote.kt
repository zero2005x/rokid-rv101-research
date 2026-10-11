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
 * WiFi media channel setup failed (carries a reason code; the UI shows a localized message by code,
 * this message text is only for logcat debugging and is not shown to the user directly).
 */
class WifiSetupException(
    val code: Code,
    message: String? = null,
    cause: Throwable? = null
) : IOException(message, cause) {
    enum class Code { AP_TIMEOUT, AP_NOT_READY, AP_JOIN_MANUAL, TCP, NOT_CONNECTED }
}

/** Phone-side remote API wrapper (singleton). */
object Remote {
    lateinit var appContext: Context
        private set

    val client = BluetoothClient()
    var device: BluetoothDevice? = null
        private set

    /**
     * WiFi channel (both control commands and media prefer WiFi, falling back to Bluetooth).
     *
     * NOTE: the glasses-side BluetoothServer and WifiServer both call `graph.connection?.close()` when accepting a
     * new connection, so the two channels are mutually exclusive and "the last one to connect wins". So as soon as any
     * ordinary command reconnects over Bluetooth it kicks out the WiFi socket, and media silently falls back to
     * Bluetooth (measured: the root cause of extremely slow 10-second video transfers).
     */
    val tcp = TcpClient()
    val wifiLink: WifiLink by lazy { WifiLink(appContext) }

    /** Usable if either channel is up (the glasses actively close Bluetooth when WiFi connects, so do not look at Bluetooth alone). */
    val isConnected: Boolean get() = client.connected || tcp.connected
    val wifiMode: Boolean get() = tcp.connected

    private const val TAG = "Remote"
    private const val PREF = "rc_phone"
    private const val KEY_AUTO_WIFI = "auto_wifi_media"

    /** Establish the WiFi channel before media (on by default; adjustable from the "WiFi transfer" switch on the settings page). */
    @Volatile
    var autoWifi: Boolean = true
        private set

    /** Cooldown after a failed auto-setup, so not every operation pops the system join dialog (accessed both inside and outside the lock). */
    @Volatile
    private var lastAutoWifiFailAt = 0L
    private const val AUTO_WIFI_COOLDOWN_MS = 60_000L

    /** Hotspot startup poll count in auto mode (5 s each); shorter than manual mode so the UI does not freeze for too long. */
    private const val AUTO_AP_TRIES = 6

    private val wifiEnsureLock = Mutex()

    /**
     * A WiFi media channel is being established (starting the hotspot / joining the AP / connecting TCP).
     *
     * The UI uses it to show "WiFi 連線中…" during the few seconds before it is connected, instead of showing
     * "藍牙" — otherwise the user downloading a video sees Bluetooth even though it runs over WiFi a moment later
     * (fast transfer, opposite label).
     */
    @Volatile
    var wifiNegotiating: Boolean = false
        private set

    /** Transfer channel state changes (negotiation started / connected / failed / disconnected) → the UI should refresh. May come from a background thread. */
    @Volatile
    var onTransportChanged: (() -> Unit)? = null

    /**
     * Messages the glasses push proactively (`PROGRESS` etc.).
     *
     * **Both channels must be registered**: the glasses-side `AppGraph.push()` pushes over the "current connection",
     * so registering Bluetooth alone means every push is dropped while WiFi is the current channel (LPE progress stops working).
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
            // Registered centrally here: any channel connecting or disconnecting must update the UI (originally only "disconnected" updated,
            // which left the screen stuck on "傳輸：藍牙" after WiFi was established automatically)
            client.onConnected = { notifyTransportChanged() }
            client.onDisconnected = { notifyTransportChanged() }
            tcp.onConnected = { notifyTransportChanged() }
            tcp.onDisconnected = { notifyTransportChanged() }
        }
    }

    /** Settings page "WiFi transfer" switch: remember the preference (applied automatically on the next media operation). */
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
     * Currently usable control channel: WiFi preferred, Bluetooth as fallback.
     *
     * Ordinary commands must go over WiFi too, otherwise a Bluetooth reconnect closes the WiFi socket (see the note on [tcp]).
     */
    fun link(): LinkClient = if (tcp.connected) tcp else client

    /** If the connection is down, try to reconnect with a known device (Bluetooth only gets a chance to reconnect when WiFi is unavailable). */
    private suspend fun ensureConnected(): Boolean {
        if (client.connected) return true
        if (tcp.connected) return false
        val d = device ?: return false
        return client.connect(d).isSuccess
    }

    /** Request timeout for long commands (running the ghostlock LPE). */
    private const val LONG_TIMEOUT = 600_000L

    private suspend fun request(msg: JSONObject, timeoutMs: Long = 30_000L): JSONObject {
        if (link() === tcp) {
            return try {
                tcp.request(msg, timeoutMs)
            } catch (e: IOException) {
                android.util.Log.w(TAG, "wifi control failed: ${e.message}")
                tcp.disconnect()
                // do not resend long commands (avoid triggering the LPE/capture twice), let the caller see the error directly
                if (timeoutMs > 30_000L) throw e
                if (!client.connected && !ensureConnected()) throw e
                client.request(msg, timeoutMs)
            }
        }
        if (!client.connected) ensureConnected()
        return client.request(msg, timeoutMs)
    }

    /**
     * Media channel: WiFi preferred.
     *
     * When not connected and auto mode is allowed, proactively ask the glasses to start a hotspot and join it; on
     * failure (including the user not authorizing the AP join) fall back to Bluetooth and enter cooldown to avoid
     * repeated dialogs.
     */
    private suspend fun ensureMediaLink(): LinkClient {
        if (tcp.connected) return tcp
        if (autoWifi) {
            wifiEnsureLock.withLock {
                // while waiting for the lock someone else may have connected, or it may just have failed (cooling down)
                if (tcp.connected) return tcp
                val cooling = System.currentTimeMillis() - lastAutoWifiFailAt < AUTO_WIFI_COOLDOWN_MS
                if (!cooling) {
                    appLog("媒體通道：建立 WiFi 中…")
                    wifiNegotiating = true
                    notifyTransportChanged()   // UI immediately switches to showing "WiFi 連線中…"
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

    /** Write to the shared in-app operation log (when there is no UI, only logcat). */
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
     * Establish the WiFi media channel:
     * ask the glasses over Bluetooth to start the hotspot (ENSURE_WIFI_AP, poll until the AP is up) → the phone joins the AP (system dialog or manual)
     * → TCP connect to 8555. On failure return a readable message (the UI can guide a manual AP join).
     *
     * @param apTries hotspot startup poll count (5 seconds apart); auto mode uses a shorter value.
     */
    suspend fun ensureWifiMedia(apTries: Int = 18): Result<WifiApInfo> {
        if (tcp.connected) return Result.success(wifiLink.apInfo)
        // the control link (Bluetooth) must be up first; otherwise request() throws IOException and crashes (this actually happened)
        if (!client.connected && !ensureConnected()) {
            return Result.failure(
                WifiSetupException(WifiSetupException.Code.NOT_CONNECTED, "glasses control link not connected")
            )
        }
        // 1) ask the glasses to ensure the SoftAP; the AP starts slowly (5-90 s), poll until up or timeout
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
        // 2) the phone joins the AP
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
        // 3) TCP connect (**must bind to the glasses AP network just joined**, otherwise it goes over mobile data and times out)
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

    /** Close the WiFi channel and bring the Bluetooth control link back (the glasses side already closed Bluetooth when it accepted WiFi). */
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

    /** Set the glasses-side dashboard contents (five switch groups; persisted on the glasses). */
    suspend fun setDisplayConfig(config: DisplayConfig): Result<Unit> = runCatching {
        val r = request(Msg.build(MsgType.SET_DISPLAY_CONFIG, 0L, "display" to config.toJson()))
        if (Msg.ok(r)) Unit else throw IOException(Msg.error(r) ?: "failed")
    }

    /** Read back the glasses-side dashboard display configuration (sync the checkbox state on connect). */
    suspend fun fetchDisplayConfig(): Result<DisplayConfig> = runCatching {
        val r = request(Msg.build(MsgType.GET_STATUS))
        DisplayConfig.fromJson(r.getJSONObject("display"))
    }

    /** Toggle glasses-side "silent capture" (force-suppress the photo/video notification sound; requires root). */
    suspend fun setSilent(enabled: Boolean): Result<Boolean> = runCatching {
        val r = request(Msg.build(MsgType.SET_SILENT, 0L, "enabled" to enabled), LONG_TIMEOUT)
        if (Msg.ok(r)) Msg.data(r)?.optBoolean("applied", false) ?: false
        else throw IOException(Msg.error(r) ?: "failed")
    }

    /** Ask the glasses to enable wireless adb (available after root; returns a connectable ip:port). */
    suspend fun enableWifiAdb(): Result<Pair<String, Int>> = runCatching {
        val r = request(Msg.build(MsgType.ENABLE_WIFI_ADB), 30_000L)
        if (!Msg.ok(r)) throw IOException(Msg.error(r) ?: "failed")
        Msg.data(r)?.getString("ip")?.let { ip -> ip to r.getJSONObject("data").optInt("port", 5555) }
            ?: throw IOException("no data")
    }


    // ---- dev mode (wireless adb switch on demand; glasses-side DevModeController) ----

    /** Start dev mode: the glasses join the ssid subnet and open an adb TCP listener. */
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

    /** Stop dev mode (clear persist.adb.tcp.port, restore the SoftAP if necessary). */
    suspend fun devModeStop(): Result<JSONObject> = runCatching {
        val r = request(Msg.build(MsgType.DEV_MODE_STOP), 30_000L)
        if (!Msg.ok(r)) throw IOException(Msg.error(r) ?: "failed")
        Msg.data(r) ?: JSONObject()
    }

    /** Query dev mode status (state/ssid/ip/listenerTcp/listenerManaged/message). */
    suspend fun devModeStatus(): Result<JSONObject> = runCatching {
        val r = request(Msg.build(MsgType.DEV_MODE_STATUS), 20_000L)
        if (!Msg.ok(r)) throw IOException(Msg.error(r) ?: "failed")
        Msg.data(r) ?: JSONObject()
    }

    /** Query the glasses-side permission/SELinux/WiFi AP situation. */
    suspend fun deviceInfo(): Result<JSONObject> = runCatching {
        val info = request(Msg.build(MsgType.GET_DEVICE_INFO)).getJSONObject("info")
        if (info.has("wifiAp")) {
            wifiLink.apInfo = WifiApInfo.fromJson(info.getJSONObject("wifiAp"))
        }
        info
    }

    /**
     * Check whether the reply is an error (on glasses-side failure it returns `{ok:false,error:"..."}` with no media field).
     * Without this check a failure turns into the obscure `No value for media` (hit this in practice).
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

    /** Path B: remotely toggle the glasses-side ring / touchpad control. */
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
     * Estimate the transfer timeout from the file size.
     *
     * Rate taken as **45 KB/s**: measured Bluetooth RFCOMM is only **51.7 KB/s** (a 16.7 MB file = 324 seconds),
     * and the original assumption of 60 KB/s made the same file **fail right at the timeout** (the bytes had in fact all arrived).
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
