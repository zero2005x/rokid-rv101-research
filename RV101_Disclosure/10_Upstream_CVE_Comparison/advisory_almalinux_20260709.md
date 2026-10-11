# [ARCHIVE] AlmaLinux Advisory — GhostLock (CVE-2026-43499)

> Archived: 2026-08-24 by RV101 research team.
> Source URL: https://almalinux.org/blog/2026-07-09-ghostlock/
> Author: Andrew Lukoshko (AlmaLinux Lead Architect), Thu Jul 9 2026.
> Archived for evidence purposes; formatting condensed. Not modified in substance.

---

## Update: The fix is now in production

**The patched kernel for GhostLock (CVE-2026-43499) has been released to the production repositories for every affected AlmaLinux release — 8, 9, and 10.**

Fixed versions released to production: **`kernel-4.18.0-553.141.2.el8_10`** (AlmaLinux 8), **`kernel-5.14.0-687.24.1.el9_8`** (AlmaLinux 9), **`kernel-6.12.0-211.32.1.el10_2`** (AlmaLinux 10), or higher.

---

A new Linux kernel local-privilege-escalation flaw has been disclosed, and we are shipping patched kernels for it. **GhostLock** ([CVE-2026-43499](https://www.cve.org/CVERecord?id=CVE-2026-43499)) is a use-after-free in the kernel's real-time mutex (rtmutex) priority-inheritance code. An unprivileged local user can exploit it to gain root, and it works from inside a container to escape to the host. No special capabilities are required. **Every supported AlmaLinux release (8, 9, and 10) is affected.**

## What the bug is

GhostLock lives in the kernel's real-time mutex code (`kernel/locking/rtmutex.c`), on the futex priority-inheritance (PI) path. The helper `remove_waiter()` assumes the waiter it is cleaning up always belongs to the currently running task. In the `FUTEX_CMP_REQUEUE_PI` requeue path, however, the kernel has to unwind on behalf of a *different*, sleeping thread when it detects a deadlock cycle and rolls back with `-EDEADLK`. During that rollback it clears `pi_blocked_on` on the wrong task, leaving a **dangling pointer into freed kernel stack memory** — a stack use-after-free.

An attacker triggers it by arranging three futexes and a set of coordinated threads into a priority-inversion deadlock, then reclaiming the freed stack frame with a forged waiter structure. From there the public write-up chains the constrained write into arbitrary kernel read/write, control-flow hijack, and finally root — a reliable, unprivileged local privilege escalation and container escape.

Like several recent kernel flaws, GhostLock is remarkably old: it was introduced back in **2011** (Linux 2.6.39) and sat unnoticed for roughly 15 years. Because it only depends on `CONFIG_FUTEX_PI`, which is enabled on essentially every distribution kernel, the exposure is broad. It was reported to [security@kernel.org](mailto:security@kernel.org) by **Nebula Security** — who found it with their automated analysis tool, VEGA — and is now fixed in mainline (commit `3bfdc63936dd`, Linux 7.1).

## Patching ahead of our upstream

Our core team has backported the upstream `rtmutex` fix to every affected AlmaLinux branch. The decision to ship ahead of a CentOS Stream / RHEL update was made by our technical steering committee, ALESCo.

## Thanks

Thanks to **Nebula Security** for finding, analyzing, and reporting this vulnerability upstream, and for the public write-up that made it possible to understand and verify the fix.
