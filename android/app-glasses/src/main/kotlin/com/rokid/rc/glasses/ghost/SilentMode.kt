package com.rokid.rc.glasses.ghost

import android.content.Context
import android.util.Log

/**
 * Silent capture (force-suppress the photo/video shutter sound) state management.
 *
 * - Needs root (bind-mounts the silent file, see [SilentPayloads]); reports failure while root is not ready.
 * - The setting is persisted in SharedPreferences (default **on**, matching the "force the shutter sound off" requirement).
 * - Applied automatically by CaptureService after escalation; the phone can toggle it live with `SET_SILENT`.
 * - Does not survive a reboot: the bind-mount disappears on reboot, so it must be re-applied after re-escalating (same lifetime as root).
 */
class SilentMode(context: Context) {

    private val prefs = context.getSharedPreferences("glasses_silent", Context.MODE_PRIVATE)

    @Volatile
    var enabled: Boolean = prefs.getBoolean("silent_capture", true)
        private set

    /** Whether it is actually mounted right now (updated by [refreshApplied]). */
    @Volatile
    var applied: Boolean = false
        private set

    /** Set the flag and apply immediately; returns whether it succeeded. */
    suspend fun setEnabled(on: Boolean): Boolean {
        enabled = on
        prefs.edit().putBoolean("silent_capture", on).apply()
        return applyNow()
    }

    /** Apply it when the setting is on (called after escalation succeeds). */
    suspend fun applyIfEnabled(): Boolean = if (enabled) applyNow() else true

    /** Re-read the actual mount state (for the STATUS report). */
    suspend fun refreshApplied(): Boolean {
        val client = RootDaemonClient()
        if (!client.isRoot()) {
            applied = false
            return false
        }
        val count = client.exec(SilentPayloads.countCommand(), 8_000L).trim().toIntOrNull() ?: 0
        applied = count > 0
        return applied
    }

    private suspend fun applyNow(): Boolean {
        val client = RootDaemonClient()
        if (!client.isRoot()) {
            Log.w(TAG, "root 未就緒，無法套用靜音拍攝")
            applied = false
            return false
        }
        return try {
            if (enabled) {
                // write the silent file once (skipped when it already exists)
                val w = client.exec(SilentPayloads.writeOggCommand(), 15_000L)
                if (w.contains("SILENT_FILE_ERR")) {
                    Log.e(TAG, "靜音檔寫入失敗")
                    applied = false
                    return false
                }
                val out = client.exec(SilentPayloads.applyCommand(), 20_000L)
                Log.i(TAG, "apply: ${out.replace('\n', ' ')}")
                applied = refreshApplied()
                applied
            } else {
                val out = client.exec(SilentPayloads.restoreCommand(), 20_000L)
                Log.i(TAG, "restore: ${out.replace('\n', ' ')}")
                refreshApplied()
                !applied
            }
        } catch (e: Exception) {
            Log.e(TAG, "套用失敗：${e.message}")
            applied = false
            false
        }
    }

    companion object {
        private const val TAG = "SilentMode"
    }
}
