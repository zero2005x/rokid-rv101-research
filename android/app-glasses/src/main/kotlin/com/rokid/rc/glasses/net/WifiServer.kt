package com.rokid.rc.glasses.net

import android.util.Log
import com.rokid.rc.glasses.AppGraph
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.ServerSocket
import java.net.Socket

/**
 * Glasses-side WiFi TCP server (media channel, port 8555).
 *
 * Completely symmetric with BluetoothServer: after accept it wraps the socket stream as a [ConnectionHandler]
 * (the same FrameCodec/CommandRouter), so the photo/video/thumbnail transfer logic needs zero changes.
 * Single connection (a new connection replaces the old one); the phone connects in over the SoftAP (192.168.43.1).
 */
class WifiServer(private val graph: AppGraph) {

    private var serverSocket: ServerSocket? = null
    private var acceptJob: Job? = null

    fun start(port: Int = com.rokid.rc.shared.protocol.Protocol.WIFI_PORT,
              onStarted: () -> Unit = {},
              onError: (String) -> Unit = {}) {
        acceptJob = graph.scope.launch(Dispatchers.IO) {
            try {
                val ss = ServerSocket(port)
                ss.reuseAddress = true
                serverSocket = ss
                onStarted()
                Log.i(TAG, "WifiServer listening on :$port")
                while (isActive) {
                    val socket = withContext(Dispatchers.IO) { ss.accept() }
                    Log.i(TAG, "wifi connection accepted from ${socket.remoteSocketAddress}")
                    graph.connection?.close()
                    var handler: ConnectionHandler? = null
                    handler = ConnectionHandler(
                        input = socket.getInputStream(),
                        output = socket.getOutputStream(),
                        onClosed = {
                            runCatching { socket.close() }
                            graph.wifiConnected = false
                            if (graph.connection === handler) graph.connection = null
                        },
                        router = CommandRouter(graph)
                    )
                    graph.connection = handler
                    graph.wifiConnected = true
                    launch { handler.run() }
                }
            } catch (e: Exception) {
                Log.e(TAG, "WifiServer error: ${e.message}")
                onError(e.message ?: "wifi server error")
            }
        }
    }

    fun stop() {
        acceptJob?.cancel()
        runCatching { serverSocket?.close() }
        serverSocket = null
    }

    companion object {
        private const val TAG = "WifiServer"
    }
}
