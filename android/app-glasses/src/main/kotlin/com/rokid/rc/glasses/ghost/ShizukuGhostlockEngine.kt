package com.rokid.rc.glasses.ghost

import android.content.ComponentName
import com.rokid.rc.shared.protocol.GhostlockCodes
import com.rokid.rc.shared.protocol.ProgressCodec
import com.rokid.rc.shared.protocol.ProgressEvent
import com.rokid.rc.glasses.GlassesPaths
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import android.os.SystemClock
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import rikka.shizuku.Shizuku
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * ghostlock LPE trigger layer (Shizuku/UserService version).
 *
 * Background (research conclusion, see docs/shizuku-feasibility-report.md):
 *  - the exploit must run as shell (uid 2000); running it directly from the app (untrusted_app) kernel-panics.
 *  - when the Shizuku server is started through adb shell, the UserService process = uid 2000 + u:r:shell:s0,
 *    equivalent to adb shell → the only clean shell-identity channel inside the app.
 *  - when the server is started as root, the child process becomes uid 0/kernel context (the exploit must start
 *    unprivileged, so this is unusable) → on detecting that, wrap it in run_shell to drop privilege, or report an explicit error.
 *
 * The flow is the same as ShellGhostlockEngine (launch → ready → install → done):
 *   LAUNCH : userService.runCommandStream("ENV… /data/local/tmp/ghostlock_rv101")
 *   READY  : poll rootScriptPath for the readyMarker
 *   INSTALL: userService.writeFile(rootScriptPath, payload, exec=true)
 *            (or, with useDirectPayloadWrite=true, the app writes directly, requiring the symlink from setup_direct.sh)
 *   DONE   : poll payloadLog for the doneMarker
 *   ⚠ The order must not be reversed: the exploit writes its own default script ~2 s after launch, so a payload
 *     written earlier would be overwritten.
 *
 * kernel-panic/reboot detection: /proc/uptime is recorded before execution, and the timeout path compares it for a decrease.
 *
 * fallback: when preflight fails (server not started / not authorized / bind failed), if [fallback] is non-null
 * (AppGraph injects BridgeGhostlockEngine), it delegates to the existing bridge helper flow automatically.
 */
class ShizukuGhostlockEngine(
    private val context: Context,
    private val config: ExploitConfig = ExploitConfig(),
    private val useDirectPayloadWrite: Boolean = false,
    private val fallback: GhostlockEngine? = null,
) : GhostlockEngine {

    private val payloadLog = GhostlockPaths.payloadLog(context)
    private val exploitLog = GhostlockPaths.exploitLog(context)
    private val permissionRequested = AtomicBoolean(false)

    @Volatile
    override var isEnabled: Boolean = false
        private set

    override fun setEnabled(enabled: Boolean) { isEnabled = enabled }

    override fun doneLog(): File = payloadLog

    override var onProgress: ((ProgressEvent) -> Unit)? = null

    @Volatile
    override var rootAcquired: Boolean = false
        private set

    override suspend fun acquireRoot(): ExecResult {
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
        // 0. preflight: Shizuku binder / permission / UserService bind
        if (!Shizuku.pingBinder()) {
            return@withContext fallbackExec(
                Stage.PREFLIGHT, payload,
                GhostlockCodes.SHIZUKU_NOT_RUNNING
            )
        }
        val granted = runCatching { Shizuku.checkSelfPermission() }
            .getOrDefault(PackageManager.PERMISSION_DENIED)
        if (granted != PackageManager.PERMISSION_GRANTED) {
            if (permissionRequested.compareAndSet(false, true)) {
                runCatching { Shizuku.requestPermission(REQUEST_CODE) }
            }
            return@withContext fallbackExec(Stage.PREFLIGHT, payload, GhostlockCodes.SHIZUKU_UNAUTHORIZED)
        }

        val svc = bindCommandService()
            ?: return@withContext fallbackExec(Stage.PREFLIGHT, payload, GhostlockCodes.SHIZUKU_BIND_FAILED)

        try {
            execInternal(svc, payload)
        } finally {
            runCatching { Shizuku.unbindUserService(svc.args, svc.conn, false) }
        }
    }

    // ---- internals ----

    /** Whether the exploit output stream has ended (process died). Set by the onExit callback, for fast-fail detection. */
    @Volatile
    private var streamEnded = false

    private fun execInternal(svc: BoundService, payload: RootScript): ExecResult {
        val binary = File(config.binaryPath)
        if (!binary.exists() || !binary.canExecute()) {
            return ExecResult.fail(Stage.PREFLIGHT, ProgressCodec.encode(GhostlockCodes.BINARY_MISSING, binary.absolutePath))
        }

        payloadLog.delete()
        exploitLog.delete()
        val bootStamp = currentUptime()

        // identity check: the server must be shell (2000); if it was started as root, drop privilege with run_shell
        val id = svc.service.runCommand("id")
        val runPrefix: String = when {
            id.contains("uid=2000") -> ""
            id.contains("uid=0") -> {
                val runShell = File(RUN_SHELL_PATH)
                if (runShell.exists() && runShell.canExecute()) {
                    progress(GhostlockCodes.SHIZUKU_ROOT_RUN_SHELL)
                    RUN_SHELL_PATH + " "
                } else {
                    return ExecResult.fail(
                        Stage.PREFLIGHT,
                        GhostlockCodes.SHIZUKU_NO_RUN_SHELL
                    )
                }
            }
            else -> {
                return ExecResult.fail(Stage.PREFLIGHT, ProgressCodec.encode(GhostlockCodes.SHIZUKU_BAD_IDENTITY, id))
            }
        }

        // internal retry (the same MAX_TRY=3 behavior as the helper):
        // exploit killed by the OOM killer / FOPS install failed / ready timed out → clear leftover processes and retry;
        // kernel panic (uptime went backwards) → cannot continue within the same boot, report failure directly.
        val envPrefix = config.env.joinToString(" ")
        val cmd = "$envPrefix $runPrefix${config.binaryPath}"

        for (attempt in 1..MAX_INTERNAL_TRY) {
            progress(GhostlockCodes.ATTEMPT_SHIZUKU, "$attempt/$MAX_INTERNAL_TRY")
            streamEnded = false
            // clear the previous leftovers (same as the helper: rm -f the default script and log before running)
            svc.service.runCommand("rm -f ${config.rootScriptPath} 2>/dev/null || true")
            svc.service.runCommandStream(cmd, streamCallback(exploitLog))

            // 1. ready
            progress(GhostlockCodes.WAIT_READY)
            if (!waitFor(config.readyTimeoutMs) { readySeen() }) {
                if (bootChanged(bootStamp)) {
                    return ExecResult.fail(Stage.READY, GhostlockCodes.KERNEL_PANIC)
                }
                val why = if (streamEnded) GhostlockCodes.PROCESS_DIED else GhostlockCodes.READY_TIMEOUT_RETRY
                progress(GhostlockCodes.RETRY_AFTER, why)
                killExploit(svc)
                continue
            }

            // 2. install payload
            progress(GhostlockCodes.PAYLOAD_REPLACED)
            val installed = if (useDirectPayloadWrite) {
                installPayloadDirect(payload)
            } else {
                svc.service.writeFile(config.rootScriptPath, payload.content.toByteArray(), true)
            }
            if (!installed) {
                killExploit(svc)
                return ExecResult.fail(Stage.INSTALL, ProgressCodec.encode(GhostlockCodes.PAYLOAD_WRITE_FAILED, config.rootScriptPath))
            }

            // 3. done
            progress(GhostlockCodes.WAIT_PAYLOAD)
            if (!waitFor(config.doneTimeoutMs) { doneSeen() }) {
                if (bootChanged(bootStamp)) {
                    return ExecResult.fail(Stage.DONE, GhostlockCodes.KERNEL_PANIC)
                }
                if (streamEnded) {
                    progress(GhostlockCodes.PROCESS_DIED_RETRY)
                    killExploit(svc)
                    continue
                }
                return ExecResult.fail(Stage.DONE, ProgressCodec.encode(GhostlockCodes.PAYLOAD_TIMEOUT, readTail(payloadLog, 800)))
            }
            // success: clear leftover idle exploit processes (same as the helper; root_daemon is unaffected)
            killExploit(svc)
            return ExecResult.ok(Stage.DONE, readTail(payloadLog, 4000))
        }
        return ExecResult.fail(Stage.DONE, ProgressCodec.encode(GhostlockCodes.ATTEMPTS_EXHAUSTED, "$MAX_INTERNAL_TRY / " + exploitLog.absolutePath))
    }

    private fun killExploit(svc: BoundService) {
        // same pkill behavior as the helper: clear leftover exploit processes, then retry
        runCatching { svc.service.runCommand("pkill -9 -f ghostlock_rv101 2>/dev/null || true") }
    }

    /** Streaming callback: write everything to exploitLog, forwarding only known key lines to onProgress (to avoid log spam). */
    private fun streamCallback(log: File): ICommandCallback =
        object : ICommandCallback.Stub() {
            override fun onLine(line: String) {
                try {
                    log.appendText(line + "\n")
                } catch (_: Exception) {
                }
                for (m in PROGRESS_MARKERS) {
                    if (line.contains(m)) {
                        progress(GhostlockCodes.EXPLOIT_OUTPUT, line.take(120))
                        break
                    }
                }
            }

            override fun onExit(exitCode: Int) {
                // record only; the flow decision is made from the ready/done markers (the exploit does not exit cleanly before it has finished)
                Log.i(TAG, "exploit process exited code=$exitCode")
                streamEnded = true
            }
        }

    private class BoundService(
        val args: Shizuku.UserServiceArgs,
        val conn: ServiceConnection,
        val service: ICommandService,
    )

    private fun bindCommandService(): BoundService? {
        val args = Shizuku.UserServiceArgs(
            ComponentName(context.packageName, CommandService::class.java.name)
        ).daemon(true).processNameSuffix("ghost").version(1)

        val latch = CountDownLatch(1)
        var binder: IBinder? = null
        val conn = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
                binder = service
                latch.countDown()
            }

            override fun onServiceDisconnected(name: ComponentName?) {
                Log.w(TAG, "CommandService disconnected")
            }
        }
        try {
            Shizuku.bindUserService(args, conn)
        } catch (e: Exception) {
            Log.e(TAG, "bindUserService failed: ${e.message}")
            return null
        }
        if (!latch.await(BIND_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
            Log.e(TAG, "bindUserService timeout")
            runCatching { Shizuku.unbindUserService(args, conn, false) }
            return null
        }
        val svc = binder?.let { ICommandService.Stub.asInterface(it) }
        if (svc == null) {
            runCatching { Shizuku.unbindUserService(args, conn, false) }
            return null
        }
        return BoundService(args, conn, svc)
    }

    /** Delegate to the fallback when the Shizuku path is unavailable (AppGraph injects BridgeGhostlockEngine). */
    private suspend fun fallbackExec(stage: String, payload: RootScript, message: String): ExecResult {
        val fb = fallback
        if (fb != null) {
            progress(GhostlockCodes.SHIZUKU_UNAVAILABLE, message)
            return try {
                fb.exec(payload)
            } catch (e: Exception) {
                ExecResult.fail(stage, ProgressCodec.encode(GhostlockCodes.FALLBACK_FAILED, e.message))
            }
        }
        return ExecResult.fail(stage, message)
    }

    private fun readySeen(): Boolean = try {
        File(config.rootScriptPath).readText().contains(config.readyMarker)
    } catch (e: Exception) { false }

    private fun installPayloadDirect(payload: RootScript): Boolean = try {
        val target = File(config.rootScriptPath)
        target.writeText(payload.content)
        target.setExecutable(true, false)
        true
    } catch (e: Exception) {
        Log.e(TAG, "installPayloadDirect failed", e)
        false
    }

    private fun doneSeen(): Boolean =
        payloadLog.exists() && payloadLog.readText().contains(config.doneMarker)

    private fun currentUptime(): Double = try {
        File("/proc/uptime").readText().trim().substringBefore(' ').toDouble()
    } catch (e: Exception) { -1.0 }

    private fun bootChanged(prev: Double): Boolean {
        if (prev < 0) return false
        val cur = currentUptime()
        return cur < prev // uptime went backwards = already rebooted (kernel panic)
    }

    private fun waitFor(timeoutMs: Long, cond: () -> Boolean): Boolean {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (SystemClock.elapsedRealtime() < deadline) {
            if (cond()) return true
            Thread.sleep(config.pollIntervalMs)
        }
        return cond()
    }

    private fun readTail(file: File, max: Int): String = try {
        if (!file.exists()) "(none)" else file.readText().takeLast(max)
    } catch (e: Exception) { "(unreadable)" }

    companion object {
        private const val TAG = "ShizukuGhostlockEngine"
        private const val REQUEST_CODE = 7700
        private const val BIND_TIMEOUT_MS = 15_000L
        private const val MAX_INTERNAL_TRY = 3
        private const val RUN_SHELL_PATH = GlassesPaths.RUN_SHELL
        private val PROGRESS_MARKERS = listOf(
            "FOPS install failed",
            "dropping to root shell",
            "child is root",
            "SELinux",
            "panic",
            "killed",
        )
    }
}
