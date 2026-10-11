package com.rokid.rc.glasses.ghost

import android.util.Log
import com.rokid.rc.glasses.AppGraph
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.io.File

/**
 * root daemon liveness watchdog (**P0**, app side).
 *
 * Complements the **root-side keep-alive loop** installed by the payload:
 * | Scenario | Handled by |
 * |------|----------|
 * | daemon crashes / is reclaimed by lmkd, but **root is still alive** | root-side keep-alive (one round every 20 s, rebuilds directly, needs no adb and no exploit re-run) |
 * | root is gone entirely (reboot, keeper also cleaned up) | this watchdog: clear the stale socket → check whether **anyone can still help escalate** → rebuild automatically or report which script to run |
 *
 * Design tradeoffs:
 *  - **Do not blindly re-run the exploit**: auto-rebuild only when (a) the bridge helper is resident or (b) Shizuku is available;
 *    when neither holds, only report status (avoid burning runs with no trigger pipeline in place, or even a panic reboot).
 *  - **Rate limit**: keep two auto-rebuilds at least [REARM_COOLDOWN_MS] apart, to avoid repeated triggering in a short window.
 */
class RootDaemonKeeper(private val graph: AppGraph) {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null

    /** Current status code (structured; the phone side renders it in its own locale, see shared `RootStates`). */
    @Volatile
    var state: String = com.rokid.rc.shared.protocol.RootStates.UNKNOWN
        private set

    /** Extra information (for example the script name to run, the engine name). */
    @Volatile
    var detail: String? = null
        private set

    @Volatile
    private var rearming = false
    private var lastRearmAtMs = 0L

    /**
     * Watchdog runs / auto-rebuild count (for P2 metrics).
     * **Persisted in prefs**: Rokid kills the app often, so keeping it in memory alone would reset it every time.
     */
    private val prefs = graph.prefs

    @Volatile
    var checks: Int = graph.prefs.getInt("keeper_checks", 0)
        private set

    @Volatile
    var rearmAttempts: Int = graph.prefs.getInt("keeper_rearms_try", 0)
        private set

    @Volatile
    var rearmSuccesses: Int = graph.prefs.getInt("keeper_rearms_ok", 0)
        private set

    private fun persistCounters() {
        prefs.edit()
            .putInt("keeper_checks", checks)
            .putInt("keeper_rearms_try", rearmAttempts)
            .putInt("keeper_rearms_ok", rearmSuccesses)
            .apply()
    }

    fun start() {
        if (job?.isActive == true) return
        job = scope.launch {
            // After startup, wait one round first so as not to collide with CaptureService's AUTO escalation
            delay(15_000)
            while (isActive) {
                runCatching { check() }.onFailure { Log.w(TAG, "check failed: ${it.message}") }
                delay(INTERVAL_MS)
            }
        }
        Log.i(TAG, "keeper started (interval=${INTERVAL_MS}ms)")
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    /**
     * When an external operator holds the lease, return its owner name; otherwise null.
     *
     * The exploit itself (ghostlock_rv101, .ghostlock_root.sh, gl_run.log) cannot be split in two
     * the way the helper and the mailbox can, so the three parties serialize through a lease file at a fixed path.
     * An external operator keeps renewing it while it works (desktop tool polls every 30 s, deploy `lease.sh` renews in the background),
     * so a fresh ts seen here means it really is running.
     *
     * The decision stays **deny-by-default** — only `owner=app` (ourselves) does not count as external;
     * any unknown owner makes us back off. **When ts is fresh but the owner cannot be parsed, conservatively treat it as external**,
     * matching the original `mine = txt.contains("owner=app")` behavior.
     *
     * Returning the name (rather than just a Boolean) lets [detail] show the **actual** holder:
     * the string used to be hardcoded to `"desktop"`, but since 2026-09-13 the deploy script (`deploy/lease.sh`)
     * also holds the lease, and hardcoding made the phone and the log point at the wrong party.
     */
    private fun externalLeaseOwner(): String? {
        return try {
            val f = File(LEASE_PATH)
            if (!f.exists()) return null
            val txt = f.readText()
            val ts = Regex("ts=(\\d+)").find(txt)?.groupValues?.get(1)?.toLongOrNull()
                ?: return null
            val ageSec = System.currentTimeMillis() / 1000 - ts
            if (ageSec !in 0..LEASE_TTL_SEC) return null          // idle / expired
            val owner = Regex("owner=([A-Za-z][A-Za-z]*)").find(txt)?.groupValues?.get(1)
            if (owner == "app") return null                       // held by ourselves
            owner ?: "unknown"                                    // cannot parse → conservatively treat as external
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Clear out leaked exploit processes after a timeout.
     *
     * When the exploit fails it leaks a large number of ghostlock_rv101 (measured 492 → 516, system load 347,
     * adb commands started timing out). **Must take PIDs with ps and kill them one by one**: a `pkill -f` pattern
     * would match the shell running it and kill itself (exit code 137).
     *
     * Limitation: the app can only kill its own processes, and these are owned by shell or root, so they can only be
     * cleaned while root is still available; when they cannot be cleaned, it is left to the desktop tool (adb shell has the permission).
     */
    private suspend fun cleanupStrayExploits() {
        runCatching {
            val client = RootDaemonClient()
            if (!client.isRoot()) return@runCatching
            client.exec(
                "for p in \$(ps -A -o PID,ARGS 2>/dev/null | grep '[g]hostlock_rv10[1]' " +
                "| awk '{print \$1}'); do kill -9 \$p 2>/dev/null; done"
            )
            Log.i(TAG, "cleaned up stray exploit processes")
        }
    }

    /**
     * Take the lease (overwrite the whole file).
     *
     * The lease file lives in `ghostlock_bridge/` (**777**), so this app can create it itself —
     * that is why the lease was moved over from `/data/local/tmp` (see the note on [LEASE_PATH]).
     * Before the move the app had no directory write permission on `/data/local/tmp`, so when the file did not exist this function **always failed**.
     *
     * Even on failure it **must leave a trace**: the original `runCatching {}` swallowed the failure, so the app would
     * run the exploit anyway while "not holding the lease" — exactly the mutual grabbing the lease exists to prevent.
     * After the move a failure only means rare disk/permission anomalies, but since what is being prevented is mutual
     * grabbing, a W-level log line is preferable to silence.
     */
    private fun takeLease(): Boolean {
        val pid = android.os.Process.myPid()
        val ts = System.currentTimeMillis() / 1000
        return runCatching {
            File(LEASE_PATH).writeText("owner=app pid=$pid ts=$ts")
            true
        }.getOrElse {
            Log.w(TAG, "takeLease FAILED (${it.message}) — 租約檔需由 shell 端以 666 預先建立；" +
                "本次將在**未持租約**的情況下繼續")
            false
        }
    }

    /**
     * Release the lease: **write an "expired" marker instead of deleting the file**.
     *
     * History: originally `delete()` was used, but the lease file was then in `/data/local/tmp` and the app had no
     * write permission on that directory → `delete()` always failed (measured `rm: Permission denied`),
     * so the lease lingered as `owner=app` + fresh `ts`, **blocking the desktop tool and deploy for up to 900 seconds**.
     *
     * After the move to the 777 `ghostlock_bridge/` deletion works, but **the marker is still written**, because:
     *  1. **Consistency**: the three parties (app/deploy/desktop) share one convention and need not each judge delete rights.
     *  2. **No window**: between delete and recreate, readers would see "file does not exist" = no lease = takeable,
     *     which is a breeding ground for TOCTOU; writing the marker is a single overwrite with no such window.
     *
     * `ts=0` fails the validity check in all three implementations (desktop requires `>1000000000`,
     * app/deploy require the age to be within `0..TTL`), so it is equivalent to "released";
     * keeping the file (666) also lets a later `takeLease()` overwrite it.
     */
    private fun dropLease() {
        runCatching { File(LEASE_PATH).writeText("owner=none pid=0 ts=0") }
            .onFailure { Log.w(TAG, "dropLease FAILED (${it.message})") }
    }

    /** One health check + auto-rebuild when necessary. */
    private suspend fun check() {
        checks++
        persistCounters()
        val client = RootDaemonClient()
        if (client.isRoot()) {
            if (state != com.rokid.rc.shared.protocol.RootStates.OK) {
                Log.i(TAG, "root ok (was $state)")
            }
            state = com.rokid.rc.shared.protocol.RootStates.OK
            detail = null
            return
        }

        /*
         * First check whether anyone is using the same exploit. Order matters: this check must come
         * before "delete the stale socket" — otherwise, in the window where the desktop tool has just brought
         * the daemon up but the socket is not yet in /proc/net/unix, we would delete it.
         */
        val externalOwner = externalLeaseOwner()
        if (externalOwner != null) {
            state = com.rokid.rc.shared.protocol.RootStates.LOST_COOLDOWN
            // show the **actual** holder (desktop/deploy/…), do not hardcode it
            detail = "external-lease($externalOwner)"
            Log.i(TAG, "external operator($externalOwner) holds lease; skip re-arm and leave socket alone")
            return
        }

        /*
         * root unavailable: try to clear a stale socket.
         *
         * ⚠ The app has **no directory write permission** on `/data/local/tmp` (see the note in takeLease),
         * so `delete()` in fact **always fails**. The original code printed "removed stale socket"
         * without looking at the return value, so the log contradicted the facts (measured: the log said deleted, the file was still there)
         * — that kind of false log sends later debugging in the wrong direction, so it now records the truth.
         */
        val stale = File(com.rokid.rc.glasses.GlassesPaths.ROOT_DAEMON_SOCK)
        if (stale.exists()) {
            if (stale.delete()) {
                Log.i(TAG, "removed stale socket ${stale.absolutePath}")
            } else {
                Log.i(TAG, "stale socket present but not removable by app uid " +
                    "(${stale.absolutePath}); leaving it (harmless: no listener)")
            }
        }

        // is there anyone who can help escalate?
        val helperResident = isBridgeHelperResident()
        val shizukuUp = isShizukuAvailable()
        if (helperResident) {
            detail = SCRIPT_BRIDGE
        } else if (shizukuUp) {
            detail = SCRIPT_SHIZUKU
        } else {
            state = com.rokid.rc.shared.protocol.RootStates.LOST_NEED_ARM
            detail = "$SCRIPT_BRIDGE|$SCRIPT_SHIZUKU"
            Log.w(TAG, "root lost and no trigger available (helper=$helperResident shizuku=$shizukuUp)")
            return
        }

        /*
         * Safe arming policy (2026-09-13): boot-loop breaker / per-boot retry cap + exponential backoff /
         * auto-arm only while charging / delay after boot. The EXPLOIT kernel-panics roughly 50% of the time → reboot after 2-5 minutes,
         * and unlimited retries turn into a boot loop (reports/glasses_reboot_after_adb_unplug_2026-09-13.md).
         * This only constrains **automatic** arming; manual escalation by the user is unaffected.
         */
        val appCtx = graph.app
        ArmPolicy.blockReason(appCtx, prefs)?.let { why ->
            state = com.rokid.rc.shared.protocol.RootStates.LOST_COOLDOWN
            detail = "safe-arm($why)"
            Log.i(TAG, "自動武裝暫停：$why（${ArmPolicy.describe(appCtx, prefs)}）")
            return
        }

        // rate limit
        val now = android.os.SystemClock.elapsedRealtime()
        if (rearming || now - lastRearmAtMs < REARM_COOLDOWN_MS) {
            state = com.rokid.rc.shared.protocol.RootStates.LOST_COOLDOWN
            return
        }
        // passed every check → record this auto-arm (used by the breaker and the backoff)
        ArmPolicy.onAutoArmAttempt(prefs)

        rearming = true
        rearmAttempts++
        persistCounters()
        lastRearmAtMs = now
        state = com.rokid.rc.shared.protocol.RootStates.REARMING
        Log.i(TAG, "auto re-arm start (helper=$helperResident shizuku=$shizukuUp detail=$detail)")
        /*
         * Lease pre-check: between the read above and here an external operator may have taken it (TOCTOU),
         * so confirm once more before actually acting, to avoid both sides rewriting .ghostlock_root.sh at the same time.
         */
        val preOwner = externalLeaseOwner()
        if (preOwner != null) {
            state = com.rokid.rc.shared.protocol.RootStates.LOST_COOLDOWN
            detail = "external-lease($preOwner,precheck)"
            rearming = false
            Log.i(TAG, "lease taken by external operator($preOwner) just before re-arm; abort")
            return
        }

        // Take the lease. Failure (for example the lease file does not exist and the app has no directory write
        // permission) still continues, but takeLease() has left a W-level log — no more silently "running the exploit without a lease".
        if (!takeLease()) {
            Log.w(TAG, "proceeding WITHOUT lease: external operators cannot see us; " +
                "run deploy/lease.sh (ensure_file) to pre-create a 666 lease file")
        }
        try {
            val started = now
            /*
             * The timeout is necessary: without one earlier, the keeper got stuck inside acquireRoot() for as long as 17 minutes,
             * during which (a) it stopped checking the lease (b) the status stayed at REARMING
             * (c) the exploit kept leaking processes and dragged the device down.
             */
            val result = withTimeoutOrNull(REARM_TIMEOUT_MS) { graph.ghostlock.acquireRoot() }
            val tookMs = android.os.SystemClock.elapsedRealtime() - started
            if (result == null) {
                Log.w(TAG, "auto re-arm timeout after ${tookMs}ms; cleaning up strays")
                cleanupStrayExploits()
                state = com.rokid.rc.shared.protocol.RootStates.LOST_NEED_ARM
                detail = "rearm-timeout"
            } else if (result.success || graph.ghostlock.rootAcquired) {
                rearmSuccesses++
                persistCounters()
                state = com.rokid.rc.shared.protocol.RootStates.REARMED
                Log.i(TAG, "auto re-arm ok in ${tookMs}ms")
                // after a successful rebuild, bring the dependent features back
                runCatching { graph.silentMode.applyIfEnabled() }
                runCatching { graph.ringControl.ensureRunning() }
            } else {
                state = com.rokid.rc.shared.protocol.RootStates.LOST_NEED_ARM
                Log.w(TAG, "auto re-arm failed: stage=${result.stage} out=${result.output.take(120)} (${tookMs}ms)")
            }
        } finally {
            dropLease()
            rearming = false
        }
    }

    /**
     * Whether the bridge helper is resident.
     *
     * ⚠ Cannot use `ps`: an Android app cannot see other processes (measured: the helper was clearly running yet this returned false,
     * which made the watchdog wrongly conclude "nobody can trigger" and skip the auto-rebuild). Read the heartbeat file the helper updates every second instead.
     */
    private fun isBridgeHelperResident(): Boolean = runCatching {
        val f = File(com.rokid.rc.glasses.GlassesPaths.BRIDGE_HELPER_ALIVE)
        if (!f.exists()) return@runCatching false
        val ts = f.readText().trim().toLongOrNull() ?: return@runCatching false
        val now = System.currentTimeMillis() / 1000
        // tolerate a 2-minute heartbeat gap (the helper loop runs once a second; a busy bridge stays within that order of magnitude)
        now - ts in 0..HEARTBEAT_TOLERANCE_S
    }.getOrDefault(false)

    /** Whether the Shizuku server is available (through the official API, not `ps`). */
    private fun isShizukuAvailable(): Boolean =
        runCatching { rikka.shizuku.Shizuku.pingBinder() }.getOrDefault(false)

    companion object {
        private const val TAG = "RootDaemonKeeper"
        private const val INTERVAL_MS = 30_000L
        /** Minimum interval between two auto-rebuilds (avoid repeatedly triggering the exploit). */
        private const val REARM_COOLDOWN_MS = 5 * 60_000L
        /** Helper heartbeat tolerance in seconds. */
        private const val HEARTBEAT_TOLERANCE_S = 120L

        /**
         * Lease file shared with ZeroLightRokidDesktop/`deploy/lease.sh`.
         * **The path must be identical in all three places** (`deploy/test_lease.sh` has a cross-file consistency check).
         *
         * Why in the `ghostlock_bridge/` subdirectory rather than `/data/local/tmp` itself:
         * `/data/local/tmp` is `drwxrwx--x shell shell`, this app (uid 10053) is "other",
         * which has only `--x` and **no directory write permission** — measured: it can **neither create nor delete** files in it,
         * so `takeLease()` necessarily failed when the file did not exist (the app then ran the exploit without holding the lease).
         * `ghostlock_bridge/` is **777**, and this app measured `CREATE_OK` / `RM_OK` inside it.
         */
        private const val LEASE_PATH = "/data/local/tmp/ghostlock_bridge/ghostlock_op.lease"
        private const val LEASE_TTL_SEC = 900L

        /** Cap on a single auto-rebuild. Without it the keeper hangs (measured 17 minutes). */
        private const val REARM_TIMEOUT_MS = 5 * 60 * 1000L
        const val SCRIPT_BRIDGE = "deploy/root_bridge.sh"
        const val SCRIPT_SHIZUKU = "deploy/start_shizuku.sh"
    }
}
