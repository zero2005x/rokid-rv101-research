package com.rokid.rc.glasses.ghost

import android.content.Context
import com.rokid.rc.glasses.GlassesPaths
import java.io.File

/** The script payload to run as root. */
data class RootScript(val name: String, val content: String)

/** root execution result. */
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

/** Trigger flow stages (for failure diagnosis). */
object Stage {
    const val PREFLIGHT = "preflight"
    const val LAUNCH = "launch"
    const val READY = "ready"
    const val INSTALL = "install"
    const val DONE = "done"
}

/** Payload completion marker. The payload script `echo`s this string into the payload log at the end. */
object PayloadMarkers {
    const val DONE = "__GHOSTLOCK_PAYLOAD_DONE__"
}

/** File paths shared by the engines (all under the app's own readable/writable cacheDir). */
object GhostlockPaths {
    fun payloadLog(context: Context): File = File(context.cacheDir, "ghostlock_payload.log")
    fun exploitLog(context: Context): File = File(context.cacheDir, "ghostlock_run.log")
}

/**
 * exploit trigger parameters, matching the existing behavior of run_*_orchestrator.sh.
 * When the paths/markers differ on a real device, only this needs adjusting.
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
    // measured 100-142 s for the exploit's core stage (the helper uses 200 s; plus headroom for internal retries)
    val doneTimeoutMs: Long = 240_000L,
    val pollIntervalMs: Long = 200L
)

/** Parameters of the root bridge daemon (matching deploy/root_bridge.sh). */
data class BridgeConfig(
    val bridgeDir: String = GlassesPaths.BRIDGE_DIR,
    val timeoutMs: Long = 500_000L,
    val pollIntervalMs: Long = 300L
)

/**
 * Ghostlock LPE invocation layer interface.
 * Every glasses-side action that needs root (suppressing the camera privacy LED) goes through this interface;
 * the phone-side ghostlock switch controls [isEnabled].
 */
interface GhostlockEngine {
    val isEnabled: Boolean

    /** Whether root has been acquired successfully (set by [acquireRoot]). */
    val rootAcquired: Boolean

    /** Progress callback (reports stages during long operations, for pushing to the phone display). */
    var onProgress: ((com.rokid.rc.shared.protocol.ProgressEvent) -> Unit)?

    /**
     * Send structured progress (code + argument); the phone composes the text in **its own locale**.
     * The glasses side no longer composes Chinese sentences (see shared `ProgressEvent`/`GhostlockCodes`).
     */
    fun progress(code: String, arg: String? = null) {
        onProgress?.invoke(com.rokid.rc.shared.protocol.ProgressEvent(code, arg))
    }

    fun setEnabled(enabled: Boolean)

    /** The log file the payload completion marker (DONE) is written to; the engine decides it so that it is guaranteed readable. */
    fun doneLog(): File

    suspend fun exec(payload: RootScript): ExecResult

    /** Run the LPE once to verify/acquire root (equivalent to running a verification payload). */
    suspend fun acquireRoot(): ExecResult
}
