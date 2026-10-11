package com.rokid.rc.glasses.net

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.Build
import android.os.IBinder
import android.util.Log
import java.io.InputStream
import java.io.OutputStream
import java.net.Inet4Address
import java.net.NetworkInterface
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.atomic.AtomicInteger

/**
 * AdbWifiService — puts the device's adbd onto TCP **without acquiring root**.
 *
 * Principle:
 * ```
 *  developer PC  ──TCP:5555──▶  this service (app process)  ──AF_UNIX──▶  /dev/socket/adbd
 *                                                                   │
 *                                                             stock adbd (key authentication, shell, sync,
 *                                                             forward are all handled by it)
 * ```
 * This service is only a **byte mover**: it does no ADB protocol parsing at all, so `adb connect` / `adb install` /
 * `logcat` / `push` and the rest behave exactly as they do over USB; authentication is still enforced by adbd with
 * RSA keys (`ro.adb.secure=1`), so this service does not weaken security.
 *
 * ⚠ Precondition (measured, and it decides success or failure): SELinux must allow the app domain to connect to
 *   `adbd_socket`. If it does not, this service reports `denied`, and the root version is used instead
 *   (`setprop persist.adb.tcp.port 5555` makes adbd listen on TCP itself, still needing no root on the developer
 *   side). See `reports/adb_app_socket_reach_2026-09-20.md` for the verdict.
 *
 * Lifecycle: a foreground service (to avoid being reclaimed by the LMK; this project has seen the LMK kill the app).
 */
class AdbWifiService : Service() {

    private var server: ServerSocket? = null
    private var worker: Thread? = null
    @Volatile private var running = false
    private val connSeq = AtomicInteger(0)
    private val openConns = AtomicInteger(0)

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        startForeground(NOTIF_ID, buildNotification("無線 ADB：啟動中…"))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopRelay("client-request")
                stopSelf()
                return START_NOT_STICKY
            }
        }
        val port = intent?.getIntExtra(EXTRA_PORT, DEFAULT_PORT) ?: DEFAULT_PORT
        if (!running) startRelay(port)
        return START_STICKY
    }

    override fun onDestroy() {
        stopRelay("service-destroy")
        super.onDestroy()
    }

    // ---- the relay itself ----

    private fun startRelay(port: Int) {
        // first confirm the adbd socket is connectable (fail early, with an actionable error)
        val probe = try {
            LocalSocket().apply {
                connect(LocalSocketAddress(SOCKET_PATH, LocalSocketAddress.Namespace.FILESYSTEM))
            }.also { runCatching { it.close() } }
            null
        } catch (e: Exception) {
            e
        }
        if (probe != null) {
            val msg = "adbd socket 不可連：${probe.javaClass.simpleName}: ${probe.message}"
            Log.e(TAG, msg)
            Companion.last = Status(false, port, null, msg, 0)
            stopSelf()
            return
        }

        val ss = try {
            ServerSocket(port).apply { reuseAddress = true }
        } catch (e: Exception) {
            Log.e(TAG, "bind :$port 失敗: ${e.message}")
            Companion.last = Status(false, port, null, "bind failed: ${e.message}", 0)
            stopSelf()
            return
        }
        server = ss
        running = true
        Companion.last = Status(true, port, localIp(), null, 0)

        worker = Thread {
            Log.i(TAG, "relay listening on :$port → $SOCKET_PATH")
            while (running) {
                val client = try {
                    ss.accept()
                } catch (e: Exception) {
                    if (running) Log.w(TAG, "accept: ${e.message}")
                    break
                }
                val id = connSeq.incrementAndGet()
                Thread { bridge(client, id) }.apply { isDaemon = true; name = "adbrelay-$id" }.start()
            }
        }.apply { isDaemon = true; name = "adbrelay-accept"; start() }

        updateNotification("無線 ADB：${localIp() ?: "?"}:$port")
    }

    private fun stopRelay(why: String) {
        running = false
        runCatching { server?.close() }
        server = null
        worker = null
        Companion.last = Companion.last.copy(running = false, note = "stopped: $why")
        Log.i(TAG, "relay stopped ($why)")
    }

    /** One connection: move bytes in both directions until either side ends. */
    private fun bridge(client: Socket, id: Int) {
        val peer = client.inetAddress?.hostAddress ?: "?"
        var up: LocalSocket? = null
        try {
            client.tcpNoDelay = true
            up = LocalSocket().apply {
                connect(LocalSocketAddress(SOCKET_PATH, LocalSocketAddress.Namespace.FILESYSTEM))
            }
            openConns.incrementAndGet()
            Companion.last = Companion.last.copy(openConnections = openConns.get())
            Log.i(TAG, "#$id connected from $peer")

            val a = Thread { pipe(client.getInputStream(), up.getOutputStream(), "#$id c→adbd") }
            val b = Thread { pipe(up.inputStream, client.getOutputStream(), "#$id adbd→c") }
            a.isDaemon = true; b.isDaemon = true
            a.start(); b.start()
            a.join(); b.join()
            Log.i(TAG, "#$id closed")
        } catch (e: Exception) {
            Log.w(TAG, "#$id error: ${e.message}")
        } finally {
            runCatching { up?.close() }
            runCatching { client.close() }
            openConns.decrementAndGet()
            Companion.last = Companion.last.copy(openConnections = openConns.get())
        }
    }

    private fun pipe(from: InputStream, to: OutputStream, tag: String) {
        val buf = ByteArray(64 * 1024)
        try {
            while (true) {
                val n = from.read(buf)
                if (n <= 0) break
                to.write(buf, 0, n)
                to.flush()
            }
        } catch (e: Exception) {
            Log.d(TAG, "$tag pipe end: ${e.message}")
        } finally {
            // one side ended → close the other side's write end so adbd/client see EOF (TIME_WAIT is handled by TCP)
            runCatching { to.close() }
            runCatching { from.close() }
        }
    }

    // ---- external state (read by the app/phone) ----

    data class Status(
        val running: Boolean,
        val port: Int,
        val ip: String?,
        val error: String?,
        val openConnections: Int,
        val note: String? = null
    )

    companion object {
        const val TAG = "AdbWifi"
        const val SOCKET_PATH = "/dev/socket/adbd"
        const val DEFAULT_PORT = 5555
        const val ACTION_START = "com.rokid.rc.glasses.ADB_WIFI_START"
        const val ACTION_STOP = "com.rokid.rc.glasses.ADB_WIFI_STOP"
        const val EXTRA_PORT = "port"
        private const val NOTIF_ID = 4711
        private const val CHANNEL = "adb_wifi"

        /** Latest status (queried inside the app; lost when the process restarts). */
        @Volatile var last: Status = Status(false, DEFAULT_PORT, null, null, 0, "never-started")
            private set

        /** State-change broadcast (lets the phone app/PC tools track dev mode). */
        const val ACTION_STATE = "com.rokid.rc.glasses.ADB_WIFI_STATE"

        fun notifyStatus(ctx: Context, dev: DevModeController.Status) {
            runCatching {
                ctx.sendBroadcast(Intent(ACTION_STATE).setPackage(ctx.packageName).apply {
                    putExtra("state", dev.state.name)
                    putExtra("ssid", dev.ssid)
                    putExtra("ip", dev.ip)
                    putExtra("listenerTcp", dev.listenerTcp)
                    putExtra("message", dev.message)
                })
            }
        }

        fun start(ctx: Context, port: Int = DEFAULT_PORT) {
            val i = Intent(ctx, AdbWifiService::class.java).setAction(ACTION_START).putExtra(EXTRA_PORT, port)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) ctx.startForegroundService(i)
            else ctx.startService(i)
        }

        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, AdbWifiService::class.java).setAction(ACTION_STOP))
        }

        /** External entry IP (STA wlan0 first, SoftAP wlan2 second). */
        fun localIp(): String? {        return try {
            var ap: String? = null
            val nifs = NetworkInterface.getNetworkInterfaces()
            while (nifs.hasMoreElements()) {
                val nif = nifs.nextElement()
                if (!nif.isUp || nif.isLoopback) continue
                val addrs = nif.inetAddresses
                while (addrs.hasMoreElements()) {
                    val a = addrs.nextElement()
                    if (a is Inet4Address && !a.isLoopbackAddress) {
                        val h = a.hostAddress ?: continue
                        if (nif.name == "wlan0") return h
                        if (nif.name == "wlan2" && ap == null) ap = h
                    }
                }
            }
            ap
        } catch (e: Exception) {
            null
        }
        }
    }

    // ---- notification (required by a foreground service) ----

    private fun buildNotification(text: String): Notification {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL, "無線 ADB", NotificationManager.IMPORTANCE_LOW)
            )
        }
        val b = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            Notification.Builder(this, CHANNEL) else @Suppress("DEPRECATION") Notification.Builder(this)
        return b.setContentTitle("無線 ADB")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        runCatching {
            val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            nm.notify(NOTIF_ID, buildNotification(text))
        }
    }
}
