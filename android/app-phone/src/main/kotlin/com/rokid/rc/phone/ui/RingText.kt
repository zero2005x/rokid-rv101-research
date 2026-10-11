package com.rokid.rc.phone.ui

import android.content.Context
import com.rokid.rc.phone.R
import com.rokid.rc.shared.protocol.CameraErrors

/**
 * Turns the **structured ring events** sent by the glasses into text in the phone's locale.
 *
 * The glasses send only codes (for example `photo_ok`) and arguments (file name / reason / flag list),
 * so changing the phone's language, or adding a language later, needs no change on the glasses (previously the glasses sent Chinese sentences directly).
 */
object RingText {

    /** Action result; returns null when it cannot be mapped (the caller may choose not to display it). */
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
        "running" -> null // running / device path are already shown by the status field, so avoid duplicating them
        else -> ctx.getString(R.string.ring_err_prefix, key)
    }

    /**
     * Turns the **error code** returned by the glasses into text in the phone's locale (2026-09-13).
     *
     * Previously every camera failure sent `"camera not ready"`, so a user seeing "photo failed: camera not ready"
     * would think it was a **ring connection** problem; in fact "currently recording" and "the service is not up"
     * call for completely different handling. An unknown code is shown as-is, for easier debugging (the information is not swallowed).
     */
    fun errorDetail(ctx: Context, code: String?): String {
        val c = code?.trim().orEmpty()
        return when (c) {
            CameraErrors.BUSY_RECORDING -> ctx.getString(R.string.ring_err_busy_recording)
            CameraErrors.RECORD_START_TIMEOUT -> ctx.getString(R.string.ring_err_record_timeout)
            CameraErrors.BUSY -> ctx.getString(R.string.ring_err_busy)
            // the most common cause of a camera not being ready is "the glasses-side CaptureService was stopped by the system as an idle app"
            // → tell the user explicitly what to do, instead of only saying "camera not ready"
            CameraErrors.NOT_READY -> ctx.getString(R.string.ring_camera_not_ready)
            else -> c
        }
    }

    /** Display-config flag list (`conn,led`) → composed in the phone's locale. */
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
