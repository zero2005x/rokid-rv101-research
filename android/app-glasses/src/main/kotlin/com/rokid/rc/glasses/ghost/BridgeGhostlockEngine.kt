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
 * ghostlock LPE trigger layer (root bridge daemon version).
 *
 * It does not touch `/data/local/tmp` directly, but delegates to a helper resident as shell
 * (deployed once by deploy/root_bridge.sh):
 *   run the exploit → wait for ready → atomically replace the payload → wait for done → reap.
 *
 * App and helper communicate through fixed files under [BridgeConfig.bridgeDir] (default
 * /data/local/tmp/ghostlock_bridge, chmod 777 by setup):
 *   - `last.cmd`: the payload script written by the app
 *   - `last.done`: the DONE marker written by the payload (root)
 *   - `last.out`: the result written back by the helper (`OK` / `ERR:...`)
 *   - `last.progress`: the stage marker written by the helper (for progress reporting)
 *
 * Because the file names are fixed, this engine serializes exec with a Mutex, guaranteeing a one-to-one match with
 * the helper's single loop.
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

            // write the command file atomically, so the helper does not read a half-written file.
            // make it world-readable/writable (0666): otherwise the helper (shell) cannot read the 0600 file the app wrote.
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

    /** helper progress file → code. */
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

    /** helper progress file → argument (needed by only some codes). */
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
