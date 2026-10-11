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
 * DevModeController — 「開發模式」狀態機：**無線 adb 的按需開關**。
 *
 * ## 機制（2026-09-20 實測後定案）
 *
 * ```
 *  ON  ：setprop persist.adb.tcp.port 5555 + setprop ctl.restart adbd
 *         → adbd 立即在 [::]:5555 監聽（**不需重開機**，且 USB 列舉不受影響）
 *  OFF ：setprop persist.adb.tcp.port ""   + setprop ctl.restart adbd
 *         → listener 立即消失（同樣不需重開機）
 * ```
 * 這兩步都需要 **root**（`persist.*` 與 `ctl.*` 只有 root/system 能寫）。
 * 無 root 時本控制器仍能控制「要不要加入網段」，但**無法**開啟或關閉 listener，
 * 會以 `listenerManaged=false` 明確回報，避免使用者誤以為已經關掉了。
 *
 * ## 為什麼不用 userland relay（TCP → /dev/socket/adbd）
 * 實測（2026-09-20）：
 *  · `/dev/socket/adbd` 是 `srw-rw---- system:system` → 非 system 身分被 DAC 拒絕（EACCES）。
 *  · 用 `SOCK_STREAM` 連會得到 **EPROTOTYPE(91)**；`SOCK_SEQPACKET` 雖 connect 成功，
 *    但**立即 EOF**（adbd 立刻關閉）→ 無法轉發。該 socket 不是可用的 relay 後端。
 *  · 因此「App 內建 relay」路不通；**原生 listener 切換**才是可靠路徑。
 *
 * ## 狀態
 * ```
 *  OFF ──start()──▶ JOINING ──(listener OK + 取得 IP)──▶ ACTIVE ──(idle 逾時／stop())──▶ OFF
 *                     └──────────(失敗／逾時)──────────▶ OFF（並回報原因）
 * ```
 * ⚠ 副作用（已實測）：`ctl.restart adbd` 會連帶殺掉以 adb shell 啟動的 root daemon
 *   （keepalive 是其子行程）。因此 OFF 之後若要再 ON，需重新武裝提權。
 *   這是本機 adbd 由 vendor 方式啟動的固有行為，不是本控制器的 bug。
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

    /** adbd 是否正在聽 TCP（IPv4 或 IPv6；本機為 IPv6 wildcard `[::]:5555`）。 */
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

    /** 輪詢等待 listener 達到期望狀態（adbd 重啟需數秒）。 */
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
     * 開啟開發模式。
     * @param ssid 目標網段（null = 用已存憑證；都沒有則只開 listener 不加入網段）
     * @param idleTimeoutMs 進入 ACTIVE 後閒置多久自動關（0 = 不自動關）
     * @param timeoutMs 加入網段的等待上限
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
                // ① 開 listener（需 root；無 root 時只能靠已存在的 listener）
                if (isRoot) {
                    set(State.JOINING, "開啟 adb TCP listener …", ssid = cred?.ssid)
                    val out = root.exec(CMD_ON, 30_000L)
                    Log.i(TAG, "ON → ${out.trim().take(200)}")
                    // 輪詢等待 listener 出現（adbd 重啟需要數秒；固定 delay 曾造成誤判）
                    if (!awaitListener(true, 20_000L)) {
                        set(State.ERROR, "已下 ON 指令但 listener 未出現：${out.trim().take(120)}")
                        return@launch
                    }
                } else if (!adbListenerPresent()) {
                    set(State.ERROR, "無 root 且 adbd 目前未監聽 TCP → 無法開啟（需提權一次）")
                    return@launch
                }

                // ② 加入網段（可選）
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

                // ③ 閒置逾時自動關（有 adb 連線即續期）
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

        /** 開啟 listener：persist（跨開機）＋ 立即生效（不重開機、不掉 USB）。 */
        private const val CMD_ON =
            "setprop persist.adb.tcp.port 5555; setprop ctl.restart adbd; sleep 3; " +
                "echo persist=$(getprop persist.adb.tcp.port) listener=$(grep -c ':15B3 ' /proc/net/tcp6)"

        /** 關閉 listener：清 persist ＋ 立即生效。 */
        private const val CMD_OFF =
            "setprop persist.adb.tcp.port ''; setprop ctl.restart adbd; sleep 3; " +
                "echo persist=[$(getprop persist.adb.tcp.port)] listener=$(grep -c ':15B3 ' /proc/net/tcp6)"
    }
}
