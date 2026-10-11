package com.rokid.rc.shared.net

import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.IOException

/**
 * 長度前綴 frame 編解碼（藍牙 RFCOMM 串流之上，傳輸無關）。
 *
 * Frame: [payloadLength: 4B big-endian][opcode: 1B][payload: N B]
 *   payloadLength = 1 + N
 *   opcode 0x01 = JSON 控制訊息；0x02 = 二進位資料塊。
 */
object FrameCodec {
    const val OP_JSON = 0x01
    const val OP_BINARY = 0x02
    const val MAX_FRAME = 256 * 1024 * 1024 // 256 MiB 上限，防呆
    const val CHUNK_SIZE = 256 * 1024       // 二進位分塊大小

    data class Frame(val opcode: Int, val payload: ByteArray)

    @Throws(IOException::class)
    fun readFrame(din: DataInputStream): Frame {
        val len = din.readInt()
        if (len < 1 || len > MAX_FRAME) throw IOException("bad frame length: $len")
        val opcode = din.readUnsignedByte()
        val payload = ByteArray(len - 1)
        din.readFully(payload)
        return Frame(opcode, payload)
    }

    @Throws(IOException::class)
    fun writeJson(dout: DataOutputStream, json: String) {
        write(dout, OP_JSON, json.toByteArray(Charsets.UTF_8))
    }

    @Throws(IOException::class)
    fun writeBinary(dout: DataOutputStream, data: ByteArray) {
        write(dout, OP_BINARY, data)
    }

    @Throws(IOException::class)
    private fun write(dout: DataOutputStream, opcode: Int, payload: ByteArray) {
        dout.writeInt(1 + payload.size)
        dout.writeByte(opcode)
        dout.write(payload)
        dout.flush()
    }
}
