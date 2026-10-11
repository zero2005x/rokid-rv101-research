# Relationship to CVE-2026-43499

## 1. Is CVE-2026-43499 already assigned?

**Yes.** CVE-2026-43499 ("GhostLock") is a publicly-assigned Linux kernel vulnerability
in the futex PI / rt-mutex locking subsystem. Public references include:

- Red Hat — [RHSB-2026-010 "Locking Subsystem Privilege Escalation" (CVE-2026-43499, CVE-2026-53166)](https://access.redhat.com/security/vulnerabilities/RHSB-2026-010)
- CloudLinux — [GhostLock (CVE-2026-43499) local root exploit kernel update](https://blog.cloudlinux.com/ghostlock-cve-2026-43499-local-root-exploit-kernel-update-for-cloudlinux/)
- AlmaLinux — [GhostLock (CVE-2026-43499) kernel privilege escalation](https://almalinux.org/sv/blog/2026-07-09-ghostlock/)
- Nebusec — [IonStack part II: GhostLock, a stack-UAF present in all Linux distributions for 15 years](https://nebusec.ai/research/ionstack-part-2/)
- Huawei Cloud — rtmutex UAF LPE / container-escape notice (CVE-2026-43499)

A related CVE (CVE-2026-53166) is also referenced in the Red Hat advisory.

## 2. Does the demonstrated kernel bug appear to be the same underlying vulnerability?

**Yes — STRONGLY SUPPORTED / CONFIRMED as present.**

- The exercised primitive is the rt-mutex `remove_waiter()` stack UAF: `remove_waiter()`
  clears `current->pi_blocked_on` instead of `waiter->task->pi_blocked_on`, leaving a
  dangling `pi_blocked_on` reference to a freed kernel-stack `rt_mutex_waiter`.
- The RV101 kernel (`5.10.209-perf`, branch `gd908d71a0404`) **lacks the upstream fix**
  (mainline commit `3bfdc63936dd`). This was verified by disassembling
  `remove_waiter()` in the shipped `kernel` image (see `research/ghostlock_refs/remove_waiter.disasm.txt`).
- The exploitation behavior (kernel-stack UAF → controlled write → SELinux disable →
  credential overwrite) matches the GhostLock description.

**Conclusion:** This is exploitation of the **existing** CVE-2026-43499 on the RV101,
not a separately-rooted kernel bug.

## 3. Does the RV101 require a separate fix beyond backporting the upstream patch?

**Primary remediation: backport the upstream fix.** No evidence indicates a distinct
kernel fix is required for the root cause.

However, the exploit chain *relies on* and *exposes* several conditions that are not part
of the upstream CVE and could be hardened independently (see §4). These are defense-in-depth
items, not a claim that a second kernel fix is required.

## 4. Are there independently fixable vendor-specific weaknesses?

**Candidate list (each requires vendor validation):**

| # | Weakness | Role in chain | Status |
|---|---|---|---|
| RS-1 | Kernel address disclosure via `/sys/kernel/tracing/printk_formats` readable by the `shell` user (shell holds `readtracefs`, gid 3012) | Directly used to defeat KASLR | **Potential independently fixable weakness — requires vendor validation** |
| RS-2 | `kernel.dmesg_restrict=0` | Info-disclosure hardening | Candidate — requires vendor validation |
| RS-3 | `unprivileged_bpf_disabled=0` | Local attack-surface hardening | Candidate — requires vendor validation |
| RS-4 | `/dev/kgsl-3d0`, `/dev/i2c-1` mode 0666 | Hardware attack surface | Candidate — requires vendor validation |
| RS-5 | `GateServiced` root + `0.0.0.0:8341` | Network-exposed root service | Candidate — requires vendor validation |
| RS-6 | ADB persistent + (reported) preloaded auth keys | Defines uid-2000 entry | Candidate — preloaded-key claim needs re-verification |
| RS-7 | OTA auth weak hash / keys in props | Supply-chain hardening | Candidate — requires vendor validation |

None of these is asserted to be a **confirmed new CVE**. They are observations for
Rokid's independent evaluation.

## 5. What should the disclosure request?

| Request | Justification |
|---|---|
| Affected-product acknowledgement | Confirm RV101 (and which firmware) is affected by CVE-2026-43499 |
| Patch confirmation | Backport `3bfdc63936dd`; identify fixed firmware version |
| Security advisory | List RV101 in the advisory/release notes |
| Researcher credit | For reporting + RV101 exploit validation |
| Separate CVE evaluation | Only for any RS-* item Rokid confirms as an independently fixable vulnerability |

**Preferred wording:**

> "The current evidence demonstrates exploitation of CVE-2026-43499 on Rokid Glasses
> RV101. I am not asserting that the existing vulnerability requires a second CVE.
> However, I request vendor review of any independently fixable Rokid-specific weaknesses
> identified during exploitation research."
