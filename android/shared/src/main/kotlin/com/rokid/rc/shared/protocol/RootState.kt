package com.rokid.rc.shared.protocol

/**
 * root acquisition status codes (structured; the glasses send only codes and the phone composes the text in **its own locale**).
 *
 * Purpose (P0-b failure visualization): let the user see at a glance "can root be used right now" and
 * **which script to run next**, instead of only seeing `root required?` in a log.
 */
object RootStates {
    const val UNKNOWN = "root_unknown"
    /** root available (the daemon is alive). */
    const val OK = "root_ok"
    /** Failure detected, auto-rebuild in progress. */
    const val REARMING = "root_rearming"
    /** Auto-rebuild succeeded. */
    const val REARMED = "root_rearmed"
    /** Failed and **nobody can help trigger it**: the operator must run a script (detail = the suggested script). */
    const val LOST_NEED_ARM = "root_lost_need_arm"
    /** Failed but still in the cooldown window (avoid hammering the exploit). */
    const val LOST_COOLDOWN = "root_lost_cooldown"
}

/**
 * Escalation metrics (P2): latest and cumulative success/failure/duration, reported to the phone with `STATUS`.
 *
 * @param lastEngine   engine used most recently (`shizuku`/`bridge`/`direct`/`none`)
 * @param lastOk       whether the latest attempt succeeded
 * @param lastMs       duration of the latest attempt (milliseconds)
 * @param attempts     cumulative attempts
 * @param successes    cumulative successes
 * @param firstTryOk   of those, how many "succeeded on the first attempt"
 * @param keeperChecks daemon watchdog checks (P0)
 * @param keeperRearms daemon auto-rebuilds (P0)
 */
data class RootMetrics(
    val lastEngine: String = "none",
    val lastOk: Boolean = false,
    val lastMs: Long = 0L,
    val attempts: Int = 0,
    val successes: Int = 0,
    val firstTryOk: Int = 0,
    val keeperChecks: Int = 0,
    val keeperRearms: Int = 0
)
