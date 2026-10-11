package com.rokid.rc.glasses.ghost

import android.net.LocalSocket
import com.rokid.rc.glasses.GlassesPaths
import android.net.LocalSocketAddress
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Socket client for the Root Daemon.
 *
 * The Root Daemon (started after the exploit escalates) listens on a UNIX domain socket;
 * this client connects, sends plain-text commands and reads back stdout. It replaces "re-run the LPE for every command".
 */
class RootDaemonClient(
    private val socketPath: String = GlassesPaths.ROOT_DAEMON_SOCK
) {

    /** The daemon counts as alive when the socket file exists (it may be a stale file from before a reboot). */
    fun isAvailable(): Boolean = File(socketPath).exists()

    /** Really run `id` once as root, to confirm the daemon is alive and is root. */
    suspend fun isRoot(): Boolean {
        if (!isAvailable()) return false
        return runCatching { exec("id").contains("uid=0") }.getOrDefault(false)
    }

    /**
     * Run a command and read back the output. Long commands (such as a wifi service transition) can hang, so a
     * timeout guards them. It never throws: after a reboot the socket file may linger with no listener
     * (Connection refused), or the daemon may be busy / time out — failures always return an empty string (the caller interprets it).
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
