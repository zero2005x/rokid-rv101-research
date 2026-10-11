package com.rokid.rc.glasses.net

import com.rokid.rc.shared.net.FrameCodec
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.InputStream
import java.io.OutputStream

/** 單一手機連線的讀寫處理（串流導向，藍牙 RFCOMM）。 */
class ConnectionHandler(
    private val input: InputStream,
    private val output: OutputStream,
    private val onClosed: () -> Unit,
    private val router: CommandRouter
) {
    private val writeMutex = Mutex()
    private val din = DataInputStream(BufferedInputStream(input))
    private val dout = DataOutputStream(BufferedOutputStream(output))

    suspend fun run() {
        try {
            while (true) {
                val frame = withContext(Dispatchers.IO) { FrameCodec.readFrame(din) }
                if (frame.opcode == FrameCodec.OP_JSON) {
                    val json = String(frame.payload, Charsets.UTF_8)
                    router.handle(this, json)
                }
                // 眼鏡端不收二進位，忽略其他 opcode
            }
        } catch (e: Exception) {
            // EOF / 連線中斷
        } finally {
            close()
            onClosed()
        }
    }

    suspend fun sendJson(json: String) = writeMutex.withLock {
        withContext(Dispatchers.IO) { FrameCodec.writeJson(dout, json) }
    }

    suspend fun sendBinary(data: ByteArray) = writeMutex.withLock {
        withContext(Dispatchers.IO) { FrameCodec.writeBinary(dout, data) }
    }

    fun close() {
        try {
            input.close()
        } catch (_: Exception) {
        }
        try {
            output.close()
        } catch (_: Exception) {
        }
    }
}
