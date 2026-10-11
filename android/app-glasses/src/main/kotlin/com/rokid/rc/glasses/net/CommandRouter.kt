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

/** Dispatch to the glasses-side components by message type. */
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


    // ---- dev mode (on-demand wireless adb switch) ----

    private suspend fun devModeStart(h: ConnectionHandler, id: Long, msg: JSONObject) {
        val ssid = msg.optString("ssid", "").ifBlank { null }
        val pass = msg.optString("pass", "")
        val security = msg.optString("security", "wpa2")
        val idleMs = msg.optLong("idleMs", com.rokid.rc.glasses.net.DevModeController.DEFAULT_IDLE_MS)
        graph.devMode.start(ssid, pass, security, idleMs)
        // give the subnet join a moment before reporting back (the phone can poll DEV_MODE_STATUS to track it)
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

    /** Phone toggles "silent capture" (force-suppress the photo/video shutter sound; needs root). */
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

    /** Path B: remotely toggle the glasses-side ring/touchpad control. */
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
     * The phone asks to turn wireless adb on.
     * ⚠ Lesson measured on device: restarting adbd on this Rokid build loses USB enumeration and shows no TCP bind,
     * so **adbd is no longer restarted blindly**: only service.adb.tcp.port is set and adbd's property watcher
     * switches over by itself (if this build supports it); when it does not, failure is reported and USB adb is unaffected.
     */
    private suspend fun enableWifiAdb(h: ConnectionHandler, id: Long) {
        val client = com.rokid.rc.glasses.ghost.RootDaemonClient()
        if (!client.isRoot()) {
            return ackError(h, id, "root not ready")
        }
        // P3: also set **persist**.adb.tcp.port so adbd listens on TCP automatically on **every boot**
        // (setting only service.* lasts for this boot, so a reboot means plugging the cable in again).
        // NOTE: Android 11+ implements wireless debugging officially through this property; this device is Android 12.
        // Side effect: adb becomes reachable by anyone who can reach it over the network; security rests on the adb RSA key (ro.adb.secure=1).
        client.exec("setprop persist.adb.tcp.port 5555", 15_000L)
        client.exec("setprop service.adb.tcp.port 5555", 15_000L)
        // poll for 8 seconds to see whether adbd bound TCP 5555.
        // ⚠ 2026-09-20 correction: **IPv4 and IPv6 must both be checked**. This device's adbd binds the
        //   IPv6 wildcard (`[::]:5555` in `/proc/net/tcp6`), so grepping only the IPv4
        //   `/proc/net/tcp` misreads "it actually is bound" as "not supported" — which is exactly where the earlier
        //   "this build does not support adb-over-TCP" conclusion came from (see reports/FACTS.md R7).
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
        // a listener is not enough: the client must be able to reach it. The SoftAP provides the fixed entry point 192.168.43.1;
        // if there is currently no 192.168/10.x interface at all (AP not up), make sure the AP comes up while we are here.
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

    /** Local IPv4 (STA wlan0 preferred, SoftAP wlan2 second). */
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

    /** Phone sets what the glasses dashboard shows (five category switches, persisted). */
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
        // make sure the WiFi hotspot is up after escalation (media channel; failure does not affect the ACQUIRE_ROOT result)
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

    /** Phone asks to make sure the WiFi hotspot is started (reports an error when root is not ready). */
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
