package com.rokid.rc.phone

import android.graphics.Bitmap
import android.os.Bundle
import android.view.View
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.LinearProgressIndicator
import com.rokid.rc.phone.ui.MediaGridAdapter
import com.rokid.rc.phone.util.LocalStore
import com.rokid.rc.phone.util.ThumbLoader
import com.rokid.rc.shared.protocol.MediaItem
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/**
 * 媒體照片牆（3 欄正方格）。
 * mode=remote：眼鏡端相簿（縮圖走 RFCOMM、多選可批次下載/刪除眼鏡端原檔）
 * mode=local ：本機已下載相簿（多選僅能刪除本機檔案）
 */
class MediaActivity : AppCompatActivity() {

    private lateinit var mode: String
    private var albumId = ""
    private var albumName = ""
    private var adapter: MediaGridAdapter? = null
    private var busy = false
    private var reloadJob: Job? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_media)

        Remote.ensureInit(applicationContext)
        mode = intent.getStringExtra("mode") ?: "remote"
        albumId = intent.getStringExtra("albumId") ?: ""
        albumName = intent.getStringExtra("albumName") ?: albumId

        val toolbar = findViewById<com.google.android.material.appbar.MaterialToolbar>(R.id.toolbarMedia)
        toolbar.title = albumName
        toolbar.setNavigationOnClickListener { finish() }

        val rv = findViewById<RecyclerView>(R.id.rvMedia)
        rv.layoutManager = GridLayoutManager(this, 3)
        adapter = MediaGridAdapter(
            coverLoader = { item -> loadCover(item) },
            onClick = { item -> openViewer(item) },
            onLongClick = { enterSelection() },
            showDuration = mode == "remote"
        )
        rv.adapter = adapter
        adapter?.onSelectionChanged = { updateSelBar() }

        findViewById<View>(R.id.btnSelClose).setOnClickListener { adapter?.exitSelection() }
        findViewById<View>(R.id.btnSelAll).setOnClickListener {
            adapter?.let { if (it.selected.size < it.itemCount) it.selectAll() else it.exitSelection() }
        }
        val btnDownload = findViewById<View>(R.id.btnSelDownload)
        btnDownload.setOnClickListener { startBatchDownload() }
        if (mode == "local") btnDownload.visibility = View.GONE
        findViewById<View>(R.id.btnSelDelete).setOnClickListener { confirmBatchDelete() }

        reload()
    }

    override fun onResume() {
        super.onResume()
        if (adapter?.selectionMode != true) reload()
    }

    override fun onDestroy() {
        reloadJob?.cancel()
        adapter?.release()
        super.onDestroy()
    }

    private fun reload() {
        if (busy) return
        reloadJob = lifecycleScope.launch {
            val items = if (mode == "remote") {
                Remote.listMedia(albumId).getOrElse { emptyList() }
            } else {
                LocalStore.localMediaItems(albumId)
            }
            val empty = findViewById<View>(R.id.llEmpty)
            val rv = findViewById<RecyclerView>(R.id.rvMedia)
            if (items.isEmpty()) {
                rv.visibility = View.GONE
                empty.visibility = View.VISIBLE
                findViewById<TextView>(R.id.tvEmptyMsg).text =
                    if (mode == "remote") getString(R.string.empty_media)
                    else getString(R.string.toast_no_local_media)
            } else {
                empty.visibility = View.GONE
                rv.visibility = View.VISIBLE
                adapter?.submit(items)
            }
        }
    }

    private suspend fun loadCover(item: MediaItem): Bitmap? = when {
        mode == "remote" -> ThumbLoader.remote(item.id, 512)
        item.type == com.rokid.rc.shared.protocol.MediaType.VIDEO -> null
        else -> ThumbLoader.local(LocalStore.localFilePath(albumId, item.fileName), 512)
    }

    // ---------- 多選 ----------

    private fun enterSelection() {
        adapter?.setSelectionMode(true)
    }

    private fun updateSelBar() {
        val a = adapter ?: return
        val selBar = findViewById<View>(R.id.selBar)
        val toolbar = findViewById<com.google.android.material.appbar.MaterialToolbar>(R.id.toolbarMedia)
        if (a.selectionMode) {
            selBar.visibility = View.VISIBLE
            toolbar.visibility = View.GONE
            findViewById<TextView>(R.id.tvSelCount).text =
                getString(R.string.sel_count, a.selected.size)
        } else {
            selBar.visibility = View.GONE
            toolbar.visibility = View.VISIBLE
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val a = adapter
        if (a != null && a.selectionMode) {
            a.exitSelection()
        } else {
            super.onBackPressed()
        }
    }

    private fun confirmBatchDelete() {
        val a = adapter ?: return
        val ids = a.selected.toList()
        if (ids.isEmpty()) return
        val message = if (mode == "remote") {
            getString(R.string.confirm_delete_media, getString(R.string.sel_count, ids.size))
        } else {
            getString(R.string.confirm_delete_local_files, ids.size)
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.action_delete)
            .setMessage(message)
            .setPositiveButton(R.string.action_delete) { _, _ ->
                val items = currentItems().filter { ids.contains(it.id) }
                lifecycleScope.launch { runBatchDelete(items) }
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    private suspend fun runBatchDelete(items: List<MediaItem>) {
        busy = true
        setProgressVisible(true)
        var ok = 0
        var fail = 0
        val total = items.size
        items.forEachIndexed { i, item ->
            tvProgress().text = getString(R.string.batch_deleting, i + 1, total)
            val result = if (mode == "remote") {
                Remote.deleteMedia(item.id)
            } else {
                Result.runCatching { LocalStore.deleteLocalFile(item.albumId, item.fileName) }
            }
            if (result.isSuccess) ok++ else fail++
        }
        busy = false
        setProgressVisible(false)
        adapter?.exitSelection()
        toast(
            if (fail > 0) getString(R.string.toast_delete_done_fail, ok, fail)
            else getString(R.string.toast_delete_done, ok)
        )
        reload()
    }

    private fun startBatchDownload() {
        val a = adapter ?: return
        val items = currentItems().filter { a.selected.contains(it.id) }
        if (items.isEmpty()) return
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.action_download)
            .setMessage(getString(R.string.sel_count, items.size) + getString(R.string.dialog_download_note))
            .setPositiveButton(R.string.action_download) { _, _ ->
                lifecycleScope.launch { runBatchDownload(items) }
            }
            .setNegativeButton(R.string.action_cancel, null)
            .show()
    }

    private suspend fun runBatchDownload(items: List<MediaItem>) {
        busy = true
        setProgressVisible(true)
        val progress = findViewById<LinearProgressIndicator>(R.id.piProgress)
        var ok = 0
        var fail = 0
        val total = items.size
        items.forEachIndexed { i, item ->
            tvProgress().text = getString(R.string.batch_downloading_file, i + 1, total, item.fileName)
            progress.isIndeterminate = true
            val existing = LocalStore.localFilePath(item.albumId, item.fileName)
            val already = existing.exists() && item.sizeBytes > 0 && existing.length() == item.sizeBytes
            val r = if (already) {
                Result.success(existing)
            } else {
                Remote.downloadMedia(item, albumName) { done, all ->
                    if (all > 0) {
                        progress.isIndeterminate = false
                        progress.setProgressCompat((done * 100 / all).toInt(), true)
                    }
                }
            }
            if (r.isSuccess) ok++ else fail++
        }
        busy = false
        setProgressVisible(false)
        adapter?.exitSelection()
        toast(
            if (fail > 0) getString(R.string.toast_download_done_fail, ok, fail)
            else getString(R.string.toast_download_done, ok)
        )
        reload()
    }

    private fun setProgressVisible(show: Boolean) {
        findViewById<View>(R.id.llProgress).visibility = if (show) View.VISIBLE else View.GONE
    }

    private fun tvProgress(): TextView = findViewById(R.id.tvProgress)

    private fun currentItems(): List<MediaItem> = when {
        mode == "remote" -> adapter?.itemsSnapshot() ?: emptyList()
        else -> LocalStore.localMediaItems(albumId)
    }

    // ---------- 檢視 ----------

    private fun openViewer(item: MediaItem) {
        val i = android.content.Intent(this, ViewerActivity::class.java)
        i.putExtra("mode", mode)
        i.putExtra("albumId", albumId)
        i.putExtra("albumName", albumName)
        if (mode == "remote") {
            i.putExtra("mediaId", item.id)
            i.putExtra("type", item.type.name)
            i.putExtra("mime", item.mime)
            i.putExtra("fileName", item.fileName)
            i.putExtra("sizeBytes", item.sizeBytes)
            i.putExtra("createdAt", item.createdAt)
        } else {
            i.putExtra("path", LocalStore.localFilePath(albumId, item.fileName).absolutePath)
            i.putExtra("mime", item.mime)
            i.putExtra("fileName", item.fileName)
        }
        startActivity(i)
    }

    private fun toast(msg: String) {
        android.widget.Toast.makeText(this, msg, android.widget.Toast.LENGTH_SHORT).show()
    }
}
