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
 * 以 CameraX 進行拍照/錄影。
 *
 * Rokid 的 Qualcomm CHI HAL 對多流組合挑剔，故採「單一 use case」：
 * 拍照只綁 ImageCapture、錄影時 rebind 為 VideoCapture。
 *
 * ghost=true 時，在拍攝前後呼叫 [LedSuppressor] 壓制/還原相機隱私 LED（隱蔽模式）。
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

    // ---- 錄影啟動看門狗（P0-2）----
    /** 等 `VideoRecordEvent.Start` 用的 continuation；逾時會被取消。 */
    private var startCont: CancellableContinuation<Unit>? = null

    /** 本輪錄影是否真的收到 `Start`。 */
    @Volatile private var videoStarted = false

    /** 本輪錄影是否被看門狗中止（Finalize 時要丟棄殘檔、不可回報成功）。 */
    @Volatile private var videoAborted = false

    // ---- 拍照序列化與最小間隔（P0-3）----
    /**
     * 同一時間只允許一筆拍照。連續 5 筆 `CAPTURE_PHOTO` 曾讓眼鏡端 App 的 RSS 衝到 100 MB
     * 而被 lowmemorykiller 殺掉（見 reports/aivela_ring_connection_failure_2026-09-13.md）。
     */
    private val photoLock = Mutex()

    /** 上一次「開始拍照」的時間（uptimeMillis）。 */
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
     * 拍照。
     *
     * 錯誤碼（見 [CameraErrors]）：
     *  - [CameraErrors.BUSY_RECORDING]：**正在錄影**（單一 use case 設計下拍照不可用）
     *  - [CameraErrors.NOT_READY]：相機真的還沒就緒
     *  - [CameraErrors.BUSY]：前一筆拍照還在跑，或距上次不足 [MIN_PHOTO_INTERVAL_MS]
     */
    suspend fun takePhoto(ghost: Boolean): Result<MediaItem> {
        // 1) 先分辨「錄影中」與「未就緒」——這兩者過去共用 "camera not ready"，會誤導成連線問題
        val ic = imageCapture ?: return Result.failure(
            IllegalStateException(
                if (isRecording) CameraErrors.BUSY_RECORDING else CameraErrors.NOT_READY
            )
        )
        // 2) 序列化 + 最小間隔（TryLock 失敗＝立刻回 busy，不排隊，避免請求堆積造成 OOM）
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

    // RECORD_AUDIO 已於 Manifest 宣告並由 MainActivity 請求
    @Suppress("MissingPermission")
    suspend fun startVideo(ghost: Boolean): Result<Unit> {
        if (activeRecording != null) return Result.failure(IllegalStateException("already recording"))
        // 先壓制再 bind：bindVideo 會啟動 streaming → lights_ctrl 發 camera-open 事件點燈，
        // 若之後才 suppress 會有初始閃一下的間隙（錄影閃爍根因之一）
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

        // 看門狗：等 VideoRecordEvent.Start。過去若 Recorder 卡在
        // "Cached audio data while we wait for video keyframe" 就永遠回不到拍照模式
        // （imageCapture 已被 bindVideo 清成 null）→ 之後每一筆拍照都失敗。
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
        // 若在 Start 之前就 Finalize（編碼器/權限錯誤），先把 startVideo 叫醒
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
                // 看門狗已中止：丟棄殘檔，不可對外回報成功
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
            // 錄影結束後切回拍照模式
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

        /** 兩次拍照之間的最小間隔（P0-3）。連續 5 筆無間隔請求曾直接把 App 打到被 LMK 殺掉。 */
        private const val MIN_PHOTO_INTERVAL_MS = 800L

        /** 錄影啟動看門狗（P0-2）：等不到 `VideoRecordEvent.Start` 就放棄。 */
        private const val RECORD_START_TIMEOUT_MS = 5_000L

        /** 中止錄影後，等 Finalize 回收的寬限時間。 */
        private const val RECORD_ABORT_SETTLE_MS = 300L
    }
}
