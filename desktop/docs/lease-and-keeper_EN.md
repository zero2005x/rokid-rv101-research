# Interaction Between the Operation Lease and the App Keeper

> 中文版：[lease-and-keeper.md](lease-and-keeper.md)

ZeroLightRokidDesktop (PC) and the glasses app's `RootDaemonKeeper` both drive the
same exploit. This document records their shared contract, the actual conflicts, and
one **failed end-to-end on-device test**.

> **2026-09-13 update**: a third operator was added — the **deploy scripts**
> (`get_root_now.sh` and others) — giving it the lease protection it previously
> **completely lacked**. In the process, on-device testing uncovered the root cause
> of **the lease on the App side never actually taking effect** (the app has no
> directory write permission on `/data/local/tmp` → it can neither create nor delete
> the lease file), and 2 defects on the deploy side + 3 on the App side were fixed in
> total. **The keeper end-to-end guard has been validated on device (15/15)**,
> filling in the item this document previously marked as "unobservable". See the
> sections below.

## Shared Resources and Division of Labor

| Resource | Desktop tool | Glasses app | deploy scripts | Shared? |
|---|---|---|---|---|
| helper | `ghostlock_bridge_helper_desktop.sh` | `ghostlock_bridge_helper.sh` | `ghostlock_bridge_helper.sh` | ❌ the first two are already separate |
| mailbox directory | `ghostlock_bridge_desktop/` | `ghostlock_bridge/` | `ghostlock_bridge/` | ❌ the first two are already separate |
| helper heartbeat | `ghostlock_bridge_desktop/helper.alive` | `ghostlock_bridge/helper.alive` | same as the app | ❌ the first two are already separate |
| `ghostlock_rv101` | same | same | same | ✅ **shared by all three** |
| `.ghostlock_root.sh` | same | same | same | ✅ **shared by all three** |
| `gl_run.log` | same | same | same | ✅ **shared by all three** |
| `root_daemon.sock` | same | same | same | ✅ shared by all three |
| `ghostlock_bridge/ghostlock_op.lease` | `owner=desktop` | `owner=app` | `owner=deploy` | ✅ shared by all three |

The exploit binary itself **cannot be separated** across the three paths (the paths
are hard-coded inside `ghostlock_rv101`), so it has to be serialized through the
lease.

## The Lease Contract

Fixed path: `/data/local/tmp/ghostlock_bridge/ghostlock_op.lease`
(hard-coded once in each of the three codebases, so **changes must be synchronized**;
**T9** in `deploy/test_lease.sh` compares the **literal values** in the three files
directly to block drift. The path is deliberately written as a full literal rather
than being concatenated from variables.)

```
owner=desktop|app|deploy
pid=<pid>
ts=<unix seconds>
```

- **Validity period 900 seconds**; the holder must renew while it is working
- Desktop tool: a one-click run lasts at most 80 minutes, so renewal is handled by
  the existing **30-second poll**
- App keeper: held only during a single `acquireRoot()` (upper bound 5 minutes),
  released in `finally`
- deploy scripts: `lease.sh`'s background renewal (30 seconds) + release via an
  `EXIT` trap
- A non-holder that sees a **fresh** lease must stand down

### Why a lease is still needed when the helpers are separate

Separating the helpers only solves "pkill-ing each other" and "the mailbox being
taken over". The exploit binary is still shared, and running both at once will:

- overwrite each other's `.ghostlock_root.sh` (the payload is replaced by the other side)
- truncate each other's `gl_run.log`
- the desktop tool's `pkill -9 -f ghostlock_rv10[1]` will kill the copy the app is running

## On-Device Test: the Conflict Really Exists

After creating a "root lost, helper resident" scenario on the glasses (the APK
contains `RootDaemonKeeper`, classes5/6/7.dex):

```
08:01:57.803  RootDaemonKeeper: removed stale socket /data/local/tmp/root_daemon.sock
08:01:57.809  RootDaemonKeeper: auto re-arm start (helper=true shizuku=false detail=deploy/root_bridge.sh)
08:01:57.812  AutoGhostlockEngine: acquireRoot via bridge (gl_engine_bridge_resident)
```

Each of the three lines proves one thing: the keeper **proactively deletes the
socket**, treats the **helper installed by the desktop tool** as a usable trigger
source, and **escalates privileges through the same bridge**. Afterwards `last.cmd`
was occupied and `last.progress=waiting-done`.

**Behavior before the fix**: the keeper's `stale.delete()` was executed
unconditionally and sat before the lease check — so during the window in which the
desktop tool had just brought the daemon up but the socket had not yet appeared in
`/proc/net/unix`, it would be deleted.

## End-to-End On-Device Test: **Failed, and in the "Unobservable" Sense Rather Than Pass or Fail**

**Test design (B1)**: write a fresh `owner=desktop` lease → create a stale socket
file → observe whether the keeper (a) logs "standing down" (b) does not delete that
socket.

**Result**:

- the socket file **survived** — consistent with the guard's behavior
- but the **keeper produced no log output at all**, so it cannot be proven that the
  guard took effect

**Cause**: the keeper was stuck inside its own 08:01 `acquireRoot()`, and **never
reached the lease check at all**. The guard I wrote is inside `check()`, and `check()`
never returned.

**Runaway state discovered at the same time**:

```
exploit_proc = 492 → grew back to 516 after being cleared
load         = 273 → 347 (adb commands began to time out)
```

## The Three Gaps This Exposed (Fixed)

### 1. `acquireRoot()` had no timeout ← most critical

The keeper was stuck for 17 minutes, during which it stopped checking the lease, its
state stayed at `REARMING` forever, and the exploit kept leaking processes.

**This made the lease mechanism fail exactly when it was needed most** — the lease
only prevents "starting at the same time", not "getting stuck and not letting go".

Fix: `withTimeoutOrNull(REARM_TIMEOUT_MS = 5 minutes)`; on timeout, clear the leaks,
release the lease, and return to `LOST_NEED_ARM` (`detail=rearm-timeout`).

### 2. Leaked exploit processes were not cleaned up

Fix (done on both sides):

- **Desktop tool**: `cleanup_stray_exploits()` before a one-click run begins
- **App keeper**: `cleanupStrayExploits()` after a timeout (only effective while root
  is still usable — the app cannot kill processes owned by shell/root; when it cannot
  clean up, that is handed to the desktop tool)

**Lesson: you must obtain PIDs with `ps` and then `kill` them one by one; you cannot
use `pkill -f`.** The `pkill -f` pattern matches the shell command that runs it,
making the shell kill itself (exit code 137 measured, aborting the whole cleanup).

### 3. Lease pre-check (TOCTOU)

Between "reading the lease" and "actually starting work", the other side may take it.
Fix: **read it once more** before `takeLease()`; if it has been taken, abort and return
to `LOST_COOLDOWN` (`detail=external-lease(desktop,precheck)`).

## The Lease on the deploy Path (Added 2026-09-13)

**Problem**: the deploy side had **no lease at all**. `grep -n lease deploy/*.sh`
produced no hits before the fix. `get_root_now.sh` would therefore drive the same
exploit at the same time as the desktop build / App keeper, fighting over the shared
files hard-coded inside the binary — exactly the conflict described at the start of
this document.

**New `deploy/lease.sh`** (a shared primitive):

| Function | Purpose |
|---|---|
| `lease_take <owner> [pid]` | pre-check → write (tmp + atomic `mv`) → read back to confirm; returns 1 if it is already held |
| `lease_wait_free <owner> <seconds>` | wait for the other side to stand down (the App keeper's single re-arm is bounded at 5 minutes) |
| `lease_renew` | renews **only while it is still held by oneself**; returning 2 = adb temporarily failed (mid-reboot) and is not misjudged as having been taken |
| `lease_autorenew_start` | background renewal; the flag file is written only if it really was taken |
| `lease_cleanup` | stop renewal → release → clear the flag (hooked to an `EXIT` trap) |

**Why adding `owner=deploy` did not require changing the other two sides**: both sides
are **deny-by-default** — only "owner equal to oneself" counts as one's own
(`strstr(txt,"owner=desktop")` / `txt.contains("owner=app")`), so an unknown owner is
always treated as someone else and correctly stood down from.

**Wiring**: `get_root_now.sh` takes the lease (`LEASE_WAIT_S` defaults to 300s; if it
cannot get it, it `exit 2`s), renews it in the background, releases it via an `EXIT`
trap, and checks `lease_lost` before every boot round. `DRY_RUN=1` was added (usable
with `DRY_RUN_HOLD_S`) to validate the lease only and **not trigger the exploit at
all**.

## Two New Defects Caught by Later On-Device Testing (2026-09-13, both fixed)

### A. Identity compared only `owner` → multiple instances with the same owner were **not mutually exclusive at all** (**High**)

T3 in `test_lease.sh` caught it: two deploy instances both write `owner=deploy`, and if
identity compares only the owner, the two regard each other as "one of us" and run the
exploit **simultaneously**.

The desktop tool (a single GUI, guarded by `g_busy`) and the app (a single keeper,
guarded by `rearming`) are singletons by nature and are mutually exclusive without
relying on the pid; but **`get_root_now.sh` is a command-line script and can be run
twice at the same time**.

Fix: identity became **(owner, pid)**. `lease_is_mine` / `lease_held_by_other` /
`lease_renew` / `lease_drop` all now require both the owner and the pid to match (when
the pid is omitted only the owner is compared, to simulate the other two sides'
viewpoint).

### B. The `pkill` pattern missed the `_desktop` variant (**High**)

The pattern's coverage was verified on device with `grep -E`:

| pattern | matches the App helper | matches the `_desktop` helper |
|---|---|---|
| `ghostlock_bridge_helper.sh` (originally used by `root_bridge.sh`) | ✅ | ❌ **missed** |
| `ghostlock_bridge_helper.s[h]` (originally used by `get_root_now.sh`) | ✅ | ❌ **missed** |
| `ghostlock_bridge_helpe[r]` | ✅ | ✅ |

Cause: the two diverge after `"helper"` (`.sh` vs `_desktop.sh`).
**Consequence**: the desktop-build helper survived the whole session and fought the
App helper over the shared files; after the device rebooted on its own it was brought
back up, saw the leftover `last.cmd`, and triggered the exploit again
→ measured at **994 child processes, `load` 75.8, adb commands timing out**.

Fix: `deploy/proc_util.sh` was added, **filtering on the host side and sending only
PIDs to the device to be killed** (the device side only runs `ps -A -o PID,ARGS`, and
the pattern never appears on a device cmdline, so self-matching is structurally
impossible); `root_bridge.sh` now calls `clear_trigger_mailboxes` (clearing
`last.cmd` from both mailboxes) **before** starting the helper, cutting off the
respawn loop. The performance problem of "one adb call per PID" was also fixed
(killing 500+ processes would exceed 60 seconds and be interrupted).

## The Permission Trap on the App Side → **Cured at the Root by "Moving Location"** (2026-09-13)

`/data/local/tmp` is `drwxrwx--x shell shell`; the glasses app is **uid 10053**,
which falls under "other" and has only `--x` (traverse) and **no directory write
permission**. Measured with `run-as` (the app is debuggable):

| Action | Result | Meaning for the lease |
|---|---|---|
| create a new file | `Permission denied` | when the lease file does not exist, `takeLease()` **necessarily fails** |
| overwrite an existing 666 file | **succeeds** | only works if the file was first created by shell |
| delete a file | `Permission denied` | `dropLease()` **never works** |

Three real defects follow from this (all fixed):

### 1. `takeLease()` failed silently → the app drove the exploit without holding the lease (**High**)

It was originally wrapped in `runCatching {}`, so the failure was swallowed. As a
result, whenever the lease file did not exist, the app drove the exploit **without
holding the lease** — exactly the mutual preemption the lease is meant to prevent.
Fix: return a `Boolean` and leave a `W`-level log entry on failure.

### 2. `dropLease()` always failed → left `owner=app` behind, blocking others for 900 seconds (**High**)

`delete()` has no effect for the app, so after re-arming finished the lease remained
as `owner=app` with a fresh `ts`, blocking the desktop tool and deploy for up to 15
minutes. Fix: **write a "released" marker instead of deleting** — write
`owner=none pid=0 ts=0` (all three sides treat it as invalid), while keeping the 666
file for later overwrites.

### 3. `stale.delete()`'s log contradicted the facts (**Medium**)

The original code printed `removed stale socket` without looking at `delete()`'s
return value. Measured: the log said it was deleted, but **the file was still there**.
This kind of false log sends debugging in the wrong direction. Fix: check the return
value and, on failure, print
`stale socket present but not removable by app uid` instead.

### Companion change: the lease file is now "reset only, never deleted"

Because the app cannot recreate a deleted file, `lease_drop()` in `deploy/lease.sh`
was also changed to **write an expired marker** (`lease_reset`), and
`lease_ensure_file` was added (create the file + `chmod 666`, called by
`root_bridge.sh` at deployment time). None of the three sides deletes the lease file
any more.

### Cure at the root: the lease moved into the 777 `ghostlock_bridge/`

Items 1./2./3. above were all "suppressing the symptoms in the old location". The real
solution was to move the lease file into a directory **the app can write to**:

| | Old: `/data/local/tmp/ghostlock_op.lease` | New: `/data/local/tmp/ghostlock_bridge/ghostlock_op.lease` |
|---|---|---|
| directory permissions | `drwxrwx--x` (the app has only `--x`) | **`drwxrwxrwx` (777)** |
| app creates a file | ❌ `Permission denied` | ✅ `CREATE_OK` |
| app deletes a file | ❌ `Permission denied` | ✅ `RM_OK` |
| `takeLease()` when the file does not exist | **necessarily fails** | succeeds |

After the move, even if the lease file is deleted, **the app can recreate it
itself** — something the old location could never do.

**Migration**: `lease_migrate_legacy` in `deploy/lease.sh` and `lease_write()` in the
desktop tool both clear the lease file at the old path along the way, so that two
leases existing side by side cannot cause a misjudgment.

### Three-way consistency is now held by tests

The path is written in three languages (bash / Kotlin / C), and historically there was
a case where "only the narrow string constant was changed while a dozen other places
still hard-coded the old path", which broke everything.
`deploy/test_lease.sh` gained two entries:

| Test | Content |
|---|---|
| **T9** | directly compare whether the lease path **literal values** in the three files agree (drift protection) |
| **T10** | use `run-as` with the **app's uid** to actually create / delete files in the lease directory and write the lease file directly |

> ✅ **The desktop build has caught up**: `lease_drop()` in `zero_light_desktop.c` was
> changed from `rm -f` to **writing `owner=none pid=0 ts=0`**, and `lease_write()` was
> changed to "`mkdir -p` + `chmod 777` first, then an atomic write via a temp file +
> `mv`", clearing the old path along the way. All three sides now use exactly the same
> convention.

## Validation Results (2026-09-13, device RV101)

### deploy side: `deploy/test_lease.sh` — **15/15 passed**

| Test | What it validates |
|---|---|
| T1 / T2 | a released state can be acquired; holding it oneself does not block oneself |
| T3 | another deploy **instance** (different pid) cannot acquire it; a non-holder cannot clear the holder's lease |
| T4 / T5 | seen from the desktop / app viewpoint, `owner=deploy` is judged "someone else" (deny-by-default) |
| T6 | an expired lease no longer blocks |
| T7 | background renewal keeps the lease fresh |
| T8 | the `EXIT` trap releases it when the process is `TERM`ed (no fresh lease left behind) |
| **T9** | **the path literal values agree in all three places** (contract drift protection) |
| **T10** | **the app can create / delete files in the lease directory and write the lease file directly** (whether the permission trap was really removed) |

### App keeper side: `deploy/test_keeper_lease_guard.sh` — **15/15 passed**

This is the item this document had previously been unable to observe. **The guard code
sits after `if (client.isRoot()) return`**, so "root unavailable to the app" has to be
created before it will execute.

**Control-group design** (the criterion is the **log**, not whether the socket file
exists — because the app has no directory write permission at all, so without the
guard it could not delete the socket either):

| Phase | Condition | Expected | Result |
|---|---|---|---|
| control group | no lease | the keeper passes the guard → logs `no trigger available` | ✓ |
| experimental group | a fresh `owner=deploy` lease | stops at the guard, does **not** pass | ✓ |

> **Re-validation on the new path (11:33)**: after moving the lease into
> `ghostlock_bridge/` and re-running, **all 14 functional assertions passed** (the
> control group passed the guard, the experimental group stopped at the guard with the
> owner shown as `deploy`). The only failure was **root restoration after the test
> ended**: the device rebooted on its own mid-test (11:37), taking
> `root_restore_delayed.sh`, which was responsible for restoration, with it — a
> device-level problem, not program logic.

Experimental-group log measured on device (**owner correctly shown as deploy**; the
defect that hard-coded `desktop` has been fixed):

```
10:18:11.237 RootDaemonKeeper: external operator(deploy) holds lease; skip re-arm and leave socket alone
10:18:41.249 RootDaemonKeeper: external operator(deploy) holds lease; skip re-arm and leave socket alone
```

Before killing the daemon, the test uses `deploy/root_restore_delayed.sh` to leave a
delayed restoration process behind with root identity, so root can be restored
**without re-running the exploit** (measured: automatic restoration after 240
seconds).

### Two Other End-to-End Checks

- **Guard path**: after injecting a fresh `owner=desktop` lease, run `get_root_now.sh`
  → it logs "already validly held by someone else", `exit 2`s, **does not steal the
  other side's lease**, and **triggers no exploit at all**.
- **Acquire / release path**: `DRY_RUN=1 DRY_RUN_HOLD_S=15` → while held, the device
  really shows `owner=deploy pid=<script pid>`; after it ends, the lease is reset to
  `owner=none`.

## Not Yet Completed

- **On this unit (RV101), `/data` rolls back after a reboot**: measured after several
  consecutive reboots, the lease file created at 11:13 disappeared, the older file
  from 10:25 came back, and the installed APK reverted to the 09:52 version
  (`lastUpdateTime` went back from 11:15 to 09:52), while older files in the same
  directory were still there.
  Impact: **any coordination state written in `/data/local/tmp` (including the lease)
  may disappear without warning**.
  Mitigation: `lease_ensure_file` (called by `root_bridge.sh` on every deployment)
  recreates the lease file, and after the move to the 777 directory **the app can
  recreate it itself**; readers treat "the file does not exist" as "no lease", so this
  is a temporary loss of coordination rather than a failure. **This is a device-level
  problem, not something caused by this project's code.**

- The leak cleanup after a timeout cannot clean up when **root has been lost** (the
  app has no permission); this is a known limitation.
- **Real-time conflicts across operators still occur**: during validation it was
  measured that the desktop build acquired the lease in the gap after deploy released
  it and rewrote the `_desktop` helper. The lease only guarantees "only one party is
  running at a time"; it does not guarantee "the first-come-first-served order matches
  expectations".
- `externalLeaseOwner()` in `RootDaemonKeeper` still **reads the file once per check**;
  the frequency is once every 30 seconds, which is harmless in practice, but this needs
  attention if the frequency is raised in the future.

## Reminders for Future Testers

- Before testing, check the `exploit` process count and `load`; **do not pile a new
  attempt on top of an already messy device**
  → use `bash deploy/proc_util.sh status` (**host-side filtering, unaffected by
  device-side grep self-matching**)
- To bring a runaway device back under control: `bash deploy/proc_util.sh kill` (kills
  both helper variants + all exploits and empties both mailboxes)
- After changing lease-related code, **run `bash deploy/test_lease.sh` first** before
  doing on-device privilege escalation — it needs no root and triggers no exploit, so
  it is safe to run in any state
- To validate the lease only and avoid another LPE run: `DRY_RUN=1 bash deploy/get_root_now.sh`
- To create "root lost", note that **the root-side keepalive has root identity and
  cannot be killed by adb shell**; it has to go through the root channel:
  `printf "kill -9 PID\n" | nc -U /data/local/tmp/root_daemon.sock`
- See `tools/wintest/README.md` for the related tools
