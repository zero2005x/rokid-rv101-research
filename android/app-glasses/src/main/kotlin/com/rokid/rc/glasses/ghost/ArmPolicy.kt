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
 * Escalation "safe arming" policy (2026-09-13).
 *
 * ## Why it is needed
 * GhostLock (CVE-2026-43499) is a stack UAF in rt_mutex / futex-PI: **roughly 50% of attempts end in a kernel
 * panic, and the device reboots by itself after 2-5 minutes** (stated in this project's own document
 * `ZeroLightRokidDesktop/docs/research-update.md`; 21/143 in this workspace's statistics).
 * The kernel command line also carries `kernel.panic_on_rcu_stall=1`, so any stall escalates straight to a panic.
 *
 * The original behavior was "auto-escalate on every boot + retry without limit every 5 minutes", which amounts to
 * gambling on that probability over and over; consecutive failures turn into a **boot loop**. See
 * `reports/glasses_reboot_after_adb_unplug_2026-09-13.md`.
 *
 * ## Four protections (controlled by `BuildConfig.SAFE_ARMING`, enabled by default)
 * 1. **Boot-loop breaker**: detect "rebooted shortly after boot"; two in a row stop auto-arming
 * 2. **Per-boot retry cap + exponential backoff**: at most [MAX_ARMS_PER_BOOT] times, at 5 → 15 → 60 minute intervals
 * 3. **Auto-arm only while charging**: do not re-run the exploit automatically while unplugged (on battery)
 * 4. **Delay after boot**: do nothing until uptime reaches [BOOT_SETTLE_MS]
 *
 * With `SAFE_ARMING=false` this object allows everything and the behavior matches the old version (for A/B comparison).
 *
 * ⚠ This does **not affect manual escalation**: it still runs when the user taps "acquire root" in the app or when
 * triggered by `deploy/root_bridge.sh` (both are deliberate decisions). This policy constrains **automatic** arming only.
 */
object ArmPolicy {

    private const val TAG = "ArmPolicy"

    // ---- prefs keys ----
    private const val K_BOOT_WALL = "arm_boot_wall_ms"        // wall-clock time of this boot (currentTimeMillis - elapsedRealtime)
    private const val K_LAST_ARM_WALL = "arm_last_wall_ms"    // wall-clock time of the last "auto-arm"
    private const val K_ARMS_THIS_BOOT = "arm_arms_this_boot" // number of auto-arms already performed this boot
    private const val K_SUSPICIOUS_BOOTS = "arm_suspicious_boots"
    private const val K_SUSPENDED = "arm_suspended"           // breaker tripped
    private const val K_SUSPEND_REASON = "arm_suspend_reason"

    /** How long after boot auto-arming becomes allowed (the project's own recommendation: `sys.boot_completed=1` and uptime > 150 seconds). */
    const val BOOT_SETTLE_MS = 150_000L

    /** Maximum number of auto-arms per boot. */
    const val MAX_ARMS_PER_BOOT = 2

    /** Exponential backoff intervals (between the 1st and 2nd, between the 2nd and 3rd, …). */
    private val BACKOFF_MS = longArrayOf(5 * 60_000L, 15 * 60_000L, 60 * 60_000L)

    /** "Boot time − last auto-arm time" below this value ⇒ treated as a suspicious panic reboot. */
    private const val PANIC_WINDOW_MS = 10 * 60_000L

    /** How many consecutive suspicious reboots trip the breaker. */
    private const val SUSPICIOUS_LIMIT = 2

    // ---------------- external queries ----------------

    /** Whether auto-arming is currently disabled by the breaker. */
    fun suspended(prefs: SharedPreferences): Boolean =
        BuildConfig.SAFE_ARMING && prefs.getBoolean(K_SUSPENDED, false)

    /** The disable reason (for logging/UI display). */
    fun suspendReason(prefs: SharedPreferences): String? =
        prefs.getString(K_SUSPEND_REASON, null)

    /** Number of auto-arms already performed this boot. */
    fun armsThisBoot(prefs: SharedPreferences): Int = prefs.getInt(K_ARMS_THIS_BOOT, 0)

    /**
     * Whether auto-arming is **allowed** right now. null = allowed; non-null = reason code (English, for logs and UI composition).
     *
     * It also refreshes the boot state once (detects a reboot, updates the breaker counter).
     */
    fun blockReason(context: Context, prefs: SharedPreferences): String? {
        if (!BuildConfig.SAFE_ARMING) return null
        refreshBootState(prefs)

        if (prefs.getBoolean(K_SUSPENDED, false)) return "breaker-open"
        if (SystemClock.elapsedRealtime() < BOOT_SETTLE_MS) return "boot-settle"
        if (armsThisBoot(prefs) >= MAX_ARMS_PER_BOOT) return "max-arms-this-boot"
        if (!isCharging(context)) return "on-battery"

        // exponential backoff
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
     * Detect "is this a new boot", and judge whether the previous boot **rebooted shortly after an auto-arm**.
     *
     * Boot identity uses `currentTimeMillis() - elapsedRealtime()`: this value equals "the wall-clock time at boot",
     * stays fixed for the rest of the boot and changes on reboot — no permission needed, and no reading of `/proc`.
     */
    fun refreshBootState(prefs: SharedPreferences) {
        if (!BuildConfig.SAFE_ARMING) return
        val bootWall = System.currentTimeMillis() - SystemClock.elapsedRealtime()
        val prevBootWall = prefs.getLong(K_BOOT_WALL, 0L)
        if (prevBootWall == bootWall) return // same boot

        val lastArm = prefs.getLong(K_LAST_ARM_WALL, 0L)
        val edit = prefs.edit().putLong(K_BOOT_WALL, bootWall).putInt(K_ARMS_THIS_BOOT, 0)

        // did the previous boot "reboot within 10 minutes of arming"? If so, add to the suspicious count
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

    /** Record "about to auto-arm" (called before the exploit is actually triggered). */
    fun onAutoArmAttempt(prefs: SharedPreferences) {
        if (!BuildConfig.SAFE_ARMING) return
        val n = armsThisBoot(prefs) + 1
        prefs.edit()
            .putInt(K_ARMS_THIS_BOOT, n)
            .putLong(K_LAST_ARM_WALL, System.currentTimeMillis())
            .apply()
        Log.i(TAG, "自動武裝第 $n/$MAX_ARMS_PER_BOOT 次（本次開機）")
    }

    /** Manually reset the policy (explicitly requested by the user in the app / through the debug broadcast). */
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

    /** Current status summary (for logging/debugging). */
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
            // show the reason only when it really tripped, so a leftover string does not mislead
            if (sus) suspendReason(prefs)?.let { append(" reason=").append(it) }
        }
    }

    // ---------------- internals ----------------

    /** Whether external power (USB/AC) is connected. Reads the sticky broadcast, needs no permission. */
    fun isCharging(context: Context): Boolean = runCatching {
        val i: Intent? = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val plugged = i?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0
        plugged != 0
    }.getOrDefault(false)
}
