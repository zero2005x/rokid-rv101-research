package com.rokid.rc.glasses.media

import android.content.Context
import com.rokid.rc.shared.protocol.MediaTypes
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.os.StatFs
import android.util.Log
import com.rokid.rc.shared.protocol.Album
import com.rokid.rc.shared.protocol.MediaItem
import com.rokid.rc.shared.protocol.MediaType
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.util.UUID

/**
 * 眼鏡端相簿冊與媒體索引。
 *
 * - 實體檔案：`<externalFilesDir>/media/albums/<albumId>/<file>`
 * - 索引：`<externalFilesDir>/media/index.json`（單一 JSON，易於檢視）
 * - 縮圖：`<cacheDir>/thumbs/<mediaId>.jpg`
 */
class MediaRepository(private val context: Context) {

    private val albumsDir = File(context.getExternalFilesDir(null), "media/albums")
    private val indexFile = File(context.getExternalFilesDir(null), "media/index.json")
    private val thumbDir = File(context.cacheDir, "thumbs")

    private val lock = Any()
    private val albums = LinkedHashMap<String, Album>()
    private val media = LinkedHashMap<String, MediaItem>()

    init {
        albumsDir.mkdirs()
        thumbDir.mkdirs()
        load()
    }

    val albumCount: Int get() = synchronized(lock) { albums.size }
    val mediaCount: Int get() = synchronized(lock) { media.size }

    fun storageFreeBytes(): Long = try {
        StatFs(context.filesDir.absolutePath).availableBytes
    } catch (e: Exception) {
        0L
    }

    fun getOrCreateDefaultAlbum(): String {
        synchronized(lock) {
            albums.values.firstOrNull { it.name == DEFAULT_ALBUM }?.let { return it.id }
        }
        return createAlbum(DEFAULT_ALBUM).id
    }

    fun listAlbums(): List<Album> = synchronized(lock) {
        albums.values.map { a ->
            a.copy(mediaCount = media.values.count { it.albumId == a.id })
        }
    }

    fun createAlbum(name: String): Album = synchronized(lock) {
        val id = "alb-" + UUID.randomUUID().toString().substring(0, 8)
        val album = Album(
            id = id,
            name = name.trim().ifEmpty { "New Album" },
            createdAt = System.currentTimeMillis(),
            mediaCount = 0
        )
        albumDir(id).mkdirs()
        albums[id] = album
        persist()
        album
    }

    fun renameAlbum(id: String, name: String): Boolean = synchronized(lock) {
        val a = albums[id] ?: return false
        albums[id] = a.copy(name = name.trim().ifEmpty { a.name })
        persist()
        true
    }

    fun deleteAlbum(id: String): Boolean = synchronized(lock) {
        if (!albums.containsKey(id)) return false
        media.values.filter { it.albumId == id }.forEach { thumbFile(it.id).delete() }
        media.entries.removeAll { it.value.albumId == id }
        albums.remove(id)
        albumDir(id).deleteRecursively()
        persist()
        true
    }

    fun listMedia(albumId: String): List<MediaItem> = synchronized(lock) {
        media.values.filter { it.albumId == albumId }.sortedByDescending { it.createdAt }
    }

    fun newPhotoFile(albumId: String): File {
        val dir = albumDir(albumId); dir.mkdirs()
        return File(dir, "IMG_${System.currentTimeMillis()}.jpg")
    }

    fun newVideoFile(albumId: String): File {
        val dir = albumDir(albumId); dir.mkdirs()
        return File(dir, "VID_${System.currentTimeMillis()}.mp4")
    }

    fun addMedia(albumId: String, type: MediaType, file: File): MediaItem {
        val info = readMediaInfo(file, type)
        val item = MediaItem(
            id = "med-" + UUID.randomUUID().toString().substring(0, 8),
            albumId = albumId,
            type = type,
            fileName = file.name,
            mime = if (type == MediaType.VIDEO) MediaTypes.MIME_MP4 else MediaTypes.MIME_JPEG,
            sizeBytes = file.length(),
            width = info.first,
            height = info.second,
            durationMs = info.third,
            createdAt = System.currentTimeMillis(),
            thumbReady = false
        )
        synchronized(lock) {
            media[item.id] = item
            persist()
        }
        return item
    }

    fun deleteMedia(id: String): Boolean = synchronized(lock) {
        if (!media.containsKey(id)) return false
        mediaFileLocked(id)?.delete()
        thumbFile(id).delete()
        media.remove(id)
        persist()
        true
    }

    fun mediaFile(id: String): File? = synchronized(lock) { mediaFileLocked(id) }

    fun getThumbnail(id: String, maxW: Int, maxH: Int): File? {
        val m = synchronized(lock) { media[id] } ?: return null
        val src = mediaFile(id) ?: return null
        // 來源檔已不在 → 不可回舊快取縮圖（否則刪檔後仍看得到圖，與清單狀態不一致）
        if (!src.isFile) return null
        val thumb = thumbFile(id)
        if (thumb.exists() && thumb.length() > 0) return thumb
        val bmp = when (m.type) {
            MediaType.PHOTO -> decodeSampled(src, maxW, maxH)
            MediaType.VIDEO -> videoFrame(src, maxW, maxH)
        } ?: return null
        try {
            thumb.parentFile?.mkdirs()
            FileOutputStream(thumb).use { bmp.compress(Bitmap.CompressFormat.JPEG, 80, it) }
        } catch (e: Exception) {
            Log.e(TAG, "thumbnail save failed", e)
            return null
        } finally {
            bmp.recycle()
        }
        synchronized(lock) {
            media[id] = m.copy(thumbReady = true)
            persist()
        }
        return thumb
    }

    // ---- internals ----

    private fun albumDir(albumId: String) = File(albumsDir, albumId)
    private fun thumbFile(id: String) = File(thumbDir, "$id.jpg")

    private fun mediaFileLocked(id: String): File? {
        val m = media[id] ?: return null
        return File(albumDir(m.albumId), m.fileName)
    }

    private fun readMediaInfo(file: File, type: MediaType): Triple<Int, Int, Long> {
        return try {
            if (type == MediaType.PHOTO) {
                val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeFile(file.absolutePath, opts)
                Triple(opts.outWidth, opts.outHeight, 0L)
            } else {
                val mmr = MediaMetadataRetriever()
                try {
                    mmr.setDataSource(file.absolutePath)
                    val w = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toIntOrNull() ?: 0
                    val h = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toIntOrNull() ?: 0
                    val d = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
                    Triple(w, h, d)
                } finally {
                    mmr.release()
                }
            }
        } catch (e: Exception) {
            Triple(0, 0, 0L)
        }
    }

    private fun decodeSampled(file: File, maxW: Int, maxH: Int): Bitmap? {
        return try {
            val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.absolutePath, opts)
            opts.inSampleSize = calculateSampleSize(opts.outWidth, opts.outHeight, maxW, maxH)
            opts.inJustDecodeBounds = false
            BitmapFactory.decodeFile(file.absolutePath, opts)
        } catch (e: Exception) {
            null
        }
    }

    private fun videoFrame(file: File, maxW: Int, maxH: Int): Bitmap? {
        val mmr = MediaMetadataRetriever()
        return try {
            mmr.setDataSource(file.absolutePath)
            val frame = mmr.frameAtTime ?: return null
            scaleDown(frame, maxW, maxH)
        } catch (e: Exception) {
            null
        } finally {
            mmr.release()
        }
    }

    private fun scaleDown(src: Bitmap, maxW: Int, maxH: Int): Bitmap {
        val w = src.width; val h = src.height
        if (w <= maxW && h <= maxH) return src
        val ratio = minOf(maxW.toFloat() / w, maxH.toFloat() / h)
        val nw = (w * ratio).toInt().coerceAtLeast(1)
        val nh = (h * ratio).toInt().coerceAtLeast(1)
        return Bitmap.createScaledBitmap(src, nw, nh, true)
    }

    private fun calculateSampleSize(w: Int, h: Int, maxW: Int, maxH: Int): Int {
        var s = 1
        if (w == 0 || h == 0) return s
        while (w / (s * 2) >= maxW && h / (s * 2) >= maxH) s *= 2
        return s
    }

    private fun load() {
        if (indexFile.exists()) {
            try {
                val root = JSONObject(indexFile.readText())
                val albArr = root.optJSONArray("albums") ?: JSONArray()
                for (i in 0 until albArr.length()) {
                    // 與 media 相同：逐筆容錯。`Album.fromJson` 用 getString("id"/"name")，
                    // 一筆壞掉若共用外層 try，會連帶讓整個 media 陣列也不載入（見 §16 A/B 的同型缺陷）
                    try {
                        val a = Album.fromJson(albArr.getJSONObject(i))
                        albums[a.id] = a
                    } catch (e: Exception) {
                        Log.e(TAG, "album entry #$i skipped: ${e.message}")
                    }
                }
                val medArr = root.optJSONArray("media") ?: JSONArray()
                for (i in 0 until medArr.length()) {
                    // 逐筆解析：單一壞掉的项目不該讓其餘全部消失
                    // （原本一筆丟例外就會被外層 catch 吞掉，後面全部不載入）
                    try {
                        val m = MediaItem.fromJson(medArr.getJSONObject(i))
                        media[m.id] = m
                    } catch (e: Exception) {
                        Log.e(TAG, "media entry #$i skipped: ${e.message}")
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "index load failed", e)
            }
        }
        reconcileWithDisk()
    }

    /**
     * 用磁碟上的實際檔案校正索引（索引不是唯一真相）。
     *
     * 為什麼需要：索引是**單一 JSON 檔**，任何一次「非原子寫入被中斷」或「解析失敗」都會讓已拍攝的
     * 項目從清單消失——**實際發生過**：相簿 7 筆只剩 1 筆，但 7 個檔案都還在磁碟上（2026-09-13 23:04）。
     * 這裡把「在磁碟上、但不在索引裡」的檔案重新登記，清單即可自我修復。
     */
    private fun reconcileWithDisk() {
        if (!albumsDir.isDirectory) return
        var added = 0
        var dropped = 0
        synchronized(lock) {
            // (0) 反向：索引有、磁碟沒有 → 移除。
            // 少了這一步，被 App 以外的方式刪掉的檔案會變成永久「幽靈項目」：
            // 清單仍顯示、每次 persist 又把它寫回去（DOWNLOAD_MEDIA 會回 media not found）。
            media.values.filter { !File(albumDir(it.albumId), it.fileName).isFile }.forEach { m ->
                media.remove(m.id)
                thumbFile(m.id).delete()
                dropped++
            }
            albumsDir.listFiles()?.forEach { dir ->
                if (!dir.isDirectory) return@forEach
                val albumId = dir.name
                if (!albums.containsKey(albumId)) {
                    albums[albumId] = Album(albumId, albumId, dir.lastModified(), 0)
                }
                val known = media.values.filter { it.albumId == albumId }
                    .mapTo(HashSet()) { it.fileName }
                dir.listFiles()?.forEach { f ->
                    if (!f.isFile || f.name.startsWith(".") || f.name in known) return@forEach
                    val type = when (f.extension.lowercase()) {
                        "mp4", "mov", "mkv" -> MediaType.VIDEO
                        "jpg", "jpeg", "png" -> MediaType.PHOTO
                        else -> return@forEach
                    }
                    val info = readMediaInfo(f, type)
                    val id = "med-" + UUID.randomUUID().toString().substring(0, 8)
                    media[id] = MediaItem(
                        id = id,
                        albumId = albumId,
                        type = type,
                        fileName = f.name,
                        mime = if (type == MediaType.VIDEO) MediaTypes.MIME_MP4 else MediaTypes.MIME_JPEG,
                        sizeBytes = f.length(),
                        width = info.first,
                        height = info.second,
                        durationMs = info.third,
                        createdAt = f.lastModified(),
                        thumbReady = false
                    )
                    added++
                }
            }
            if (added > 0 || dropped > 0) persist()
        }
        if (added > 0 || dropped > 0) {
            Log.w(TAG, "index reconciled: +$added re-registered / -$dropped removed (file missing on disk)")
        }
    }

    private fun persist() {
        try {
            val root = JSONObject()
            val albArr = JSONArray(); albums.values.forEach { albArr.put(it.toJson()) }
            val medArr = JSONArray(); media.values.forEach { medArr.put(it.toJson()) }
            root.put("albums", albArr).put("media", medArr)
            val dir = indexFile.parentFile
            dir?.mkdirs()
            // 原子寫入：先寫 .tmp 再 rename。原本直接 writeText（truncate 後才寫），
            // 程序若在兩者之間被殺（LMK／force-stop／安裝 APK）就會留下空或半截的索引。
            val tmp = File(dir, "index.json.tmp")
            tmp.writeText(root.toString())
            if (!tmp.renameTo(indexFile)) {
                indexFile.writeText(root.toString())
                tmp.delete()
            }
        } catch (e: Exception) {
            Log.e(TAG, "index persist failed", e)
        }
    }

    companion object {
        private const val TAG = "MediaRepository"
        private const val DEFAULT_ALBUM = "Camera"
    }
}
