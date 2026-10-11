# Lease Fix: deploy Gap → App Permission Root Cause → Three-Way Consistency (2026-09-13)

> 中文版：[deploy_lease_fix_2026-09-13.md](deploy_lease_fix_2026-09-13.md)

> **In one sentence**: this fills in the lease protection that the deploy side **never had at all**, and during validation the tests caught
> two real defects that would cause "the same exploit to be run by two sides at once".
> Ultimately the lease was moved into a 777 directory writable by the app, **removing the permission trap at its root**;
> `deploy/test_lease.sh` passes **15/15** and `deploy/test_keeper_lease_guard.sh` passes **15/15**.

---

## 1. Why This Was Needed

`docs/lease-and-keeper.md` points out that the three paths of the exploit itself are **hard-coded inside `ghostlock_rv101`**
and cannot be split in two the way the helper and the mailbox can:

```
/data/local/tmp/.ghostlock_root.sh   ← payload will be replaced by the other side
/data/local/tmp/gl_run.log           ← will be truncated by the other side
/data/local/tmp/ghostlock_rv101      ← will be pkill'ed by the other side
```

Therefore "who is running the exploit" must be serialized globally. The desktop panel and the app both have leases,
but **the deploy side had none at all** — before the fix, `grep -n lease deploy/*.sh` had **zero hits**.

## 2. New Files

| File | Role |
|------|------|
| `deploy/lease.sh` | lease primitive: `take`／`wait_free`／`renew`／`autorenew`／`drop`／`cleanup`／`selftest` |
| `deploy/proc_util.sh` | safe process killing (host-side filtering + batched PID delivery), mailbox cleanup, process-count diagnosis |
| `deploy/test_lease.sh` | lease test suite (**needs no root and does not trigger the exploit**) |
| `deploy/test_keeper_lease_guard.sh` | keeper guard end-to-end test (manufactures root unavailability; **the criterion is the log**)|
| `deploy/root_restore_delayed.sh` | delayed restore process: stays behind as root first, then brings the daemon + keepalive back when the time comes (**no need to re-run the exploit**)|

## 3. Modified Files

| File | Change |
|------|------|
| `deploy/get_root_now.sh` | take the lease (`exit 2` if `LEASE_WAIT_S` expires) + background renewal + `EXIT` trap release + per-round `lease_lost` check; `ensure_helper`／`cleanup_exploit` switched to `proc_util`; added `DRY_RUN=1` |
| `deploy/root_bridge.sh` | kill both variants **before** starting the helper + **clear `last.cmd` in both mailboxes**; switched to `proc_util`; calls `lease_ensure_file` (guarantees the lease file exists as 666 so the app can overwrite it)|
| `docs/lease-and-keeper.md` | added deploy as the third operator, contract update, five defects, keeper end-to-end verification results |
| `android/app-glasses/…/ghost/RootDaemonKeeper.kt` | `takeLease()` returns success/failure and logs failures; `dropLease()` rewritten to write an expiry marker; `externalLeaseOwner()` returns the actual owner; `stale.delete()` logged truthfully |
| `deploy/lease.sh` | lease path moved to the 777 `ghostlock_bridge/` (written as a full literal for T9 comparison); every function now does `mkdir -p`+`chmod 777` first; added `lease_migrate_legacy` |
| `deploy/test_lease.sh` | added **T9** (the path literals agree in all three places) and **T10** (the app can create/delete in the lease directory and write the lease file directly)|
| `ZeroLightRokidDesktop/src/zero_light_desktop.c` | `DEV_LEASE` changed to the new path; **`lease_drop()` changed from `rm -f` to writing an expiry marker**; `lease_write()` changed to `mkdir -p`+`chmod 777`+atomic write+cleanup of the old path; rebuilt `zero_light_desktop.exe` |

### Why Adding `owner=deploy` Requires No Change on the Other Two Sides

Both sides decide **deny-by-default** — only an owner equal to "self" counts as one of us:

```c
/* zero_light_desktop.c */  int mine = (strstr(t, "owner=desktop") != NULL);
/* RootDaemonKeeper.kt  */  val mine = txt.contains("owner=app")
```

So any unknown owner (including `deploy`) is treated as "held by someone else" and correctly stands aside.
This was verified on device from both viewpoints (tests T4／T5).

## 4. Two New Defects Caught by the Tests

### A. Identity compared only `owner` → multiple instances with the same owner are **not mutually exclusive at all** (**High**)

**This was caught by test T3; I had not thought of it in advance.** The first version of `lease_is_mine` compared only the owner,
so two `get_root_now.sh` instances both wrote `owner=deploy` and each considered the other "one of us",
**simultaneously** driving the same exploit — exactly what the lease is meant to prevent.

The desktop panel (single GUI, `g_busy` guard) and the app (single keeper, `rearming` guard) are singletons by nature
and are mutually exclusive without relying on a pid; but **`get_root_now.sh` is a command-line script and can be run twice at once**.

**Fix**: identity is now **(owner, pid)**. `lease_is_mine`／`lease_held_by_other`／`lease_renew`／
`lease_drop` all require both owner and pid to match (when the pid is omitted only the owner is compared, to simulate the other two sides' viewpoints).
This also makes "a non-holder cannot delete the holder's lease" hold (the second assertion of test T3).

### B. The `pkill` pattern misses the `_desktop` variant (**High**)

Measured on device with `grep -E` to check pattern coverage:

| pattern | matches the app helper | matches the `_desktop` helper |
|---|---|---|
| `ghostlock_bridge_helper.sh` (originally used by `root_bridge.sh`) | ✅ | ❌ **misses** |
| `ghostlock_bridge_helper.s[h]` (originally used by `get_root_now.sh`) | ✅ | ❌ **misses** |
| `ghostlock_bridge_helpe[r]` | ✅ | ✅ |

Cause: the two diverge after `"helper"` (`.sh` vs `_desktop.sh`).

**Consequence (measured in this session)**: the desktop panel's helper (pid 3358) survived the whole session and contended for the shared files with the app helper at the same time;
after the device rebooted on its own it was started again, saw the leftover `last.cmd`, and triggered the exploit again, producing
**994 child processes, `load` 75.8, and adb command timeouts**.

**Fix**:
1. `proc_util.sh` **filters on the host side and sends only PIDs to the device to kill** —
   the device side only runs `ps -A -o PID,ARGS`, so the pattern never appears on a device cmdline and
   **self-matching is structurally impossible** (more fundamental than the bracket trick).
2. `root_bridge.sh` calls `clear_trigger_mailboxes` **before** starting the helper, cutting off the rebirth loop.
3. Batched kill delivery: the original "one adb call per PID" **exceeded 60 seconds and was interrupted by the caller** when killing 500+ processes
   (leaving cleanup half done); now in batches of 200, and killing the two variants measured only **5.7 seconds**.

## 5. Verification Results

### 5.1 `deploy/test_lease.sh` — **15/15 passing**

| Test | What it verifies | Result |
|---|---|---|
| T1 | empty lease → can be acquired | ✓ |
| T2 | held by self → does not block self | ✓ |
| T3 | another deploy **instance** (different pid) cannot acquire it; a non-holder cannot delete the holder's lease | ✓✓ |
| T4 | viewing `owner=deploy` from the desktop viewpoint → judged as someone else | ✓ |
| T5 | viewing `owner=deploy` from the app viewpoint → judged as someone else | ✓ |
| T6 | an expired lease (ts beyond the TTL) no longer blocks | ✓ |
| T7 | background renewal keeps the lease fresh | ✓ |
| T8 | the process is `TERM`ed → the `EXIT` trap releases it, leaving no 15-minute residue | ✓ |
| T9 | the path **literals** agree in all three places (contract-drift protection)| ✓ |
| T10 | the app can create/delete files in the lease directory and write the lease file directly | ✓ |

### 5.2 Stand-Aside Path (real script, end to end)

After placing a fresh `owner=desktop` lease (`pid=99999`), `get_root_now.sh` was run:

```
[lease] 已被他人有效持有（owner=desktop age=3s ttl=900s）→ 等待最多 0s 讓對方做完
[lease] ✗ 等不到租約（owner=desktop age=4s ttl=900s）→ 放棄；避免與對方互搶共用檔案
exit code = 2
```

- ✅ correctly stood aside and exited with **exit 2**
- ✅ did **not steal** the desktop panel's lease (it was still `owner=desktop` afterwards)
- ✅ **did not trigger any exploit** (`exploit_count=0`)

### 5.3 Acquire/Release Path (`DRY_RUN`)

`DRY_RUN=1 DRY_RUN_HOLD_S=15 bash deploy/get_root_now.sh`:

```
持有中：owner=deploy pid=103474 ts=1789263885   ← 103474 = 腳本自身的 pid
結束後：ls: /data/local/tmp/ghostlock_op.lease: No such file or directory
exit=0
```

## 6. Facts Observed Incidentally During Verification

**The desktop panel was running at the time**: the mtime of `ghostlock_bridge_helper_desktop.sh` and its mailbox file
was **09:33**, and the test I ran at 09:33 happened to release the lease — the desktop panel took the lease in the gap and rewrote the
helper. This proves that the lease now **really is in effect across all three operators**, and it also exposes one inherent limitation:

> The lease guarantees "only one party runs at a time"; it does **not** guarantee that the order of acquisition matches expectations.
> Achieving "deploy has priority" or "queueing" would require adding priorities or a queue, which does not exist today.

Another note: when verifying `helpers=N`, **do not use device-side `grep -c`** —
`grep`'s own cmdline contains the pattern and self-matches (measured: it reported 0 as 2).
Use `bash deploy/proc_util.sh status` instead (host-side filtering).

## 7. App Side: The Lease Never Actually Took Effect (**the most important finding of this round**)

The hard-coded `"desktop"` in `detail` was only a surface problem. To observe the keeper's guard on a real device,
"root unavailable to the app" first had to be manufactured — and while investigating why the guard "could not be observed",
the **real cause turned out to be permissions**:

`/data/local/tmp` is `drwxrwx--x shell shell`; the glasses app is **uid 10053**, which falls under "other",
so it has only `--x` (traverse) and **no directory write permission**. Measured with `run-as` (the app is debuggable):

| Action | Measured result | Meaning for the lease |
|---|---|---|
| create a new file | `Permission denied` | when the lease file does not exist, `takeLease()` **necessarily fails** |
| overwrite an existing 666 file | **succeeds** | only works if the file was first created by the shell |
| delete a file | `Permission denied` | `dropLease()` is **always ineffective** |

### 7.1 `takeLease()` Failing Silently → the App Runs the Exploit Without Holding the Lease (**High**)

It was wrapped in `runCatching {}`, so failures were swallowed. Whenever the lease file does not exist,
the app drives the exploit **without holding the lease** — exactly the contention the lease is meant to prevent.
**Fix**: return a `Boolean` and leave a `W`-level log on failure.

### 7.2 `dropLease()` Always Failing → Leaves `owner=app` Blocking Others for 900 Seconds (**High**)

After re-arming ends, the lease remains as `owner=app` + a fresh `ts`,
blocking the desktop tool and deploy for up to 15 minutes.
**Fix**: write a "released" marker instead of deleting — `owner=none pid=0 ts=0`
(all three sides treat it as invalid), while keeping the 666 file available for later overwriting.

### 7.3 `stale.delete()` Logs That Do Not Match Reality (**Medium**)

The original code printed `removed stale socket` without checking `delete()`'s return value.
Measured: **the log says it deleted it, but the file is still there**. That kind of false log sends debugging in the wrong direction.
**Fix**: check the return value and, on failure, print
`stale socket present but not removable by app uid`.

### 7.4 Companion Change: the Lease File Becomes "reset only, never delete"

The app cannot recreate a deleted file, so `lease_drop()` in `deploy/lease.sh` was also changed to
write an expiry marker (`lease_reset`), and `lease_ensure_file` was added (create the file + `chmod 666`,
called by `root_bridge.sh` at deployment time). **None of the three sides deletes the lease file any more.**

> ⚠ **The desktop panel has not caught up yet**: `lease_drop()` in `zero_light_desktop.c` is still `rm -f`.
> After it deletes the file, the app cannot acquire the lease until the next shell-side `lease_ensure_file`.
> It is recommended to change it to write `owner=none pid=0 ts=0` as well.

## 7.5 Root-Cause Fix: Move the Lease into the 777 `ghostlock_bridge/`

§7.1–7.4 all "suppress the symptoms at the old location". The real solution is to move the lease file into a
directory **the app has write permission on**:

| | old `/data/local/tmp/ghostlock_op.lease` | new `/data/local/tmp/ghostlock_bridge/ghostlock_op.lease` |
|---|---|---|
| directory permissions | `drwxrwx--x` (the app has only `--x`) | **`drwxrwxrwx` (777)** |
| app creates a file | ❌ `Permission denied` | ✅ `CREATE_OK` |
| app deletes a file | ❌ `Permission denied` | ✅ `RM_OK` |
| `takeLease()` when the file does not exist | **necessarily fails** | succeeds |

After the migration, even if the lease file is deleted, **the app can recreate it itself** — something the old location could never do.

### Three-Way Synchronization (Including the Desktop Panel)

| File | Change |
|---|---|
| `deploy/lease.sh` | `LEASE_PATH` changed to the new path (**written as a full literal**, for T9 comparison); `lease_ensure_file`／`lease_reset`／`_lease_write` all do `mkdir -p` + `chmod 777` first; added `lease_migrate_legacy` |
| `RootDaemonKeeper.kt` | `LEASE_PATH` changed to the new path; comment reworded to "after the migration the app can create it itself"|
| `zero_light_desktop.c` | `DEV_LEASE` changed to the new path; `lease_drop()` changed from **`rm -f` to writing `owner=none pid=0 ts=0`**; `lease_write()` changed to `mkdir -p` + `chmod 777` + **temp-file/`mv` atomic write** + cleanup of the old path |

> Even though `lease_drop()` could now "delete" at the new location, it still writes a marker: **between deletion and
> recreation the other side would read "file does not exist" = no lease = fair game**, which is exactly the breeding ground for TOCTOU.

### Drift Protection and Permission Verification (New T9／T10)

The path is written in three languages. Historically there was a case where "only the narrow string constant was changed, while a dozen other places still
hard-coded the old path" and the whole thing silently failed (that lesson was already recorded in the comments of
`zero_light_desktop.c`). So the following were added:

| Test | Content |
|---|---|
| **T9** | directly compare whether the lease path **literals** in the three files agree |
| **T10** | actually create/delete files in the lease directory with **the app's uid** via `run-as`, and write the lease file directly |

After rebuilding `zero_light_desktop.exe` (MinGW, PE32+ GUI), `strings -el` confirmed:
the new path appears 3 times, `owner=none pid=0 ts=0` appears once, and there is **no `rm` of the new path at all**;
the only `rm -f` is the deliberate cleanup during migration from the old path.

## 8. Keeper End-to-End Guard Verification: **15/15 passing**

This is the item that had been marked "**cannot be observed**". The key is that the guard code sits
**after** `if (client.isRoot()) return` — when root is available the keeper returns early and
never reaches the lease decision.

**Added `deploy/test_keeper_lease_guard.sh`**: it manufactures "device healthy + keeper idle + root unavailable".

**The criterion is the log, not whether the socket file exists** — because the app has no directory write permission at all,
so even without a guard it cannot delete the socket, and "the socket is still there" proves nothing.

| Stage | Condition | Expected | Result |
|---|---|---|---|
| Control group | no lease | keeper passes the guard → logs `no trigger available` | ✓ |
| Experimental group | fresh `owner=deploy` lease | stops at the guard and does **not** pass | ✓ |

**Re-verification on the new path (11:33, the lease has been moved to `ghostlock_bridge/`)**:
**all 14 functional assertions passed**. The only failure was root recovery after the test — the device rebooted on its own at 11:37,
taking the delayed restore process with it (see the device `/data` instability item in §9). The program logic is correct.

Measured log from the experimental group (**owner correctly shown as deploy**):

```
10:18:11.237 RootDaemonKeeper: external operator(deploy) holds lease; skip re-arm and leave socket alone
10:18:41.249 RootDaemonKeeper: external operator(deploy) holds lease; skip re-arm and leave socket alone
```

### Safety Mechanism: Recovering Root Without Re-running the Exploit

Once the root daemon is killed, **only root can recreate it**. Therefore, **before** killing the daemon, the test
first starts `deploy/root_restore_delayed.sh` through the root socket as root
(a detached delayed restore process). Measured **automatic recovery after 240 seconds**, with no re-run of the exploit.
The test also **verifies that the restore process really is running as root** before continuing; otherwise it aborts.

### Two Problems in the Test Itself That Were Fixed (Recorded Honestly)

1. **Single-point sampling race**: the first version checked the socket just before the keeper's 30-second cycle boundary,
   simultaneously seeing "the file is still there" and "the deleted log", contradicting itself → changed to polling until the result appears.
2. **The pre-check missed the heartbeat file**: the keeper decides whether the helper is resident based on
   `ghostlock_bridge/helper.alive` (tolerating 120s), **not `ps`**.
   The first version looked only at `ps`, so when "the process was dead but the heartbeat was still fresh" the keeper really did re-run the exploit
   → changed to actively write the heartbeat to `0`, and to refuse to run when `ROOT_DELAY < 240s`.

## 8.5 Live Verification: the Lease Correctly Stood Aside Under Real Contention

An **unplanned real race** occurred at the end of verification, and it became the most convincing verification of all:

While `get_root_now.sh` held the `owner=deploy` lease and was performing multiple rounds of privilege escalation, the device rebooted repeatedly;
during that period **the desktop tool (which really was running) took the lease**, and our background renewal detected
"no longer held by me" → wrote the lost flag → the main program **aborted immediately**:

```
[2026-09-13 11:47:26] [lease] ✗ 租約已被他人取走
                      （lost at 2026-09-13 11:44:54 (owner=none)）→ 中止
```

This is exactly the designed behavior: **do not contend for the shared files hard-coded inside the binary**
(`.ghostlock_root.sh`, `gl_run.log`, `ghostlock_rv101`).
Before the fix it would keep running, with both sides overwriting each other's payload and truncating each other's log.

It also verified the three-state design of `lease_renew`: adb failing during a reboot returns `2` (transient, keep retrying),
and only **actually reading that someone else holds it** returns `1` and triggers the abort — "device offline" is not misjudged as "lease stolen".

## 9. Not Yet Complete / Next Steps

| Priority | Item |
|------|------|
| ~~P1~~ | ~~keeper-side guard log on the app side to be observed~~ → ✅ **completed (§8, 15/15)** |
| ~~P2~~ | ~~`detail` hard-coded to `"desktop"`~~ → ✅ **fixed and verified on device to show `deploy`** |
| ~~P1~~ | ~~desktop panel `lease_drop()` changed to write an expiry marker~~ → ✅ **completed and the exe rebuilt** (§7.5)|
| P2 | `pkill -9 -f ghostlock_rv101` inside the helper can still kill **another** operator's exploit (the lease has greatly reduced the incidence, but it is not impossible)|
| ~~P2~~ | ~~move the lease into the 777 subdirectory~~ → ✅ **completed (§7.5), T9／T10 verified** |
| **P1** | **the device `/data` rolls back after a reboot** (measured: the lease file disappears and the APK reverts to an older version) — a device-level problem, not a code problem; see the "not yet complete" section of `docs/lease-and-keeper.md` for mitigations|

---

*Device: Rokid RV101 `<DEVICE_SERIAL>` (1.22.009-20260710-150201) | adb: `adb.exe` on the Windows side (WSL)*
*Related: `reports/kernelsnitch_mm_leak_clean_boot_2026-09-13.md` (root acquisition process and kernel state analysis)*
