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
 * WifiJoin — 讓眼鏡加入開發者網段（兩條路徑，依權限自動選擇）。
 *
 * 路徑 **A（免 root，App 權限）**：`WifiNetworkSuggestion`
 *   - 這是 Android 10+ 給一般 App 的唯一合法介面；建議清單會持久化（重開機仍有效）。
 *   - 需要：`ACCESS_FINE_LOCATION`（Android 12；13+ 另需 `NEARBY_WIFI_DEVICES`）。
 *   - 限制：若本機有多個已儲存網路，系統可能彈出「要改用哪個網路」通知（需使用者點一次）。
 *     本機眼鏡**無螢幕**，若彈窗無法點擊，請改用路徑 B。
 *
 * 路徑 **B（需 root／shell）**：`cmd wifi connect-network <ssid> wpa2 <pass>`
 *   - 走既有 root_daemon socket；適合研究版或有提權的裝置。
 *   - 這條路徑也是唯一能**可靠無人介入**的方式。
 *
 * ⚠ 本機 STA 與 SoftAP **互斥**：加入 STA 會讓 SoftAP 關閉（反之亦然）。
 *   開發模式結束時由 [DevModeController] 依原狀態決定是否還原 SoftAP。
 */
class WifiJoin(private val ctx: Context) {

    private val wifi = ctx.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    private val cm = ctx.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    data class Cred(val ssid: String, val pass: String, val security: String = "wpa2")

    enum class Method { SUGGESTION, ROOT_SHELL, NONE }

    /** 憑證持久化（供開發模式重開後自動使用）。 */
    fun saveCred(c: Cred) = prefs().edit()
        .putString(K_SSID, c.ssid).putString(K_PASS, c.pass).putString(K_SEC, c.security).apply()

    fun loadCred(): Cred? {
        val s = prefs().getString(K_SSID, null) ?: return null
        return Cred(s, prefs().getString(K_PASS, "") ?: "", prefs().getString(K_SEC, "wpa2") ?: "wpa2")
    }

    fun clearCred() = prefs().edit().remove(K_SSID).remove(K_PASS).remove(K_SEC).apply()

    private fun prefs() = ctx.getSharedPreferences("dev_mode_wifi", Context.MODE_PRIVATE)

    /** 目前是否連上該 SSID 且取得 IPv4。 */
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

    /** 目前對外可用的 IPv4（STA wlan0 優先，SoftAP wlan2 次之）。 */
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

    /** 是否具備路徑 A 所需權限。 */
    fun hasSuggestionPermissions(): Boolean {
        val fine = ctx.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (Build.VERSION.SDK_INT >= 33) {
            val nearby = ctx.checkSelfPermission("android.permission.NEARBY_WIFI_DEVICES") == PackageManager.PERMISSION_GRANTED
            return fine || nearby
        }
        return fine
    }

    /**
     * 加入網段。回傳 (成功, 說明, 使用的方法)。
     * @param preferRoot true = 先試路徑 B（可靠、無人介入）；false = 先試路徑 A（免 root）
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
                    // 先確保 WiFi 已啟用（重開機後 adbd 重啟會讓 WiFi 工作被殺；此步驟冪等）
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

    /** 路徑 A：建立 suggestion 並等待 NetworkCallback 給出該網路的 IP。 */
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
