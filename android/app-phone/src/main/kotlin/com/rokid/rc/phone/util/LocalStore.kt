package com.rokid.rc.phone.util

import android.content.Context
import com.rokid.rc.shared.protocol.MediaTypes
import com.rokid.rc.phone.Remote
import com.rokid.rc.shared.protocol.MediaItem
import com.rokid.rc.shared.protocol.MediaType
import org.json.JSONObject
import java.io.File

/** Local album data (a single album folder). */
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
 * Management of media downloaded on the phone: directory scanning, albumId->name index, pseudo MediaItem mapping.
 * Location: getExternalFilesDir(null)/albums/<albumId>/<file>
 * Index: getExternalFilesDir(null)/albums_index.json
 */
object LocalStore {

    private val ctx: Context get() = Remote.appContext

    fun rootDir(): File = File(ctx.getExternalFilesDir(null), "albums")

    /**
     * Clean up leftover partial files (`*.part`) from interrupted/timed-out transfers.
     *
     * A transfer writes `<transferId>.part` first and renames it to the real file only on completion,
     * so every failure or cancellation leaves a whole file behind (measured: the Bluetooth timeout
     * case left 16.7 MB, 63 MB accumulated). A single transfer's timeout is about 4 hours
     * (664 MB / 45 KB/s), so a 24-hour threshold cannot delete an in-progress file by mistake.
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

    /** Record the name associated with an albumId when downloading. */
    fun rememberAlbumName(albumId: String, name: String) {
        val map = readIndex()
        if (map[albumId] != name) {
            map[albumId] = name
            writeIndex(map)
        }
    }

    /** Overwrite the index with the album list from the latest connection (names sync after an album rename). */
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

    /** Map an album's local files to pseudo MediaItems (id = file name, shared with the UI). */
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

    /** Delete one media file; remove the folder too once it is empty. */
    fun deleteLocalFile(albumId: String, fileName: String): Boolean {
        val dir = File(rootDir(), albumId)
        val f = File(dir, fileName)
        val ok = f.delete()
        if (dir.isDirectory && dir.listFiles().isNullOrEmpty()) dir.delete()
        return ok
    }
}
