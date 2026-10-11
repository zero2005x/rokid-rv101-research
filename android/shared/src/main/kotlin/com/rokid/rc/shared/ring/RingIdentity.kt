package com.rokid.rc.shared.ring

/**
 * **Single source of truth** for the ring identity match string.
 *
 * Background: the phone side (path A) and the glasses side (path B) each hardcoded `"RGR"` in 11 places,
 * and this string is **the only basis for selecting a device** (see `reports/ring_capture_security_audit_2026-09-13.md` §2 R6).
 * The scattered failure mode is **silent**: as soon as one place is out of sync, "the paired device" and "the device
 * actually listened to" diverge, leaving only a single log line.
 *
 * ⚠ **This is not a security boundary**: a BLE advertising name is declared by the peripheral itself, so any device
 * can call itself `RGR`. Real source binding needs "a persisted identity address after pairing + user confirmation",
 * which is **not implemented yet** (same report §3).
 */
object RingIdentity {

    /** Default advertising-name match string for official Rokid rings (RGR06 etc.). */
    const val DEFAULT_NAME_PATTERN = "RGR"

    /** Some rings only advertise `RING`; used as a degraded match only when [DEFAULT_NAME_PATTERN] finds nothing at all. */
    const val FALLBACK_NAME_PATTERN = "RING"

    /** Name match string for the glasses' built-in touchpad (`ROKID,PSOC-TP-R`); for debugging/alternative sources. */
    const val TOUCHPAD_NAME_PATTERN = "TP"

    /**
     * Whether the user explicitly specified a match string.
     *
     * `null`/an empty string means "the caller decides" — the phone side reads it as "take the connectable device
     * with the strongest RSSI", the glasses side as [DEFAULT_NAME_PATTERN]. This difference used to be expressed by
     * `?: "RGR"` scattered around the code.
     */
    fun orDefault(pattern: String?): String =
        if (pattern.isNullOrBlank()) DEFAULT_NAME_PATTERN else pattern
}
