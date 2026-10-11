package com.rokid.rc.phone.ui

import android.content.Context
import com.rokid.rc.phone.R
import com.rokid.rc.shared.protocol.CameraErrors

/**
 * 把眼鏡端送來的**結構化戒指事件**轉成手機語系的文字。
 *
 * 眼鏡端只送代碼（例如 `photo_ok`）與參數（檔名／原因／旗標清單），
 * 因此手機換語言、或未來加語言，都不需要動眼鏡端（原本眼鏡直接送中文句子）。
 */
object RingText {

    /** 動作結果；無法對應時回 null（呼叫端可選擇不顯示）。 */
    fun result(ctx: Context, key: String?, arg: String?): String? = when (key) {
        null, "" -> null
        "photo_ok" -> ctx.getString(R.string.ring_photo_ok, arg.orEmpty())
        "photo_fail" -> ctx.getString(R.string.ring_photo_fail, errorDetail(ctx, arg))
        "video_start" -> ctx.getString(R.string.ring_video_start)
        "video_fail" -> ctx.getString(R.string.ring_video_fail, errorDetail(ctx, arg))
        "video_saved" -> ctx.getString(R.string.ring_video_saved, arg.orEmpty())
        "stop_fail" -> ctx.getString(R.string.ring_stop_fail, errorDetail(ctx, arg))
        "silent_on" -> ctx.getString(R.string.ring_silent_on)
        "silent_on_pending" -> ctx.getString(R.string.ring_silent_on_pending)
        "silent_off" -> ctx.getString(R.string.ring_silent_off)
        "display_switched" -> ctx.getString(R.string.ring_display_switched, displayList(ctx, arg))
        "camera_not_ready" -> ctx.getString(R.string.ring_camera_not_ready)
        "no_action" -> ctx.getString(R.string.ring_no_action, arg.orEmpty())
        "no_device" -> ctx.getString(R.string.ring_status_no_device, arg.orEmpty())
        "start_failed" -> ctx.getString(R.string.ring_status_start_failed)
        "stopped" -> ctx.getString(R.string.ring_status_stopped)
        "running" -> null // 執行中/裝置路徑已由狀態欄位呈現，避免重複
        else -> ctx.getString(R.string.ring_err_prefix, key)
    }

    /**
     * 把眼鏡端回傳的**錯誤碼**轉成手機語系文字（2026-09-13）。
     *
     * 先前所有相機失敗都送 `"camera not ready"`，使用者看到「拍照失敗：camera not ready」
     * 會以為是**戒指連線**問題；實際上「正在錄影」與「服務沒起來」是完全不同的處置。
     * 未知的碼原樣顯示，方便除錯（不吞掉資訊）。
     */
    fun errorDetail(ctx: Context, code: String?): String {
        val c = code?.trim().orEmpty()
        return when (c) {
            CameraErrors.BUSY_RECORDING -> ctx.getString(R.string.ring_err_busy_recording)
            CameraErrors.RECORD_START_TIMEOUT -> ctx.getString(R.string.ring_err_record_timeout)
            CameraErrors.BUSY -> ctx.getString(R.string.ring_err_busy)
            // 相機未就緒最常見的原因是「眼鏡端的 CaptureService 被系統以 app idle 停掉」
            // → 明確告訴使用者要做什麼，而不是只說「相機未就緒」
            CameraErrors.NOT_READY -> ctx.getString(R.string.ring_camera_not_ready)
            else -> c
        }
    }

    /** 顯示配置旗標清單（`conn,led`）→ 以手機語系組字。 */
    fun displayList(ctx: Context, tokens: String?): String {
        if (tokens.isNullOrBlank()) return ctx.getString(R.string.ring_display_none)
        return tokens.split(",").mapNotNull { t ->
            when (t.trim()) {
                "conn" -> ctx.getString(R.string.chip_disp_conn)
                "root" -> ctx.getString(R.string.chip_disp_root)
                "camera" -> ctx.getString(R.string.chip_disp_camera)
                "storage" -> ctx.getString(R.string.chip_disp_storage)
                "led" -> ctx.getString(R.string.chip_disp_led)
                else -> null
            }
        }.joinToString("+")
    }
}
