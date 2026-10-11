package com.rokid.rc.phone.ui

import android.content.Context
import com.rokid.rc.phone.R
import com.rokid.rc.shared.protocol.GhostlockCodes
import com.rokid.rc.shared.protocol.ProgressCodec

/**
 * 把眼鏡端送來的 **ghostlock／LPE 進度代碼** 轉成手機語系的文字。
 *
 * 眼鏡端只送代碼（`GhostlockCodes`）與參數（路徑／原因／次數），
 * 因此手機換語言不需要動眼鏡端（原本眼鏡直接送中文句子）。
 */
object GhostlockText {

    /** 進度事件 → 文字；未知代碼回 null（呼叫端可改用原始 message）。 */
    fun progress(ctx: Context, code: String?, arg: String?): String? {
        if (code.isNullOrEmpty()) return null
        val a = arg.orEmpty()
        return when (code) {
            GhostlockCodes.SKIP_LPE -> ctx.getString(R.string.ghost_skip_lpe)
            GhostlockCodes.PREPARE_PAYLOAD -> ctx.getString(R.string.ghost_prepare_payload)
            GhostlockCodes.DONE -> ctx.getString(R.string.ghost_done)
            GhostlockCodes.FAILED -> ctx.getString(R.string.ghost_failed, a)
            GhostlockCodes.BRIDGE_DIR_MISSING -> ctx.getString(R.string.ghost_bridge_dir_missing, a)
            GhostlockCodes.BRIDGE_SENT -> ctx.getString(R.string.ghost_bridge_sent)
            GhostlockCodes.BRIDGE_TIMEOUT -> ctx.getString(R.string.ghost_bridge_timeout, a)
            GhostlockCodes.BRIDGE_REPORT -> ctx.getString(R.string.ghost_bridge_report, a)
            GhostlockCodes.ATTEMPT -> ctx.getString(R.string.ghost_attempt, a)
            GhostlockCodes.ATTEMPT_SHIZUKU -> ctx.getString(R.string.ghost_attempt_shizuku, a)
            GhostlockCodes.HELPER_STARTED -> ctx.getString(R.string.ghost_helper_started)
            GhostlockCodes.READY_SEEN -> ctx.getString(R.string.ghost_ready_seen)
            GhostlockCodes.PAYLOAD_REPLACED -> ctx.getString(R.string.ghost_payload_replaced)
            GhostlockCodes.WAIT_PAYLOAD_ROOT -> ctx.getString(R.string.ghost_wait_payload_root)
            GhostlockCodes.BINARY_MISSING -> ctx.getString(R.string.ghost_binary_missing, a)
            GhostlockCodes.EXPLOIT_STARTED -> ctx.getString(R.string.ghost_exploit_started)
            GhostlockCodes.EXPLOIT_START_FAILED -> ctx.getString(R.string.ghost_exploit_start_failed, a)
            GhostlockCodes.WAIT_READY -> ctx.getString(R.string.ghost_wait_ready)
            GhostlockCodes.READY_TIMEOUT -> ctx.getString(R.string.ghost_ready_timeout, a)
            GhostlockCodes.READY_TIMEOUT_RETRY -> ctx.getString(R.string.ghost_ready_timeout_retry)
            GhostlockCodes.PAYLOAD_WRITE_FAILED -> ctx.getString(R.string.ghost_payload_write_failed, a)
            GhostlockCodes.WAIT_PAYLOAD -> ctx.getString(R.string.ghost_wait_payload)
            GhostlockCodes.PAYLOAD_TIMEOUT -> ctx.getString(R.string.ghost_payload_timeout, a)
            GhostlockCodes.EXPLOIT_OUTPUT -> ctx.getString(R.string.ghost_exploit_output, a)
            GhostlockCodes.SHIZUKU_NOT_RUNNING -> ctx.getString(R.string.ghost_shizuku_not_running)
            GhostlockCodes.SHIZUKU_UNAUTHORIZED -> ctx.getString(R.string.ghost_shizuku_unauthorized)
            GhostlockCodes.SHIZUKU_BIND_FAILED -> ctx.getString(R.string.ghost_shizuku_bind_failed)
            GhostlockCodes.SHIZUKU_ROOT_RUN_SHELL -> ctx.getString(R.string.ghost_shizuku_root_run_shell)
            GhostlockCodes.SHIZUKU_NO_RUN_SHELL -> ctx.getString(R.string.ghost_shizuku_no_run_shell)
            GhostlockCodes.SHIZUKU_BAD_IDENTITY -> ctx.getString(R.string.ghost_shizuku_bad_identity, a)
            GhostlockCodes.SHIZUKU_UNAVAILABLE -> ctx.getString(R.string.ghost_shizuku_unavailable, a)
            GhostlockCodes.KERNEL_PANIC -> ctx.getString(R.string.ghost_kernel_panic)
            GhostlockCodes.PROCESS_DIED -> ctx.getString(R.string.ghost_process_died)
            GhostlockCodes.PROCESS_DIED_RETRY -> ctx.getString(R.string.ghost_process_died_retry)
            GhostlockCodes.RETRY_AFTER -> ctx.getString(R.string.ghost_retry_after, a)
            GhostlockCodes.ATTEMPTS_EXHAUSTED -> ctx.getString(R.string.ghost_attempts_exhausted, a)
            GhostlockCodes.FALLBACK_FAILED -> ctx.getString(R.string.ghost_fallback_failed, a)
            GhostlockCodes.ENGINE_SELECTED -> ctx.getString(R.string.ghost_engine_selected, a)
            else -> null
        }
    }

    /**
     * 失敗訊息（`ExecResult.fail` → ack error）→ 手機語系文字。
     * 眼鏡端以 `code|arg` 形式傳遞；不是已知代碼時原樣回傳（例如第三方或舊版訊息）。
     */
    fun error(ctx: Context, raw: String?): String {
        if (raw.isNullOrBlank()) return ""
        val (code, arg) = ProgressCodec.decode(raw)
        return progress(ctx, code, arg) ?: raw
    }
}
