package com.rokid.rc.phone.util

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Display formatting helpers. */
object Format {

    private val dateFmt = SimpleDateFormat("yyyy/MM/dd HH:mm", Locale.US)

    fun size(bytes: Long): String = when {
        bytes >= 1_048_576 -> String.format(Locale.US, "%.1f MB", bytes / 1_048_576.0)
        bytes >= 1024 -> String.format(Locale.US, "%.0f KB", bytes / 1024.0)
        else -> "$bytes B"
    }

    /** ms → mm:ss (shows h:mm:ss beyond one hour). */
    fun duration(ms: Long): String {
        val totalSec = ms / 1000
        val h = totalSec / 3600
        val m = (totalSec % 3600) / 60
        val s = totalSec % 60
        return if (h > 0) String.format(Locale.US, "%d:%02d:%02d", h, m, s)
        else String.format(Locale.US, "%02d:%02d", m, s)
    }

    fun date(epochMs: Long): String = dateFmt.format(Date(epochMs))

    /** String resource id for the media type label (video/photo); the caller uses getString to get the localized text. */
    fun typeLabelRes(type: com.rokid.rc.shared.protocol.MediaType): Int =
        if (type == com.rokid.rc.shared.protocol.MediaType.VIDEO) com.rokid.rc.phone.R.string.media_type_video
        else com.rokid.rc.phone.R.string.media_type_photo
}
