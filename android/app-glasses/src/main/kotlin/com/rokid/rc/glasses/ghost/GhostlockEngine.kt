package com.rokid.rc.glasses.ghost

import android.content.Context
import com.rokid.rc.glasses.GlassesPaths
import java.io.File

/** 欲以 root 執行的腳本 payload。 */
data class RootScript(val name: String, val content: String)

/** root 執行結果。 */
data class ExecResult(
    val success: Boolean,
    val exitCode: Int,
    val stage: String,
    val output: String
) {
    companion object {
        fun fail(stage: String, message: String): ExecResult =
            ExecResult(success = false, exitCode = -1, stage = stage, output = message)

        fun ok(stage: String, output: String): ExecResult =
            ExecResult(success = true, exitCode = 0, stage = stage, output = output)
    }
}

/** 觸發流程階段（供失敗診斷）。 */
object Stage {
    const val PREFLIGHT = "preflight"
    const val LAUNCH = "launch"
    const val READY = "ready"
    const val INSTALL = "install"
    const val DONE = "done"
}

/** payload 完成標記。payload 腳本末尾 `echo` 此字串到 payload log。 */
object PayloadMarkers {
    const val DONE = "__GHOSTLOCK_PAYLOAD_DONE__"
}

/** 引擎共用的檔案路徑（都放在 App 自身可讀寫的 cacheDir）。 */
object GhostlockPaths {
    fun payloadLog(context: Context): File = File(context.cacheDir, "ghostlock_payload.log")
    fun exploitLog(context: Context): File = File(context.cacheDir, "ghostlock_run.log")
}

/**
 * exploit 觸發參數，對應 run_*_orchestrator.sh 的既有行為。
 * 實機路徑/標記不同時只需調整此處。
 */
data class ExploitConfig(
    val binaryPath: String = GlassesPaths.GHOSTLOCK_BIN,
    val rootScriptPath: String = GlassesPaths.GHOSTLOCK_ROOT_SCRIPT,
    val env: List<String> = listOf(
        "GL_PURGE_CNT=0",
        "GL_TAIL_KILLS=16",
        "GL_DUMMY_KILLS=0",
        "W2_ATTEMPTS=1"
    ),
    val readyMarker: String = "dropping to root shell",
    val doneMarker: String = PayloadMarkers.DONE,
    val readyTimeoutMs: Long = 90_000L,
    // exploit 核心階段實測 100–142s（helper 用 200s；再加內部重試餘裕）
    val doneTimeoutMs: Long = 240_000L,
    val pollIntervalMs: Long = 200L
)

/** root 橋接 daemon 的參數（對應 deploy/root_bridge.sh）。 */
data class BridgeConfig(
    val bridgeDir: String = GlassesPaths.BRIDGE_DIR,
    val timeoutMs: Long = 500_000L,
    val pollIntervalMs: Long = 300L
)

/**
 * Ghostlock LPE 呼叫層介面。
 * 眼鏡端所有需要 root 的動作（壓制相機隱私 LED）都經由此介面；
 * 手機端的 ghostlock 開關控制 [isEnabled]。
 */
interface GhostlockEngine {
    val isEnabled: Boolean

    /** 是否已成功取得 root（由 [acquireRoot] 設定）。 */
    val rootAcquired: Boolean

    /** 進度回呼（長時操作期間回報階段，供推送給手機顯示）。 */
    var onProgress: ((com.rokid.rc.shared.protocol.ProgressEvent) -> Unit)?

    /**
     * 送出結構化進度（代碼＋參數），由手機端以**自己的語系**組字。
     * 眼鏡端不再組中文句子（見 shared `ProgressEvent`／`GhostlockCodes`）。
     */
    fun progress(code: String, arg: String? = null) {
        onProgress?.invoke(com.rokid.rc.shared.protocol.ProgressEvent(code, arg))
    }

    fun setEnabled(enabled: Boolean)

    /** payload 完成標記（DONE）要寫入的 log 檔，由引擎決定以確保可被讀到。 */
    fun doneLog(): File

    suspend fun exec(payload: RootScript): ExecResult

    /** 跑一次 LPE 驗證/取得 root（等同跑一個驗證 payload）。 */
    suspend fun acquireRoot(): ExecResult
}
