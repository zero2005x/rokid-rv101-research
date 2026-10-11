package com.rokid.rc.shared.protocol

/**
 * Camera-side error codes (glasses → phone **machine-readable** reasons).
 *
 * These strings reach the phone through `CommandRouter.ackError()` or the ring's `RingOutcome(..., arg)`,
 * and the phone composes the text in **its own locale** (see `errorDetail()` in phone `ui/RingText.kt`).
 *
 * ⚠ Changing this is changing the protocol: both ends must be updated together.
 *
 * Background (2026-09-13): every failure used to return `"camera not ready"`, so "**currently recording**" and
 * "**service not up**" looked identical to the user, misreading a camera state problem as "ring connection failure".
 * See `reports/aivela_ring_connection_failure_2026-09-13.md`.
 */
object CameraErrors {

    /** Camera not ready yet (service just started, bind failed, Activity not in the foreground…). */
    const val NOT_READY = "camera_not_ready"

    /** **Currently recording**: this project uses a single use case (photo and video are mutually exclusive), so a photo cannot be taken while recording. */
    const val BUSY_RECORDING = "busy_recording"

    /** The previous photo is still in progress, or less than the minimum interval has passed since the last one (burst protection). */
    const val BUSY = "busy"

    /** Video recording start timed out (no `VideoRecordEvent.Start`) → already stopped automatically and switched back to photo mode. */
    const val RECORD_START_TIMEOUT = "record_start_timeout"
}
