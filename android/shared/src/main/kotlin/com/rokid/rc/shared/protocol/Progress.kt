package com.rokid.rc.shared.protocol

/**
 * **Structured progress events** from the glasses to the phone.
 *
 * The glasses send only codes and arguments (e.g. `gl_ready_timeout` + `/data/local/tmp/...`),
 * and the phone composes the text in its own locale (see app-phone `ui/GhostlockText.kt`).
 * That way changing the phone's language, or adding a language, needs no change on the glasses, and the two sides
 * never mix locales.
 */
data class ProgressEvent(val code: String, val arg: String? = null)

/**
 * A failure message travels in the same `message` field in the form `code|arg` (arg may be omitted).
 * Older clients see the raw string; a new phone decodes it and displays it in its own locale.
 */
object ProgressCodec {
    private const val SEP = '|'

    fun encode(code: String, arg: String? = null): String =
        if (arg.isNullOrEmpty()) code else "$code$SEP$arg"

    /** Decode; a non-code string (e.g. an older or third-party message) is returned unchanged as code with arg=null. */
    fun decode(raw: String): Pair<String, String?> {
        val i = raw.indexOf(SEP)
        return if (i <= 0) raw to null else raw.substring(0, i) to raw.substring(i + 1)
    }
}

/** Progress codes of the Ghostlock/LPE flow. One-to-one with the phone-side `GhostlockText`. */
object GhostlockCodes {
    // ---- generic ----
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

    // ---- direct execution (shell/Shizuku) ----
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

    // ---- Shizuku-specific ----
    const val SHIZUKU_NOT_RUNNING = "gl_shizuku_not_running"
    const val SHIZUKU_UNAUTHORIZED = "gl_shizuku_unauthorized"
    const val SHIZUKU_BIND_FAILED = "gl_shizuku_bind_failed"
    const val SHIZUKU_ROOT_RUN_SHELL = "gl_shizuku_root_run_shell"
    const val SHIZUKU_NO_RUN_SHELL = "gl_shizuku_no_run_shell"
    const val SHIZUKU_BAD_IDENTITY = "gl_shizuku_bad_identity"
    const val SHIZUKU_UNAVAILABLE = "gl_shizuku_unavailable"
    const val ATTEMPT_SHIZUKU = "gl_attempt_shizuku"

    // ---- engine selection (P1 runtime probing) ----
    /** The engine actually used (arg = `shizuku`/`bridge`). */
    const val ENGINE_SELECTED = "gl_engine_selected"
    const val ENGINE_SHIZUKU_RUNNING = "gl_engine_shizuku_running"
    const val ENGINE_BRIDGE_RESIDENT = "gl_engine_bridge_resident"
    const val ENGINE_NONE_AVAILABLE = "gl_engine_none"
    /** Retry on the other pipeline after the preferred engine failed. */
    const val ENGINE_RETRY_OTHER = "gl_engine_retry_other"

    // ---- failure classification ----
    const val KERNEL_PANIC = "gl_kernel_panic"
    const val PROCESS_DIED = "gl_process_died"
    const val PROCESS_DIED_RETRY = "gl_process_died_retry"
    const val RETRY_AFTER = "gl_retry_after"
    const val ATTEMPTS_EXHAUSTED = "gl_attempts_exhausted"
    const val FALLBACK_FAILED = "gl_fallback_failed"
}
