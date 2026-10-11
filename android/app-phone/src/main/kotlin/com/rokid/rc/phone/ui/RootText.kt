package com.rokid.rc.phone.ui

import android.content.Context
import com.rokid.rc.phone.R
import com.rokid.rc.shared.protocol.RootStates
import com.rokid.rc.shared.protocol.Status

/**
 * root state and escalation metrics → text in the phone's locale (P0-b/P2).
 *
 * The glasses send only status codes (`RootStates`) and hints (script names), and the phone composes the text,
 * so the two sides' languages stay independent.
 */
object RootText {

    /** Status row text, for example "root: available" / "root: lost, needs re-arming". */
    fun state(ctx: Context, state: String?): String = when (state) {
        RootStates.OK -> ctx.getString(R.string.root_state_ok)
        RootStates.REARMING -> ctx.getString(R.string.root_state_rearming)
        RootStates.REARMED -> ctx.getString(R.string.root_state_rearmed)
        RootStates.LOST_NEED_ARM -> ctx.getString(R.string.root_state_lost)
        RootStates.LOST_COOLDOWN -> ctx.getString(R.string.root_state_cooldown)
        else -> ctx.getString(R.string.root_state_unknown)
    }

    /** Operating hint when operator intervention is needed (including the script to run). */
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
     * Metrics row, for example "last 84 s (bridge) · 3/5 succeeded".
     * null when there is no data (the caller does not display it).
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

    /** Watchdog statistics (P0), for example "12 watchdog runs · 1 auto-rebuild". */
    fun keeper(ctx: Context, s: Status): String? {
        if (s.suKeeperChecks <= 0) return null
        return ctx.getString(R.string.root_keeper_fmt, s.suKeeperChecks, s.suKeeperRearms)
    }
}
