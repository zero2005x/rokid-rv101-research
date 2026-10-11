package com.rokid.rc.phone.net

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import android.util.Log
import com.rokid.rc.shared.protocol.Protocol
import com.rokid.rc.shared.protocol.WifiApInfo
import kotlinx.coroutines.delay
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume

/**
 * Phone-side WiFi media channel management:
 *   1. record the AP information reported by the glasses (ssid/pass/ip/port, from DEVICE_INFO / WIFI_AP_STATUS)
 *   2. guide the phone to join the glasses SoftAP (try the WifiNetworkSpecifier system dialog first; on failure return false → the UI guides a manual join)
 *   3. establish the TCP connection ([TcpClient])
 *
 * Control commands stay on Bluetooth; this class handles only the WiFi part of the "media channel", and the Bluetooth fallback is decided by the Remote layer.
 */
class WifiLink(private val context: Context) {

    @Volatile
    var apInfo: WifiApInfo = WifiApInfo(
        up = false, ssid = Protocol.AP_SSID, pass = Protocol.AP_PASS,
        ip = Protocol.AP_IP, port = Protocol.WIFI_PORT
    )

    @Volatile
    var onStateChanged: (() -> Unit)? = null

    private val wifiManager: WifiManager?
        get() = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager

    /**
     * The [Network] that `WifiNetworkSpecifier` reported as "available".
     *
     * ⚠ Why it is needed (measured 2026-09-13): although the phone app declared `NEARBY_WIFI_DEVICES` in the Manifest,
     * it **never requested** that permission at runtime (`granted=false`), so on Android 13 `WifiInfo.getSSID()` returns
     * only `<unknown ssid>`, which made [currentSsid]/[isOnAp] **always false** → joining the AP was still judged a failure
     * and WiFi transfer was completely unusable. It now prefers checking "whether this Network's local address falls in the
     * glasses AP subnet", which needs no permission at all.
     */
    @Volatile
    private var joinedNetwork: Network? = null

    /**
     * The specifier callback currently in effect.
     *
     * **Deliberately kept registered**: the connection created by `WifiNetworkSpecifier` is bound to this request, so
     * unregistering makes the system tear that WiFi connection down (an already established TCP socket dies with it).
     * The old one is released only when "a new join request is about to be issued", so retries do not pile up callbacks.
     */
    private var activeCallback: ConnectivityManager.NetworkCallback? = null

    /** The WiFi SSID currently connected (quotes stripped); null when not on WiFi. */
    fun currentSsid(): String? {
        return try {
            val info = wifiManager?.connectionInfo ?: return null
            val ssid = info.ssid ?: return null
            if (ssid == "<unknown ssid>") null else ssid.trim('"')
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Whether it is already on the glasses AP.
     *
     * Decision order:
     *  1. whether the **local address** falls in the glasses AP subnet (`192.168.43.x`) — the most reliable, and it **needs no permission**;
     *  2. failing that, compare the SSID (the real value requires NEARBY_WIFI_DEVICES / location permission).
     */
    fun isOnAp(): Boolean = isOnApSubnet() || currentSsid() == apInfo.ssid

    /** Check whether [network]'s local IPv4 is inside the glasses AP subnet (default 192.168.43.0/24). */
    private fun onApSubnet(network: Network?): Boolean {
        val n = network ?: return false
        return runCatching {
            val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            val prefix = apInfo.ip.substringBeforeLast('.') + "."   // e.g. "192.168.43."
            cm.getLinkProperties(n)?.linkAddresses.orEmpty().any { la ->
                val host = la.address?.hostAddress ?: return@any false
                host.startsWith(prefix)
            }
        }.getOrDefault(false)
    }

    private fun isOnApSubnet(): Boolean = onApSubnet(joinedNetwork)

    /**
     * The glasses AP [Network] that has been joined; the caller must use it to **bind the socket**,
     * otherwise TCP goes over the default network instead of the glasses AP (see the note in TcpClient.connect).
     */
    fun joinedApNetwork(): Network? = joinedNetwork?.takeIf { onApSubnet(it) }

    /** The local IP while connected to the glasses AP (for testing/display). */
    fun currentLocalIp(): String? = runCatching {
        val n = joinedNetwork ?: return null
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        cm.getLinkProperties(n)?.linkAddresses?.firstOrNull()?.address?.hostAddress
    }.getOrNull()

    /**
     * Request to join the glasses AP with WifiNetworkSpecifier (the system pops a "join this network?" confirmation).
     * Waits up to [timeoutMs], polling whether it is connected. Returns true on success; false on failure/cancel/timeout.
     * Supported on Android 10+; MIUI supports it too (it needs location permission, or NEARBY_WIFI_DEVICES on 12+, which is declared in the Manifest).
     */
    suspend fun requestJoinAp(timeoutMs: Long = 30_000L): Boolean {
        if (isOnAp()) return true
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false
        return try {
            val specifier = WifiNetworkSpecifier.Builder()
                .setSsid(apInfo.ssid)
                .setWpa2Passphrase(apInfo.pass)
                .build()
            val request = NetworkRequest.Builder()
                .addTransportType(android.net.NetworkCapabilities.TRANSPORT_WIFI)
                .setNetworkSpecifier(specifier)
                .build()
            val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

            suspendCancellableCoroutine { cont ->
                val callback = object : ConnectivityManager.NetworkCallback() {
                    override fun onAvailable(network: Network) {
                        joinedNetwork = network
                        // only when it really has an IP does it count as "on the AP" (linkAddress may still be empty when onAvailable fires)
                        Log.i(TAG, "specifier: network available (localIp=${currentLocalIp()})")
                    }

                    override fun onUnavailable() {
                        Log.w(TAG, "specifier: network unavailable (user declined?)")
                        if (cont.isActive) cont.resume(false)
                    }

                    override fun onLost(network: Network) {
                        // connected and then jumped away again (e.g. the user switched back to the original WiFi)
                        if (joinedNetwork == network) joinedNetwork = null
                        if (cont.isActive) cont.resume(false)
                    }
                }
                activeCallback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
                activeCallback = callback
                cm.requestNetwork(request, callback)
                // poll isOnAp() until the timeout
                val deadline = System.currentTimeMillis() + timeoutMs
                val poller = Thread {
                    while (System.currentTimeMillis() < deadline && cont.isActive) {
                        if (isOnAp()) {
                            cont.resume(true)
                            return@Thread
                        }
                        try {
                            Thread.sleep(500)
                        } catch (_: InterruptedException) {
                            return@Thread
                        }
                    }
                    if (cont.isActive) cont.resume(false)
                }.apply { isDaemon = true; start() }
                // single cancellation handler: unregister the network callback + stop the polling thread
                cont.invokeOnCancellation {
                    runCatching { cm.unregisterNetworkCallback(callback) }
                    poller.interrupt()
                }
            }.also {
                // after success, wait for IP/DHCP to settle before reporting
                if (it) delay(1500)
            }
        } catch (e: Exception) {
            Log.e(TAG, "requestJoinAp failed: ${e.message}")
            false
        }
    }

    companion object {
        private const val TAG = "WifiLink"
    }
}
