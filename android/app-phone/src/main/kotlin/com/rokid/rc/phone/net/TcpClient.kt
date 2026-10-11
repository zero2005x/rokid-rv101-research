package com.rokid.rc.phone.net

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import android.net.Network
import java.net.InetSocketAddress
import java.net.Socket

/**
 * Phone-side WiFi TCP link (media channel): connects to the glasses SoftAP at 192.168.43.1:8555.
 * The transfer logic is identical to Bluetooth ([LinkClient]); only the transport medium differs.
 */
class TcpClient : LinkClient() {

    private var socket: Socket? = null

    /**
     * Connect to the glasses' media TCP service.
     *
     * ⚠ [network] matters (measured 2026-09-13): a glasses SoftAP joined with `WifiNetworkSpecifier` **does not
     * become the app's default network**, so a plain `Socket()` sends traffic over mobile data / the original WiFi,
     * producing `failed to connect to /192.168.43.1 from /10.225.98.247` (the measured error message).
     * The socket must be bound to that [Network] ([Network.bindSocket]) to really go over the glasses AP.
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
