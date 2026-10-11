package com.rokid.rc.phone.ui

import android.content.Context
import com.rokid.rc.phone.R
import com.rokid.rc.shared.protocol.RootStates
import com.rokid.rc.shared.protocol.Status

/**
 * root 狀態與提權度量 → 手機語系文字（P0-b／P2）。
 *
 * 眼鏡端只送狀態代碼（`RootStates`）與提示（腳本名稱），由手機組字，
 * 因此兩端語言可各自獨立。
 */
object RootText {

    /** 狀態列文字，例如「root：可用」／「root：已失效，需重新武裝」。 */
    fun state(ctx: Context, state: String?): String = when (state) {
        RootStates.OK -> ctx.getString(R.string.root_state_ok)
        RootStates.REARMING -> ctx.getString(R.string.root_state_rearming)
        RootStates.REARMED -> ctx.getString(R.string.root_state_rearmed)
        RootStates.LOST_NEED_ARM -> ctx.getString(R.string.root_state_lost)
        RootStates.LOST_COOLDOWN -> ctx.getString(R.string.root_state_cooldown)
        else -> ctx.getString(R.string.root_state_unknown)
    }

    /** 需要操作者介入時的操作提示（含要跑的腳本）。 */
    fun hint(ctx: Context, state: String?, hint: String?): String? {
        if (hint.isNullOrBlank()) return null
        val scripts = hint.split("|").filter { it.isNotBlank() }
        return when {
            state == RootStates.LOST_NEED_ARM && scripts.isNotEmpty() ->
                ctx.getString(R.string.root_hint_run_scripts, scripts.joinToString("  或  "))
            scripts.size == 1 -> ctx.getString(R.string.root_hint_engine, scripts[0])
            else -> null
        }
    }

    /**
     * 度量列，例如「上次 84 秒（bridge）· 成功 3/5」。
     * 沒有資料時回 null（呼叫端不顯示）。
     */
    fun metrics(ctx: Context, s: Status): String? {
        if (s.suAttempts <= 0) return null
        return ctx.getString(
            R.string.root_metrics_fmt,
            s.suLastMs / 1000,
            s.suEngine ?: "?",
            s.suSuccesses,
            s.suAttempts
        )
    }

    /** 看守統計（P0），例如「看守 12 次 · 自動重建 1 次」。 */
    fun keeper(ctx: Context, s: Status): String? {
        if (s.suKeeperChecks <= 0) return null
        return ctx.getString(R.string.root_keeper_fmt, s.suKeeperChecks, s.suKeeperRearms)
    }
}
