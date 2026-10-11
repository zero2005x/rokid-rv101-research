package com.rokid.rc.glasses.ghost

import android.util.Log
import java.io.BufferedReader
import java.io.File
import java.io.FileOutputStream
import java.io.InputStreamReader
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Shizuku UserService 實作：在 Shizuku server 進程內（server 以 adb shell 啟動時 =
 * uid 2000 / u:r:shell:s0）以 shell 身份執行指令。
 *
 * 設計（參考 ghostlock-aak-apk 的 runCommandStream/writeFile 模式，自行重寫）：
 *  - [runCommandStream]：服務端另起 Thread 執行 `sh -c`，雙線程並行讀 stdout/stderr
 *    （防 pipe 寫滿死鎖），逐行 oneway 回呼；`waitFor()` 不做硬 timeout（exploit 需
 *    100–150 秒）。line 回呼限制 64KB/行，超長輸出（如二進位噴出）會先截斷。
 *  - [writeFile]：以 shell 身份直接寫檔，可 chmod 755（等同 helper 的 cp+chmod）。
 *  - [destroy]：Shizuku server 保留碼（16777114），server 銷毀本服務時呼叫。
 *
 * ⚠ 安全約束：本服務只允許跑「呼叫 App 預先組好的指令」。引擎側保證永不從
 * App uid 直接 exec exploit（會 kernel panic），一律經此服務的 shell 身份。
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
