package com.rokid.rc.phone

import android.annotation.SuppressLint
import com.rokid.rc.shared.protocol.MediaTypes
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.rokid.rc.shared.protocol.DisplayConfig
import com.rokid.rc.shared.protocol.RootMode
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * Debug broadcast receiver: lets `adb shell am broadcast` trigger remote commands,
 * for easier automated verification (MIUI blocks input injection from shell).
 */
class DebugReceiver : BroadcastReceiver() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // the debug receiver is used only in development/automation; the Bluetooth permission is declared in the Manifest and granted to the app
    @SuppressLint("MissingPermission")
    override fun onReceive(context: Context, intent: Intent) {
        Remote.ensureInit(context.applicationContext)
        val cmd = intent.getStringExtra("cmd") ?: return
        Log.i(TAG, "onReceive cmd=$cmd connected=${Remote.client.connected}")
        scope.launch {
            try {
                Log.i(TAG, "executing $cmd connected=${Remote.client.connected}")
                when (cmd) {
                    "SET_LED" -> Log.i(TAG, "SET_LED result=${Remote.setLed(intent.getBooleanExtra("on", true), intent.getStringExtra("color") ?: "white")}")
                    "SET_GHOSTLOCK" -> Log.i(TAG, "SET_GHOSTLOCK result=${Remote.setGhostlock(intent.getBooleanExtra("enabled", true))}")
                    // on failure also print the **reason** returned by the glasses (printing only a boolean gives no remote diagnosis)
                    "CAPTURE_PHOTO" -> Remote.capturePhoto().fold(
                        onSuccess = { Log.i(TAG, "CAPTURE_PHOTO ok ${it.fileName}") },
                        onFailure = { Log.w(TAG, "CAPTURE_PHOTO failed: ${it.message}") }
                    )
                    "START_VIDEO" -> Remote.startVideo().fold(
                        onSuccess = { Log.i(TAG, "START_VIDEO ok") },
                        onFailure = { Log.w(TAG, "START_VIDEO failed: ${it.message}") }
                    )
                    "STOP_VIDEO" -> Remote.stopVideo().fold(
                        onSuccess = { Log.i(TAG, "STOP_VIDEO ok ${it.fileName}") },
                        onFailure = { Log.w(TAG, "STOP_VIDEO failed: ${it.message}") }
                    )
                    // remote diagnosis: print every field of the glasses-side STATUS (including cameraBusy/ringDevice/rootAcquired)
                    "DUMP_STATUS" -> Remote.status().fold(
                        onSuccess = { st ->
                            Log.i(TAG, "STATUS cameraBusy=${st.cameraBusy} rootMode=${st.rootMode} " +
                                "rootAcquired=${st.rootAcquired} silent=${st.silentEnabled}/${st.silentApplied} " +
                                "ringEnabled=${st.ringEnabled} ringRunning=${st.ringRunning} " +
                                "ringDevice=${st.ringDevice} ringLast=${st.ringLastKey}/${st.ringLastArg}")
                        },
                        onFailure = { Log.w(TAG, "STATUS failed: ${it.message}") }
                    )
                    "ACQUIRE_ROOT" -> Log.i(TAG, "ACQUIRE_ROOT result=${Remote.acquireRoot()}")
                    "WIFI_CONNECT" -> {
                        // debug direct connect (for testing on the same LAN): --es host <ip> [--ei port 8555]
                        val host = intent.getStringExtra("host")
                        if (host != null) {
                            val port = intent.getIntExtra("port", com.rokid.rc.shared.protocol.Protocol.WIFI_PORT)
                            Log.i(TAG, "WIFI_CONNECT(direct $host:$port) result=${Remote.tcp.connect(host, port)}")
                        } else {
                            Log.i(TAG, "WIFI_CONNECT result=${Remote.ensureWifiMedia()}")
                        }
                    }
                    "WIFI_DISCONNECT" -> {
                        Remote.closeWifiMedia()
                        Log.i(TAG, "WIFI_DISCONNECT done, wifiMode=${Remote.wifiMode}")
                    }
                    "SET_AUTO_WIFI" -> {
                        val on = intent.getBooleanExtra("on", true)
                        Remote.setAutoWifi(on)
                        Log.i(TAG, "SET_AUTO_WIFI=$on wifiMode=${Remote.wifiMode}")
                    }
                    "DEV_MODE_START" -> Log.i(TAG, "DEV_MODE_START result=" + Remote.devModeStart(
                        intent.getStringExtra("ssid"), intent.getStringExtra("pass"),
                        intent.getStringExtra("security") ?: "wpa2",
                        intent.getLongExtra("idleMs", 10 * 60_000L)))
                    "DEV_MODE_STOP" -> Log.i(TAG, "DEV_MODE_STOP result=${Remote.devModeStop()}")
                    "DEV_MODE_STATUS" -> Log.i(TAG, "DEV_MODE_STATUS result=${Remote.devModeStatus()}")
                    "WIFI_ADB" -> Log.i(TAG, "WIFI_ADB result=${Remote.enableWifiAdb()}")
                    "SET_SILENT" -> Log.i(TAG, "SET_SILENT result=${Remote.setSilent(intent.getBooleanExtra("enabled", true))}")
                    "SET_LOCALE" -> {
                        val lang = intent.getStringExtra("lang")
                        LocaleHelper.set(context, lang)
                        Log.i(TAG, "SET_LOCALE=$lang（下次 Activity 建立時生效）")
                    }
                    "SET_DISPLAY" -> {
                        val cfg = DisplayConfig(
                            conn = intent.getBooleanExtra("conn", true),
                            root = intent.getBooleanExtra("root", true),
                            camera = intent.getBooleanExtra("camera", true),
                            storage = intent.getBooleanExtra("storage", true),
                            led = intent.getBooleanExtra("led", true)
                        )
                        Log.i(TAG, "SET_DISPLAY result=${Remote.setDisplayConfig(cfg)}")
                    }
                    // verify ghostlock progress code → phone-locale text (GhostlockText)
                    "TEST_PROGRESS" -> {
                        val arg = intent.getStringExtra("arg") ?: "/data/local/tmp/ghostlock_rv101"
                        val codes = listOf(
                            "gl_skip_lpe", "gl_prepare_payload", "gl_done", "gl_failed",
                            "gl_bridge_dir_missing", "gl_bridge_sent", "gl_bridge_timeout", "gl_bridge_report",
                            "gl_attempt", "gl_helper_started", "gl_ready_seen", "gl_payload_replaced",
                            "gl_wait_payload_root", "gl_binary_missing", "gl_exploit_started",
                            "gl_exploit_start_failed", "gl_wait_ready", "gl_ready_timeout",
                            "gl_ready_timeout_retry", "gl_payload_write_failed", "gl_wait_payload",
                            "gl_payload_timeout", "gl_exploit_output", "gl_shizuku_not_running",
                            "gl_shizuku_unauthorized", "gl_shizuku_bind_failed", "gl_shizuku_root_run_shell",
                            "gl_shizuku_no_run_shell", "gl_shizuku_bad_identity", "gl_shizuku_unavailable",
                            "gl_attempt_shizuku", "gl_kernel_panic", "gl_process_died",
                            "gl_process_died_retry", "gl_retry_after", "gl_attempts_exhausted",
                            "gl_fallback_failed"
                        )
                        codes.forEach { c ->
                            val shown = com.rokid.rc.phone.ui.GhostlockText.progress(context, c, arg)
                            Log.i(TAG, "TEST_PROGRESS $c -> ${shown ?: "(unmapped)"}")
                        }
                    }
                    // verify the "glasses structured event → phone-locale text" mapping table (RingText)
                    // usage: --es cmd TEST_RING_TEXT [--es key photo_ok] [--es arg IMG_1.jpg]
                    "TEST_RING_TEXT" -> {
                        val keys = intent.getStringExtra("key")?.let { listOf(it) }
                            ?: listOf(
                                "photo_ok", "photo_fail", "video_start", "video_saved", "stop_fail",
                                "silent_on", "silent_on_pending", "silent_off", "display_switched",
                                "camera_not_ready", "no_action", "no_device", "start_failed",
                                "stopped", "running", "unknown_key"
                            )
                        val arg = intent.getStringExtra("arg") ?: "IMG_1.jpg"
                        keys.forEach { k ->
                            val shown = com.rokid.rc.phone.ui.RingText.result(context, k, arg)
                            Log.i(TAG, "TEST_RING_TEXT $k -> ${shown ?: "(hidden)"}")
                        }
                    }
                    "TEST_LOG" -> {
                        val n = intent.getIntExtra("n", 20)
                        val frag = com.rokid.rc.phone.ui.RemoteFragment.current
                        if (frag == null) {
                            Log.i(TAG, "TEST_LOG skip: fragment 不在前景")
                        } else {
                            for (i in 1..n) frag.logPublic("測試訊息 #$i — log 捲動驗證（上下滑動查看歷史訊息）")
                            Log.i(TAG, "TEST_LOG done n=$n")
                        }
                    }
                    "SET_ROOT_MODE" ->
                        Remote.setRootMode(if (intent.getBooleanExtra("auto", false)) RootMode.AUTO else RootMode.MANUAL)
                    "GET_DEVICE_INFO" -> Log.i(TAG, "DEVICE_INFO=${Remote.deviceInfo().getOrNull()}")
                    "SCAN_RING" -> {
                        val secs = intent.getIntExtra("secs", 12)
                        val filter = intent.getStringExtra("filter")
                        com.rokid.rc.shared.ring.RingScanner(context).scan(secs * 1000L, filter) { list ->
                            Log.i(TAG, "SCAN_RING done found=${list.size} filter=${filter ?: "-"}")
                        }
                    }
                    "PROBE_RING" -> {
                        val mac = intent.getStringExtra("mac")
                        if (mac.isNullOrEmpty()) {
                            com.rokid.rc.shared.ring.RingGatt.scanAndProbe(context, 3) { res ->
                                Log.i(TAG, "PROBE_RING(scan) 完成，可連線裝置 ${res.size}")
                            }
                        } else {
                            com.rokid.rc.shared.ring.RingGatt.probe(context, mac) { ok, svcs, note ->
                                Log.i(TAG, "PROBE_RING $mac ok=$ok svcs=${svcs.size} note=$note")
                            }
                        }
                    }
                    "PAIR_RING" -> {
                        val mac = intent.getStringExtra("mac")
                        val pattern = intent.getStringExtra("pattern")
                        if (mac.isNullOrEmpty()) {
                            com.rokid.rc.shared.ring.RingBonder.scanAndBond(context, com.rokid.rc.shared.ring.RingIdentity.orDefault(pattern)) { ok, note, m ->
                                Log.i(TAG, "PAIR_RING(scanAndBond) ok=$ok mac=$m note=$note")
                            }
                        } else {
                            com.rokid.rc.shared.ring.RingBonder.bond(context, mac) { ok, note ->
                                Log.i(TAG, "PAIR_RING $mac ok=$ok note=$note")
                            }
                        }
                    }
                    "UNPAIR_RING" -> {
                        val mac = intent.getStringExtra("mac") ?: ""
                        com.rokid.rc.shared.ring.RingBonder.unbond(context, mac) { ok, note ->
                            Log.i(TAG, "UNPAIR_RING $mac ok=$ok note=$note")
                        }
                    }
                    "WATCH_RING" -> {
                        val rounds = intent.getIntExtra("rounds", 12)
                        val pattern = com.rokid.rc.shared.ring.RingIdentity.orDefault(intent.getStringExtra("pattern"))
                        val autoBond = intent.getBooleanExtra("autoBond", true)
                        com.rokid.rc.shared.ring.RingWatcher(context).watch(
                            rounds = rounds,
                            namePattern = pattern,
                            onConnectable = { found ->
                                val dev = found.device
                                if (autoBond && dev != null) {
                                    com.rokid.rc.shared.ring.RingBonder.bond(context, dev) { ok, note ->
                                        Log.i(TAG, "WATCH_RING 自動配對 ok=$ok note=$note")
                                    }
                                }
                            }
                        ) { i, msg -> Log.i(TAG, "WATCH_RING $msg") }
                    }
                    "SET_RING" -> {
                        val enabled = intent.getBooleanExtra("enabled", true)
                        val pattern = intent.getStringExtra("pattern")
                        Log.i(TAG, "SET_RING enabled=$enabled result=${Remote.setRing(enabled, pattern)}")
                    }
                    "CONNECT" -> {
                        val adapter = android.bluetooth.BluetoothAdapter.getDefaultAdapter()
                        @Suppress("MissingPermission")
                        val dev = adapter?.bondedDevices?.firstOrNull {
                            val n = it.name ?: ""
                            n.contains("Glasses", true) || n.contains("Rokid", true)
                        }
                        if (dev == null) {
                            Log.i(TAG, "CONNECT 找不到眼鏡（已配對裝置中無 Glasses/Rokid）")
                        } else {
                            Log.i(TAG, "CONNECT ${dev.name} result=${Remote.connect(dev)}")
                        }
                    }
                    "DELETE_MEDIA" -> Log.i(TAG, "DELETE_MEDIA=${Remote.deleteMedia(intent.getStringExtra("mediaId") ?: "")}")
                    "TEST_TRANSFER" -> testTransfer(intent.getStringExtra("mediaId"))
                    "TEST_CHAIN" -> testMediaChain()
                    "LIST_MEDIA_INFO" -> {
                        val albums = Remote.listAlbums().getOrNull() ?: emptyList()
                        val album = albums.firstOrNull { it.id == intent.getStringExtra("albumId") } ?: albums.firstOrNull()
                        if (album != null) {
                            val items = Remote.listMedia(album.id).getOrNull() ?: emptyList()
                            val head = items.take(3).map {
                                "${it.fileName}|${it.id}|${it.type}|${it.sizeBytes}|${it.durationMs}"
                            }
                            Log.i(TAG, "LIST_MEDIA_INFO album=${album.id} total=${items.size} head=$head")
                        } else Log.i(TAG, "LIST_MEDIA_INFO no-album")
                    }
                    "OPEN_TAB" -> {
                        val tab = intent.getStringExtra("tab") ?: "remote"
                        val i = Intent(context, MainActivity::class.java)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)
                            .putExtra("tab", tab)
                        context.startActivity(i)
                        Log.i(TAG, "OPEN_TAB=$tab")
                    }
                    "OPEN_ALBUM" -> {
                        val i = Intent(context, MediaActivity::class.java)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            .putExtra("mode", intent.getStringExtra("mode") ?: "remote")
                            .putExtra("albumId", intent.getStringExtra("albumId") ?: "")
                            .putExtra("albumName", intent.getStringExtra("albumName") ?: "")
                        context.startActivity(i)
                        Log.i(TAG, "OPEN_ALBUM done")
                    }
                    "OPEN_VIEWER" -> {
                        val mode = intent.getStringExtra("mode") ?: "remote"
                        val i = Intent(context, ViewerActivity::class.java)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                            .putExtra("mode", mode)
                        if (mode == "local") {
                            i.putExtra("path", intent.getStringExtra("path") ?: "")
                            i.putExtra("mime", intent.getStringExtra("mime") ?: MediaTypes.MIME_JPEG)
                            i.putExtra("fileName", intent.getStringExtra("fileName") ?: "")
                        } else {
                            i.putExtra("mediaId", intent.getStringExtra("mediaId") ?: "")
                            i.putExtra("albumId", intent.getStringExtra("albumId") ?: "")
                            i.putExtra("albumName", intent.getStringExtra("albumName") ?: "")
                            i.putExtra("type", intent.getStringExtra("type") ?: "PHOTO")
                            i.putExtra("mime", intent.getStringExtra("mime") ?: MediaTypes.MIME_JPEG)
                            i.putExtra("fileName", intent.getStringExtra("fileName") ?: "")
                            i.putExtra("sizeBytes", intent.getLongExtra("sizeBytes", 0L))
                            i.putExtra("createdAt", intent.getLongExtra("createdAt", 0L))
                        }
                        context.startActivity(i)
                        Log.i(TAG, "OPEN_VIEWER done")
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "execute $cmd failed", e)
            }
        }
    }

    /**
     * Timed download test: `TEST_TRANSFER [--es mediaId X]`.
     * When no mediaId is given it picks the "largest" media (a video), so the WiFi/Bluetooth difference becomes visible.
     */
    private suspend fun testTransfer(mediaId: String?) {
        val albums = Remote.listAlbums().getOrNull() ?: emptyList()
        val all = albums.flatMap { a ->
            Remote.listMedia(a.id).getOrNull().orEmpty().map { a.name to it }
        }
        if (all.isEmpty()) {
            Log.i(TAG, "TEST_TRANSFER 沒有媒體，中止")
            return
        }
        val (albumName, item) = all.firstOrNull { it.second.id == mediaId }
            ?: all.maxByOrNull { it.second.sizeBytes }!!
        Log.i(TAG, "TEST_TRANSFER target=${item.fileName} size=${item.sizeBytes} " +
            "album=$albumName link=${if (Remote.tcp.connected) "wifi" else "bt"}")
        val started = System.currentTimeMillis()
        val r = Remote.downloadMedia(item, albumName)
        val ms = System.currentTimeMillis() - started
        val kbps = if (ms > 0) item.sizeBytes.toDouble() / ms else 0.0
        Log.i(TAG, "TEST_TRANSFER result=$r elapsed=${ms}ms throughput=${"%.1f".format(kbps)}KB/s " +
            "link=${if (Remote.tcp.connected) "wifi" else "bt"}")
    }

    private suspend fun testMediaChain() {
        val albums = Remote.listAlbums().getOrNull() ?: emptyList()
        Log.i(TAG, "TEST_CHAIN albums=${albums.map { it.name + "(" + it.mediaCount + ")" }}")
        if (albums.isEmpty()) {
            Log.i(TAG, "TEST_CHAIN 沒有相簿，中止")
            return
        }
        val album = albums.first()
        val media = Remote.listMedia(album.id).getOrNull() ?: emptyList()
        Log.i(TAG, "TEST_CHAIN media(${album.name}) count=${media.size}")
        if (media.isEmpty()) {
            Log.i(TAG, "TEST_CHAIN 相簿內無媒體，中止")
            return
        }
        val first = media.first()
        Log.i(TAG, "TEST_CHAIN 縮圖=${Remote.getThumbnail(first.id)}")
        Log.i(TAG, "TEST_CHAIN 下載=${Remote.downloadMedia(first)}")
    }

    companion object {
        private const val TAG = "DebugReceiver"
    }
}
