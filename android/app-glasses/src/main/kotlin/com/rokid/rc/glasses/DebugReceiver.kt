package com.rokid.rc.glasses

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.rokid.rc.shared.protocol.DisplayConfig
import kotlinx.coroutines.launch

/**
 * 除錯用廣播接收器（自動化驗證；避免在眼鏡上做 input 注入——實測有整機重啟風險）。
 *  - action=com.rokid.rc.glasses.DEBUG，--es cmd <TOGGLE_PANEL|SET_DISPLAY|SET_LOCALE>
 *  - SET_DISPLAY：--ez conn/root/camera/storage/led true|false
 *  - SET_LOCALE：--es lang en（下次 Activity 建立生效）
 */
class DebugReceiver : BroadcastReceiver() {

    /**
     * 敏感指令（會改變對外曝露面：加入網段／開 adb listener）的來源檢查。
     * 允許：①本 App 自己（同 uid）②adb shell（uid 2000，開發者自動化）③持有簽章權限的同簽章 App。
     * 拒絕：其他任何 App（含 untrusted_app）——避免第三方用隱式廣播把眼鏡推進開發模式。
     */
    private fun isTrustedSender(ctx: Context, intent: Intent): Boolean {
        val myUid = android.os.Process.myUid()
        val uid = try {
            val sf = intent.getIntExtra("android.intent.extra.SENT_FROM_UID", -1)
            if (sf >= 0) sf else android.os.Binder.getCallingUid()
        } catch (e: Exception) {
            return false
        }
        if (uid == myUid || uid == 2000) return true   // 自己 / adb shell
        return ctx.checkCallingOrSelfPermission(PERM_SIGNATURE) == android.content.pm.PackageManager.PERMISSION_GRANTED
    }

    private val SENSITIVE_CMDS = setOf(
        "DEV_MODE_START", "DEV_MODE_STOP", "WIFI_SET_CRED", "ADB_WIFI_START", "ADB_WIFI_STOP"
    )

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != ACTION) return
        // 開發模式會改變對外曝露面（加入網段／開 adb listener）→ 要求簽章權限，
        // 避免任何第三方 App 用隱式廣播把眼鏡推進開發模式。
        val cmd0 = intent.getStringExtra("cmd")
        if (cmd0 in SENSITIVE_CMDS && !isTrustedSender(context, intent)) {
            Log.w(TAG, "拒絕 $cmd0：來源不可信（非本 App／非 adb shell／無 $PERM_SIGNATURE）")
            return
        }
        // 與手機端一致：整個處理包在 try/catch 內。除錯廣播不該讓 App 崩潰
        // （實測踩到：Android 12 從 receiver context 呼叫 RingBonder.bond 會丟
        //  ReceiverCallNotAllowedException 把整個行程帶走 —— 見 reports/rgr06_pairing_failure_2026-09-13.md）。
        try {
        when (intent.getStringExtra("cmd")) {
            // ---- 開發模式（無線 adb 按需開關：加入網段＋開 listener＋逾時自動關）----
            "DEV_MODE_START" -> {
                val app = context.applicationContext as App
                // 路徑 A（WifiNetworkSuggestion）需要定位權限；缺權限時明確回報而不是靜默失敗
                if (!app.graph.wifiJoin.hasSuggestionPermissions()) {
                    Log.w(TAG, "DEV_MODE_START 警告：缺少定位權限 → 只能走 root 路徑 B")
                }
                val idle = intent.getLongExtra("idleMs", com.rokid.rc.glasses.net.DevModeController.DEFAULT_IDLE_MS)
                app.graph.devMode.start(
                    ssid = intent.getStringExtra("ssid"),
                    pass = intent.getStringExtra("pass"),
                    security = intent.getStringExtra("security") ?: "wpa2",
                    idleTimeoutMs = idle
                )
                Log.i(TAG, "DEV_MODE_START ssid=${intent.getStringExtra("ssid")} idleMs=$idle")
            }
            "DEV_MODE_STOP" -> {
                val app = context.applicationContext as App
                app.graph.devMode.stop()
                Log.i(TAG, "DEV_MODE_STOP")
            }
            "DEV_MODE_STATUS" -> {
                val app = context.applicationContext as App
                Log.i(TAG, "DEV_MODE_STATUS ${app.graph.devMode.statusJson()}")
            }
            "WIFI_SET_CRED" -> {
                val app = context.applicationContext as App
                app.graph.wifiJoin.saveCred(
                    com.rokid.rc.glasses.net.WifiJoin.Cred(
                        intent.getStringExtra("ssid") ?: "",
                        intent.getStringExtra("pass") ?: "",
                        intent.getStringExtra("security") ?: "wpa2"
                    )
                )
                Log.i(TAG, "WIFI_SET_CRED ssid=${intent.getStringExtra("ssid")}")
            }
            // ---- 無線 ADB relay（不需 root；TCP:5555 → /dev/socket/adbd）----
            "ADB_WIFI_START" -> {
                val port = intent.getIntExtra("port", com.rokid.rc.glasses.net.AdbWifiService.DEFAULT_PORT)
                com.rokid.rc.glasses.net.AdbWifiService.start(context, port)
                Log.i(TAG, "ADB_WIFI_START port=$port")
            }
            "ADB_WIFI_STOP" -> {
                com.rokid.rc.glasses.net.AdbWifiService.stop(context)
                Log.i(TAG, "ADB_WIFI_STOP")
            }
            "ADB_WIFI_STATUS" -> {
                val s = com.rokid.rc.glasses.net.AdbWifiService.last
                Log.i(TAG, "ADB_WIFI_STATUS $s")
            }
            "SET_LOCALE" -> {
                LocaleHelper.set(context, intent.getStringExtra("lang"))
                Log.i(TAG, "locale set")
            }
            "SET_DISPLAY" -> {
                val cfg = DisplayConfig(
                    conn = intent.getBooleanExtra("conn", true),
                    root = intent.getBooleanExtra("root", true),
                    camera = intent.getBooleanExtra("camera", true),
                    storage = intent.getBooleanExtra("storage", true),
                    led = intent.getBooleanExtra("led", true)
                )
                val app = context.applicationContext as App
                app.graph.displayConfig.update(cfg)
                Log.i(TAG, "display set: $cfg")
            }
            // 測試用：主動推送一個結構化進度事件，驗證「眼鏡端 push 走當前連線 → 手機端
            // 兩條通道都收到」。用途：不必真的跑 LPE 就能驗證 WiFi 為當前通道時的推播路徑
            // （修正前只註冊了藍牙的 onPush，WiFi 下的推播會被丟棄）。
            "PUSH_TEST" -> {
                val app = context.applicationContext as App
                val code = intent.getStringExtra("code") ?: "gl_attempt"
                val arg = intent.getStringExtra("arg") ?: "unit-test"
                app.graph.pushProgress(
                    com.rokid.rc.shared.protocol.ProgressEvent(code, arg)
                )
                Log.i(TAG, "PUSH_TEST sent code=$code arg=$arg wifi=${app.graph.wifiConnected} bt=${app.graph.btConnected}")
            }
            "TOGGLE_PANEL" -> {
                val activity = MainActivity.currentInstance
                activity?.runOnUiThread { activity.togglePanelPublic() }
                Log.i(TAG, "panel toggled (activity=${activity != null})")
            }
            // ---- 戒指（BLE HID）探索／配對／探測：與手機端共用 shared 模組 ----
            "SCAN_RING" -> {
                val secs = intent.getIntExtra("secs", 12)
                com.rokid.rc.shared.ring.RingScanner(context)
                    .scan(secs * 1000L, intent.getStringExtra("filter")) { list ->
                        Log.i(TAG, "SCAN_RING done found=${list.size}")
                    }
            }
            "PAIR_RING" -> {
                val mac = intent.getStringExtra("mac")
                if (mac.isNullOrEmpty()) {
                    com.rokid.rc.shared.ring.RingBonder.scanAndBond(
                        context, com.rokid.rc.shared.ring.RingIdentity.orDefault(intent.getStringExtra("pattern"))
                    ) { ok, note, m -> Log.i(TAG, "PAIR_RING(scan) ok=$ok mac=$m note=$note") }
                } else {
                    com.rokid.rc.shared.ring.RingBonder.bond(context, mac) { ok, note ->
                        Log.i(TAG, "PAIR_RING $mac ok=$ok note=$note")
                    }
                }
            }
            "PROBE_RING" -> {
                val mac = intent.getStringExtra("mac")
                if (mac.isNullOrEmpty()) {
                    com.rokid.rc.shared.ring.RingGatt.scanAndProbe(context, 3) { res ->
                        Log.i(TAG, "PROBE_RING(scan) 可連線裝置 ${res.size}")
                    }
                } else {
                    com.rokid.rc.shared.ring.RingGatt.probe(context, mac) { ok, svcs, note ->
                        Log.i(TAG, "PROBE_RING $mac ok=$ok svcs=${svcs.size} note=$note")
                    }
                }
            }
            // ---- 路徑 B：root 直讀 evdev 的戒指／觸控板控制 ----
            "RING_DIAG" -> {
                val app = context.applicationContext as App
                app.graph.scope.launch {
                    val client = com.rokid.rc.glasses.ghost.RootDaemonClient()
                    val probes = listOf(
                        "id",
                        "which setsid getevent pgrep pkill",
                        "setsid sh -c 'echo A' 2>&1",
                        "rm -f /data/local/tmp/diag_b.txt ; setsid sh -c 'echo B > /data/local/tmp/diag_b.txt' >/dev/null 2>&1 & sleep 1 ; ls -la /data/local/tmp/diag_b.txt 2>&1",
                        "cat /data/local/tmp/diag_b.txt 2>&1"
                    )
                    probes.forEach { cmd ->
                        val out = client.exec(cmd, 15_000L).trim().replace('\n', '|')
                        Log.i(TAG, "RING_DIAG [$cmd] => $out")
                    }
                }
            }
            "RING_DEVICES" -> {
                val app = context.applicationContext as App
                app.graph.scope.launch {
                    val list = com.rokid.rc.glasses.ring.RingEvdevReader().listDevices()
                    list.forEach { (path, name) -> Log.i(TAG, "RING_DEVICE $path $name") }
                    Log.i(TAG, "RING_DEVICES total=${list.size}")
                }
            }
            "RING_START" -> {
                val app = context.applicationContext as App
                val mgr = app.graph.ringControl
                val name = com.rokid.rc.shared.ring.RingIdentity.orDefault(intent.getStringExtra("name"))
                val device = intent.getStringExtra("device")
                if (!device.isNullOrEmpty()) mgr.devicePath = device
                app.graph.scope.launch {
                    val ok = mgr.start(name)
                    Log.i(TAG, "RING_START ok=$ok device=${mgr.devicePath} last=${mgr.lastKey}/${mgr.lastArg}")
                }
            }
            "RING_STOP" -> {
                val app = context.applicationContext as App
                app.graph.ringControl.stop()
                Log.i(TAG, "RING_STOP last=${app.graph.ringControl.lastKey}/${app.graph.ringControl.lastArg}")
            }
            // ---- 安全武裝政策（開機迴圈斷路器）：查詢／重置 ----
            "ARM_POLICY_STATUS" -> {
                val app = context.applicationContext as App
                Log.i(TAG, "ARM_POLICY ${com.rokid.rc.glasses.ghost.ArmPolicy.describe(app, app.graph.prefs)}")
            }
            "ARM_POLICY_RESET" -> {
                val app = context.applicationContext as App
                com.rokid.rc.glasses.ghost.ArmPolicy.reset(app.graph.prefs)
                Log.i(TAG, "ARM_POLICY reset → ${com.rokid.rc.glasses.ghost.ArmPolicy.describe(app, app.graph.prefs)}")
            }
            "RING_STATUS" -> {
                val app = context.applicationContext as App
                val mgr = app.graph.ringControl
                Log.i(
                    TAG,
                    "RING_STATUS enabled=${mgr.enabled} running=${mgr.isRunning()} " +
                        "device=${mgr.devicePath} last=${mgr.lastKey}/${mgr.lastArg}"
                )
            }
        }
        } catch (e: Exception) {
            Log.e(TAG, "debug cmd failed: ${intent.getStringExtra("cmd")}", e)
        }
    }

    companion object {
        const val ACTION = "com.rokid.rc.glasses.DEBUG"
        private const val TAG = "GlassesDebugReceiver"
        /** 開發模式相關指令的必要權限（同簽章 App／adb shell 可送；第三方 App 不行）。 */
        const val PERM_SIGNATURE = "com.rokid.rc.glasses.permission.DEV_MODE"
    }
}
