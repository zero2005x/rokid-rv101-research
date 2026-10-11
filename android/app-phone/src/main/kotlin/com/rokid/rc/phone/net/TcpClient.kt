package com.rokid.rc.phone.net

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import android.net.Network
import java.net.InetSocketAddress
import java.net.Socket

/**
 * 手機端 WiFi TCP 鏈路（媒體通道）：連眼鏡 SoftAP 的 192.168.43.1:8555。
 * 傳輸邏輯與藍牙完全一致（[LinkClient]），僅傳輸介質不同。
 */
class TcpClient : LinkClient() {

    private var socket: Socket? = null

    /**
     * 連線到眼鏡的媒體 TCP 服務。
     *
     * ⚠ [network] 很重要（2026-09-13 實測）：用 `WifiNetworkSpecifier` 加入的眼鏡 SoftAP
     * **不會成為 App 的預設網路**，若直接 `Socket()`，流量會走行動網路／原本的 WiFi，
     * 導致 `failed to connect to /192.168.43.1 from /10.225.98.247`（實測錯誤訊息）。
     * 必須把 socket 綁到那個 [Network]（[Network.bindSocket]），才會真的走眼鏡 AP。
     */
    suspend fun connect(
        host: String,
        port: Int,
        timeoutMs: Int = 6000,
        network: Network? = null
    ): Result<Unit> =
        withContext(Dispatchers.IO) {
            try {
                if (connected) return@withContext Result.success(Unit)
                val s = Socket()
                network?.let { runCatching { it.bindSocket(s) } }
                s.tcpNoDelay = true
                s.keepAlive = true
                s.connect(InetSocketAddress(host, port), timeoutMs)
                socket = s
                attachStreams(s.getInputStream(), s.getOutputStream())
                Result.success(Unit)
            } catch (e: Exception) {
                runCatching { socket?.close() }
                socket = null
                Result.failure(e)
            }
        }

    override fun closeTransport() {
        try {
            socket?.close()
        } catch (_: Exception) {
        }
        socket = null
    }
}
