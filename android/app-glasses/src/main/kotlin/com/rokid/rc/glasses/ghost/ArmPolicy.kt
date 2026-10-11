package com.rokid.rc.glasses.ghost

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.os.BatteryManager
import android.os.SystemClock
import android.util.Log
import com.rokid.rc.glasses.BuildConfig

/**
 * 提權「安全武裝」政策（2026-09-13）。
 *
 * ## 為什麼需要
 * GhostLock（CVE-2026-43499）是 rt_mutex / futex-PI 的堆疊 UAF：**每次嘗試約 50% 會以
 * kernel panic 收場，裝置在 2–5 分鐘後自動重開機**（本專案自身文件
 * `ZeroLightRokidDesktop/docs/research-update.md` 明載；本 workspace 統計 21/143）。
 * 核心命令列又有 `kernel.panic_on_rcu_stall=1`，任何卡死都會直接升級成 panic。
 *
 * 原本的行為是「每次開機自動提權 ＋ 每 5 分鐘無限重試」，等於反覆去賭那個機率，
 * 一旦連續失敗就會變成**開機迴圈**。見 `reports/glasses_reboot_after_adb_unplug_2026-09-13.md`。
 *
 * ## 四道保護（由 `BuildConfig.SAFE_ARMING` 控制，預設開啟）
 * 1. **開機迴圈斷路器**：偵測「開機後不久就重開」，連續 2 次即停止自動武裝
 * 2. **每次開機重試上限 ＋ 指數退避**：上限 [MAX_ARMS_PER_BOOT] 次，間隔 5 → 15 → 60 分鐘
 * 3. **僅充電時自動武裝**：拔線（吃電池）時不自動重跑 exploit
 * 4. **開機後延遲**：uptime 未滿 [BOOT_SETTLE_MS] 前不動作
 *
 * `SAFE_ARMING=false` 時本物件全部放行，行為與舊版一致（供 A/B 對照）。
 *
 * ⚠ 這裡**不影響手動提權**：使用者在 App 按「取得 root」或由 `deploy/root_bridge.sh`
 * 觸發時仍會執行（那是有意識的決定）。本政策只約束**自動**武裝。
 */
object ArmPolicy {

    private const val TAG = "ArmPolicy"

    // ---- prefs keys ----
    private const val K_BOOT_WALL = "arm_boot_wall_ms"        // 本次開機的牆鐘時刻（currentTimeMillis - elapsedRealtime）
    private const val K_LAST_ARM_WALL = "arm_last_wall_ms"    // 最後一次「自動武裝」的牆鐘時刻
    private const val K_ARMS_THIS_BOOT = "arm_arms_this_boot" // 本次開機已自動武裝次數
    private const val K_SUSPICIOUS_BOOTS = "arm_suspicious_boots"
    private const val K_SUSPENDED = "arm_suspended"           // 斷路器已跳脫
    private const val K_SUSPEND_REASON = "arm_suspend_reason"

    /** 開機後多久才允許自動武裝（專案自身建議：`sys.boot_completed=1` 且 uptime > 150 秒）。 */
    const val BOOT_SETTLE_MS = 150_000L

    /** 每次開機最多自動武裝幾次。 */
    const val MAX_ARMS_PER_BOOT = 2

    /** 指數退避間隔（第 1→2 次之間、第 2→3 次之間…）。 */
    private val BACKOFF_MS = longArrayOf(5 * 60_000L, 15 * 60_000L, 60 * 60_000L)

    /** 「開機時刻 − 最後一次自動武裝時刻」小於此值 ⇒ 視為可疑的 panic 重開。 */
    private const val PANIC_WINDOW_MS = 10 * 60_000L

    /** 連續幾次可疑重開就跳脫斷路器。 */
    private const val SUSPICIOUS_LIMIT = 2

    // ---------------- 對外查詢 ----------------

    /** 目前是否由斷路器停用自動武裝。 */
    fun suspended(prefs: SharedPreferences): Boolean =
        BuildConfig.SAFE_ARMING && prefs.getBoolean(K_SUSPENDED, false)

    /** 停用原因（給日誌／UI 顯示）。 */
    fun suspendReason(prefs: SharedPreferences): String? =
        prefs.getString(K_SUSPEND_REASON, null)

    /** 本次開機已自動武裝次數。 */
    fun armsThisBoot(prefs: SharedPreferences): Int = prefs.getInt(K_ARMS_THIS_BOOT, 0)

    /**
     * 現在是否**允許自動武裝**。回傳 null＝允許；非 null＝原因代碼（英文，供日誌與 UI 組字）。
     *
     * 會順便做一次「開機狀態刷新」（偵測重開、更新斷路器計數）。
     */
    fun blockReason(context: Context, prefs: SharedPreferences): String? {
        if (!BuildConfig.SAFE_ARMING) return null
        refreshBootState(prefs)

        if (prefs.getBoolean(K_SUSPENDED, false)) return "breaker-open"
        if (SystemClock.elapsedRealtime() < BOOT_SETTLE_MS) return "boot-settle"
        if (armsThisBoot(prefs) >= MAX_ARMS_PER_BOOT) return "max-arms-this-boot"
        if (!isCharging(context)) return "on-battery"

        // 指數退避
        val last = prefs.getLong(K_LAST_ARM_WALL, 0L)
        if (last > 0L) {
            val n = armsThisBoot(prefs).coerceIn(0, BACKOFF_MS.size - 1)
            val wait = BACKOFF_MS[n]
            val elapsed = System.currentTimeMillis() - last
            if (elapsed in 0 until wait) return "backoff-${wait / 60_000}m"
        }
        return null
    }

    /**
     * 偵測「這次是不是新的開機」，並判斷上一次開機是否**在自動武裝後不久就重開**。
     *
     * 開機識別用 `currentTimeMillis() - elapsedRealtime()`：這個值等於「開機當時的牆鐘時間」，
     * 開機後固定不變，重開機就會改變 —— 不需要任何權限，也不需要讀 `/proc`。
     */
    fun refreshBootState(prefs: SharedPreferences) {
        if (!BuildConfig.SAFE_ARMING) return
        val bootWall = System.currentTimeMillis() - SystemClock.elapsedRealtime()
        val prevBootWall = prefs.getLong(K_BOOT_WALL, 0L)
        if (prevBootWall == bootWall) return // 同一次開機

        val lastArm = prefs.getLong(K_LAST_ARM_WALL, 0L)
        val edit = prefs.edit().putLong(K_BOOT_WALL, bootWall).putInt(K_ARMS_THIS_BOOT, 0)

        // 上一次開機是否「武裝後 10 分鐘內就重開」？是的話累計可疑次數
        var suspicious = prefs.getInt(K_SUSPICIOUS_BOOTS, 0)
        val sinceArm = bootWall - lastArm
        if (lastArm > 0L && sinceArm in 0..PANIC_WINDOW_MS) {
            suspicious++
            Log.w(
                TAG,
                "可疑重開：上次自動武裝後 ${sinceArm / 1000}s 就重開" +
                    "（累計 $suspicious/$SUSPICIOUS_LIMIT）"
            )
        } else {
            if (suspicious != 0) Log.i(TAG, "本次開機非武裝後短時間重開 → 可疑計數歸零")
            suspicious = 0
        }
        edit.putInt(K_SUSPICIOUS_BOOTS, suspicious)

        if (suspicious >= SUSPICIOUS_LIMIT && !prefs.getBoolean(K_SUSPENDED, false)) {
            edit.putBoolean(K_SUSPENDED, true)
                .putString(K_SUSPEND_REASON, "reboot-loop")
            Log.e(
                TAG,
                "⛔ 開機迴圈斷路器跳脫：連續 $suspicious 次在自動武裝後短時間內重開 → " +
                    "停用自動武裝（需手動提權或重置政策）"
            )
        }
        edit.apply()
    }

    /** 記錄「即將自動武裝」（在真正觸發 exploit 之前呼叫）。 */
    fun onAutoArmAttempt(prefs: SharedPreferences) {
        if (!BuildConfig.SAFE_ARMING) return
        val n = armsThisBoot(prefs) + 1
        prefs.edit()
            .putInt(K_ARMS_THIS_BOOT, n)
            .putLong(K_LAST_ARM_WALL, System.currentTimeMillis())
            .apply()
        Log.i(TAG, "自動武裝第 $n/$MAX_ARMS_PER_BOOT 次（本次開機）")
    }

    /** 手動重置政策（使用者在 App／除錯廣播中明確要求）。 */
    fun reset(prefs: SharedPreferences) {
        prefs.edit()
            .putBoolean(K_SUSPENDED, false)
            .remove(K_SUSPEND_REASON)
            .putInt(K_SUSPICIOUS_BOOTS, 0)
            .putInt(K_ARMS_THIS_BOOT, 0)
            .putLong(K_LAST_ARM_WALL, 0L)
            .apply()
        Log.i(TAG, "政策已重置：允許自動武裝，計數歸零")
    }

    /** 目前狀態摘要（日誌／除錯用）。 */
    fun describe(context: Context, prefs: SharedPreferences): String {
        if (!BuildConfig.SAFE_ARMING) return "safeArming=off（原行為）"
        refreshBootState(prefs)
        return buildString {
            append("safeArming=on")
            append(" up=").append(SystemClock.elapsedRealtime() / 1000).append('s')
            append(" charging=").append(isCharging(context))
            append(" arms=").append(armsThisBoot(prefs)).append('/').append(MAX_ARMS_PER_BOOT)
            append(" suspicious=").append(prefs.getInt(K_SUSPICIOUS_BOOTS, 0))
            val sus = prefs.getBoolean(K_SUSPENDED, false)
            append(" suspended=").append(sus)
            // 只在真的跳脫時顯示原因，避免殘留字串誤導
            if (sus) suspendReason(prefs)?.let { append(" reason=").append(it) }
        }
    }

    // ---------------- 內部 ----------------

    /** 是否接著外部電源（USB／AC）。讀 sticky broadcast，不需要權限。 */
    fun isCharging(context: Context): Boolean = runCatching {
        val i: Intent? = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val plugged = i?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0
        plugged != 0
    }.getOrDefault(false)
}
