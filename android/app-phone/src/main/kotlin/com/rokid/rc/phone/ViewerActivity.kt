package com.rokid.rc.phone

import android.net.Uri
import com.rokid.rc.shared.protocol.MediaTypes
import android.os.Bundle
import android.view.View
import android.widget.MediaController
import android.widget.TextView
import android.widget.VideoView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.core.view.isVisible
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.CircularProgressIndicator
import com.google.android.material.progressindicator.LinearProgressIndicator
import com.rokid.rc.phone.util.Format
import com.rokid.rc.phone.util.LocalStore
import com.rokid.rc.phone.util.ThumbLoader
import com.rokid.rc.phone.widget.ZoomImageView
import com.rokid.rc.shared.protocol.MediaItem
import com.rokid.rc.shared.protocol.MediaType
import kotlinx.coroutines.launch
import java.io.File

/**
 * Full-screen viewer:
 * - Remote media: show the large thumbnail immediately first, then download the original in the background (progress bar)
 *   and replace it with the full image when done; a video plays automatically once downloaded.
 * - Local media: read the file directly.
 * Photos support pinch-to-zoom / double-tap to zoom; a single tap toggles the chrome.
 */
class ViewerActivity : AppCompatActivity() {

    private var chromeVisible = true
    private var jobDone = false
    private var retryRunnable: (() -> Unit)? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_viewer)

        val mode = intent.getStringExtra("mode") ?: "local"
        val fileName = intent.getStringExtra("fileName") ?: ""
        val mime = intent.getStringExtra("mime") ?: MediaTypes.MIME_JPEG

        val zoom = findViewById<ZoomImageView>(R.id.ivZoom)
        val video = findViewById<VideoView>(R.id.vvVideo)
        val top = findViewById<View>(R.id.llTop)
        val bottom = findViewById<View>(R.id.llBottom)
        val msg = findViewById<View>(R.id.llMsg)
        val tvMsg = findViewById<TextView>(R.id.tvMsg)
        val spinner = findViewById<CircularProgressIndicator>(R.id.piSpinner)
        val retry = findViewById<View>(R.id.btnRetry)
        val dl = findViewById<LinearProgressIndicator>(R.id.piDownload)
        val btnDelete = findViewById<View>(R.id.btnVDelete)

        findViewById<TextView>(R.id.tvVName).text = fileName
        findViewById<View>(R.id.btnVClose).setOnClickListener { finish() }

        zoom.onSingleTap = { toggleChrome() }

        val insets = WindowCompat.getInsetsController(window, zoom)
        hideSystemUi(insets)
        hideChrome(insets)

        val albumName = intent.getStringExtra("albumName") ?: ""
        val albumId = intent.getStringExtra("albumId") ?: ""

        fun showMsg(text: String, withRetry: Boolean, showSpinner: Boolean) {
            tvMsg.text = text
            retry.visibility = if (withRetry) View.VISIBLE else View.GONE
            spinner.visibility = if (showSpinner) View.VISIBLE else View.GONE
            msg.visibility = View.VISIBLE
            dl.isVisible = false
        }

        fun hideMsg() {
            msg.visibility = View.GONE
        }

        fun showImage(file: File) {
            zoom.visibility = View.VISIBLE
            video.visibility = View.GONE
            video.stopPlayback()
            lifecycleScope.launch {
                val bmp = ThumbLoader.local(file, 4096)
                if (bmp != null) zoom.setImageBitmap(bmp)
            }
        }

        fun playVideo(file: File) {
            zoom.visibility = View.INVISIBLE
            video.visibility = View.VISIBLE
            video.setVideoURI(Uri.fromFile(file))
            val mc = MediaController(this)
            mc.setAnchorView(video)
            video.setMediaController(mc)
            video.setOnPreparedListener { it.start() }
            video.setOnClickListener { toggleChrome() }
            video.start()
        }

        if (mode == "local") {
            val path = intent.getStringExtra("path")
            val f = path?.let { File(it) }
            if (f == null || !f.exists()) {
                showMsg(getString(R.string.err_local_file_missing), false, false)
                return
            }
            findViewById<TextView>(R.id.tvVInfo).text =
                "${Format.date(f.lastModified())} · ${Format.size(f.length())}"
            retryRunnable = null
            if (mime.startsWith("video")) playVideo(f) else showImage(f)
            btnDelete.visibility = View.GONE
            return
        }

        // ---------------- remote ----------------
        btnDelete.visibility = View.VISIBLE
        btnDelete.setOnClickListener {
            MaterialAlertDialogBuilder(this)
                .setTitle(R.string.viewer_delete_remote)
                .setMessage(getString(R.string.confirm_delete_media, fileName))
                .setPositiveButton(R.string.action_delete) { _, _ ->
                    lifecycleScope.launch {
                        Remote.deleteMedia(intent.getStringExtra("mediaId") ?: "").fold(
                            onSuccess = { finish() },
                            onFailure = { e -> toast(getString(R.string.toast_delete_fail, e.message ?: "")) }
                        )
                    }
                }
                .setNegativeButton(R.string.action_cancel, null)
                .show()
        }

        val mediaId = intent.getStringExtra("mediaId") ?: ""
        val sizeBytes = intent.getLongExtra("sizeBytes", 0L)
        val createdAt = intent.getLongExtra("createdAt", 0L)
        val type = if (intent.getStringExtra("type") == MediaType.VIDEO.name) MediaType.VIDEO else MediaType.PHOTO
        val item = MediaItem(
            id = mediaId, albumId = albumId, type = type, fileName = fileName, mime = mime,
            sizeBytes = sizeBytes, width = 0, height = 0, durationMs = 0L, createdAt = createdAt,
            thumbReady = false
        )
        val typeLabel = getString(Format.typeLabelRes(type))
        findViewById<TextView>(R.id.tvVInfo).text =
            "$typeLabel · ${Format.size(sizeBytes)} · ${Format.date(createdAt)}"

        val target = LocalStore.localFilePath(albumId, fileName)

        fun doDownload() {
            jobDone = false
            retry.visibility = View.GONE
            lifecycleScope.launch {
                if (target.exists() && target.length() == sizeBytes) {
                    jobDone = true
                    hideMsg()
                    if (type == MediaType.VIDEO) playVideo(target) else showImage(target)
                    return@launch
                }
                // show the large thumbnail immediately first (photos)
                if (type == MediaType.PHOTO) {
                    hideMsg()
                    zoom.visibility = View.VISIBLE
                    val bmp = ThumbLoader.remote(mediaId, 1920)
                    if (bmp != null && !jobDone) zoom.setImageBitmap(bmp)
                } else {
                    showMsg(getString(R.string.viewer_downloading), false, true)
                }
                dl.isVisible = true
                dl.setProgressCompat(0, false)
                Remote.downloadMedia(item, albumName) { done, all ->
                    if (all > 0) dl.setProgressCompat((done * 100 / all).toInt(), true)
                }.fold(
                    onSuccess = { f ->
                        jobDone = true
                        hideMsg()
                        dl.isVisible = false
                        if (type == MediaType.VIDEO) playVideo(f) else showImage(f)
                    },
                    onFailure = { e ->
                        jobDone = true
                        dl.isVisible = false
                        showMsg(getString(R.string.err_download_fail, e.message ?: ""), true, false)
                        retryRunnable = ::doDownload
                        retry.setOnClickListener { doDownload() }
                    }
                )
            }
        }

        doDownload()
    }

    private fun hideSystemUi(insets: WindowInsetsControllerCompat) {
        insets.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        insets.hide(WindowInsetsCompat.Type.systemBars())
    }

    private fun showSystemUi(insets: WindowInsetsControllerCompat) {
        insets.show(WindowInsetsCompat.Type.systemBars())
    }

    private fun toggleChrome() {
        chromeVisible = !chromeVisible
        if (chromeVisible) {
            findViewById<View>(R.id.llTop).visibility = View.VISIBLE
            findViewById<View>(R.id.llBottom).visibility = View.VISIBLE
            showSystemUi(WindowCompat.getInsetsController(window, window.decorView))
        } else {
            findViewById<View>(R.id.llTop).visibility = View.GONE
            findViewById<View>(R.id.llBottom).visibility = View.GONE
            hideSystemUi(WindowCompat.getInsetsController(window, window.decorView))
        }
    }

    private fun hideChrome(insets: WindowInsetsControllerCompat) {
        chromeVisible = false
        findViewById<View>(R.id.llTop).visibility = View.GONE
        findViewById<View>(R.id.llBottom).visibility = View.GONE
        hideSystemUi(insets)
    }

    private fun toast(msg: String) {
        android.widget.Toast.makeText(this, msg, android.widget.Toast.LENGTH_SHORT).show()
    }
}
