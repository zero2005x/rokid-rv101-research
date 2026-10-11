package com.rokid.rc.phone.util

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.LruCache
import com.rokid.rc.phone.Remote
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Thumbnail loader: the disk file is provided by Remote.getThumbnail (cached in the app cache by mediaId);
 * this handles "fetch file → downsample to the target size → in-memory LruCache".
 */
object ThumbLoader {

    private val mem = object : LruCache<String, Bitmap>((Runtime.getRuntime().maxMemory() / 8).toInt()) {
        override fun sizeOf(key: String, value: Bitmap): Int = value.byteCount
    }

    private fun key(mediaId: String, maxPx: Int) = "$mediaId@$maxPx"

    /**
     * Get a remote thumbnail bitmap.
     * @param mediaId media id
     * @param maxPx   target long-edge pixels (about 512 for the grid, about 1024 for the cover)
     * @param renew   re-fetch even when a cache file exists (after an album re-scan)
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

    /** Downsample a local image directly (does not go to the remote). */
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
