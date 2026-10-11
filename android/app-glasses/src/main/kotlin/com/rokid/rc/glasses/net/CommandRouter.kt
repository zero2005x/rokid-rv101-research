package com.rokid.rc.glasses.net

import com.rokid.rc.glasses.AppGraph
import com.rokid.rc.shared.protocol.MediaTypes
import android.util.Log
import com.rokid.rc.shared.net.FrameCodec
import com.rokid.rc.shared.protocol.DisplayConfig
import com.rokid.rc.shared.protocol.Msg
import com.rokid.rc.shared.protocol.MsgType
import com.rokid.rc.shared.protocol.Protocol
import com.rokid.rc.shared.ring.RingIdentity
import com.rokid.rc.shared.protocol.RootMode
import com.rokid.rc.shared.protocol.Status
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/** 依訊息 type 分派到眼鏡端各元件。 */
class CommandRouter(private val graph: AppGraph) {

    suspend fun handle(handler: ConnectionHandler, json: String) {
        val msg = try {
            JSONObject(json)
        } catch (e: Exception) {
            return
        }
        val type = Msg.type(msg)
        val id = Msg.id(msg)
        Log.i(TAG, "recv: $type id=$id")
        try {
            when (type) {
                MsgType.HELLO -> sendHelloAck(handler, id)
                MsgType.GET_STATUS -> sendStatus(handler, id)
                MsgType.SET_GHOSTLOCK -> setGhostlock(handler, id, msg)
                MsgType.SET_ROOT_MODE -> setRootMode(handler, id, msg)
                MsgType.ACQUIRE_ROOT -> acquireRoot(handler, id)
                MsgType.SET_LED -> setLed(handler, id, msg)
                MsgType.GET_DEVICE_INFO -> getDeviceInfo(handler, id)
                MsgType.ENSURE_WIFI_AP -> ensureWifiAp(handler, id)
                MsgType.SET_DISPLAY_CONFIG -> setDisplayConfig(handler, id, msg)
                MsgType.ENABLE_WIFI_ADB -> enableWifiAdb(handler, id)
                MsgType.DEV_MODE_START -> devModeStart(handler, id, msg)
                MsgType.DEV_MODE_STOP -> devModeStop(handler, id)
                MsgType.DEV_MODE_STATUS -> devModeStatus(handler, id)
                MsgType.SET_SILENT -> setSilent(handler, id, msg)
                MsgType.SET_RING -> setRing(handler, id, msg)
                MsgType.CAPTURE_PHOTO -> capturePhoto(handler, id)
                MsgType.START_VIDEO -> startVideo(handler, id)
                MsgType.STOP_VIDEO -> stopVideo(handler, id)
                MsgType.LIST_ALBUMS -> listAlbums(handler, id)
                MsgType.CREATE_ALBUM -> createAlbum(handler, id, msg)
                MsgType.RENAME_ALBUM -> renameAlbum(handler, id, msg)
                MsgType.DELETE_ALBUM -> deleteAlbum(handler, id, msg)
                MsgType.LIST_MEDIA -> listMedia(handler, id, msg)
                MsgType.GET_THUMBNAIL -> sendThumbnail(handler, id, msg)
                MsgType.DOWNLOAD_MEDIA -> sendMedia(handler, id, msg)
                MsgType.DELETE_MEDIA -> deleteMedia(handler, id, msg)
                else -> handler.sendJson(Msg.ack(id, false, "unknown_type").toString())
            }
        } catch (e: Exception) {
            handler.sendJson(Msg.reply(id, MsgType.ERROR, "message" to (e.message ?: "error")).toString())
        }
    }

    // ---- handlers ----

    private suspend fun sendHelloAck(h: ConnectionHandler, id: Long) {
        val o = Msg.reply(
            id, MsgType.HELLO_ACK,
            "protoVersion" to Protocol.PROTO_VERSION,
            "deviceName" to Protocol.APP_NAME_GLASSES,
            "appVersion" to "1.0"
        )
        h.sendJson(o.toString())
    }


    // ---- 開發模式（無線 adb 按需開關） ----

    private suspend fun devModeStart(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val ssid = msg.optString("ssid", "").ifBlank { null }
        val pass = msg.optString("pass", "")
        val security = msg.optString("security", "wpa2")
        val idleMs = msg.optLong("idleMs", com.rokid.rc.glasses.net.DevModeController.DEFAULT_IDLE_MS)
        graph.devMode.start(ssid, pass, security, idleMs)
        // 給加入網段一點時間再回報（手機端可輪詢 DEV_MODE_STATUS 追蹤）
        kotlinx.coroutines.delay(1500)
        h.sendJson(Msg.ack(id, true, null, graph.devMode.statusJson()).toString())
    }

    private suspend fun devModeStop(h: ConnectionHandler, id: Long) {
        graph.devMode.stop()
        kotlinx.coroutines.delay(800)
        h.sendJson(Msg.ack(id, true, null, graph.devMode.statusJson()).toString())
    }

    private suspend fun devModeStatus(h: ConnectionHandler, id: Long) {
        h.sendJson(Msg.ack(id, true, null, graph.devMode.statusJson()).toString())
    }

    /** 手機切換「靜音拍攝」（強制壓制拍照/錄影提示音；需 root）。 */
    private suspend fun setSilent(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val on = msg.optBoolean("enabled", true)
        val ok = graph.silentMode.setEnabled(on)
        val data = JSONObject()
            .put("enabled", graph.silentMode.enabled)
            .put("applied", graph.silentMode.applied)
        h.sendJson(
            Msg.ack(id, ok, if (ok) null else "silent mode failed (root required?)", data).toString()
        )
    }

    /** 路徑 B：遠端開關眼鏡端的戒指／觸控板控制。 */
    private suspend fun setRing(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val enabled = msg.optBoolean("enabled", true)
        val pattern = msg.optString("pattern").takeIf { it.isNotBlank() }
        val mgr = graph.ringControl
        mgr.enabled = enabled
        val ok = if (enabled) mgr.start(RingIdentity.orDefault(pattern)) else { mgr.stop(); true }
        h.sendJson(
            Msg.ack(
                id, true, null,
                JSONObject()
                    .put("ringEnabled", enabled)
                    .put("ringRunning", mgr.isRunning())
                    .put("ringDevice", mgr.devicePath ?: JSONObject.NULL)
            ).toString()
        )
        Log.i(TAG, "SET_RING enabled=$enabled ok=$ok device=${mgr.devicePath}")
    }

    /**
     * 手機要求開啟無線 adb。
     * ⚠ 實測教訓：此 Rokid build 的 adbd 重啟會讓 USB 列舉丟失且未見 TCP 綁定，
     * 因此**不再盲目重啟 adbd**：只設 service.adb.tcp.port，讓 adbd 的屬性監聽
     * 自行切換（若此 build 支援）；不支援時回報失敗，USB adb 不受影響。
     */
    private suspend fun enableWifiAdb(h: ConnectionHandler, id: Long) {
        val client = com.rokid.rc.glasses.ghost.RootDaemonClient()
        if (!client.isRoot()) {
            return ackError(h, id, "root not ready")
        }
        // P3：同時設 **persist**.adb.tcp.port，讓 adbd 在**每次開機**都自動監聽 TCP
        // （只設 service.* 只在本次開機有效，重開機就要插線重來）。
        // 注意：Android 11+ 官方即以此屬性實作無線偵錯；本機為 Android 12。
        // 副作用：adb 會變成「只要網路可達就能連」，安全性由 adb RSA 金鑰把關（ro.adb.secure=1）。
        client.exec("setprop persist.adb.tcp.port 5555", 15_000L)
        client.exec("setprop service.adb.tcp.port 5555", 15_000L)
        // 輪詢 8 秒看 adbd 是否綁定 TCP 5555。
        // ⚠ 2026-09-20 修正：**必須同時看 IPv4 與 IPv6**。本機 adbd 綁的是
        //   IPv6 wildcard（`/proc/net/tcp6` 的 `[::]:5555`），只 grep IPv4 的
        //   `/proc/net/tcp` 會把「其實有綁」誤判成「不支援」——這正是先前
        //   「本 build 不支援 adb-over-TCP」結論的來源（見 reports/FACTS.md R7）。
        var bound = false
        var boundWhere = ""
        repeat(8) {
            if (bound) return@repeat
            val out = client.exec(
                "grep -q ':15B3 ' /proc/net/tcp && echo v4; " +
                    "grep -q ':15B3 ' /proc/net/tcp6 && echo v6; true", 5_000L
            )
            if (out.contains("v4")) { bound = true; boundWhere = "ipv4" }
            else if (out.contains("v6")) { bound = true; boundWhere = "ipv6" }
            else kotlinx.coroutines.delay(1000)
        }
        if (!bound) {
            return ackError(h, id, "adb-over-tcp not supported on this build")
        }
        // 有 listener 還不夠：客戶端要連得到。SoftAP 提供固定入口 192.168.43.1；
        // 若目前沒有任何 192.168/10.x 介面（AP 未開），順手確保 AP 起來。
        var ip = localIpv4()
        if (ip == "0.0.0.0") {
            runCatching { graph.wifiHotspot.ensureStarted() }
            repeat(6) {
                if (ip != "0.0.0.0") return@repeat
                ip = localIpv4()
                if (ip == "0.0.0.0") kotlinx.coroutines.delay(1000)
            }
        }
        val persist = client.exec("getprop persist.adb.tcp.port", 5_000L).trim()
        val data = JSONObject().put("ip", ip).put("port", 5555)
            .put("persistPort", persist)
            .put("boundOn", boundWhere)
            .put("autoAfterReboot", persist == "5555")
        h.sendJson(Msg.ack(id, true, null, data).toString())
    }

    /** 本機 IPv4（STA wlan0 優先，SoftAP wlan2 次之）。 */
    private fun localIpv4(): String {
        try {
            val nets = java.net.NetworkInterface.getNetworkInterfaces() ?: return "0.0.0.0"
            var apIp: String? = null
            while (nets.hasMoreElements()) {
                val nif = nets.nextElement()
                if (!nif.isUp || nif.isLoopback) continue
                val addrs = nif.inetAddresses
                while (addrs.hasMoreElements()) {
                    val a = addrs.nextElement()
                    val host = a.hostAddress ?: continue
                    if (host.startsWith("192.168.") || host.startsWith("10.")) {
                        if (nif.name == "wlan0") return host
                        if (nif.name == "wlan2") apIp = host
                    }
                }
            }
            return apIp ?: "0.0.0.0"
        } catch (e: Exception) {
            return "0.0.0.0"
        }
    }

    /** 手機設定眼鏡儀表板顯示內容（五類開關，持久化）。 */
    private suspend fun setDisplayConfig(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val cfg = msg.optJSONObject("display")
        if (cfg == null) {
            return ackError(h, id, "display config missing")
        }
        val parsed = DisplayConfig.fromJson(cfg)
        Log.i(TAG, "setDisplayConfig payload=$parsed")
        graph.displayConfig.update(parsed)
        h.sendJson(
            Msg.ack(id, true, null, JSONObject().put("display", graph.displayConfig.config.toJson())).toString()
        )
    }

    private suspend fun sendStatus(h: ConnectionHandler, id: Long) {
        val camera = graph.camera
        val status = Status(
            ghostlockEnabled = graph.ghostlock.isEnabled,
            cameraBusy = false,
            recording = camera?.isRecording ?: false,
            albumCount = graph.media.albumCount,
            mediaCount = graph.media.mediaCount,
            storageFreeBytes = graph.media.storageFreeBytes(),
            rootMode = graph.rootMode.name,
            rootAcquired = graph.ghostlock.rootAcquired,
            ledOn = graph.ledOn,
            silentEnabled = graph.silentMode.enabled,
            silentApplied = graph.silentMode.applied,
            ringEnabled = graph.ringControl.enabled,
            ringRunning = graph.ringControl.isRunning(),
            ringDevice = graph.ringControl.devicePath,
            ringLastKey = graph.ringControl.lastKey,
            ringLastArg = graph.ringControl.lastArg,
            rootState = graph.rootKeeper.state,
            rootHint = graph.rootKeeper.detail,
            suEngine = graph.rootMetrics.lastEngine,
            suLastOk = graph.rootMetrics.lastOk,
            suLastMs = graph.rootMetrics.lastMs,
            suAttempts = graph.rootMetrics.attempts,
            suSuccesses = graph.rootMetrics.successes,
            suFirstTryOk = graph.rootMetrics.firstTryOk,
            suKeeperChecks = graph.rootKeeper.checks,
            suKeeperRearms = graph.rootKeeper.rearmSuccesses
        )
        h.sendJson(
            flat(id, MsgType.STATUS, status.toJson())
                .put("display", graph.displayConfig.config.toJson())
                .toString()
        )
    }

    private suspend fun setGhostlock(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val enabled = msg.optBoolean("enabled", false)
        graph.ghostlock.setEnabled(enabled)
        h.sendJson(Msg.ack(id, true, null, JSONObject().put("enabled", enabled)).toString())
    }

    private suspend fun setRootMode(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val mode = try {
            RootMode.valueOf(msg.optString("mode", ""))
        } catch (e: Exception) {
            RootMode.MANUAL
        }
        graph.setRootMode(mode)
        h.sendJson(Msg.ack(id, true, null, JSONObject().put("mode", mode.name)).toString())
    }

    private suspend fun acquireRoot(h: ConnectionHandler, id: Long) {
        val result = graph.acquireRootMetered()
        // 提權成功後順手確保 WiFi 熱點（媒體通道；失敗不影響 ACQUIRE_ROOT 結果）
        if (result.success || graph.ghostlock.rootAcquired) {
            graph.scope.launch {
                val apOk = graph.wifiHotspot.ensureStarted()
                val silentOk = graph.silentMode.applyIfEnabled()
                Log.i(TAG, "acquireRoot → wifi ap up=$apOk, silent=$silentOk")
            }
        }
        val data = JSONObject()
            .put("rootAcquired", result.success)
            .put("output", result.output)
        h.sendJson(Msg.ack(id, result.success, if (result.success) null else "acquire failed: ${result.stage}", data).toString())
    }

    private suspend fun setLed(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val on = msg.optBoolean("on", true)
        val color = msg.optString("color", "white")
        graph.led.setLed(color, on)
        if (color == "white" || color == "all") graph.ledOn = on
        h.sendJson(Msg.ack(id, true, null, JSONObject().put("on", on).put("color", color)).toString())
    }

    private suspend fun getDeviceInfo(h: ConnectionHandler, id: Long) {
        val info = JSONObject()
            .put("selinux", runCmd("getenforce").trim())
            .put("id", runCmd("id").trim())
            .put("uid", android.os.Process.myUid())
            .put("rootAcquired", graph.ghostlock.rootAcquired)
            .put("ghostlockEnabled", graph.ghostlock.isEnabled)
            .put("rootMode", graph.rootMode.name)
            .put("wifiAp", graph.wifiHotspot.status().toJson())
        h.sendJson(Msg.reply(id, MsgType.DEVICE_INFO, "info" to info).toString())
    }

    /** 手機要求確保 WiFi 熱點已啟動（root 未就緒時回報錯誤）。 */
    private suspend fun ensureWifiAp(h: ConnectionHandler, id: Long) {
        val ok = graph.wifiHotspot.ensureStarted()
        val status = graph.wifiHotspot.status()
        h.sendJson(Msg.reply(id, MsgType.WIFI_AP_STATUS, "ok" to ok, "wifiAp" to status.toJson()).toString())
    }

    private fun runCmd(cmd: String): String = try {
        val p = ProcessBuilder("sh", "-c", cmd).redirectErrorStream(true).start()
        p.inputStream.bufferedReader().readText()
    } catch (e: Exception) {
        e.message ?: "error"
    }

    private suspend fun capturePhoto(h: ConnectionHandler, id: Long) {
        val camera = graph.camera
            ?: return ackError(h, id, "camera not ready")
        camera.takePhoto(graph.ghostlock.isEnabled).fold(
            onSuccess = { item ->
                h.sendJson(Msg.reply(id, MsgType.PHOTO_TAKEN, "media" to item.toJson()).toString())
            },
            onFailure = { e -> ackError(h, id, e.message ?: "capture failed") }
        )
    }

    private suspend fun startVideo(h: ConnectionHandler, id: Long) {
        val camera = graph.camera
            ?: return ackError(h, id, "camera not ready")
        camera.startVideo(graph.ghostlock.isEnabled).fold(
            onSuccess = { h.sendJson(Msg.reply(id, MsgType.VIDEO_STARTED).toString()) },
            onFailure = { e -> ackError(h, id, e.message ?: "start video failed") }
        )
    }

    private suspend fun stopVideo(h: ConnectionHandler, id: Long) {
        val camera = graph.camera
            ?: return ackError(h, id, "camera not ready")
        try {
            val item = camera.stopVideo()
            h.sendJson(Msg.reply(id, MsgType.VIDEO_STOPPED, "media" to item.toJson()).toString())
        } catch (e: Exception) {
            ackError(h, id, e.message ?: "stop video failed")
        }
    }

    private suspend fun listAlbums(h: ConnectionHandler, id: Long) {
        val arr = JSONArray()
        graph.media.listAlbums().forEach { arr.put(it.toJson()) }
        h.sendJson(Msg.reply(id, MsgType.ALBUM_LIST, "albums" to arr).toString())
    }

    private suspend fun createAlbum(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val album = graph.media.createAlbum(msg.optString("name"))
        h.sendJson(Msg.ack(id, true, null, JSONObject().put("album", album.toJson())).toString())
    }

    private suspend fun renameAlbum(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val ok = graph.media.renameAlbum(msg.optString("albumId"), msg.optString("name"))
        h.sendJson(Msg.ack(id, ok, if (ok) null else "album not found").toString())
    }

    private suspend fun deleteAlbum(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val ok = graph.media.deleteAlbum(msg.optString("albumId"))
        h.sendJson(Msg.ack(id, ok, if (ok) null else "album not found").toString())
    }

    private suspend fun listMedia(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val albumId = msg.optString("albumId")
        val arr = JSONArray()
        graph.media.listMedia(albumId).forEach { arr.put(it.toJson()) }
        h.sendJson(Msg.reply(id, MsgType.MEDIA_LIST, "albumId" to albumId, "items" to arr).toString())
    }

    private suspend fun deleteMedia(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val ok = graph.media.deleteMedia(msg.optString("mediaId"))
        h.sendJson(Msg.ack(id, ok, if (ok) null else "media not found").toString())
    }

    private suspend fun sendThumbnail(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val mediaId = msg.optString("mediaId")
        val maxW = msg.optInt("maxW", 320)
        val maxH = msg.optInt("maxH", 320)
        val file = graph.media.getThumbnail(mediaId, maxW, maxH)
        if (file == null) return ackError(h, id, "thumbnail unavailable")
        streamFile(h, id, mediaId, file, MediaTypes.MIME_JPEG)
    }

    private suspend fun sendMedia(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val mediaId = msg.optString("mediaId")
        val file = graph.media.mediaFile(mediaId)
        if (file == null || !file.exists()) return ackError(h, id, "media not found")
        val mime = MediaTypes.mimeOf(file.name)
        streamFile(h, id, mediaId, file, mime)
    }

    // ---- helpers ----

    private suspend fun streamFile(
        h: ConnectionHandler,
        id: Long,
        mediaId: String,
        file: java.io.File,
        mime: String
    ) {
        val transferId = UUID.randomUUID().toString()
        h.sendJson(
            Msg.reply(
                id, MsgType.MEDIA_META,
                "mediaId" to mediaId,
                "transferId" to transferId,
                "fileName" to file.name,
                "mime" to mime,
                "size" to file.length()
            ).toString()
        )
        var sent = 0L
        try {
            val buf = ByteArray(FrameCodec.CHUNK_SIZE)
            file.inputStream().use { input ->
                while (true) {
                    val n = input.read(buf)
                    if (n <= 0) break
                    val chunk = if (n == buf.size) buf else buf.copyOf(n)
                    h.sendBinary(chunk)
                    sent += n
                }
            }
            h.sendJson(
                Msg.reply(
                    id, MsgType.TRANSFER_END,
                    "mediaId" to mediaId,
                    "transferId" to transferId,
                    "ok" to true,
                    "bytes" to sent
                ).toString()
            )
        } catch (e: Exception) {
            h.sendJson(
                Msg.reply(
                    id, MsgType.TRANSFER_END,
                    "mediaId" to mediaId,
                    "transferId" to transferId,
                    "ok" to false,
                    "bytes" to sent
                ).toString()
            )
        }
    }

    private suspend fun ackError(h: ConnectionHandler, id: Long, msg: String) {
        h.sendJson(Msg.ack(id, false, msg).toString())
    }

    private fun flat(replyTo: Long, type: String, obj: JSONObject): JSONObject {
        val o = Msg.reply(replyTo, type)
        val it = obj.keys()
        while (it.hasNext()) {
            val k = it.next()
            o.put(k, obj.get(k))
        }
        return o
    }

    companion object {
        private const val TAG = "CommandRouter"
    }
}
