package com.rokid.rc.phone.util

import android.content.Context
import com.rokid.rc.shared.protocol.MediaTypes
import com.rokid.rc.phone.Remote
import com.rokid.rc.shared.protocol.MediaItem
import com.rokid.rc.shared.protocol.MediaType
import org.json.JSONObject
import java.io.File

/** 本機相簿資料（單一「相簿夾」）。 */
data class LocalAlbum(
    val albumId: String,
    val name: String,
    val dir: File,
    val mediaCount: Int,
    val sizeBytes: Long,
    val coverFile: File?,
    val newestAt: Long
)

/**
 * 手機端已下載媒體的管理：目錄掃描、albumId→名稱索引、偽 MediaItem 映射。
 * 位置：getExternalFilesDir(null)/albums/<albumId>/<file>
 * 索引：getExternalFilesDir(null)/albums_index.json
 */
object LocalStore {

    private val ctx: Context get() = Remote.appContext

    fun rootDir(): File = File(ctx.getExternalFilesDir(null), "albums")

    /**
     * 清掉中斷／逾時傳輸留下的暫存檔（`*.part`）。
     *
     * 傳輸是「先寫 `<transferId>.part`，完成才 rename 成正式檔」，所以任何一次失敗或取消都會留下
     * 一整個檔案的殘骸（實測藍牙逾時那次留下 16.7 MB，累積已有 63 MB）。單筆傳輸的逾時上限
     * 約 4 小時（664 MB ÷ 45 KB/s），故以 24 小時為界不可能誤刪進行中的檔案。
     */
    fun cleanupStaleParts(maxAgeMs: Long = 24 * 60 * 60 * 1000L) {
        val now = System.currentTimeMillis()
        runCatching {
            rootDir().listFiles()?.forEach { dir ->
                if (!dir.isDirectory) return@forEach
                dir.listFiles()?.forEach { f ->
                    if (f.isFile && f.name.endsWith(".part") && now - f.lastModified() > maxAgeMs) {
                        f.delete()
                    }
                }
            }
        }
    }

    fun albumDir(albumId: String): File {
        val d = File(rootDir(), albumId)
        d.mkdirs()
        return d
    }

    private fun indexFile(): File = File(ctx.getExternalFilesDir(null), "albums_index.json")

    private fun readIndex(): MutableMap<String, String> {
        val out = mutableMapOf<String, String>()
        try {
            val f = indexFile()
            if (f.exists()) {
                val o = JSONObject(f.readText())
                val albums = o.optJSONObject("albums") ?: return out
                albums.keys().forEach { k -> out[k] = albums.optString(k) }
            }
        } catch (_: Exception) {
        }
        return out
    }

    private fun writeIndex(map: Map<String, String>) {
        try {
            val o = JSONObject()
            val albums = JSONObject()
            map.forEach { (k, v) -> albums.put(k, v) }
            o.put("albums", albums)
            indexFile().writeText(o.toString())
        } catch (_: Exception) {
        }
    }

    /** 下載時記錄 albumId 對應的名稱。 */
    fun rememberAlbumName(albumId: String, name: String) {
        val map = readIndex()
        if (map[albumId] != name) {
            map[albumId] = name
            writeIndex(map)
        }
    }

    /** 以最新連線取得的相簿清單覆寫索引（相簿更名後名稱同步）。 */
    fun syncNamesFromRemote(remoteAlbums: List<com.rokid.rc.shared.protocol.Album>) {
        if (remoteAlbums.isEmpty()) return
        val map = readIndex()
        remoteAlbums.forEach { map[it.id] = it.name }
        writeIndex(map)
    }

    fun albumName(albumId: String): String? = readIndex()[albumId]

    fun localAlbumIds(): List<String> =
        rootDir().listFiles()?.filter { it.isDirectory && it.listFiles()?.isNotEmpty() == true }
            ?.map { it.name } ?: emptyList()

    fun listLocalAlbums(): List<LocalAlbum> {
        val names = readIndex()
        return localAlbumIds().mapNotNull { id ->
            val dir = File(rootDir(), id)
            val files = dir.listFiles()?.filter { it.isFile } ?: emptyList()
            if (files.isEmpty()) return@mapNotNull null
            val newest = files.maxByOrNull { it.lastModified() } ?: return@mapNotNull null
            LocalAlbum(
                albumId = id,
                name = names[id] ?: id,
                dir = dir,
                mediaCount = files.size,
                sizeBytes = files.sumOf { it.length() },
                coverFile = newest,
                newestAt = newest.lastModified()
            )
        }.sortedByDescending { it.newestAt }
    }

    /** 把某相簿的本機檔案映射成偽 MediaItem（id = 檔名，供 UI 共用）。 */
    fun localMediaItems(albumId: String): List<MediaItem> {
        val dir = File(rootDir(), albumId)
        val files = dir.listFiles()?.filter { it.isFile && !MediaTypes.isPartial(it.name) } ?: emptyList()
        return files.map { f ->
            val video = MediaTypes.isVideo(f.name)
            MediaItem(
                id = f.name,
                albumId = albumId,
                type = if (video) MediaType.VIDEO else MediaType.PHOTO,
                fileName = f.name,
                mime = if (video) MediaTypes.MIME_MP4 else MediaTypes.MIME_JPEG,
                sizeBytes = f.length(),
                width = 0, height = 0, durationMs = 0L,
                createdAt = f.lastModified(),
                thumbReady = !video
            )
        }.sortedByDescending { it.createdAt }
    }

    fun localFilePath(albumId: String, fileName: String): File =
        File(File(rootDir(), albumId), fileName)

    /** 刪除單一媒體檔；資料夾空了就一併移除。 */
    fun deleteLocalFile(albumId: String, fileName: String): Boolean {
        val dir = File(rootDir(), albumId)
        val f = File(dir, fileName)
        val ok = f.delete()
        if (dir.isDirectory && dir.listFiles().isNullOrEmpty()) dir.delete()
        return ok
    }
}
