package com.rokid.rc.glasses.net

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.MacAddress
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.NetworkSpecifier
import android.net.wifi.WifiNetworkSpecifier
import android.net.wifi.WifiNetworkSuggestion
import android.net.wifi.WifiManager
import android.os.Build
import android.util.Log
import com.rokid.rc.glasses.ghost.RootDaemonClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.net.Inet4Address
import java.net.NetworkInterface

/**
 * WifiJoin — makes the glasses join a developer subnet (two paths, chosen automatically by privilege).
 *
 * Path **A (no root, app permission)**: `WifiNetworkSuggestion`
 *   - This is the only legitimate interface Android 10+ offers an ordinary app; the suggestion list is persisted (still valid after reboot).
 *   - Requires: `ACCESS_FINE_LOCATION` (Android 12; 13+ additionally needs `NEARBY_WIFI_DEVICES`).
 *   - Limitation: if the device has several saved networks, the system may pop a "要改用哪個網路" notification (the user must tap once).
 *     These glasses have **no screen**, so if the dialog cannot be tapped, use path B instead.
 *
 * Path **B (requires root/shell)**: `cmd wifi connect-network <ssid> wpa2 <pass>`
 *   - Goes through the existing root_daemon socket; suited to research units or devices with escalation.
 *   - This path is also the only **reliably unattended** one.
 *
 * ⚠ On this device STA and SoftAP are **mutually exclusive**: joining STA turns the SoftAP off (and vice versa).
 *   When dev mode ends, [DevModeController] decides from the original state whether to restore the SoftAP.
 */
class WifiJoin(private val ctx: Context) {

    private val wifi = ctx.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    private val cm = ctx.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    data class Cred(val ssid: String, val pass: String, val security: String = "wpa2")

    enum class Method { SUGGESTION, ROOT_SHELL, NONE }

    /** Credential persistence (so dev mode can reuse it after a restart). */
    fun saveCred(c: Cred) = prefs().edit()
        .putString(K_SSID, c.ssid).putString(K_PASS, c.pass).putString(K_SEC, c.security).apply()

    fun loadCred(): Cred? {
        val s = prefs().getString(K_SSID, null) ?: return null
        return Cred(s, prefs().getString(K_PASS, "") ?: "", prefs().getString(K_SEC, "wpa2") ?: "wpa2")
    }

    fun clearCred() = prefs().edit().remove(K_SSID).remove(K_PASS).remove(K_SEC).apply()

    private fun prefs() = ctx.getSharedPreferences("dev_mode_wifi", Context.MODE_PRIVATE)

    /** Whether currently connected to that SSID with an IPv4 address. */
    fun isConnected(ssid: String): Boolean {
        val cur = currentSsid() ?: return false
        return cur.trim('"') == ssid.trim('"') && ipv4() != null
    }

    fun currentSsid(): String? {
        return runCatching {
            @Suppress("DEPRECATION") val info = wifi.connectionInfo
            @Suppress("DEPRECATION") info?.ssid
        }.getOrNull()
    }

    /** Currently usable external IPv4 (STA wlan0 first, SoftAP wlan2 second). */
    fun ipv4(): String? {
        return try {
            val nifs = NetworkInterface.getNetworkInterfaces()
            var ap: String? = null
            while (nifs.hasMoreElements()) {
                val nif = nifs.nextElement()
                if (!nif.isUp || nif.isLoopback) continue
                val addrs = nif.inetAddresses
                while (addrs.hasMoreElements()) {
                    val a = addrs.nextElement()
                    if (a is Inet4Address && !a.isLoopbackAddress) {
                        val h = a.hostAddress ?: continue
                        if (nif.name == "wlan0") return h
                        if (nif.name == "wlan2" && ap == null) ap = h
                    }
                }
            }
            ap
        } catch (e: Exception) {
            null
        }
    }

    /** Whether the permissions required by path A are present. */
    fun hasSuggestionPermissions(): Boolean {
        val fine = ctx.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (Build.VERSION.SDK_INT >= 33) {
            val nearby = ctx.checkSelfPermission("android.permission.NEARBY_WIFI_DEVICES") == PackageManager.PERMISSION_GRANTED
            return fine || nearby
        }
        return fine
    }

    /**
     * Join the subnet. Returns (success, explanation, method used).
     * @param preferRoot true = try path B first (reliable, unattended); false = try path A first (no root)
     */
    suspend fun join(c: Cred, preferRoot: Boolean = false, timeoutMs: Long = 45_000L): Triple<Boolean, String, Method> {
        val root = RootDaemonClient()
        val order = if (preferRoot) listOf(Method.ROOT_SHELL, Method.SUGGESTION) else listOf(Method.SUGGESTION, Method.ROOT_SHELL)
        var lastMsg = ""
        for (m in order) {
            when (m) {
                Method.SUGGESTION -> {
                    if (!hasSuggestionPermissions()) { lastMsg = "缺少定位/附近裝置權限（路徑 A 不可用）"; continue }
                    val ok = runCatching { suggestAndWait(c, timeoutMs) }.getOrElse { lastMsg = "suggestion: ${it.message}"; false }
                    if (ok) return Triple(true, "已透過 WifiNetworkSuggestion 加入", Method.SUGGESTION)
                    if (lastMsg.isEmpty()) lastMsg = "suggestion 逾時未取得 IP（可能被系統彈窗阻擋）"
                }
                Method.ROOT_SHELL -> {
                    if (!root.isRoot()) { lastMsg = "無 root（路徑 B 不可用）"; continue }
                    // first make sure WiFi is enabled (after a reboot the adbd restart kills the WiFi job; this step is idempotent)
                    root.exec("cmd wifi set-wifi-enabled enabled", 20_000L)
                    delay(2000)
                    val out = root.exec("cmd wifi connect-network ${c.ssid} ${c.security} ${c.pass}", 40_000L)
                    Log.i(TAG, "connect-network out=${out.trim()}")
                    val ok = waitForIp(c.ssid, timeoutMs)
                    if (ok) return Triple(true, "已透過 cmd wifi connect-network 加入", Method.ROOT_SHELL)
                    lastMsg = "connect-network 後仍未取得 IP：${out.trim().take(120)}"
                }
                Method.NONE -> {}
            }
        }
        return Triple(false, lastMsg.ifEmpty { "無法加入 ${c.ssid}" }, Method.NONE)
    }

    /** Path A: create the suggestion and wait for the NetworkCallback to hand that network an IP. */
    private suspend fun suggestAndWait(c: Cred, timeoutMs: Long): Boolean {
        val specifier: NetworkSpecifier = WifiNetworkSpecifier.Builder()
            .setSsid(c.ssid)
            .apply { if (c.pass.isNotEmpty()) setWpa2Passphrase(c.pass) }
            .build()

        val suggestion = WifiNetworkSuggestion.Builder()
            .setSsid(c.ssid)
            .apply { if (c.pass.isNotEmpty()) setWpa2Passphrase(c.pass) }
            .build()

        val status = wifi.addNetworkSuggestions(listOf(suggestion))
        Log.i(TAG, "addNetworkSuggestions status=$status")
        if (status != WifiManager.STATUS_NETWORK_SUGGESTIONS_SUCCESS) {
            Log.w(TAG, "suggestion 被拒（status=$status），改走 requestNetwork 一次")
        }

        var got: Network? = null
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { got = network }
        }
        val req = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .setNetworkSpecifier(specifier)
            .build()
        cm.requestNetwork(req, cb)
        try {
            val deadline = System.currentTimeMillis() + timeoutMs
            while (System.currentTimeMillis() < deadline) {
                if (got != null && ipv4() != null) return true
                delay(1000)
            }
            return ipv4() != null
        } finally {
            runCatching { cm.unregisterNetworkCallback(cb) }
        }
    }

    private suspend fun waitForIp(ssid: String, timeoutMs: Long): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (ipv4() != null) return true
            delay(1000)
        }
        return false
    }

    companion object {
        const val TAG = "WifiJoin"
        private const val K_SSID = "ssid"
        private const val K_PASS = "pass"
        private const val K_SEC = "security"
    }
}
