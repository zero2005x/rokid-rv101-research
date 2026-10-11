package com.rokid.rc.glasses.ghost

import android.os.SystemClock
import com.rokid.rc.glasses.GlassesPaths
import com.rokid.rc.shared.protocol.GhostlockCodes
import com.rokid.rc.shared.protocol.ProgressCodec
import com.rokid.rc.shared.protocol.ProgressEvent
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File

/**
 * ghostlock LPE 觸發層（root 橋接 daemon 版）。
 *
 * 不直接碰 `/data/local/tmp`，而是委託一個以 shell 常駐的 helper
 * （由 deploy/root_bridge.sh 一次性佈署）代為：
 *   跑 exploit → 等 ready → 原子置換 payload → 等 done → 回收。
 *
 * App 與 helper 之間用 [BridgeConfig.bridgeDir]（預設 /data/local/tmp/ghostlock_bridge，
 * 由 setup chmod 777）下的固定檔案通訊：
 *   - `last.cmd`：App 寫入的 payload 腳本
 *   - `last.done`：payload（root）寫入的 DONE 標記
 *   - `last.out`：helper 回寫的結果（`OK` / `ERR:...`）
 *   - `last.progress`：helper 寫入的階段標記（供進度回報）
 *
 * 由於採固定檔名，本引擎以 Mutex 序列化 exec，保證與 helper 的單一迴圈一對一。
 */
class BridgeGhostlockEngine(
    private val config: BridgeConfig = BridgeConfig()
) : GhostlockEngine {

    private val bridgeDir = File(config.bridgeDir)
    private val cmdFile = File(bridgeDir, "last.cmd")
    private val outFile = File(bridgeDir, "last.out")
    private val doneFile = File(bridgeDir, "last.done")
    private val progressFile = File(bridgeDir, "last.progress")
    private val mutex = Mutex()

    @Volatile
    override var isEnabled: Boolean = false
        private set

    override var onProgress: ((ProgressEvent) -> Unit)? = null

    override fun setEnabled(enabled: Boolean) {
        isEnabled = enabled
    }

    override fun doneLog(): File = doneFile

    @Volatile
    override var rootAcquired: Boolean = false
        private set

    override suspend fun acquireRoot(): ExecResult {
        // 若已有常駐 root daemon（uid=0），跳過重跑 exploit，避免無謂的 reboot 風險
        val client = RootDaemonClient()
        if (client.isRoot()) {
            rootAcquired = true
            progress(GhostlockCodes.SKIP_LPE)
            return ExecResult.ok(Stage.DONE, "already rooted")
        }
        val result = exec(LedPayloads.startRootDaemon(doneLog()))
        rootAcquired = result.success
        return result
    }

    override suspend fun exec(payload: RootScript): ExecResult = mutex.withLock {
        withContext(Dispatchers.IO) {
            if (!bridgeDir.exists() || !bridgeDir.canWrite()) {
                return@withContext ExecResult.fail(
                    Stage.PREFLIGHT,
                    ProgressCodec.encode(GhostlockCodes.BRIDGE_DIR_MISSING, bridgeDir.absolutePath)
                )
            }

            progress(GhostlockCodes.PREPARE_PAYLOAD)
            outFile.delete()
            doneFile.delete()
            progressFile.delete()

            // 原子寫入命令檔，避免 helper 讀到半寫入。
            // 設為 world-readable/writable(0666)：否則 helper(shell) 讀不到 app 寫的 0600 檔。
            val tmp = File(bridgeDir, "last.cmd.tmp")
            tmp.writeText(payload.content)
            tmp.setReadable(true, false)
            tmp.setWritable(true, false)
            if (!tmp.renameTo(cmdFile)) {
                cmdFile.delete()
                tmp.renameTo(cmdFile)
            }

            progress(GhostlockCodes.BRIDGE_SENT)

            var lastProgress = ""
            val finished = waitFor(config.timeoutMs) {
                val p = if (progressFile.exists()) progressFile.readText().trim() else ""
                if (p.isNotEmpty() && p != lastProgress) {
                    lastProgress = p
                    progress(progressCode(p), progressArg(p))
                }
                outFile.exists() && outFile.readText().isNotBlank()
            }
            if (!finished) {
                return@withContext ExecResult.fail(
                    Stage.DONE,
                    ProgressCodec.encode(GhostlockCodes.BRIDGE_TIMEOUT, GlassesPaths.BRIDGE_DIR + "/helper.log")
                )
            }

            val result = outFile.readText().trim()
            outFile.delete()
            if (result.startsWith("OK")) {
                progress(GhostlockCodes.DONE)
                ExecResult.ok(Stage.DONE, result)
            } else {
                progress(GhostlockCodes.FAILED, result)
                ExecResult.fail(Stage.DONE, ProgressCodec.encode(GhostlockCodes.BRIDGE_REPORT, result))
            }
        }
    }

    /** helper 進度檔 → 代碼。 */
    private fun progressCode(p: String): String = when {
        p.startsWith("exploit-attempt") -> GhostlockCodes.ATTEMPT
        p == "started" -> GhostlockCodes.HELPER_STARTED
        p == "ready" -> GhostlockCodes.READY_SEEN
        p == "installed" -> GhostlockCodes.PAYLOAD_REPLACED
        p == "waiting-done" -> GhostlockCodes.WAIT_PAYLOAD_ROOT
        p == "done" -> GhostlockCodes.DONE
        p.startsWith("failed") -> GhostlockCodes.FAILED
        else -> p
    }

    /** helper 進度檔 → 參數（僅部分代碼需要）。 */
    private fun progressArg(p: String): String? = when {
        p.startsWith("exploit-attempt") -> p.removePrefix("exploit-attempt-")
        p.startsWith("failed") -> p
        else -> null
    }

    private fun waitFor(timeoutMs: Long, cond: () -> Boolean): Boolean {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (SystemClock.elapsedRealtime() < deadline) {
            if (cond()) return true
            Thread.sleep(config.pollIntervalMs)
        }
        return cond()
    }
}
