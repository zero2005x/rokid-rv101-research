package com.rokid.rc.glasses

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Intent
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.lifecycle.LifecycleService
import com.rokid.rc.glasses.camera.CameraController
import com.rokid.rc.glasses.net.BluetoothServer
import com.rokid.rc.glasses.net.WifiServer
import com.rokid.rc.shared.protocol.RootMode
import kotlinx.coroutines.launch

/**
 * 眼鏡端核心前台服務：同時擁有藍牙 RFCOMM server 與 CameraX 相機。
 * MainActivity 只是本機狀態/除錯介面，啟動本服務即開始對外服務。
 */
class CaptureService : LifecycleService() {

    private lateinit var server: BluetoothServer
    private lateinit var wifiServer: WifiServer
    private lateinit var camera: CameraController
    private var started = false
    private var wakeLock: PowerManager.WakeLock? = null
    private var screenLock: PowerManager.WakeLock? = null

    override fun onCreate() {
        super.onCreate()
        startForeground(NOTIF_ID, buildNotification())
        // 持有 wake lock：partial（CPU）+ screen bright（螢幕），
        // 防止 Rokid 激進休眠（screen_offTimeout=5）在跑 LPE 期間把服務/app 殺掉。
        val pm = getSystemService(POWER_SERVICE) as PowerManager
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "rokid:capture:cpu")
        wakeLock?.acquire()
        @Suppress("DEPRECATION")
        screenLock = pm.newWakeLock(
            PowerManager.SCREEN_BRIGHT_WAKE_LOCK or PowerManager.ACQUIRE_CAUSES_WAKEUP,
            "rokid:capture:screen"
        )
        screenLock?.acquire()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)
        if (!started) {
            started = true
            startComponents()
        }
        return START_STICKY
    }

    private fun startComponents() {
        val graph = (application as App).graph

        server = BluetoothServer(graph)
        server.start(
            onStarted = { Log.i(TAG, "Bluetooth RFCOMM server ready (${BluetoothServer.SERVICE_NAME})") },
            onError = { e -> Log.e(TAG, "bluetooth server error: $e") }
        )

        // WiFi 媒體通道（TCP 8555，經 SoftAP 192.168.43.1；控制指令仍走藍牙）
        wifiServer = WifiServer(graph)
        wifiServer.start(
            onStarted = { Log.i(TAG, "WiFi media server ready (:8555)") },
            onError = { e -> Log.e(TAG, "wifi server error: $e") }
        )

        camera = CameraController(this, this, graph.media, graph.led)
        graph.camera = camera
        camera.start(
            onReady = { Log.i(TAG, "camera ready") },
            onError = { e -> Log.e(TAG, "camera error: $e") }
        )

        // 路徑 B：戒指/觸控板控制（root 直讀 evdev）。getevent 需要 root，
        // 尚未提權時 start() 會失敗，看門狗會持續重試直到 root 就緒。
        graph.scope.launch {
            if (graph.ringControl.enabled) graph.ringControl.ensureRunning()
        }

        // root daemon 存活看守（P0）：無論 root 模式都啟動（MANUAL 模式只觀察與回報，不自動重建）
        graph.rootKeeper.start()

        // root 取得模式：AUTO = 開啟即自動跑一次 LPE
        /*
         * AUTO 提權同樣受安全武裝政策約束（開機迴圈斷路器／重試上限＋退避／僅充電時／開機後延遲）。
         * 不套用的話，App 每次被喚起都會重新賭一次 ~50% 的 kernel panic。
         */
        val armBlock = if (graph.rootMode == RootMode.AUTO) {
            com.rokid.rc.glasses.ghost.ArmPolicy.blockReason(this, graph.prefs)
        } else null
        if (graph.rootMode == RootMode.AUTO && armBlock != null) {
            Log.i(TAG, "AUTO 提權暫停：$armBlock（${com.rokid.rc.glasses.ghost.ArmPolicy.describe(this, graph.prefs)}）")
        } else if (graph.rootMode == RootMode.AUTO) {
            com.rokid.rc.glasses.ghost.ArmPolicy.onAutoArmAttempt(graph.prefs)
            graph.scope.launch {
                val result = graph.acquireRootMetered()
                Log.i(TAG, "auto acquireRoot: success=${result.success} stage=${result.stage}")
                // 提權成功後自動開 WiFi 熱點（媒體通道）；失敗不阻斷藍牙功能
                if (result.success || graph.ghostlock.rootAcquired) {
                    val apOk = graph.wifiHotspot.ensureStarted()
                    val silentOk = graph.silentMode.applyIfEnabled()
                    Log.i(TAG, "auto wifi ap: up=$apOk, silent capture: $silentOk")
                    // 路徑 B：戒指/觸控板控制（root 直讀 evdev）——提權成功後自動啟動；
                    // App 程序被系統重啟時也會跟著 CaptureService 一起恢復。
                    if (graph.ringControl.enabled && !graph.ringControl.isRunning()) {
                        val ringOk = graph.ringControl.start()
                        Log.i(TAG, "auto ring control: running=$ringOk device=${graph.ringControl.devicePath}")
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        if (::camera.isInitialized) camera.shutdown()
        if (::server.isInitialized) server.stop()
        if (::wifiServer.isInitialized) wifiServer.stop()
        (application as App).graph.camera = null
        wakeLock?.release()
        wakeLock = null
        screenLock?.release()
        screenLock = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent): IBinder? = super.onBind(intent)

    /**
     * 前景服務通知（Android 規定 FGS 必須有通知，無法完全移除）。
     *
     * ⚠ 2026-09-13：AR 眼鏡上任何「彈出式卡片」都會擋住視線，而通知卡在系統預設是
     * **圓角深灰底**。原本通道是 `IMPORTANCE_LOW`，但 OEM 對 FGS 通知仍可能顯示為浮動卡片。
     * 改為 **`IMPORTANCE_MIN`＋靜音**：只留在通知抽屜、**永不彈出**。
     * （通道 importance 建立後無法由程式調降，故改用新的 channel id `capture_quiet`。）
     */
    private fun buildNotification(): Notification {
        val channelId = "capture_quiet"
        val nm = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
        nm.createNotificationChannel(
            NotificationChannel(channelId, "CaptureService", NotificationManager.IMPORTANCE_MIN).apply {
                setShowBadge(false)
                enableVibration(false)
                setSound(null, null)
            }
        )
        return NotificationCompat.Builder(this, channelId)
            .setContentTitle("Rokid Capture")
            .setContentText("bluetooth capture service running")
            .setSmallIcon(R.drawable.ic_notification)
            .setPriority(NotificationCompat.PRIORITY_MIN)
            .setSilent(true)
            .build()
    }

    companion object {
        private const val TAG = "CaptureService"
        private const val NOTIF_ID = 1001
    }
}
