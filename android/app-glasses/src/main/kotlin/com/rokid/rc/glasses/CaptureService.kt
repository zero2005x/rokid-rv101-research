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
 * Glasses-side core foreground service: owns both the Bluetooth RFCOMM server and the CameraX camera.
 * MainActivity is only the local status/debug interface; starting this service starts serving the outside world.
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
        // hold a wake lock: partial (CPU) + screen bright (screen),
        // to stop Rokid's aggressive sleep (screen_offTimeout=5) from killing the service/app while the LPE runs.
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

        // WiFi media channel (TCP 8555, through the SoftAP 192.168.43.1; control commands still go over Bluetooth)
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

        // Path B: ring/touchpad control (root reads evdev directly). getevent needs root,
        // so start() fails before escalation and the watchdog keeps retrying until root is ready.
        graph.scope.launch {
            if (graph.ringControl.enabled) graph.ringControl.ensureRunning()
        }

        // root daemon liveness watchdog (P0): started in either root mode (in MANUAL mode it only observes and reports, it does not auto-rebuild)
        graph.rootKeeper.start()

        // root acquisition mode: AUTO = run the LPE once automatically on start
        /*
         * AUTO escalation is likewise bound by the safe arming policy (boot-loop breaker / retry cap + backoff / charging only / delay after boot).
         * Without it, every time the app is brought up it would gamble again on that ~50% kernel panic.
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
                // after a successful escalation, start the WiFi hotspot automatically (media channel); a failure does not break the Bluetooth functionality
                if (result.success || graph.ghostlock.rootAcquired) {
                    val apOk = graph.wifiHotspot.ensureStarted()
                    val silentOk = graph.silentMode.applyIfEnabled()
                    Log.i(TAG, "auto wifi ap: up=$apOk, silent capture: $silentOk")
                    // Path B: ring/touchpad control (root reads evdev directly) — started automatically after a successful escalation;
                    // it also comes back together with CaptureService when the app process is restarted by the system.
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
     * Foreground service notification (Android requires an FGS to have one; it cannot be removed entirely).
     *
     * ⚠ 2026-09-13: on AR glasses any "pop-up card" blocks the view, and notification cards are **dark grey with
     * rounded corners** by default in the system. The channel used to be `IMPORTANCE_LOW`, but an OEM may still show
     * FGS notifications as floating cards. Changed to **`IMPORTANCE_MIN` + silent**: it stays in the notification
     * drawer and **never pops up**. (A channel's importance cannot be lowered programmatically after creation, hence
     * the new channel id `capture_quiet`.)
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
