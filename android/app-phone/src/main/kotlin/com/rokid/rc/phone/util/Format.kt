package com.rokid.rc.phone.util

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** 顯示格式化工具。 */
object Format {

    private val dateFmt = SimpleDateFormat("yyyy/MM/dd HH:mm", Locale.US)

    fun size(bytes: Long): String = when {
        bytes >= 1_048_576 -> String.format(Locale.US, "%.1f MB", bytes / 1_048_576.0)
        bytes >= 1024 -> String.format(Locale.US, "%.0f KB", bytes / 1024.0)
        else -> "$bytes B"
    }

    /** ms → mm:ss（超過一小時顯示 h:mm:ss）。 */
    fun duration(ms: Long): String {
        val totalSec = ms / 1000
        val h = totalSec / 3600
        val m = (totalSec % 3600) / 60
        val s = totalSec % 60
        return if (h > 0) String.format(Locale.US, "%d:%02d:%02d", h, m, s)
        else String.format(Locale.US, "%02d:%02d", m, s)
    }

    fun date(epochMs: Long): String = dateFmt.format(Date(epochMs))

    /** 媒體類型標籤（影片／照片）對應的字串資源 id；由呼叫端以 getString 取在地化文字。 */
    fun typeLabelRes(type: com.rokid.rc.shared.protocol.MediaType): Int =
        if (type == com.rokid.rc.shared.protocol.MediaType.VIDEO) com.rokid.rc.phone.R.string.media_type_video
        else com.rokid.rc.phone.R.string.media_type_photo
}
