package com.rokid.rc.glasses.net

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothServerSocket
import android.util.Log
import com.rokid.rc.glasses.AppGraph
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.UUID

/** Glasses-side Bluetooth RFCOMM server. Single connection by default; a new connection replaces the old one. */
class BluetoothServer(private val graph: AppGraph) {

    private var serverSocket: BluetoothServerSocket? = null
    private var acceptJob: Job? = null

    fun start(onStarted: () -> Unit, onError: (String) -> Unit) {
        acceptJob = graph.scope.launch {
            // the Bluetooth stack may not be ready yet (Rokid restarts Bluetooth after boot), so retry and wait
            var adapter: BluetoothAdapter? = null
            for (attempt in 1..15) {
                adapter = BluetoothAdapter.getDefaultAdapter()
                if (adapter != null && adapter.isEnabled) break
                delay(2000)
            }
            if (adapter == null || !adapter.isEnabled) {
                onError("藍牙不可用或未開啟")
                return@launch
            }
            try {
                // BLUETOOTH_CONNECT is already declared in the Manifest and requested by MainActivity
                @Suppress("MissingPermission")
                val ss = adapter.listenUsingRfcommWithServiceRecord(SERVICE_NAME, SPP_UUID)
                serverSocket = ss
                onStarted()
                while (isActive) {
                    val socket = withContext(Dispatchers.IO) { ss.accept() }
                    Log.i(TAG, "RFCOMM connection accepted from ${socket.remoteDevice.address}")
                    graph.connection?.close()
                    var handler: ConnectionHandler? = null
                    handler = ConnectionHandler(
                        input = socket.inputStream,
                        output = socket.outputStream,
                        onClosed = {
                            graph.btConnected = false
                            if (graph.connection === handler) graph.connection = null
                        },
                        router = CommandRouter(graph)
                    )
                    graph.connection = handler
                    graph.btConnected = true
                    launch(Dispatchers.IO) { handler.run() }
                    // after a successful connection, turn the privacy LED off by default (when enabled)
                    if (graph.autoLedOffOnConnect) {
                        Log.i(TAG, "連線成功，自動關閉隱私指示燈")
                        graph.scope.launch { graph.led.setLed(false) }
                    }
                }
            } catch (e: Exception) {
                if (isActive) onError(e.message ?: "bt server error")
            }
        }
    }

    fun stop() {
        acceptJob?.cancel()
        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
        graph.connection?.close()
        graph.connection = null
    }

    companion object {
        private const val TAG = "BluetoothServer"
        val SPP_UUID: UUID = UUID.fromString(com.rokid.rc.shared.protocol.Protocol.SPP_UUID)
        const val SERVICE_NAME = "RokidCapture"
    }
}
