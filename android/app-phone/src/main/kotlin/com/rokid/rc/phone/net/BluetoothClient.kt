package com.rokid.rc.phone.net

import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.UUID

/**
 * Phone-side Bluetooth RFCOMM link (transfer logic in [LinkClient]).
 * Control commands and the fallback media channel can both use this link.
 */
class BluetoothClient : LinkClient() {

    private var socket: BluetoothSocket? = null

    // BLUETOOTH_CONNECT is declared in the Manifest and requested before connecting
    @Suppress("MissingPermission")
    suspend fun connect(device: BluetoothDevice): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            if (connected) return@withContext Result.success(Unit)
            val s = device.createRfcommSocketToServiceRecord(SPP_UUID)
            s.connect()
            socket = s
            attachStreams(s.inputStream, s.outputStream)
            Result.success(Unit)
        } catch (e: Exception) {
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

    companion object {
        val SPP_UUID: UUID = UUID.fromString(com.rokid.rc.shared.protocol.Protocol.SPP_UUID)
    }
}
