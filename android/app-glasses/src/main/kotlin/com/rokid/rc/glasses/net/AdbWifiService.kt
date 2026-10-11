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
 * AdbWifiService — 在**不取得 root** 的前提下，把裝置的 adbd 接上 TCP。
 *
 * 原理：
 * ```
 *  開發者 PC  ──TCP:5555──▶  本服務（App 行程）  ──AF_UNIX──▶  /dev/socket/adbd
 *                                                                   │
 *                                                             原廠 adbd（金鑰認證、shell、sync、
 *                                                             forward 全由它處理）
 * ```
 * 本服務只是**位元搬運工**：不做任何 ADB 協定解析，因此 `adb connect` / `adb install` /
 * `logcat` / `push` 等全部與 USB 連線行為一致；認證仍由 adbd 以 RSA 金鑰把關
 * （`ro.adb.secure=1`），本服務不降低安全性。
 *
 * ⚠ 前提（實測決定成敗）：SELinux 必須允許 App domain 連 `adbd_socket`。
 *   若不允許，本服務會回報 `denied`，此時改用 root 版（`setprop persist.adb.tcp.port 5555`
 *   讓 adbd 自己聽 TCP，開發者端仍免 root）。判定結果見
 *   `reports/adb_app_socket_reach_2026-09-20.md`。
 *
 * 生命週期：前景服務（避免被 LMK 回收；本專案有 LMK 殺 App 的前例）。
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

    // ---- relay 本體 ----

    private fun startRelay(port: Int) {
        // 先確認 adbd socket 可連（早失敗、給出可行動的錯誤）
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

    /** 一條連線：雙向搬運直到任一邊結束。 */
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
            // 單邊結束 → 關閉對邊寫入，讓 adbd／client 看到 EOF（TIME_WAIT 由 TCP 處理）
            runCatching { to.close() }
            runCatching { from.close() }
        }
    }

    // ---- 對外狀態（給 App／手機端讀） ----

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

        /** 最近一次狀態（App 內查詢用；行程重啟即失效）。 */
        @Volatile var last: Status = Status(false, DEFAULT_PORT, null, null, 0, "never-started")
            private set

        /** 狀態變更廣播（供手機 App／PC 工具追蹤開發模式）。 */
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

        /** 對外入口 IP（STA wlan0 優先，SoftAP wlan2 次之）。 */
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

    // ---- 通知（前景服務必要） ----

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
