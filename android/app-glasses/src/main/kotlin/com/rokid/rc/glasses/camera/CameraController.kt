package com.rokid.rc.glasses.camera

import android.content.Context
import android.os.SystemClock
import android.util.Log
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.video.FallbackStrategy
import androidx.camera.video.FileOutputOptions
import androidx.camera.video.PendingRecording
import androidx.camera.video.Quality
import androidx.camera.video.QualitySelector
import androidx.camera.video.Recorder
import androidx.camera.video.Recording
import androidx.camera.video.VideoCapture
import androidx.camera.video.VideoRecordEvent
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import com.rokid.rc.glasses.ghost.LedSuppressor
import com.rokid.rc.glasses.media.MediaRepository
import com.rokid.rc.shared.protocol.CameraErrors
import com.rokid.rc.shared.protocol.MediaItem
import com.rokid.rc.shared.protocol.MediaType
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.io.File
import java.util.concurrent.Executor
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

/**
 * Photo/video capture via CameraX.
 *
 * Rokid's Qualcomm CHI HAL is picky about multi-stream combinations, so this uses a "single use case":
 * photos bind only ImageCapture, and recording rebinds to VideoCapture.
 *
 * With ghost=true it calls [LedSuppressor] before and after capture to suppress/restore the camera privacy LED (stealth mode).
 */
class CameraController(
    private val context: Context,
    private val lifecycleOwner: LifecycleOwner,
    private val media: MediaRepository,
    private val led: LedSuppressor
) {

    private val mainExecutor: Executor = ContextCompat.getMainExecutor(context)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private var provider: ProcessCameraProvider? = null
    private var imageCapture: ImageCapture? = null
    private var recorder: Recorder? = null
    private var activeRecording: Recording? = null
    private var videoState: VideoState? = null
    private var finalizeCont: CancellableContinuation<MediaItem>? = null

    // ---- recording-start watchdog (P0-2) ----
    /** Continuation used to wait for `VideoRecordEvent.Start`; it is cancelled on timeout. */
    private var startCont: CancellableContinuation<Unit>? = null

    /** Whether this recording actually received `Start`. */
    @Volatile private var videoStarted = false

    /** Whether this recording was aborted by the watchdog (Finalize must discard the partial file and must not report success). */
    @Volatile private var videoAborted = false

    // ---- photo serialization and minimum interval (P0-3) ----
    /**
     * Only one photo at a time. Five back-to-back `CAPTURE_PHOTO` requests once pushed the glasses-side app's RSS to 100 MB
     * and got it killed by the lowmemorykiller (see reports/aivela_ring_connection_failure_2026-09-13.md).
     */
    private val photoLock = Mutex()

    /** Time of the last "photo started" (uptimeMillis). */
    @Volatile private var lastPhotoAtMs = 0L

    val isRecording: Boolean get() = activeRecording != null

    fun start(onReady: () -> Unit, onError: (String) -> Unit) {
        val future = ProcessCameraProvider.getInstance(context)
        future.addListener({
            try {
                provider = future.get()
                bindPhoto()
                onReady()
            } catch (e: Exception) {
                onError(e.message ?: "camera bind failed")
            }
        }, mainExecutor)
    }

    private fun bindPhoto() {
        val p = provider ?: return
        val ic = ImageCapture.Builder()
            .setCaptureMode(ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY)
            .build()
        p.unbindAll()
        p.bindToLifecycle(lifecycleOwner, pickSelector(p), ic)
        imageCapture = ic
        recorder = null
    }

    private fun bindVideo() {
        val p = provider ?: return
        val rec = Recorder.Builder()
            .setQualitySelector(
                QualitySelector.from(
                    Quality.HD,
                    FallbackStrategy.higherQualityOrLowerThan(Quality.SD)
                )
            )
            .build()
        val vc = VideoCapture.withOutput(rec)
        p.unbindAll()
        p.bindToLifecycle(lifecycleOwner, pickSelector(p), vc)
        recorder = rec
        imageCapture = null
    }

    fun shutdown() {
        try {
            activeRecording?.stop()
            provider?.unbindAll()
        } catch (e: Exception) {
            Log.e(TAG, "shutdown failed", e)
        }
        imageCapture = null
        recorder = null
        activeRecording = null
        provider = null
    }

    /**
     * Take a photo.
     *
     * Error codes (see [CameraErrors]):
     *  - [CameraErrors.BUSY_RECORDING]: **recording is in progress** (photo is unavailable under the single use case design)
     *  - [CameraErrors.NOT_READY]: the camera really is not ready yet
     *  - [CameraErrors.BUSY]: the previous photo is still running, or less than [MIN_PHOTO_INTERVAL_MS] has passed since it
     */
    suspend fun takePhoto(ghost: Boolean): Result<MediaItem> {
        // 1) distinguish "recording" from "not ready" first — the two used to share "camera not ready", which misled as a connection problem
        val ic = imageCapture ?: return Result.failure(
            IllegalStateException(
                if (isRecording) CameraErrors.BUSY_RECORDING else CameraErrors.NOT_READY
            )
        )
        // 2) serialize + minimum interval (a failed TryLock returns busy immediately instead of queueing, so requests cannot pile up into an OOM)
        if (!photoLock.tryLock()) return Result.failure(IllegalStateException(CameraErrors.BUSY))
        try {
            val sinceLast = SystemClock.uptimeMillis() - lastPhotoAtMs
            if (lastPhotoAtMs > 0L && sinceLast < MIN_PHOTO_INTERVAL_MS) {
                val wait = MIN_PHOTO_INTERVAL_MS - sinceLast
                Log.i(TAG, "拍照間隔 ${sinceLast}ms < ${MIN_PHOTO_INTERVAL_MS}ms → 等待 ${wait}ms")
                delay(wait)
            }
            lastPhotoAtMs = SystemClock.uptimeMillis()

            val albumId = media.getOrCreateDefaultAlbum()
            val file = media.newPhotoFile(albumId)
            if (ghost) led.suppress()
            try {
                val saved = suspendCancellableCoroutine<Boolean> { cont ->
                    val opts = ImageCapture.OutputFileOptions.Builder(file).build()
                    ic.takePicture(opts, mainExecutor, object : ImageCapture.OnImageSavedCallback {
                        override fun onImageSaved(output: ImageCapture.OutputFileResults) {
                            cont.resume(true)
                        }

                        override fun onError(exc: ImageCaptureException) {
                            cont.resumeWithException(exc)
                        }
                    })
                }
                return if (saved) {
                    Result.success(media.addMedia(albumId, MediaType.PHOTO, file))
                } else {
                    Result.failure(IllegalStateException("save failed"))
                }
            } catch (e: Exception) {
                file.delete()
                return Result.failure(e)
            } finally {
                if (ghost) led.restore()
            }
        } finally {
            photoLock.unlock()
        }
    }

    // RECORD_AUDIO is declared in the Manifest and requested by MainActivity
    @Suppress("MissingPermission")
    suspend fun startVideo(ghost: Boolean): Result<Unit> {
        if (activeRecording != null) return Result.failure(IllegalStateException("already recording"))
        // suppress before bind: bindVideo starts streaming -> lights_ctrl emits a camera-open event and turns the LED on,
        // so suppressing afterwards leaves an initial flicker gap (one of the root causes of recording flicker)
        if (ghost) led.suppress()
        withContext(Dispatchers.Main) { bindVideo() }
        val rec = recorder ?: return Result.failure(IllegalStateException("recorder not ready"))
        val albumId = media.getOrCreateDefaultAlbum()
        val file = media.newVideoFile(albumId)
        val pending: PendingRecording = rec.prepareRecording(
            context,
            FileOutputOptions.Builder(file).build()
        ).withAudioEnabled()
        videoStarted = false
        videoAborted = false
        val recording = pending.start(mainExecutor) { event -> onVideoEvent(event) }
        activeRecording = recording
        videoState = VideoState(albumId, file, ghost)

        // watchdog: wait for VideoRecordEvent.Start. Previously, if the Recorder got stuck on
        // "Cached audio data while we wait for video keyframe" it never returned to photo mode
        // (imageCapture had been cleared to null by bindVideo) -> every later photo failed.
        val started = withTimeoutOrNull(RECORD_START_TIMEOUT_MS) {
            suspendCancellableCoroutine<Unit> { c -> startCont = c }
        }
        if (started == null && !videoStarted) {
            Log.w(
                TAG,
                "startVideo 逾時 ${RECORD_START_TIMEOUT_MS}ms（等不到 VideoRecordEvent.Start）" +
                    " → 自動停止並切回拍照"
            )
            startCont = null
            videoAborted = true
            runCatching { recording.stop() }
            delay(RECORD_ABORT_SETTLE_MS)
            withContext(Dispatchers.Main) { runCatching { bindPhoto() } }
            return Result.failure(IllegalStateException(CameraErrors.RECORD_START_TIMEOUT))
        }
        return Result.success(Unit)
    }

    suspend fun stopVideo(): MediaItem = suspendCancellableCoroutine { cont ->
        val r = activeRecording
        if (r == null) {
            cont.resumeWithException(IllegalStateException("not recording"))
            return@suspendCancellableCoroutine
        }
        finalizeCont = cont
        r.stop()
    }

    private fun onVideoEvent(event: VideoRecordEvent) {
        if (event is VideoRecordEvent.Start) {
            videoStarted = true
            startCont?.let { c -> if (c.isActive) c.resume(Unit) }
            startCont = null
            return
        }
        if (event !is VideoRecordEvent.Finalize) return
        // if Finalize arrives before Start (encoder/permission error), wake startVideo first
        startCont?.let { c ->
            if (c.isActive) {
                c.resumeWithException(event.cause ?: IllegalStateException("video finalize before start"))
            }
        }
        startCont = null
        val st = videoState
        val aborted = videoAborted
        videoState = null
        activeRecording = null
        videoStarted = false
        videoAborted = false
        scope.launch {
            if (st?.ghost == true) led.restore()
            val cont = finalizeCont
            finalizeCont = null
            when {
                st == null -> cont?.resumeWithException(IllegalStateException("no video state"))
                // the watchdog aborted it: discard the partial file and never report success
                aborted -> {
                    runCatching { st.file.delete() }
                    cont?.resumeWithException(IllegalStateException(CameraErrors.RECORD_START_TIMEOUT))
                }
                event.hasError() -> {
                    st.file.delete()
                    cont?.resumeWithException(event.cause ?: RuntimeException("video error ${event.error}"))
                }
                else -> cont?.resume(media.addMedia(st.albumId, MediaType.VIDEO, st.file))
            }
            // switch back to photo mode once recording has ended
            withContext(Dispatchers.Main) { runCatching { bindPhoto() } }
        }
    }

    private fun pickSelector(provider: ProcessCameraProvider): CameraSelector {
        return try {
            if (provider.hasCamera(CameraSelector.DEFAULT_BACK_CAMERA)) {
                CameraSelector.DEFAULT_BACK_CAMERA
            } else {
                CameraSelector.DEFAULT_FRONT_CAMERA
            }
        } catch (e: Exception) {
            CameraSelector.DEFAULT_BACK_CAMERA
        }
    }

    private data class VideoState(val albumId: String, val file: File, val ghost: Boolean)

    companion object {
        private const val TAG = "CameraController"

        /** Minimum interval between two photos (P0-3). Five back-to-back requests once got the app killed by the LMK outright. */
        private const val MIN_PHOTO_INTERVAL_MS = 800L

        /** Recording-start watchdog (P0-2): give up when `VideoRecordEvent.Start` never arrives. */
        private const val RECORD_START_TIMEOUT_MS = 5_000L

        /** Grace period to wait for Finalize to clean up after aborting a recording. */
        private const val RECORD_ABORT_SETTLE_MS = 300L
    }
}
