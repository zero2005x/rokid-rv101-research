package com.rokid.rc.glasses.ghost

import android.net.LocalSocket
import com.rokid.rc.glasses.GlassesPaths
import android.net.LocalSocketAddress
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Root Daemon 的 socket client。
 *
 * Root Daemon（由 exploit 提權後啟動）監聽 UNIX domain socket，
 * 此 client 連上後送出純文字指令、讀回 stdout。取代「每次指令重跑 LPE」。
 */
class RootDaemonClient(
    private val socketPath: String = GlassesPaths.ROOT_DAEMON_SOCK
) {

    /** socket 檔存在即視為 daemon 存活（可能為 reboot 後的 stale 檔）。 */
    fun isAvailable(): Boolean = File(socketPath).exists()

    /** 真的以 root 執行一次 `id`，確認 daemon 活著且是 root。 */
    suspend fun isRoot(): Boolean {
        if (!isAvailable()) return false
        return runCatching { exec("id").contains("uid=0") }.getOrDefault(false)
    }

    /**
     * 執行指令並讀回輸出。長指令（如 wifi 服務轉換）可能掛住，加逾時保護。
     * 永不拋出：reboot 後 socket 檔可能殘留但無 listener（Connection refused），
     * 或 daemon 忙碌/逾時——失敗一律回傳空字串（呼叫端自行判讀）。
     */
    suspend fun exec(cmd: String, timeoutMs: Long = 30_000L): String =
        kotlinx.coroutines.withTimeoutOrNull(timeoutMs) {
            withContext(Dispatchers.IO) {
                runCatching {
                    val socket = LocalSocket()
                    try {
                        socket.connect(LocalSocketAddress(socketPath, LocalSocketAddress.Namespace.FILESYSTEM))
                        val out = socket.outputStream
                        out.write(cmd.toByteArray())
                        out.flush()
                        socket.shutdownOutput()
                        socket.inputStream.bufferedReader().readText()
                    } finally {
                        try {
                            socket.close()
                        } catch (_: Exception) {
                        }
                    }
                }.getOrDefault("")
            }
        } ?: ""
}
