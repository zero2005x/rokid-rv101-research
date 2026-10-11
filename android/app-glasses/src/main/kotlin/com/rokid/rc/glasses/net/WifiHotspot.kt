package com.rokid.rc.glasses.net

import android.util.Log
import com.rokid.rc.glasses.ghost.RootDaemonClient
import com.rokid.rc.shared.protocol.Protocol
import com.rokid.rc.shared.protocol.WifiApInfo

/**
 * Glasses-side WiFi hotspot (SoftAP) management.
 *
 * Topology (the user chose "root-assisted"): after a successful escalation the root_daemon runs
 * `cmd wifi start-softap <ssid> wpa2 <pass>`, the glasses serve at the fixed IP 192.168.43.1,
 * and the phone joins that AP and transfers media over TCP to 192.168.43.1:8555.
 *
 * Trigger channel: RootDaemonClient (root, after escalation). Measured: adb shell (uid 2000) can also run
 * start-softap, but the app (untrusted_app) calling cmd wifi directly is denied by permissions, so everything
 * goes through the root socket (consistent with the project's "root once per boot" lifecycle).
 *
 * The SoftAP does not survive a reboot; after a restart it needs another escalation + ensureStarted()
 * (CaptureService calls it automatically once rootMode=AUTO escalation succeeds).
 */
class WifiHotspot {

    private val ssid = Protocol.AP_SSID
    private val pass = Protocol.AP_PASS

    /** Query the current AP state (reads wlan2 through Java NetworkInterface, no shell dependency, no root needed). */
    fun status(): WifiApInfo {
        val up = try {
            val nif = java.net.NetworkInterface.getByName("wlan2")
            nif?.inetAddresses?.asSequence()?.any { it.hostAddress?.startsWith("192.168.43.") == true } == true
        } catch (e: Exception) {
            false
        }
        return WifiApInfo(up = up, ssid = ssid, pass = pass, ip = Protocol.AP_IP, port = Protocol.WIFI_PORT)
    }

    /**
     * Trigger the SoftAP start (idempotent) and report the current state after a short wait.
     * NOTE: on this device the AP can take 5-90 seconds to come up fully (depends on STA state), so this does not
     * wait long; once `cmd wifi start-softap` is issued the system brings the AP up in the background, and the phone
     * polls ENSURE_WIFI_AP until up=true.
     */
    suspend fun ensureStarted(): Boolean {
        val client = RootDaemonClient()
        if (!client.isRoot()) {
            Log.w(TAG, "root daemon 未就緒，無法啟動 SoftAP")
            return false
        }
        try {
            // background it: start-softap can block for a long time during wifi service transitions, so do not wait synchronously
            client.exec("cmd wifi start-softap $ssid wpa2 $pass >/dev/null 2>&1 &")
            repeat(8) {
                if (status().up) return true
                kotlinx.coroutines.delay(1000)
            }
            return status().up
        } catch (e: Exception) {
            Log.e(TAG, "ensureStarted failed: ${e.message}")
            return false
        }
    }

    companion object {
        private const val TAG = "WifiHotspot"
    }
}
