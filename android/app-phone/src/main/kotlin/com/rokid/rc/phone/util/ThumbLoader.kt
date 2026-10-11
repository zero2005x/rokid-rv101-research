package com.rokid.rc.phone.util

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import com.rokid.rc.phone.Remote
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * 縮圖載入器：磁碟檔由 Remote.getThumbnail 提供（依 mediaId cache 在 app cache），
 * 這裡負責「取檔 → 依目標尺寸降採樣 → 記憶體 LruCache」。
 */
object ThumbLoader {

    private val mem = object : LruCache<String, Bitmap>((Runtime.getRuntime().maxMemory() / 8).toInt()) {
        override fun sizeOf(key: String, value: Bitmap): Int = value.byteCount
    }

    private fun key(mediaId: String, maxPx: Int) = "$mediaId@$maxPx"

    /**
     * 取得遠端縮圖 bitmap。
     * @param mediaId 媒體 id
     * @param maxPx   目標長邊像素（網格約 512、封面約 1024）
     * @param renew   即使有 cache 檔也重新抓（相簿重掃後）
     */
    suspend fun remote(mediaId: String, maxPx: Int, renew: Boolean = false): Bitmap? {
        mem.get(key(mediaId, maxPx))?.let { return it }
        val cached = File(Remote.appContext.cacheDir, "thumbs/$mediaId.jpg")
        if (!renew && cached.exists() && cached.length() > 0) {
            decode(cached, maxPx)?.let { mem.put(key(mediaId, maxPx), it); return it }
        }
        val f = try {
            Remote.getThumbnail(mediaId, maxPx, maxPx).getOrNull()
        } catch (_: Exception) {
            null
        } ?: return null
        return decode(f, maxPx)?.also { mem.put(key(mediaId, maxPx), it) }
    }

    /** 本機圖片直接降採樣（不進遠端）。 */
    suspend fun local(file: File?, maxPx: Int): Bitmap? {
        if (file == null || !file.exists()) return null
        return decode(file, maxPx)
    }

    private suspend fun decode(file: File, maxPx: Int): Bitmap? = withContext(Dispatchers.IO) {
        try {
            val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.absolutePath, opts)
            if (opts.outWidth <= 0 || opts.outHeight <= 0) return@withContext null
            var sample = 1
            while (opts.outWidth / sample > maxPx * 2 || opts.outHeight / sample > maxPx * 2) sample *= 2
            val d = BitmapFactory.Options().apply { inSampleSize = sample }
            BitmapFactory.decodeFile(file.absolutePath, d)
        } catch (_: Exception) {
            null
        }
    }
}
