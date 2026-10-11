package com.rokid.rc.shared.protocol

/**
 * 眼鏡端 → 手機的**結構化進度事件**。
 *
 * 眼鏡端只送代碼與參數（例：`gl_ready_timeout` + `/data/local/tmp/...`），
 * 由手機端用自己的語系組字（見 app-phone `ui/GhostlockText.kt`）。
 * 這樣手機換語言、或新增語言，都不需要動眼鏡端，也不會出現兩端語系混雜。
 */
data class ProgressEvent(val code: String, val arg: String? = null)

/**
 * 失敗訊息在同一個 `message` 欄位裡以 `code|arg` 形式傳遞（arg 可省略）。
 * 舊版客戶端會看到原始字串；新版手機端會解碼後以自己語系顯示。
 */
object ProgressCodec {
    private const val SEP = '|'

    fun encode(code: String, arg: String? = null): String =
        if (arg.isNullOrEmpty()) code else "$code$SEP$arg"

    /** 解碼；非代碼字串（例如舊版或第三方訊息）原樣回傳為 code、arg=null。 */
    fun decode(raw: String): Pair<String, String?> {
        val i = raw.indexOf(SEP)
        return if (i <= 0) raw to null else raw.substring(0, i) to raw.substring(i + 1)
    }
}

/** Ghostlock／LPE 流程的進度代碼。與手機端 `GhostlockText` 一對一。 */
object GhostlockCodes {
    // ---- 通用 ----
    const val SKIP_LPE = "gl_skip_lpe"
    const val PREPARE_PAYLOAD = "gl_prepare_payload"
    const val DONE = "gl_done"
    const val FAILED = "gl_failed"

    // ---- bridge helper ----
    const val BRIDGE_DIR_MISSING = "gl_bridge_dir_missing"
    const val BRIDGE_SENT = "gl_bridge_sent"
    const val BRIDGE_TIMEOUT = "gl_bridge_timeout"
    const val BRIDGE_REPORT = "gl_bridge_report"
    const val ATTEMPT = "gl_attempt"
    const val HELPER_STARTED = "gl_helper_started"
    const val READY_SEEN = "gl_ready_seen"
    const val PAYLOAD_REPLACED = "gl_payload_replaced"
    const val WAIT_PAYLOAD_ROOT = "gl_wait_payload_root"

    // ---- 直接執行（shell／Shizuku）----
    const val BINARY_MISSING = "gl_binary_missing"
    const val EXPLOIT_STARTED = "gl_exploit_started"
    const val EXPLOIT_START_FAILED = "gl_exploit_start_failed"
    const val WAIT_READY = "gl_wait_ready"
    const val READY_TIMEOUT = "gl_ready_timeout"
    const val READY_TIMEOUT_RETRY = "gl_ready_timeout_retry"
    const val PAYLOAD_WRITE_FAILED = "gl_payload_write_failed"
    const val WAIT_PAYLOAD = "gl_wait_payload"
    const val PAYLOAD_TIMEOUT = "gl_payload_timeout"
    const val EXPLOIT_OUTPUT = "gl_exploit_output"

    // ---- Shizuku 專屬 ----
    const val SHIZUKU_NOT_RUNNING = "gl_shizuku_not_running"
    const val SHIZUKU_UNAUTHORIZED = "gl_shizuku_unauthorized"
    const val SHIZUKU_BIND_FAILED = "gl_shizuku_bind_failed"
    const val SHIZUKU_ROOT_RUN_SHELL = "gl_shizuku_root_run_shell"
    const val SHIZUKU_NO_RUN_SHELL = "gl_shizuku_no_run_shell"
    const val SHIZUKU_BAD_IDENTITY = "gl_shizuku_bad_identity"
    const val SHIZUKU_UNAVAILABLE = "gl_shizuku_unavailable"
    const val ATTEMPT_SHIZUKU = "gl_attempt_shizuku"

    // ---- 引擎選擇（P1 執行期探測）----
    /** 實際使用的引擎（arg = `shizuku`／`bridge`）。 */
    const val ENGINE_SELECTED = "gl_engine_selected"
    const val ENGINE_SHIZUKU_RUNNING = "gl_engine_shizuku_running"
    const val ENGINE_BRIDGE_RESIDENT = "gl_engine_bridge_resident"
    const val ENGINE_NONE_AVAILABLE = "gl_engine_none"
    /** 首選引擎失敗後改用另一條管線重試。 */
    const val ENGINE_RETRY_OTHER = "gl_engine_retry_other"

    // ---- 失敗分類 ----
    const val KERNEL_PANIC = "gl_kernel_panic"
    const val PROCESS_DIED = "gl_process_died"
    const val PROCESS_DIED_RETRY = "gl_process_died_retry"
    const val RETRY_AFTER = "gl_retry_after"
    const val ATTEMPTS_EXHAUSTED = "gl_attempts_exhausted"
    const val FALLBACK_FAILED = "gl_fallback_failed"
}
