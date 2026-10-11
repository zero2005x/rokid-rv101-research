package com.rokid.rc.glasses

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.rokid.rc.shared.protocol.DisplayConfig
import kotlinx.coroutines.launch

/**
 * Debug broadcast receiver (automated verification; avoids input injection on the glasses — measured to risk a full device reboot).
 *  - action=com.rokid.rc.glasses.DEBUG, --es cmd <TOGGLE_PANEL|SET_DISPLAY|SET_LOCALE>
 *  - SET_DISPLAY: --ez conn/root/camera/storage/led true|false
 *  - SET_LOCALE: --es lang en (takes effect the next time an Activity is created)
 */
class DebugReceiver : BroadcastReceiver() {

    /**
     * Origin check for sensitive commands (ones that change the externally exposed surface: joining a subnet / opening an adb listener).
     * Allowed: (1) this app itself (same uid) (2) adb shell (uid 2000, developer automation) (3) a same-signature app holding the signature permission.
     * Denied: any other app (including untrusted_app) — so a third party cannot push the glasses into dev mode with an implicit broadcast.
     */
    private fun isTrustedSender(ctx: Context, intent: Intent): Boolean {
        val myUid = android.os.Process.myUid()
        val uid = try {
            val sf = intent.getIntExtra("android.intent.extra.SENT_FROM_UID", -1)
            if (sf >= 0) sf else android.os.Binder.getCallingUid()
        } catch (e: Exception) {
            return false
        }
        if (uid == myUid || uid == 2000) return true   // ourselves / adb shell
        return ctx.checkCallingOrSelfPermission(PERM_SIGNATURE) == android.content.pm.PackageManager.PERMISSION_GRANTED
    }

    private val SENSITIVE_CMDS = setOf(
        "DEV_MODE_START", "DEV_MODE_STOP", "WIFI_SET_CRED", "ADB_WIFI_START", "ADB_WIFI_STOP"
    )

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != ACTION) return
        // dev mode changes the externally exposed surface (joining a subnet / opening an adb listener) → require the signature permission,
        // so no third-party app can push the glasses into dev mode with an implicit broadcast.
        val cmd0 = intent.getStringExtra("cmd")
        if (cmd0 in SENSITIVE_CMDS && !isTrustedSender(context, intent)) {
            Log.w(TAG, "拒絕 $cmd0：來源不可信（非本 App／非 adb shell／無 $PERM_SIGNATURE）")
            return
        }
        // same as the phone side: the whole handler is wrapped in try/catch. A debug broadcast must not crash the app
        // (measured: on Android 12, calling RingBonder.bond from a receiver context throws
        //  ReceiverCallNotAllowedException and takes the whole process down — see reports/rgr06_pairing_failure_2026-09-13.md).
        try {
        when (intent.getStringExtra("cmd")) {
            // ---- dev mode (wireless adb switch on demand: join the subnet + open the listener + auto-off on timeout) ----
            "DEV_MODE_START" -> {
                val app = context.applicationContext as App
                // path A (WifiNetworkSuggestion) needs location permission; when it is missing, report it explicitly instead of failing silently
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
            // ---- wireless ADB relay (no root needed; TCP:5555 → /dev/socket/adbd) ----
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
            // for testing: proactively push one structured progress event, to verify that "a glasses-side push travels over the current
            // connection → the phone receives it on both channels". Use: verify the push path while WiFi is the current channel
            // without actually running the LPE (before the fix only the Bluetooth onPush was registered, so pushes over WiFi were dropped).
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
            // ---- ring (BLE HID) discovery/pairing/probing: shares the shared module with the phone side ----
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
            // ---- path B: ring/touchpad control by reading evdev directly as root ----
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
            // ---- safe arming policy (boot-loop breaker): query/reset ----
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
        /** Required permission for dev-mode commands (a same-signature app / adb shell can send it; a third-party app cannot). */
        const val PERM_SIGNATURE = "com.rokid.rc.glasses.permission.DEV_MODE"
    }
}
