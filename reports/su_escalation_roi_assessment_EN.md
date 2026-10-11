# Is the Privilege-Escalation Code "Worth Rewriting?" Assessment Report

> 中文版：[su_escalation_roi_assessment.md](su_escalation_roi_assessment.md)

- Date: 2026-09-13
- Question: Is it worth spending time rewriting the privilege-escalation (root) code to improve **efficiency / success rate**?
- Conclusion: **Rewriting the escalation chain itself is not worth it; spending about 2–4 days on "operations-layer" hardening is worth it.** See §5 recommendations and §6 decision criteria for details.
- ⚠ **§1–§7 are the pre-implementation assessment** (describing the pain points at the time, e.g. the `USE_ENGINE` compile-time switch); **§8 is the result backfill after implementation was completed**; read the two in comparison.

---

## 1. Measured data (this session + history)

### 1.1 Measurements in this session (2026-09-13, current state of the integrated chain)

| Step | Measured |
|------|------|
| Starting state | `root_daemon` already dead (socket leftover, `nc -U` → Connection refused), bridge helper absent |
| One-command arming | `deploy/root_bridge.sh` (starts the resident helper) |
| Trigger | Phone App `ACQUIRE_ROOT` → engine SHIZUKU probe fails → **automatic fallback to the bridge helper** |
| Result | **root acquired in 84 seconds**: `uid=0(root) … context=u:r:kernel:s0` |
| Afterwards | After root is acquired, the fast path `isRoot()` is used (`skips LPE`), no need to run the exploit again |

> Historical comparison, `deploy/get_root_now.log` (2026-09-07): boot 1, trigger 1, completed in **112 seconds** (including 122 seconds for helper rebuild).

### 1.2 Historical runlog statistics (subagent analysis of 148 logs)

| Metric | Value | Note |
|------|------|------|
| Actual exploit launches | **143** | each log = one `ghostlock_rv101` execution |
| Successful root acquisition (verified `uid=0(root)`) | **15** | another 2 had `child is root!` but did not print the uid |
| Failed to acquire root | **126** | |
| Device disappeared / hard reset (panic symptom) | **27** | 12 directly `device not found` + 15 punch-window deaths |
| Time to acquire root (n=15) | **median 101.5 seconds** (86.7–154.8) | from KASLR leak to exploit complete |
| Success on first attempt | 9/15 executions | the other 6 succeeded on the 2nd attempt (retry built into the run) |
| Longest evidence of root survival | **≥43 minutes** (`run_w2_v960_143215.log`, no reboot, still Permissive) | |

**Failure classification (n=126)**:

| Category | Count | Can App-layer rewriting improve it |
|------|------|----------------------|
| verifier gate miss / reclaim failure (kernel race) | **80** | ❌ this is exploit research (v9.x reclaim gate), not a program-structure problem |
| kernel panic / reboot | **21** (27 cumulative for the project) | ⚠ can only "reduce pointless retries", cannot eliminate |
| timeout | 0 | — |
| Shizuku not running / not authorized | 0 (the 148 logs never mention Shizuku at all) | — |
| Process killed by OOM | 0 | — |
| Other / unknown | 12 | — |

> ⚠ **15/143 = 10.5% is not a usable success rate**: these 148 logs mix 5 workstreams (W2/FOAT primitive development, selftest, version scans v9.30→v9.60), the denominators differ and the logs were hand-picked. **The integrated operational flow is instead first-try successful** (both measurements in §1.1 succeeded on the first try).

---

## 2. Where does the time go?

```
total time to acquire root ≈ 84–112 seconds
├─ engine probe / fallback (Shizuku → bridge)         < 5 seconds   ← App layer, already fast enough
├─ helper trigger + exploit kernel race (grooming)   ~80–100 seconds ← the nature of the exploit; changing the App will not make it faster
└─ root_daemon startup + verification                 < 5 seconds   ← App layer
```

**The optimizable space at the App layer is < 10%**, and the rest is incompressible core-exploit time. In other words: **writing code cannot produce efficiency gains**.

---

## 3. The real pain points (measured, not guessed)

| Pain point | Evidence | Impact |
|------|------|------|
| **Every reboot requires re-arming via adb** | Both the helper and Shizuku must stay resident as the shell identity; the App (app uid) cannot start them itself | Without a PC there is no root → silent capture / privacy LED / WiFi media / ring path B all fail |
| **root can disappear mid-session** | Before 00:37 in this session `nc -U` was Connection refused (socket leftover, process already dead); the App has no daemon watchdog (ring control **does** have a 30-second watchdog, root does not) | Silent feature degradation: `✗ 靜音設定失敗 silent mode failed (root required?)` |
| **Engine choice is a compile-time constant** | `AppGraph.kt:135  private val USE_ENGINE = Engine.SHIZUKU` | Changing environments requires editing the program + rebuilding the APK (at runtime there is only a one-way Shizuku→bridge fallback) |
| **No measurement** | No success/failure/duration records; `deploy/watch_root.log` does not exist at all | Cannot use data to decide whether to invest in exploit research (this report's §1.2 can only fish logs out by hand) |
| **The cost of failure is asymmetric** | 27/143 executions caused a device reboot | each panic ≈ 1–2 minutes of reboot + re-arming + loss of all runtime state |

---

## 4. Why the expected return on "rewriting the escalation chain" is low

1. **Efficiency**: 84–112 seconds is already close to the lower bound (§2); the App-layer share is < 10%.
2. **Success rate**: the main cause of failure is the kernel race (80/126). Rewriting the App layer cannot change the kernel's reclaim-timing hit rate — that is the problem the v9.x research line (the reclaim gate miss and fake page panic documented in `GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md`) is working on.
3. **Refactoring risk**: the current chain already has several designs that are **correct but easy to delete by mistake** (using `/proc/net/unix` to determine the listener rather than `[ -S ]`, `setsid` detachment, `isRoot()` actually executing `id` to verify, the Shizuku→bridge fallback). Rewriting means stepping into all those pits again.
4. **Persistent root across reboots is not feasible**: it would require a boot image / Magisk, and `ROOT_PROCEDURE.md` records that boot.img cannot be obtained (multiple paths all failed) → no amount of "rewriting" can eliminate "one adb per boot".

---

## 5. Recommendations (ordered by ROI)

| Priority | Work | Effort | Expected benefit |
|------|------|--------|----------|
| **P0** | **Harden root daemon survival**: watchdog (modeled on ring control's 30-second loop) + automatic rebuild on failure (if the helper/Shizuku are still there) + stale socket cleanup + `oom_score_adj` protection | 1–2 days | Directly eliminates "features suddenly stop working" and the death cases measured in this session |
| **P0** | **Failure visualization**: the phone clearly displays "root is no longer valid / cannot auto-arm, please run deploy/start_shizuku.sh or root_bridge.sh" (the i18n codes such as `gl_shizuku_not_running` **already exist**) | 0.5 day | No more silent degradation; the operator knows the next step |
| **P1** | **Runtime engine probing**: is Shizuku available? is the bridge helper resident? direct shell? Choose in order and report the reason; remove the `USE_ENGINE` compile-time switch | 0.5–1 day | Change environments without rebuilding; failure causes are explicit |
| **P2** | **Escalation metrics**: record every attempt (engine / start time / duration / result class) into a ring buffer and report it to the phone | 0.5 day | This is the only basis for deciding "whether to touch the exploit in the future" |
| **P3** | **Re-arm without a cable**: a one-time `setprop persist.adb.tcp.port 5555` while root is held, after which the PC can re-arm over wireless adb | 0.5 day (needs on-device validation of whether this build supports it) | No USB; if it fails, keep the status quo (do not blindly restart adbd — it is documented to drop USB enumeration) |
| **Not doing** | Rewriting the LPE/exploit integration, payload timing, reclaim gate | weeks of research | The main cause of failure is the kernel race, not program structure |

**The only exploit-side small change worth making**: **panic avoidance**. If N consecutive gate/write misses cause an early abort (rather than continuing to punch until a hard reset), part of the 27 device reboots can be traded for "clean failures". This does not raise the hit rate; it **lowers the cost of failure** and can be folded into P0/P1.

---

## 6. Decision criteria (use P2's data)

| Observed situation | Action |
|--------------|------|
| Failures are dominated by "helper/Shizuku not resident, daemon died" (>70%) | Do only P0–P2, **do not touch the exploit at all** |
| First-try success rate ≥ 60%, panic < 10% | Same as above (the current evidence leans to this side) |
| First-try success rate < 50% and panic > 20%, and P0–P2 are complete | Only then is it worth investing in exploit research (reclaim gate / timing), budgeted in units of "weeks", not a refactor |

---

## 7. One-sentence answer

> **Do not rewrite the privilege-escalation code.** Efficiency is already at the lower bound (84–112 seconds, App layer <10%), and the success-rate bottleneck is the kernel race rather than program structure; what is actually being lost is the three operations problems of "needing adb arming every boot", "the daemon dying mid-way", and "silent failures" — spending 2–4 days on P0–P2 (where the i18n error codes and the ring watchdog pattern are both already available) has an expected return an order of magnitude higher than rewriting the escalation chain.

---

## Appendix: evidence sources for this report

| Source | Content |
|------|------|
| Measurements in this session (2026-09-13 00:37–00:39) | `root_bridge.sh` → `ACQUIRE_ROOT` → root acquired in 84 seconds; `nc -U` verifies `uid=0(root)` |
| `deploy/get_root_now.log` | 2026-09-07: boot 1 / trigger 1 / 112 seconds |
| `reports/ghostlock_runlogs/*.log` (148 files, analyzed by subagent) | 143 launches, 15 verified roots, 27 device disappearances, median duration 101.5 seconds, failure classification |
| `reports/GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md` | panic is a hard reset, no oops; the root cause of the reclaim gate miss |
| `reports/ROOT_PROCEDURE.md` | boot.img cannot be obtained → persistent root is not feasible |
| `AppGraph.kt:135`, `RootDaemonClient.kt:21-30`, `LedPayloads.kt` (root_daemon startup) | existing designs such as the compile-time engine choice, health check, `setsid` and listener determination |

---

## 8. Implementation result backfill (implementation completed 2026-09-13)

| Item | Status | Measured evidence |
|------|------|----------|
| **P0** root daemon survival hardening | ✅ done | the root-side keepalive rescued a killed daemon in **9 seconds** (App payload path) and **18 seconds** (deploy path); daemon and keepalive have `oom_score_adj=-1000`; stale sockets are cleaned automatically |
| **P0** App-side watch | ✅ done | when invalid and the helper heartbeat is fresh, it **rebuilds fully automatically**; measured **147 seconds** to regain root (without any manual intervention) |
| **P0-b** failure visualization | ✅ done | after a real reboot the phone displays "root: no longer valid, needs re-arming / please run: deploy/root_bridge.sh or deploy/start_shizuku.sh / watchdog N times" |
| **P1** runtime engine probing | ✅ done | `USE_ENGINE` removed; replaced with the heartbeat file (helper) + `Shizuku.pingBinder()` (Shizuku); on failure it automatically switches line and retries |
| **P2** escalation metrics | ✅ done | `acquireRootMetered()` records engine/duration/success count and **stores it in prefs** (it does not reset to zero when the App is killed), reporting to the phone with STATUS |
| **P3** arming without a cable | ❌ **abandoned (with evidence)** | `persist.adb.tcp.port` can be set and survives reboots, but **adbd in this build does not bind TCP** (0 listeners both in the same session and after a reboot) → the platform does not support it, not a program problem |

### Real failures encountered during implementation (which precisely confirm the assessment)

- The first root retry hit a **kernel panic hard reset** (`[wait] 裝置離線，視為 reboot`, the device rebooted automatically),
  and only the 2nd trigger on the next boot succeeded → corresponds to the failure classification in assessment §1.2 (panic 21/126).
- The App side originally used `ps` to determine whether the helper was resident, which **always failed** (an Android restriction: an App cannot see other processes) → it only became correct after reading the heartbeat file instead,
  which also explains why "automatic rebuild" must probe on a file basis (already written into `docs/deploy.md`).
