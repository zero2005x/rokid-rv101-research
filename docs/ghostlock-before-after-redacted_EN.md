# GhostLock (CVE-2026-43499) — Address-Redacted Before/After Comparison

> 中文版：[ghostlock-before-after-redacted.md](ghostlock-before-after-redacted.md)

> **Purpose**: to serve as the "redacted technical comparison" sent to Rokid (`degang.xu@rokid.com`).
>
> **Red lines applied**: this document has had all **absolute kernel addresses**, **device serial
> numbers** and **weaponization details** removed (heap grooming, reclaim, controlled-write
> parameters, the complete exploit). Instructions are expressed as **function-relative offsets**,
> and call/jump targets are given as **symbol names**. The complete disassembly of the original
> files (including absolute addresses) is retained only in a private workspace and is not sent out.
>
> Version identification and hashes are in §1; the basis for the patch determination is in §2 and §3.2.

---

## 1. Version and image identification

### 1.1 Affected version

| Item | Value |
|---|---|
| firmware/build | `1.22.009-20260710-150201` |
| OTA package filename | `RVE01-ota-1.22.009-20260710-150201.zip` |
| Full package hash | SHA-256 `163ab2058d8e8789f82e962c7bfccbe6ca9725437d15adaf4bc4a811bafc94e5` (MD5 not retained; re-downloaded from the vendor CDN and measured on 2026-09-16, consistent with our retained list) |
| Android version | 12 (`SKQ1.240613.001`, `user/release-keys`) |
| kernel version string | `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` |
| Android SPL | `2024-07-05` |

### 1.2 Patched version

| Item | Value |
|---|---|
| firmware/build | `1.25.012-20260901-150201` |
| OTA package filename | `RVE01-ota-1.25.012-20260901-150201.zip` (~1 GB) |
| Full package hash | MD5 `bec406d9e28ee779d8e876e57ba69f51` (matching the vendor OTA metadata; re-downloaded from the vendor CDN and measured on 2026-09-16)<br>SHA-256 `a24412ad8bc19d95e605952a3bf32e91e3692002e5195139650f73056e8985f5` |
| Android version | 12 (`SKQ1.240613.001`, `user/release-keys`) |
| kernel version string | `5.10.209-perf-g9a43350cc9ee-ab1.25.012-20260901-150201` |
| Android SPL | `2024-07-05` |

> **Note**: both versions have a `post-security-patch-level` of `2024-07-05`, so **looking at the
> SPL alone misjudges the build as "unpatched"**. Determining whether a build is patched requires
> disassembling `remove_waiter()`.

---

## 2. Decision criteria (upstream patch)

- CVE: `CVE-2026-43499` ("GhostLock"), a use-after-free in `remove_waiter()` in `kernel/locking/rtmutex.c`.
- Official fix commit: `3bfdc63` — *"rtmutex: Use waiter::task instead of current in remove_waiter()"*.
- Fix semantics: the operations on `current` inside `remove_waiter()` are changed to operate on `waiter->task`.
- ARM64 distinguishing features (**you must look at the source of `x20`, not merely search for a `#0x30` load**):
  - `remove_waiter()+0x040` is `mrs x20, sp_el0` (`x20 = current`) → **vulnerable version**.
  - `remove_waiter()+0x040` is `ldr x20, [x23, #0x30]` (`x23 = waiter`, `x20 = waiter->task`) → **patched version**.
  - ⚠ **Relying only on "does `ldr …, #0x30` appear inside the function" leads to a misjudgement**: the
    `remove_waiter()` of 1.22.009 also has an `ldr x14, [x9, #0x30]` (at `+0x160`), but its base is `x9`
    and it is unrelated to `waiter->task`. You must confirm whether the three uses — `+0x044` (`pi_lock`),
    `+0x088` (`pi_blocked_on`) and `+0x210` (the task argument to `adjust_prio_chain`) — are **all fed by
    the `x20` loaded at `+0x040`**.

---

## 3. `remove_waiter()` control-flow comparison (address-redacted)

> The listings in this section were produced by unpacking `boot.img` → kernel Image from the OTA
> packages listed in §1.1/§1.2 and then disassembling. The function is located by the symbol
> `remove_waiter`, and instruction addresses are always converted to function-relative offsets.
> **Both versions were processed with the same toolchain** (`vmlinux-to-elf` symbol recovery plus
> Capstone disassembly), so the two listings are directly comparable.
>
> **Byte-by-byte comparison result**: the `remove_waiter()` of both versions is **exactly the same
> size (`0x28c` = 163 instructions)**, and **only one 4-byte instruction differs in the machine
> code** — namely `+0x040`. The other 162 instructions (including all call targets) are identical.

### 3.1 Affected version 1.22.009 — vulnerable (`current`)

The following is the address-redacted listing of 1.22.009 `remove_waiter()` (absolute addresses have been converted to **function-relative offsets**; call targets are shown as symbols):

```
remove_waiter:
  +0x000  paciasp
  +0x004  str     x30, [x18], #8
  +0x008  stp     x29, x30, [sp, #-0x40]!
  ...
  +0x01c  ldr     x22, [x0, #0x10]
  +0x020  mov     x19, x0                   ; x19 = lock
  +0x024  mov     x23, x1                   ; x23 = waiter
  ...
  +0x040  mrs     x20, sp_el0               ; x20 = current            ← key to the vulnerable version
  +0x044  add     x21, x20, #0x85c          ; x21 = &current->pi_lock
  +0x048  mov     x0, x21
  +0x04c  bl      _raw_spin_lock            ; raw_spin_lock(&current->pi_lock)
  ...
  +0x088  str     xzr, [x20, #0x888]        ; current->pi_blocked_on = NULL  ← wrong target cleared
  +0x08c  bl      _raw_spin_unlock
  ...
  +0x160  ldr     x14, [x9, #0x30]          ; note: base is x9, unrelated to waiter->task
  ...
  +0x210  mov     x5, x20                   ; 5th argument = current
  +0x214  bl      rt_mutex_adjust_prio_chain
  ...
  (inside the function there is no source of current other than `mrs`; `current` runs through
   all three uses: pi_lock / pi_blocked_on / adjust_prio_chain)
```

**Reading**: `x20` is obtained directly from `mrs x20, sp_el0` (`current`), and `pi_lock` (`#0x85c`),
`pi_blocked_on = NULL` (`#0x888`) and the task argument of `rt_mutex_adjust_prio_chain` **all target
`current`**. When `current` (the requeue side) is not equal to `waiter->task` (the task owning the
waiter being requeued), `current->pi_blocked_on` is cleared incorrectly — this is the precondition
for the CVE-2026-43499 UAF.

### 3.2 Patched version 1.25.012 — patched (`waiter->task`)

The following is taken from 1.25.012 `remove_waiter()`; absolute addresses have been converted to **function-relative offsets** and call targets are shown as symbols:

```
remove_waiter:
  +0x000  paciasp
  +0x004  str     x30, [x18], #8
  +0x008  stp     x29, x30, [sp, #-0x40]!
  ...
  +0x040  ldr     x20, [x23, #0x30]        ; x20 = waiter->task (rt_mutex_waiter.task)
  +0x044  add     x21, x20, #0x85c         ; x21 = &task->pi_lock
  +0x04c  bl      raw_spin_lock             ; raw_spin_lock(&task->pi_lock)
  ...
  +0x088  str     xzr, [x20, #0x888]       ; task->pi_blocked_on = NULL  ← correct target
  ...
  +0x210  mov     x5, x20                   ; 5th argument = task
  +0x214  bl      rt_mutex_adjust_prio_chain
  ...
  (no mrs ... sp_el0 anywhere → current is not used)
```

**Reading**: after `x20` is loaded from `waiter->task` (`+0x040`, `[x23, #0x30]`), `pi_lock` (`#0x85c`),
`pi_blocked_on = NULL` (`#0x888`) and the task argument of `rt_mutex_adjust_prio_chain` **all target
`waiter->task`**, and `current` is no longer used (there is no `mrs …, sp_el0` in the function). This is
exactly the semantics of upstream commit `3bfdc63`.

> **Key facts from comparing the two versions**: `remove_waiter()` is **the same size (`0x28c`)** in
> both versions, and a **byte-by-byte comparison of the machine code differs in only the one
> instruction at `+0x040`** (`mrs x20, sp_el0` → `ldr x20, [x23, #0x30]`); the other 162 instructions
> are completely identical. This is a minimal backport: changing a single task source removes the
> precondition for CVE-2026-43499.

---

## 4. Key differences conclusion

| Feature | 1.22.009 | 1.25.012 |
|---|---|---|
| Source of `x20` at `+0x040` | `mrs x20, sp_el0` (`current`) | `ldr x20, [x23, #0x30]` (`waiter->task`) |
| Reads `current` (`mrs …, sp_el0`) | Yes (vulnerable version) | No |
| `+0x044` `pi_lock` target | `current->pi_lock` | `waiter->task->pi_lock` |
| `+0x088` `pi_blocked_on = NULL` write target | `current->pi_blocked_on` | `waiter->task->pi_blocked_on` |
| task argument of `adjust_prio_chain` at `+0x210` | `current` | `waiter->task` |
| Other `#0x30` loads | Present, but the base is `x9` (`+0x160`) and it is unrelated to `waiter->task` | — |
| Function size | `0x28c` (163 instructions) | `0x28c` (163 instructions, **identical**) |
| Machine-code difference | Only the one 4-byte instruction at `+0x040` | (same as left) |
| kernel git hash (version string) | `gd908d71a0404` | `g9a43350cc9ee` (different build) |
| upstream `3bfdc63` semantics | Not included | Included |
| On-device CVE-2026-43499 exploit | Successful (`uid 2000 → uid 0`) | Precondition no longer holds |

---

## 5. On-device before/after evidence (de-identified)

- **Before (1.22.009)**: an ADB shell went from the low-privileged `shell` (`uid=2000`,
  `u:r:shell:s0`, SELinux Enforcing) to `uid=0`, `u:r:kernel:s0`, SELinux Permissive, and full
  effective capabilities.
- **After (1.25.012)**: `remove_waiter()` now uses `waiter->task`, so in PI scenarios where
  `current` ≠ `waiter->task` it no longer writes the wrong task's `pi_blocked_on`, and the
  vulnerability's precondition is removed.

> Not provided in this document: complete logs, kernel addresses, serial numbers, ADB keys, heap
> parameters and exploit source code.

---
