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
 * 手機端 WiFi 媒體通道管理：
 *   1. 記錄眼鏡回報的 AP 資訊（ssid/pass/ip/port，來自 DEVICE_INFO / WIFI_AP_STATUS）
 *   2. 引導手機加入眼鏡 SoftAP（先試 WifiNetworkSpecifier 系統彈窗，失敗回傳 false → UI 引導手動）
 *   3. 建立 TCP 連線（[TcpClient]）
 *
 * 控制指令維持藍牙；本類只負責「媒體通道」的 WiFi 部分，藍牙回退由 Remote 層決定。
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
     * `WifiNetworkSpecifier` 回報「已可用」的 [Network]。
     *
     * ⚠ 為什麼需要它（2026-09-13 實測）：手機 App 雖然在 Manifest 宣告了
     * `NEARBY_WIFI_DEVICES`，但執行期**從未要求**該權限（`granted=false`），
     * 在 Android 13 上 `WifiInfo.getSSID()` 便只回 `<unknown ssid>`，
     * 導致 [currentSsid]/[isOnAp] **永遠是 false** → 加入 AP 後仍判定失敗、WiFi 傳輸完全不能用。
     * 改為優先看「這個 Network 的本機位址是否落在眼鏡 AP 的子網」，不需要任何權限。
     */
    @Volatile
    private var joinedNetwork: Network? = null

    /**
     * 目前生效的 specifier callback。
     *
     * **刻意保持註冊**：`WifiNetworkSpecifier` 建立的連線綁在這個 request 上，一旦反註冊，
     * 系統會把該 WiFi 連線拆掉（已建立的 TCP socket 也會一起死）。
     * 只有「要發出新的加入請求」時才放掉舊的，避免每次重試都疊一個 callback。
     */
    private var activeCallback: ConnectivityManager.NetworkCallback? = null

    /** 目前連到的 WiFi SSID（去引號）；非 WiFi 時 null。 */
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
     * 是否已在眼鏡的 AP 上。
     *
     * 判定順序：
     *  1. **本機位址**是否落在眼鏡 AP 子網（`192.168.43.x`）——最可靠且**不需權限**；
     *  2. 退而比對 SSID（需要 NEARBY_WIFI_DEVICES／位置權限才會拿到真值）。
     */
    fun isOnAp(): Boolean = isOnApSubnet() || currentSsid() == apInfo.ssid

    /** 檢查 [network] 的本機 IPv4 是否在眼鏡 AP 子網內（預設 192.168.43.0/24）。 */
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
     * 已加入的眼鏡 AP [Network]；呼叫端必須用它來**綁定 socket**，
     * 否則 TCP 會走預設網路而不是眼鏡 AP（見 TcpClient.connect 的說明）。
     */
    fun joinedApNetwork(): Network? = joinedNetwork?.takeIf { onApSubnet(it) }

    /** 目前連到眼鏡 AP 時的本機 IP（測試／顯示用）。 */
    fun currentLocalIp(): String? = runCatching {
        val n = joinedNetwork ?: return null
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        cm.getLinkProperties(n)?.linkAddresses?.firstOrNull()?.address?.hostAddress
    }.getOrNull()

    /**
     * 用 WifiNetworkSpecifier 請求加入眼鏡 AP（系統會彈「加入此網路?」確認框）。
     * 等待最多 [timeoutMs]：期間輪詢是否已連上。成功回 true；失敗/取消/逾時回 false。
     * Android 10+ 支援；MIUI 亦支援（需位置權限或 12+ 的 NEARBY_WIFI_DEVICES 已於 Manifest 宣告）。
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
                        // 等到真的有 IP 才算「在 AP 上」（onAvailable 當下 linkAddress 可能還是空的）
                        Log.i(TAG, "specifier: network available (localIp=${currentLocalIp()})")
                    }

                    override fun onUnavailable() {
                        Log.w(TAG, "specifier: network unavailable (user declined?)")
                        if (cont.isActive) cont.resume(false)
                    }

                    override fun onLost(network: Network) {
                        // 連上後又跳走（例如使用者切回原本 WiFi）
                        if (joinedNetwork == network) joinedNetwork = null
                        if (cont.isActive) cont.resume(false)
                    }
                }
                activeCallback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
                activeCallback = callback
                cm.requestNetwork(request, callback)
                // 輪詢 isOnAp() 直到逾時
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
                // 單一取消 handler：反註冊 network callback + 停掉輪詢執行緒
                cont.invokeOnCancellation {
                    runCatching { cm.unregisterNetworkCallback(callback) }
                    poller.interrupt()
                }
            }.also {
                // 成功後等 IP/DHCP 就緒再回報
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
