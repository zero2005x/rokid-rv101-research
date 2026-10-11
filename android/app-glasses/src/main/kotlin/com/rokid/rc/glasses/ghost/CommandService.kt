package com.rokid.rc.glasses.ghost

import android.util.Log
import java.io.BufferedReader
import java.io.File
import java.io.FileOutputStream
import java.io.InputStreamReader
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Shizuku UserService implementation: runs commands as shell inside the Shizuku server process (when the server was
 * started through adb shell = uid 2000 / u:r:shell:s0).
 *
 * Design (following ghostlock-aak-apk's runCommandStream/writeFile pattern, rewritten from scratch):
 *  - [runCommandStream]: the service starts a separate Thread to run `sh -c`, reading stdout/stderr on two threads in
 *    parallel (to keep a full pipe from deadlocking), calling back oneway line by line; `waitFor()` applies no hard
 *    timeout (the exploit needs 100-150 seconds). The line callback is capped at 64KB/line, so very long output
 *    (e.g. binary spew) is truncated first.
 *  - [writeFile]: write a file directly as shell, with chmod 755 (equivalent to the helper's cp+chmod).
 *  - [destroy]: Shizuku server reserved code (16777114), called when the server destroys this service.
 *
 * ⚠ Safety constraint: this service may only run "commands the calling app has assembled in advance". The engine side
 * guarantees it never execs the exploit directly from the app uid (that kernel-panics); everything goes through this
 * service's shell identity.
 */
class CommandService : ICommandService.Stub() {

    private val stopped = AtomicBoolean(false)

    override fun runCommandStream(cmd: String, callback: ICommandCallback) {
        Thread({
            var exitCode: Int
            try {
                val p = Runtime.getRuntime().exec(arrayOf("sh", "-c", cmd))
                val outThread = Thread({
                    try {
                        BufferedReader(InputStreamReader(p.inputStream)).use { r ->
                            var line: String? = r.readLine()
                            while (line != null) {
                                safeLine(callback, line)
                                line = r.readLine()
                            }
                        }
                    } catch (_: Exception) {
                    }
                }, "shizuku-cmd-out").apply { isDaemon = true; start() }
                val errThread = Thread({
                    try {
                        BufferedReader(InputStreamReader(p.errorStream)).use { r ->
                            var line: String? = r.readLine()
                            while (line != null) {
                                safeLine(callback, "[E] ${line ?: ""}")
                                line = r.readLine()
                            }
                        }
                    } catch (_: Exception) {
                    }
                }, "shizuku-cmd-err").apply { isDaemon = true; start() }
                exitCode = p.waitFor()
                outThread.join(2000)
                errThread.join(2000)
            } catch (e: Exception) {
                Log.e(TAG, "runCommandStream failed: ${e.message}")
                exitCode = 127
            }
            try {
                callback.onExit(exitCode)
            } catch (_: Exception) {
            }
        }, "shizuku-cmd").apply { isDaemon = true; start() }
    }

    override fun runCommand(cmd: String): String = try {
        val p = Runtime.getRuntime().exec(arrayOf("sh", "-c", cmd))
        var outBuf = ""
        var errBuf = ""
        val outT = Thread({ outBuf = p.inputStream.bufferedReader().readText() }, "shizuku-cmd-out-sync")
            .apply { isDaemon = true; start() }
        val errT = Thread({ errBuf = p.errorStream.bufferedReader().readText() }, "shizuku-cmd-err-sync")
            .apply { isDaemon = true; start() }
        val exit = p.waitFor()
        outT.join(2000)
        errT.join(2000)
        "exit=$exit\n$outBuf" + if (errBuf.isNotEmpty()) "[E] $errBuf" else ""
    } catch (e: Exception) {
        "exception: ${e.message}"
    }

    override fun writeFile(path: String, data: ByteArray, executable: Boolean): Boolean = try {
        val f = File(path)
        FileOutputStream(f).use { it.write(data) }
        if (executable) f.setExecutable(true, false)
        true
    } catch (e: Exception) {
        Log.e(TAG, "writeFile($path) failed: ${e.message}")
        false
    }

    override fun destroy() {
        stopped.set(true)
    }

    private fun safeLine(callback: ICommandCallback, line: String) {
        try {
            callback.onLine(if (line.length > MAX_LINE) line.substring(0, MAX_LINE) else line)
        } catch (_: Exception) {
        }
    }

    companion object {
        private const val TAG = "ShizukuCommandService"
        private const val MAX_LINE = 65536
    }
}
