# Glasses "Reboots Shortly After adb Is Unplugged" — Root-Cause Analysis and What Can Be Fixed

> 中文版：[glasses_reboot_after_adb_unplug_2026-09-13.md](glasses_reboot_after_adb_unplug_2026-09-13.md)

- **Date**: 2026-09-13
- **Device**: Rokid RV101 `<DEVICE_SERIAL>` (Android 12 / API 32, kernel `5.10.209-perf`, firmware `1.22.009`)
- **Symptom**: after adb is disconnected from the PC/phone, the glasses reboot on their own after a while

---

## 0. Conclusion

**Primary cause (confidence High–Very High): a kernel panic when GhostLock (CVE-2026-43499) privilege escalation fails.**
The project's own records state this plainly:

> `~/ZeroLightRokidDesktop/docs/research-update.md`
> **Success rate: about 50% per boot; failure = kernel panic → automatic reboot after 2–5 minutes, with the KASLR slide changing**

The statistics in this workspace agree: **21 panics out of 143 attempts** (`reports/FACTS.md` §6 G9).
"Shortly after" (2–5 minutes) is exactly the timescale of this failure mode.

**What can be fixed**: we **cannot** make the exploit stop panicking (an inherent risk; P3 evaluated and abandoned rewriting it),
**but we can** remove the "automatic, unlimited retry" behavior so it stops betting on that 50% over and over, and fix the
false impression that "it recovers automatically after a reboot".

---

## 1. Evidence

### 1.1 This project's own records (the most direct)

| Source | Content |
|------|------|
| `ZeroLightRokidDesktop/docs/research-update.md` | "**about 50% per boot**; failure = kernel panic → **automatic reboot after 2–5 minutes**" |
| Same as above (pitfall table) | "about 50% panic → reboot (2–5 min) \| before retrying, wait for `sys.boot_completed=1` **and uptime > 150 seconds**" |
| `reports/FACTS.md` G9 | history of 143 runs: verifier/reclaim failures 80, **panic 21**, timeout 0, OOM 0 |
| `deploy/test_shizuku_e2e.sh:7` | "the exploit has a ~50% chance of kernel panic → reboot" |

### 1.2 Kernel configuration amplifies the consequences of a panic

```
Kernel command line: ... kernel.panic_on_rcu_stall=1 ...  ← any RCU stall (21 seconds by default) panics immediately
```

GhostLock is **`remove_waiter()` in rt_mutex / futex-PI clearing the wrong `pi_blocked_on` → a dangling `rt_mutex_waiter`** (CWE-416).
When the race is lost, that corrupted PI chain leaves some task stuck in the kernel → **RCU stall → immediate panic → reboot**.
`kernel.panic_on_rcu_stall=1` upgrades "stuck" directly into "panic", without waiting for the watchdog.

### 1.3 Boot reason and how the previous shutdown happened

```
$ getprop ro.boot.bootreason          → reboot
$ getprop persist.sys.boot.reason.history
  reboot,1789291362        ← 17:22:42 (this boot)
  reboot,shell,1789278832  ← 13:53:52 (this entry has shell = adb reboot)
  reboot,1789271315        ← 11:48:35
  reboot,1789271070        ← 11:44:30
$ dmesg | grep -i ps_hold
  qcom,pm8150@0:qcom,power-on@800: Power-off reason:
      Triggered from PS_HOLD (PS_HOLD/MSM Controlled Shutdown)
```

- The previous shutdown was a **PS_HOLD / MSM-controlled shutdown** (= a clean software reset); it was neither a hardware watchdog bite nor a power loss.
- This is consistent with "panic → orderly reboot" (if the PMIC watchdog had bitten, bootreason would normally be `watchdog`).
- The 17:22:42 entry **has no `shell`**, so it was **not** an `adb reboot` (`deploy/get_root_now.sh` uses `adb reboot`,
  which leaves `reboot,shell,…` in the history, as in the 13:53:52 entry).

### 1.4 What can be ruled out

| Candidate | Verdict | Basis |
|------|------|------|
| **RGR06 ring** | ❌ Ruled out | today's reboots were at 11:44:30, 11:48:35, 13:53:52 — **all earlier than** pairing the ring to the glasses (17:12:58) |
| **The app issuing a reboot directly** | ❌ Ruled out | a repo-wide grep finds no `reboot` / `sys.powerctl`; the only thing that reboots is `adb reboot` in `deploy/get_root_now.sh` (which leaves a `shell` marker) |
| **Battery / power loss** | ❌ Ruled out | currently 96%, `USB powered: true`, and the shutdown reason is PS_HOLD (software) rather than undervoltage |
| **A leftover loop on the PC** | ❌ Ruled out | neither `get_root_now.sh` nor `watch_root.sh` is running on the PC |
| **Heat** | ❌ Ruled out | the `ThermalEngine … actions shutdown` lines are **monitor registration at boot**, not an actual shutdown |

### 1.5 Why it was only noticed "after adb was unplugged"

1. While USB is plugged in the device is **charging**, and adb keeps the system more active → the corrupted kernel state does not necessarily get a chance to blow up right away.
2. After unplugging, the device enters standby / the scheduling state changes → the dangling rt_mutex waiter is more likely to be stepped on during the next **scheduling or suspend path** → stuck → RCU stall → panic.
3. Unplugging also removes `adb`, so the user can only notice via "the device rebooted by itself" → intuitively attributing it to "unplugging".
4. The real **trigger is the earlier privilege-escalation attempt** (run while the cable was plugged in); the reboot is only its delayed consequence.

### 1.6 An overlooked fact: after a reboot our app **does not come up automatically at all**

```
W/BroadcastQueue: Background execution Third-Party APP not allowed:
    receiving Intent { act=android.intent.action.BOOT_COMPLETED ... }
    to com.rokid.rc.glasses/.BootReceiver
```

Measured on device (at the time of writing):

```
app pid: none            ← the app is not running
uptime: 277.22           ← 4.6 minutes since the last reboot
root_daemon alive: 0     ← root is gone too
```

→ This glasses system **forbids third-party apps from receiving BOOT_COMPLETED**, so:
- "Automatic recovery of root / ring control after a reboot" is currently **ineffective in practice**;
- but the design of `root_mode = AUTO` (the value measured in prefs) + the keeper retrying indefinitely every 5 minutes
  will **re-run the exploit repeatedly** while the app is still alive → which amounts to betting on that ~50% every time → and can escalate into a **boot loop**.

---

## 2. Can It Be Fixed?

### 2.1 Cannot be fixed (inherent risk)

The exploit's success rate and its stability are **two sides of the same race condition**: winning the race requires hitting that
time window, and losing it leaves the UAF scene behind. P3 evaluated "rewriting it for greater efficiency / success rate" and
**abandoned** that (see `reports/su_escalation_roi_assessment.md` §8). **There is no way to guarantee no reboot.**

### 2.2 Can be fixed (can greatly reduce reboot frequency; all at the app/script layer)

| Priority | Measure | Why it works |
|------|------|-----------|
| **P0** | **Boot-loop circuit breaker**: persist "the time of the last LPE" and "this boot's time"; if the device **reboots again within 10 minutes of boot** twice in a row → automatically switch `rootMode` to `MANUAL`, disable auto-arming, and show the reason on the phone | It directly cuts the "reboot → automatically bet again → reboot again" loop |
| **P0** | **Per-boot retry cap + exponential backoff** (e.g. a cap of 2; 5 → 15 → 60 minutes), replacing the current unlimited retry every 5 minutes | It turns "betting on 50% infinitely" into something bounded |
| **P1** | **Auto-arm only while charging / plugged in** (`BatteryManager` checks `USB/AC powered`); while unplugged the keeper does not rebuild automatically | It matches exactly "it only goes wrong after unplugging"; while unplugged no new corruption scene is created |
| **P1** | **Delay arming after boot**: wait for `uptime > 150 s` (the project's own recommendation in `research-update.md`) | It avoids the boot load peak and lowers the race failure rate |
| **P1** | **Fix the false impression that "it does not come back automatically after a reboot"**: since BOOT_COMPLETED is blocked by the system, stop pretending it can recover automatically — instead tell the user plainly "after a reboot, plug in the cable and run `deploy/root_bridge.sh`", and have the phone display the correct guidance | The current automatic recovery is **ineffective**, yet it makes the status display disagree with reality |
| **P2** | **Let the user disable automatic privilege escalation with one tap** (a settings-page switch; no code change needed) | When root is not needed, there is no reason to carry the panic risk |

### 2.3 Usage recommendations (actionable immediately)

- **When root does not need to be resident**: switch `root_mode` to `MANUAL` and disable automatic keeper rebuild → the device will almost never reboot for no reason again.
  How: change the phone app's "Settings → Capture & Permissions → root mode" to manual, or simply disable the app.
- **When root is needed**: accept that "each attempt has about a 50% chance of rebooting the device", and that **every reboot requires re-arming**
  (because auto-start is blocked by the system); confirm `uptime > 150 s` before retrying.

---

## 3. Actionable Validation Experiments (low risk)

1. **A/B test**: turn off the app's automatic privilege escalation (MANUAL) → unplug and leave it for 30–60 minutes → observe whether it still reboots.
   - No more reboots ⇒ confirms it is the exploit panic (and also proves the P0 measures work).
   - Still reboots ⇒ the investigation needs to move to the hardware/system layer (though all current evidence argues against that).
2. **Record the boot history**: after every reboot, capture `getprop persist.sys.boot.reason.history` (this document demonstrates how to decode it);
   accumulating "whether a shell marker is present" and the time distribution is enough to distinguish an "`adb reboot`" from a "panic reboot".

---

## 4. Confidence

| Statement | Label | Confidence | Basis |
|------|------|------|------|
| The primary cause is a kernel panic from a failed exploit | INFERENCE (strong) | **High** | The project's own documents state ~50% panic → reboot in 2–5 minutes; 21/143 panics in this workspace; `panic_on_rcu_stall=1` makes a stall panic immediately |
| The previous shutdown was a clean PS_HOLD reset (not a watchdog / power loss) | FACT | **Very High** | the raw dmesg text |
| The 17:22 event was not an `adb reboot` | FACT | **Very High** | that boot-history entry has no `shell`, while 13:53:52 does |
| It was not caused by the RGR06 ring | FACT | **Very High** | the three events at 11:44 / 11:48 / 13:53 all precede the ring pairing (17:12:58) |
| The app does not start automatically after a reboot | FACT | **Very High** | `BOOT_COMPLETED … not allowed` plus the measured `app pid: none` |
| `root_mode=AUTO` + unlimited keeper retries amplify the risk | FACT | **Very High** | prefs measured `root_mode=AUTO`; `RootDaemonKeeper` has only a 5-minute cooldown and no attempt cap |
| "Unplugging" is itself the trigger | INFERENCE | Medium | more likely "a delayed consequence + only observed after unplugging"; no unplug A/B was performed |

## 5. Not Performed
No automatic privilege escalation was disabled, the ring pairing was not removed, and no unplug A/B experiment was performed (it requires the user to help observe for 30–60 minutes).
