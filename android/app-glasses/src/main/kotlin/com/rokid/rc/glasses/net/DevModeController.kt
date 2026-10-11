package com.rokid.rc.glasses.net

import android.content.Context
import android.util.Log
import com.rokid.rc.glasses.ghost.RootDaemonClient
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject

/**
 * DevModeController — the "dev mode" state machine: **an on-demand switch for wireless adb**.
 *
 * ## Mechanism (settled after real-device testing on 2026-09-20)
 *
 * ```
 *  ON  : setprop persist.adb.tcp.port 5555 + setprop ctl.restart adbd
 *         → adbd immediately listens on [::]:5555 (**no reboot needed**, and USB enumeration is unaffected)
 *  OFF : setprop persist.adb.tcp.port ""   + setprop ctl.restart adbd
 *         → the listener disappears immediately (likewise with no reboot)
 * ```
 * Both steps need **root** (only root/system can write `persist.*` and `ctl.*`).
 * Without root this controller can still control "whether to join the subnet", but it **cannot** open or close the
 * listener, and it reports that explicitly as `listenerManaged=false`, so the user is not misled into thinking it is off.
 *
 * ## Why not a userland relay (TCP → /dev/socket/adbd)
 * Measured (2026-09-20):
 *  · `/dev/socket/adbd` is `srw-rw---- system:system` → a non-system identity is rejected by DAC (EACCES).
 *  · Connecting with `SOCK_STREAM` yields **EPROTOTYPE(91)**; `SOCK_SEQPACKET` does connect but is **immediately EOF**
 *    (adbd closes at once) → it cannot forward. That socket is not a usable relay backend.
 *  · So "a relay inside the app" is a dead end; **switching the native listener** is the reliable path.
 *
 * ## States
 * ```
 *  OFF ──start()──▶ JOINING ──(listener OK + got an IP)──▶ ACTIVE ──(idle timeout / stop())──▶ OFF
 *                     └──────────(failure / timeout)──────────▶ OFF (and report the reason)
 * ```
 * ⚠ Side effect (measured): `ctl.restart adbd` also kills the root daemon started through adb shell
 *   (keepalive is its child process). So after OFF, turning ON again requires re-arming escalation.
 *   This is inherent to the way the vendor starts adbd on this device, not a bug in this controller.
 */
class DevModeController(private val ctx: Context) {

    enum class State { OFF, JOINING, ACTIVE, REVERTING, ERROR }

    data class Status(
        val state: State,
        val ssid: String?,
        val ip: String?,
        val listenerTcp: Boolean,
        val listenerManaged: Boolean,
        val method: WifiJoin.Method,
        val message: String,
        val since: Long
    ) {
        fun toJson(): JSONObject = JSONObject()
            .put("state", state.name)
            .put("ssid", ssid ?: JSONObject.NULL)
            .put("ip", ip ?: JSONObject.NULL)
            .put("listenerTcp", listenerTcp)
            .put("listenerManaged", listenerManaged)
            .put("method", method.name)
            .put("message", message)
            .put("since", since)
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val wifiJoin = WifiJoin(ctx)
    private var job: Job? = null

    @Volatile
    var status: Status = Status(State.OFF, null, null, false, false, WifiJoin.Method.NONE, "idle", 0L)
        private set

    private fun set(
        state: State,
        msg: String,
        ip: String? = status.ip,
        ssid: String? = status.ssid,
        method: WifiJoin.Method = status.method
    ) {
        status = Status(state, ssid, ip, adbListenerPresent(), status.listenerManaged, method, msg, System.currentTimeMillis())
        Log.i(TAG, "state=$state msg=$msg ip=${ip ?: "-"} listener=${status.listenerTcp} managed=${status.listenerManaged}")
        AdbWifiService.notifyStatus(ctx, status)
    }

    /** Whether adbd is listening on TCP (IPv4 or IPv6; on this device it is the IPv6 wildcard `[::]:5555`). */
    private fun adbListenerPresent(): Boolean = try {
        val p = ProcessBuilder("sh", "-c",
            "grep -q ':15B3 ' /proc/net/tcp && echo v4; grep -q ':15B3 ' /proc/net/tcp6 && echo v6")
            .redirectErrorStream(true).start()
        val out = p.inputStream.bufferedReader().readText()
        p.waitFor()
        out.contains("v4") || out.contains("v6")
    } catch (e: Exception) {
        false
    }

    /** Poll until the listener reaches the expected state (restarting adbd takes a few seconds). */
    private suspend fun awaitListener(want: Boolean, timeoutMs: Long): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (adbListenerPresent() == want) return true
            delay(700)
        }
        return adbListenerPresent() == want
    }

    fun statusJson(): JSONObject = status.toJson()

    /**
     * Turn dev mode on.
     * @param ssid target subnet (null = use the saved credential; if there is none either, just open the listener without joining a subnet)
     * @param idleTimeoutMs how long to stay idle after entering ACTIVE before switching off automatically (0 = never)
     * @param timeoutMs upper bound on waiting to join the subnet
     */
    fun start(
        ssid: String?,
        pass: String?,
        security: String = "wpa2",
        idleTimeoutMs: Long = DEFAULT_IDLE_MS,
        timeoutMs: Long = DEFAULT_JOIN_MS
    ) {
        val cred = when {
            !ssid.isNullOrBlank() -> WifiJoin.Cred(ssid, pass ?: "", security).also { wifiJoin.saveCred(it) }
            else -> wifiJoin.loadCred()
        }
        job?.cancel()
        job = scope.launch {
            val root = RootDaemonClient()
            val isRoot = root.isRoot()
            status = status.copy(listenerManaged = isRoot)
            try {
                // 1) open the listener (needs root; without root only a pre-existing listener can be used)
                if (isRoot) {
                    set(State.JOINING, "開啟 adb TCP listener …", ssid = cred?.ssid)
                    val out = root.exec(CMD_ON, 30_000L)
                    Log.i(TAG, "ON → ${out.trim().take(200)}")
                    // poll until the listener appears (restarting adbd takes a few seconds; a fixed delay once caused a misreport)
                    if (!awaitListener(true, 20_000L)) {
                        set(State.ERROR, "已下 ON 指令但 listener 未出現：${out.trim().take(120)}")
                        return@launch
                    }
                } else if (!adbListenerPresent()) {
                    set(State.ERROR, "無 root 且 adbd 目前未監聽 TCP → 無法開啟（需提權一次）")
                    return@launch
                }

                // 2) join the subnet (optional)
                if (cred != null) {
                    set(State.JOINING, "加入 ${cred.ssid} …", ssid = cred.ssid)
                    val (ok, msg, method) = wifiJoin.join(cred, preferRoot = isRoot, timeoutMs = timeoutMs)
                    if (!ok) {
                        set(State.REVERTING, "加入失敗：$msg", method = method)
                        if (isRoot) root.exec(CMD_OFF, 30_000L)
                        set(State.OFF, "已回退（加入網段失敗）", ip = null, method = method)
                        return@launch
                    }
                    set(State.ACTIVE, "開發模式已開啟", ip = wifiJoin.ipv4(), method = method)
                } else {
                    set(State.ACTIVE, "listener 已開啟（未指定網段）", ip = wifiJoin.ipv4())
                }

                // 3) auto-off on idle timeout (any adb connection renews it)
                if (idleTimeoutMs > 0) {
                    var waited = 0L
                    while (waited < idleTimeoutMs) {
                        delay(5_000); waited += 5_000
                        if (AdbWifiService.last.openConnections > 0) waited = 0
                    }
                    set(State.REVERTING, "閒置逾時（${idleTimeoutMs / 60_000} 分）→ 自動關閉")
                    stopInternal("idle-timeout")
                }
            } catch (e: Exception) {
                Log.e(TAG, "start failed: ${e.message}")
                set(State.ERROR, "例外：${e.message}")
                runCatching { RootDaemonClient().exec(CMD_OFF, 20_000L) }
                set(State.OFF, "已回退（例外）", ip = null)
            }
        }
    }

    fun stop() {
        job?.cancel()
        scope.launch { stopInternal("user-stop") }
    }

    private suspend fun stopInternal(why: String) {
        val root = RootDaemonClient()
        if (root.isRoot()) {
            set(State.REVERTING, "關閉 adb TCP listener（$why）")
            val out = root.exec(CMD_OFF, 30_000L)
            Log.i(TAG, "OFF → ${out.trim().take(200)}")
            val gone = awaitListener(false, 20_000L)
            set(
                State.OFF,
                if (gone) "已關閉（$why）" else "persist 已清除，但 listener 仍在（adbd 未重啟？）— $why",
                ip = null, method = WifiJoin.Method.NONE
            )
        } else {
            set(State.OFF, "已停止開發模式（無 root，無法關閉 listener）", ip = null)
        }
    }

    companion object {
        const val TAG = "DevMode"
        const val DEFAULT_IDLE_MS = 10 * 60_000L
        const val DEFAULT_JOIN_MS = 45_000L

        /** Open the listener: persist (survives reboots) + takes effect immediately (no reboot, no USB drop). */
        private const val CMD_ON =
            "setprop persist.adb.tcp.port 5555; setprop ctl.restart adbd; sleep 3; " +
                "echo persist=$(getprop persist.adb.tcp.port) listener=$(grep -c ':15B3 ' /proc/net/tcp6)"

        /** Close the listener: clear persist + take effect immediately. */
        private const val CMD_OFF =
            "setprop persist.adb.tcp.port ''; setprop ctl.restart adbd; sleep 3; " +
                "echo persist=[$(getprop persist.adb.tcp.port)] listener=$(grep -c ':15B3 ' /proc/net/tcp6)"
    }
}
