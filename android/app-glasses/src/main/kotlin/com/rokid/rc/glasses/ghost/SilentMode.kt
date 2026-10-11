package com.rokid.rc.glasses.ghost

import android.content.Context
import android.util.Log

/**
 * 靜音拍攝（強制壓制拍照/錄影提示音）狀態管理。
 *
 * - 需要 root（bind-mount 掛載靜音檔，見 [SilentPayloads]）；root 未就緒時回報失敗。
 * - 設定持久化於 SharedPreferences（預設 **開啟**，符合「強迫關閉提示音」需求）。
 * - 提權成功後由 CaptureService 自動套用；手機可用 `SET_SILENT` 即時切換。
 * - 不跨 reboot：reboot 後 bind-mount 自動消失，需重新提權後再套用（與 root 同生命週期）。
 */
class SilentMode(context: Context) {

    private val prefs = context.getSharedPreferences("glasses_silent", Context.MODE_PRIVATE)

    @Volatile
    var enabled: Boolean = prefs.getBoolean("silent_capture", true)
        private set

    /** 目前是否確實已掛載（由 [refreshApplied] 更新）。 */
    @Volatile
    var applied: Boolean = false
        private set

    /** 設定並立即套用；回傳是否成功。 */
    suspend fun setEnabled(on: Boolean): Boolean {
        enabled = on
        prefs.edit().putBoolean("silent_capture", on).apply()
        return applyNow()
    }

    /** 若設定為開啟則套用（提權成功後呼叫）。 */
    suspend fun applyIfEnabled(): Boolean = if (enabled) applyNow() else true

    /** 重新讀取實際掛載狀態（供 STATUS 回報）。 */
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
                // 一次性寫入靜音檔（已存在則跳過）
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
