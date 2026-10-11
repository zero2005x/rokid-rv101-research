package com.rokid.rc.glasses.net

import android.util.Log
import com.rokid.rc.glasses.ghost.RootDaemonClient
import com.rokid.rc.shared.protocol.Protocol
import com.rokid.rc.shared.protocol.WifiApInfo

/**
 * 眼鏡端 WiFi 熱點（SoftAP）管理。
 *
 * 拓撲（使用者選定「root 輔助」）：提權成功後由 root_daemon 執行
 * `cmd wifi start-softap <ssid> wpa2 <pass>`，眼鏡以固定 IP 192.168.43.1 服務，
 * 手機加入該 AP 後以 TCP 連 192.168.43.1:8555 傳輸媒體。
 *
 * 觸發通道：RootDaemonClient（root，提權後）。實測 adb shell(uid 2000) 也可執行
 * start-softap，但 App(untrusted_app) 直接呼叫 cmd wifi 會被權限拒絕，故一律走
 * root socket（與專案「root 每 boot 一次」的生命週期一致）。
 *
 * SoftAP 不跨 reboot；重啟後需再次提權 + ensureStarted()（CaptureService 會在
 * rootMode=AUTO 提權成功後自動呼叫）。
 */
class WifiHotspot {

    private val ssid = Protocol.AP_SSID
    private val pass = Protocol.AP_PASS

    /** 查詢目前 AP 狀態（Java NetworkInterface 讀 wlan2，無 shell 依賴、無須 root）。 */
    fun status(): WifiApInfo {
        val up = try {
            val nif = java.net.NetworkInterface.getByName("wlan2")
            nif?.inetAddresses?.asSequence()?.any { it.hostAddress?.startsWith("192.168.43.") == true } == true
        } catch (e: Exception) {
            false
        }
        return WifiApInfo(up = up, ssid = ssid, pass = pass, ip = Protocol.AP_IP, port = Protocol.WIFI_PORT)
    }

    /**
     * 觸發 SoftAP 啟動（冪等）並短暫等待後回報目前狀態。
     * 注意：本裝置 AP 完整起來可能需 5~90 秒（與 STA 狀態有關），此處不做長等待；
     * `cmd wifi start-softap` 一旦送出，系統會在背景把 AP 帶起來，手機端採輪詢
     * ENSURE_WIFI_AP 直到 up=true。
     */
    suspend fun ensureStarted(): Boolean {
        val client = RootDaemonClient()
        if (!client.isRoot()) {
            Log.w(TAG, "root daemon 未就緒，無法啟動 SoftAP")
            return false
        }
        try {
            // 背景化：start-softap 在 wifi 服務轉換期可能長時間阻塞，不能同步等待
            client.exec("cmd wifi start-softap $ssid wpa2 $pass >/dev/null 2>&1 &")
            repeat(8) {
                if (status().up) return true
                kotlinx.coroutines.delay(1000)
            }
            return status().up
        } catch (e: Exception) {
            Log.e(TAG, "ensureStarted failed: ${e.message}")
            return false
        }
    }

    companion object {
        private const val TAG = "WifiHotspot"
    }
}
