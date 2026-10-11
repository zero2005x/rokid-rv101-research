# Privilege-Escalation "Safe Arming": Four Guard Rails — Implementation and On-Device Validation

> 中文版：[arm_safety_policy_2026-09-13.md](arm_safety_policy_2026-09-13.md)

- **Date**: 2026-09-13
- **Motivation**: `reports/glasses_reboot_after_adb_unplug_2026-09-13.md`
  — GhostLock (CVE-2026-43499) **kernel-panics on roughly 50% of attempts, and the device reboots itself 2–5 minutes later**;
  the previous "auto-escalate on every boot + retry indefinitely every 5 minutes" amounted to repeatedly betting on that
  probability, and could produce a boot loop.
- **Artifacts**: **two distinguishable APK builds** (same source, one build flag)
  | File | versionCode / versionName | Behavior |
  |------|---------------------------|------|
  | `dist/app-glasses-v1.0-safe.apk` | 3 / **`1.0-safe`** | **all four guard rails enabled** (default build) |
  | `dist/app-glasses-v1.0-original.apk` | 1 / `1.0` | the original pre-change version (kept as a control) |
  | `dist/app-phone-v1.1-original.apk` | 2 / `1.1` | phone side **unchanged** (all changes this round are glasses-side) |

  sha256: `a82be4422012e34f5b18d5b6afcb34528301250b398d3978545817b66bdb755a` (safe),
  `b9937d665f30e0de8872abfd4b2879d3501f70f3964703124f034014d357c2f0` (original)

---

## 1. Implementation

### 1.1 Build flag (single source, both versions reproducible)

`android/app-glasses/build.gradle.kts`:

```kotlin
val safeArming: Boolean = (project.findProperty("safeArming") as String?)?.toBoolean() ?: true
...
versionCode = if (safeArming) 3 else 1
versionName = if (safeArming) "1.0-safe" else "1.0"
buildConfigField("boolean", "SAFE_ARMING", "$safeArming")
```

```bash
cd android
./gradlew :app-glasses:assembleDebug                      # → safe build (default)
./gradlew :app-glasses:assembleDebug -PsafeArming=false   # → original-behavior build
```

Because both builds share the **same applicationId**, they can be installed over each other directly;
`dumpsys package … | grep versionName` identifies which one is currently installed.

### 1.2 `ghost/ArmPolicy.kt` (new, four guard rails)

| # | Guard rail | Implementation |
|---|------|------|
| 1 | **Boot-loop circuit breaker** | Boot identity = `currentTimeMillis() - elapsedRealtime()` (= the wall-clock time at boot; no permission needed). On each detection of a "new boot", if `bootWall - lastArmWall ∈ 0..10 minutes` ⇒ record one **suspicious reboot**; **2** consecutive occurrences ⇒ `arm_suspended=true` (reason `reboot-loop`), and **auto-arming stops** until a manual reset. |
| 2 | **Per-boot retry cap + exponential backoff** | `MAX_ARMS_PER_BOOT = 2`; backoff 5 → 15 → 60 minutes (`BACKOFF_MS`). Automatically zeroed on every boot. |
| 3 | **Auto-arm only while charging** | `BatteryManager` (reads `EXTRA_PLUGGED` from the sticky `ACTION_BATTERY_CHANGED`; no permission needed); on battery, `blockReason = "on-battery"`. |
| 4 | **Delay after boot** | No action before `uptime < 150 seconds` (`BOOT_SETTLE_MS`, using the project's own recommended value). |

`blockReason()` returning null = allowed; otherwise it returns a reason code: `breaker-open` / `boot-settle` /
`max-arms-this-boot` / `on-battery` / `backoff-5m|15m|60m`.

### 1.3 Wiring (both automatic paths are constrained)

| Location | Change |
|------|------|
| `CaptureService` (escalates when AUTO starts) | First asks `ArmPolicy.blockReason()`; if blocked it only logs and **does not call `acquireRootMetered()`**; if allowed it first records the attempt via `onAutoArmAttempt()` |
| `RootDaemonKeeper.check()` (automatic rebuild when root is lost) | Same as above; when blocked, the state is set to `LOST_COOLDOWN` with `detail = "safe-arm(<reason>)"` |
| `AppGraph` | `app` changed from `private val` to `val` (the policy needs a Context to read charging state) |
| Glasses `DebugReceiver` | Added `ARM_POLICY_STATUS` (query) / `ARM_POLICY_RESET` (reset the breaker and counters) |

> ⚠ **Manual privilege escalation is unaffected**: when the user presses "acquire root" in the app, or when it is
> triggered by `deploy/root_bridge.sh`, it still runs — that is a deliberate decision. This policy only constrains
> **automatic** arming.
> When `SAFE_ARMING=false`, `ArmPolicy` allows everything and behavior is exactly the same as the old version.

---

## 2. On-Device Validation (each guard rail verified individually, and **the exploit was never actually run at any point**)

Validation method: after rewriting prefs with `run-as` (debug build), **cold-start** the app so that `ArmPolicy`
refreshes the boot state and makes a decision on the first `ARM_POLICY_STATUS`; then read the actual decision from
logcat. The circuit-breaker test used a prefs injection simulating "a reboot 60 seconds after the last arming"
(`build-cache/sim_boot.sh`).

| Test | Injected condition | Actual output | Result |
|------|----------|----------|------|
| Policy online | — | `ARM_POLICY safeArming=on up=773s charging=true arms=0/2 suspicious=0 suspended=false` | ✅ |
| **Circuit breaker 1/2** | `arm_last_wall_ms = this boot − 60s`, `arm_boot_wall_ms = 0` | `W ArmPolicy: 可疑重開：上次自動武裝後 60s 就重開（累計 1/2）` | ✅ |
| **Circuit breaker 2/2** | Same as above, once more | `E ArmPolicy: ⛔ 開機迴圈斷路器跳脫 …` → `suspended=true reason=reboot-loop` | ✅ |
| **End-to-end block** | `root_mode=AUTO` + circuit breaker already tripped → launch the app | `CaptureService: AUTO 提權暫停：breaker-open（… suspended=true reason=reboot-loop）`; `ghostlock_rv101/.ghostlock_root.sh` appears **0** times in logcat, and `ps` shows no leftover process | ✅ **the exploit really was not run** |
| **Retry cap** | `root_mode=AUTO`, `arm_arms_this_boot=2` | `AUTO 提權暫停：max-arms-this-boot（arms=2/2）` | ✅ |
| **Exponential backoff** | `root_mode=AUTO`, `arms=1`, `arm_last_wall_ms = now−30s` | `AUTO 提權暫停：backoff-5m` | ✅ |
| Reset | `ARM_POLICY_RESET` | `suspended=false`, counters zeroed, `reason` no longer shown | ✅ |

**The two guard rails not verified directly** (they need a matching environment; the code path is the same as above):
- `boot-settle`: requires launching the app within 150 seconds of boot.
- `on-battery`: requires launching the app with **USB unplugged** (battery only). ← **this is exactly the scenario you reported, so we suggest you confirm it once with the cable unplugged**
  (method: unplug → open the app → while `adb` is unavailable you can check the "glasses side" status on the phone, or plug the cable back in afterwards and capture
  `logcat -s CaptureService:* | grep 暫停`, which should show `on-battery`).

---

## 3. Current Device State (a safe state deliberately left in place after validation)

```
$ adb -s <DEVICE_SERIAL> shell dumpsys package com.rokid.rc.glasses | grep versionName
    versionName=1.0-safe
$ ARM_POLICY_STATUS
    ARM_POLICY safeArming=on up=1050s charging=true arms=0/2 suspicious=0 suspended=false
$ prefs root_mode = MANUAL
```

- **The safe build is installed** (`1.0-safe`).
- **`root_mode` is set to `MANUAL`**: the exploit is not run on its own, so there will be no more "reboots in order to escalate privileges" until you decide otherwise.
- The circuit breaker and the counters are **reset** (a clean state, equivalent to a fresh install).
- To return to automatic privilege escalation: change the phone app's "Settings → Capture & Permissions → root mode" to AUTO, or use
  `am broadcast … --es cmd ARM_POLICY_RESET` (if the circuit breaker has tripped).

---

## 4. Known Limitations

1. **The app still does not start automatically after a reboot** (the glasses system blocks third-party `BOOT_COMPLETED`; see
   `reports/glasses_reboot_after_adb_unplug_2026-09-13.md` §1.6). This policy only ensures that "reboots do not repeat because of automatic
   retries"; it does **not** solve "you must plug the cable back in to re-arm after a reboot". That needs another revival path (no reliable one has been found yet).
2. **It cannot reduce the per-attempt panic probability** (~50% is an inherent risk; P3 evaluated and abandoned rewriting the exploit).
3. With `root_mode=AUTO` + USB plugged in + more than 150 seconds since boot + the retry cap not reached, **one attempt will still be made** (this is by design:
   bounded and predictable); for zero risk, keep it at `MANUAL`.

## 5. Files

| File | Description |
|------|------|
| `android/app-glasses/src/main/kotlin/com/rokid/rc/glasses/ghost/ArmPolicy.kt` | New: the four guard rails |
| `android/app-glasses/build.gradle.kts` | `safeArming` flag / `SAFE_ARMING` / version strings |
| `CaptureService.kt`, `ghost/RootDaemonKeeper.kt` | The two automatic paths wired to the policy |
| `DebugReceiver.kt` (glasses) | `ARM_POLICY_STATUS` / `ARM_POLICY_RESET` |
| `build-cache/sim_boot.sh`, `build-cache/seed_prefs.sh` | For validation (simulate a reboot / seed prefs) |
| `dist/app-glasses-v1.0-safe.apk`, `dist/app-glasses-v1.0-original.apk` | The two APK builds |
