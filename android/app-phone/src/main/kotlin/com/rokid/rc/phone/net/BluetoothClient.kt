package com.rokid.rc.phone.net

import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.UUID

/**
 * 手機端藍牙 RFCOMM 鏈路（傳輸邏輯見 [LinkClient]）。
 * 控制指令/回退媒體通道皆可走此鏈路。
 */
class BluetoothClient : LinkClient() {

    private var socket: BluetoothSocket? = null

    // BLUETOOTH_CONNECT 已於 Manifest 宣告並於連線前請求
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
