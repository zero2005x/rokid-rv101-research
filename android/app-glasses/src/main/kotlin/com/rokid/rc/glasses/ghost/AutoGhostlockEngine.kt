package com.rokid.rc.glasses.ghost

import android.content.Context
import android.util.Log
import com.rokid.rc.shared.protocol.GhostlockCodes
import java.io.File

/**
 * Runtime engine selection (**P1**): the compile-time constant `USE_ENGINE` is no longer used.
 *
 * On each call it picks an engine according to "**which trigger pipeline is alive right now**" and delegates:
 * | Order | Condition | Engine | Note |
 * |------|------|------|------|
 * | 1 | Shizuku server is running | [ShizukuGhostlockEngine] | runs the exploit as shell; an internal failure falls back to bridge |
 * | 2 | bridge helper is resident | [BridgeGhostlockEngine] | the resident `deploy/root_bridge.sh` loop |
 * | 3 | neither | [ShizukuGhostlockEngine] | it reports "Shizuku not running" and attempts the fallback, giving the most complete message |
 *
 * If the root daemon is already alive, any engine's `acquireRoot()` takes the fast path and reports success directly (skipping the LPE).
 *
 * This way, changing the environment (running start_shizuku or root_bridge first) **needs no APK rebuild**,
 * and a reported failure reason corresponds to the actual probe result.
 */
class AutoGhostlockEngine(private val app: Context) : GhostlockEngine {

    private val shizuku = ShizukuGhostlockEngine(app, fallback = BridgeGhostlockEngine())
    private val bridge = BridgeGhostlockEngine()

    /** Name of the engine actually used most recently (`shizuku`/`bridge`), for P2 metrics and status display. */
    @Volatile
    var lastEngineName: String = "none"
        private set

    /** Reason for the most recent selection (structured code, shown on the phone). */
    @Volatile
    var lastReasonCode: String? = null
        private set

    override val isEnabled: Boolean
        get() = current().isEnabled

    override val rootAcquired: Boolean
        get() = shizuku.rootAcquired || bridge.rootAcquired

    override var onProgress: ((com.rokid.rc.shared.protocol.ProgressEvent) -> Unit)?
        get() = shizuku.onProgress
        set(value) {
            shizuku.onProgress = value
            bridge.onProgress = value
        }

    override fun setEnabled(enabled: Boolean) {
        shizuku.setEnabled(enabled)
        bridge.setEnabled(enabled)
    }

    override fun doneLog(): File = File(com.rokid.rc.glasses.GlassesPaths.TMP, "root_done.log")

    override suspend fun exec(payload: RootScript): ExecResult {
        val engine = resolve()
        Log.i(TAG, "exec via $lastEngineName (${lastReasonCode})")
        return engine.exec(payload)
    }

    override suspend fun acquireRoot(): ExecResult {
        val engine = resolve()
        Log.i(TAG, "acquireRoot via $lastEngineName (${lastReasonCode})")
        engine.progress(GhostlockCodes.ENGINE_SELECTED, lastEngineName)
        val result = engine.acquireRoot()
        if (result.success || engine.rootAcquired) return result
        // if the selected engine fails and the other pipeline is in fact usable, try once more (avoid misjudging on probe timing)
        val other = if (engine === shizuku) bridge else shizuku
        val otherReady = if (other === bridge) isBridgeHelperResident() else isShizukuAvailable()
        if (!otherReady) return result
        Log.i(TAG, "$lastEngineName failed -> retry with ${if (other === bridge) "bridge" else "shizuku"}")
        lastEngineName = if (other === bridge) "bridge" else "shizuku"
        lastReasonCode = GhostlockCodes.ENGINE_RETRY_OTHER
        other.progress(GhostlockCodes.ENGINE_SELECTED, lastEngineName)
        val retry = other.acquireRoot()
        return if (retry.success || other.rootAcquired) retry else result
    }

    /** Pick an engine for the current environment (and record the reason code). */
    private fun resolve(): GhostlockEngine {
        val shizukuUp = isShizukuAvailable()
        val helperUp = isBridgeHelperResident()
        return when {
            shizukuUp -> {
                lastEngineName = "shizuku"
                lastReasonCode = GhostlockCodes.ENGINE_SHIZUKU_RUNNING
                shizuku
            }
            helperUp -> {
                lastEngineName = "bridge"
                lastReasonCode = GhostlockCodes.ENGINE_BRIDGE_RESIDENT
                bridge
            }
            else -> {
                lastEngineName = "shizuku"
                lastReasonCode = GhostlockCodes.ENGINE_NONE_AVAILABLE
                shizuku
            }
        }
    }

    /**
     * Whether Shizuku is available: use the official API (`Shizuku.pingBinder()`).
     * ⚠ Do not judge by `ps` — an Android app cannot see other processes.
     */
    private fun isShizukuAvailable(): Boolean =
        runCatching { rikka.shizuku.Shizuku.pingBinder() }.getOrDefault(false)

    /** Whether the bridge helper is resident: read the helper heartbeat file (one round a second). */
    private fun isBridgeHelperResident(): Boolean = runCatching {
        val f = File(com.rokid.rc.glasses.GlassesPaths.BRIDGE_HELPER_ALIVE)
        if (!f.exists()) return@runCatching false
        val ts = f.readText().trim().toLongOrNull() ?: return@runCatching false
        (System.currentTimeMillis() / 1000) - ts in 0..120L
    }.getOrDefault(false)

    private fun current(): GhostlockEngine = when (lastEngineName) {
        "bridge" -> bridge
        else -> shizuku
    }

    companion object {
        private const val TAG = "AutoGhostlockEngine"
    }
}
