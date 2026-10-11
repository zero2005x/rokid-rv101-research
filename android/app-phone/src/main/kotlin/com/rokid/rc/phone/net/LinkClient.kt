package com.rokid.rc.phone.net

import com.rokid.rc.shared.net.FrameCodec
import com.rokid.rc.shared.protocol.Msg
import com.rokid.rc.shared.protocol.MsgType
import com.rokid.rc.shared.protocol.TransferMeta
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap

/**
 * 傳輸無關的串流鏈路基類：request/response 配對（id ↔ replyTo）＋
 * 眼鏡→手機二進位檔案傳輸（MEDIA_META → chunks → TRANSFER_END）。
 *
 * 子類：[BluetoothClient]（RFCOMM）、[TcpClient]（WiFi/SoftAP TCP 8555）。
 * 建立實體連線後呼叫 [attachStreams] 即啟用讀迴圈。
 */
abstract class LinkClient {

    protected val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val writeMutex = Mutex()
    private val pending = ConcurrentHashMap<Long, CompletableDeferred<JSONObject>>()
    private val fileRequests = ConcurrentHashMap<Long, FileRequest>()

    protected var din: DataInputStream? = null
    protected var dout: DataOutputStream? = null
    private var readJob: Job? = null
    private var currentTransfer: CurrentTransfer? = null
    private var nextId = 1L

    @Volatile
    var connected = false
        protected set

    @Volatile
    var onDisconnected: (() -> Unit)? = null

    /** 實體連線建立（含 WiFi 通道自動建立）時通知；可能來自背景執行緒。 */
    @Volatile
    var onConnected: (() -> Unit)? = null

    /** 未配對的推送訊息（如 PROGRESS）。 */
    @Volatile
    var onPush: ((JSONObject) -> Unit)? = null

    /** 子類建立實體連線後呼叫：接上串流並啟動讀迴圈。 */
    protected fun attachStreams(input: java.io.InputStream, output: java.io.OutputStream) {
        din = DataInputStream(BufferedInputStream(input, 64 * 1024))
        dout = DataOutputStream(BufferedOutputStream(output, 64 * 1024))
        connected = true
        onConnected?.invoke()
        readJob = scope.launch { readLoop() }
    }

    /** 子類關閉實體傳輸（socket）。 */
    protected abstract fun closeTransport()

    open fun disconnect() {
        // 顯式斷線（使用者按「斷線」、關閉 WiFi 通道）也要通知 UI；
        // 原本只有 readLoop 的 finally 會通知，導致主動關閉時畫面停在舊的傳輸方式
        val wasConnected = connected
        connected = false
        readJob?.cancel()
        try {
            closeTransport()
        } catch (_: Exception) {
        }
        din = null
        dout = null
        val io = IOException("disconnected")
        pending.values.forEach { it.completeExceptionally(io) }
        pending.clear()
        fileRequests.values.forEach { it.deferred.completeExceptionally(io) }
        fileRequests.clear()
        try {
            currentTransfer?.out?.close()
        } catch (_: Exception) {
        }
        currentTransfer = null
        if (wasConnected) onDisconnected?.invoke()
    }

    suspend fun request(msg: JSONObject, timeoutMs: Long = 30_000L): JSONObject {
        val id = nextId++
        msg.put(Msg.KEY_ID, id)
        val d = CompletableDeferred<JSONObject>()
        pending[id] = d
        try {
            send(msg)
            return withTimeout(timeoutMs) { d.await() }
        } catch (e: kotlinx.coroutines.TimeoutCancellationException) {
            throw IOException("request timeout")
        } finally {
            pending.remove(id)
        }
    }

    suspend fun requestFile(
        msg: JSONObject,
        targetDir: File,
        fileName: String,
        timeoutMs: Long = 120_000L,
        onProgress: ((doneBytes: Long, totalBytes: Long) -> Unit)? = null
    ): Result<File> {
        val id = nextId++
        msg.put(Msg.KEY_ID, id)
        val d = CompletableDeferred<File>()
        fileRequests[id] = FileRequest(d, targetDir, fileName, onProgress)
        return try {
            send(msg)
            Result.success(withTimeout(timeoutMs) { d.await() })
        } catch (e: Exception) {
            Result.failure(e)
        } finally {
            fileRequests.remove(id)
        }
    }

    private suspend fun send(msg: JSONObject) = writeMutex.withLock {
        val out = dout ?: throw IOException("not connected")
        withContext(Dispatchers.IO) { FrameCodec.writeJson(out, msg.toString()) }
    }

    private suspend fun readLoop() {
        val input = din ?: return
        try {
            while (true) {
                val frame = withContext(Dispatchers.IO) { FrameCodec.readFrame(input) }
                when (frame.opcode) {
                    FrameCodec.OP_JSON -> handleJson(String(frame.payload, Charsets.UTF_8))
                    FrameCodec.OP_BINARY -> handleBinary(frame.payload)
                }
            }
        } catch (e: Exception) {
            // EOF / 連線中斷
        } finally {
            if (connected) {
                connected = false
                onDisconnected?.invoke()
            }
        }
    }

    private fun handleJson(json: String) {
        val o = try {
            JSONObject(json)
        } catch (e: Exception) {
            return
        }
        when (Msg.type(o)) {
            MsgType.MEDIA_META -> startTransfer(o)
            MsgType.TRANSFER_END -> endTransfer(o)
            else -> {
                val replyTo = Msg.replyTo(o)
                if (replyTo != 0L) {
                    pending.remove(replyTo)?.complete(o)
                    val fr = fileRequests[replyTo]
                    if (fr != null && Msg.type(o) == MsgType.ERROR) {
                        fileRequests.remove(replyTo)
                        fr.deferred.completeExceptionally(IOException(Msg.error(o) ?: "error"))
                    }
                } else {
                    onPush?.invoke(o)
                }
            }
        }
    }

    private fun startTransfer(o: JSONObject) {
        val replyTo = Msg.replyTo(o)
        val fr = fileRequests[replyTo] ?: return
        val meta = TransferMeta.fromJson(o)
        try {
            fr.targetDir.mkdirs()
            val temp = File(fr.targetDir, "${meta.transferId}.part")
            currentTransfer = CurrentTransfer(fr, temp, FileOutputStream(temp), meta.size, 0L, 0L)
        } catch (e: Exception) {
            fileRequests.remove(replyTo)
            fr.deferred.completeExceptionally(e)
        }
    }

    private fun handleBinary(data: ByteArray) {
        val ct = currentTransfer ?: return
        try {
            ct.out.write(data)
            ct.done += data.size
            // 節流：每 ~256KB 或每 2% 回報一次（WiFi 速度快，降低回呼頻率）
            if (ct.done - ct.lastReport >= 256 * 1024 || ct.total > 0 &&
                (ct.done * 100 / ct.total - ct.lastReportPct >= 2)
            ) {
                ct.lastReport = ct.done
                ct.lastReportPct = if (ct.total > 0) ct.done * 100 / ct.total else 0L
                ct.fr.onProgress?.invoke(ct.done, ct.total)
            }
        } catch (_: Exception) {
        }
    }

    private fun endTransfer(o: JSONObject) {
        val replyTo = Msg.replyTo(o)
        val ok = Msg.ok(o)
        val ct = currentTransfer
        currentTransfer = null
        val fr = fileRequests.remove(replyTo)
        if (fr == null) {
            try {
                ct?.out?.close()
            } catch (_: Exception) {
            }
            return
        }
        try {
            ct?.out?.close()
            if (ok && ct != null) {
                ct.fr.onProgress?.invoke(ct.total, ct.total)
                val final = File(fr.targetDir, fr.fileName)
                if (!ct.temp.renameTo(final)) {
                    ct.temp.copyTo(final, overwrite = true)
                    ct.temp.delete()
                }
                fr.deferred.complete(final)
            } else {
                ct?.temp?.delete()
                fr.deferred.completeExceptionally(IOException("transfer failed"))
            }
        } catch (e: Exception) {
            fr.deferred.completeExceptionally(e)
        }
    }

    private data class FileRequest(
        val deferred: CompletableDeferred<File>,
        val targetDir: File,
        val fileName: String,
        val onProgress: ((doneBytes: Long, totalBytes: Long) -> Unit)? = null
    )

    private data class CurrentTransfer(
        val fr: FileRequest,
        val temp: File,
        val out: FileOutputStream,
        val total: Long,
        var done: Long,
        var lastReport: Long,
        var lastReportPct: Long = 0L
    )
}
