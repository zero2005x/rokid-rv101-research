# TWCERT/CC CVE Vulnerability Report — Completed Content (Rokid RV101)

> 中文版：[TWCERT_CVE通報_填寫內容.md](TWCERT_CVE%E9%80%9A%E5%A0%B1_%E5%A1%AB%E5%AF%AB%E5%85%A7%E5%AE%B9.md)

> Corresponding form: https://www.twcert.org.tw/tw/sp-cve-declare-1.html
> Document status: **draft, not yet submitted**
> Confidentiality level: Restricted (the supporting material submitted should be a de-identified version, containing no exploit source / offsets / raw kernel addresses)
> Positioning statement: this case reports the **affected-product confirmation and exploitability validation of an existing CVE (CVE-2026-43499) on the Rokid Glasses RV101**;
> it is **not** a new CVE request, **not** a zero-day, and **not** a remote vulnerability.
> Suggested timing: first submit the initial report to the vendor (security@rokid.com) and obtain acceptance/entry into the coordination channel; use this TWCERT form for
> "coordination/tracking of affected products in the Taiwan market", submitting it after the vendor acknowledges (or as an escalation channel if there is no response in 90 days),
> so as to avoid conflicting with the vendor's coordination policy of "no disclosure before the official advisory".

---

## 1. Reporter information

| Field | Value |
|---|---|
| Reporter or organization name * | 〔請填寫：姓名 或 alias〕 |
| Organization | Independent security researcher (may be left blank) |
| E-mail * | 〔請填寫 E-mail〕 |
| Disclose name and organization | **No** (recommended not to disclose) |

---

## 2. Report description

| Field | Value |
|---|---|
| Source of vulnerability information * | **Self-discovered** (the "affected-product confirmation + exploitability" of the existing CVE was independently reverse-engineered and validated on device by the reporter) |
| Vulnerability discovery date * | **2026-08-22** (first on-device proof of uid 0; prior research dates back to 2026-01-31) |
| Affected product name * | Rokid Glasses RV101 (system model `RG-glasses`, platform codename `neo` / NEO LUNA-V2) |
| Affected product version * | Firmware `1.22.009-20260710-150201` (Android 12 / SDK 32, Linux kernel `5.10.209-perf`, security patch level 2024-07-05) |
| Product developer or vendor * | Rokid (杭州灵伴科技有限公司) |
| Product website * | https://global.rokid.com (Taiwan: https://www.rokid.com.tw ) |

---

## 3. Report content (1)

| Field | Value |
|---|---|
| Vulnerability type * | Local privilege escalation / kernel memory corruption (use-after-free) |
| Confidentiality * | High |
| Integrity * | High |
| Availability * | High |
| CVSS v3.1 Vector String * | `CVSS:3.1/AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H` (7.8 High) |
| CWE types * | CWE-416 (Use After Free) |

### Vulnerability description * (can be pasted directly; 1000-character limit)

```
The tested product, Rokid Glasses RV101 (firmware 1.22.009-20260710-150201, Android 12,
Linux kernel 5.10.209-perf), ships a kernel that does not include the upstream fix, and is
affected by the existing Linux kernel vulnerability CVE-2026-43499 ("GhostLock", an rtmutex
futex-PI stack use-after-free).

Root cause of the vulnerability: in the kernel locking subsystem, remove_waiter() on the
proxy-lock rollback path (rt_mutex_start_proxy_lock() called from futex_requeue()) operates
on current instead of waiter::task by mistake, so the waiter task's pi_blocked_on is not
cleared, leaving a dangling pointer to the already-freed kernel-stack rt_mutex_waiter that
can be used for UAF exploitation. Upstream fixed this in 5.10.261 (as well as the 5.15.212 /
6.1.175 and other branches); the tested device's kernel is 5.10.209-perf, lower than the
fixed version, and offline disassembly confirmed that remove_waiter() does not contain the fix.

Demonstrated result: starting from a low-privilege Android ADB shell (uid 2000, SELinux
Enforcing, context u:r:shell:s0), a runtime uid 0 root shell can be obtained reliably
(~50%/boot, with automatic retry), with full effective Linux capabilities and context
u:r:kernel:s0, and SELinux is changed from Enforcing to Permissive.

This case does not claim: persistent root, AVB/dm-verity or Secure Boot bypass, remote
exploitation, exploitability by any app, or that all firmware versions are affected; nor does
it claim that this is a new CVE.
```

### Specific conditions required to trigger the vulnerability * (can be pasted directly)

```
1. Local low-privilege code execution (demonstrated entry point: Android ADB shell,
   uid 2000).
2. Ability to read kernel tracing data to defeat KASLR (on the tested device,
   /sys/kernel/tracing/printk_formats is readable by the shell user; this is a separately
   patchable hardening issue, not the root cause of the core CVE).
3. SELinux initially Enforcing (the exploit changes it to Permissive during execution).

Environmental conditions (observed, not proven necessary): the tested device's bootloader is
unlocked (orange), and USB debugging (ADB) is enabled.
```

### Method to trigger the vulnerability * (can be pasted directly; not a weaponized description)

```
1. In a low-privilege shell, record the initial id / id -Z / getenforce / capabilities.
2. Run the private PoC (ELF) built for this firmware; its flow is: KASLR leak → heap object
   leak → trigger the CVE-2026-43499 stack UAF → reclaim the freed slot to forge a waiter →
   controlled kernel write → disable SELinux enforcement → overwrite the process credentials.
3. After completion, record whoami / id / id -Z / getenforce / the capabilities from
   /proc/self/status, and compare the before and after states.
The PoC source code, sensitive offsets and raw run log will be provided privately after
confidential-handling confirmation.
```

### Privileges required to trigger the vulnerability * (drop-down menu)

```
Low (local uid 2000 / Android `shell`, SELinux Enforcing; no user interaction required)
```

### Other notes (optional)

```
1. Under CVSS 3.1, this vulnerability is AV:L (local), PR:L (low privilege), AC:L, UI:N,
   with C/I/A all High, scoring 7.8 High.
2. Remediation recommendation: port the upstream fix (mainline 3bfdc63936dd or the
   corresponding 5.10.y backport) to the RV101 5.10.209-perf branch and release it via OTA;
   verify with the same PoC, comparing before and after the patch.
3. Incidental findings (require independent vendor assessment; no new CVE is claimed): kernel
   addresses can leak via tracefs (printk_formats), dmesg_restrict=0,
   unprivileged_bpf_disabled=0, /dev/kgsl-3d0 and /dev/i2c-1 are 0666, and the entire
   privacy-indicator (white light) control chain during camera use resides in the REE and can
   be suppressed or forged by root through three paths: sysfs / Binder / system properties.
   The above items were validated on device between 2026-08-22 and 08-24, but all of them are
   "after obtaining root" or defense-in-depth issues; whether they constitute separate
   vulnerabilities is for the vendor / reviewing body to judge.
```

---

## 4. Supporting material upload

- Recommended upload: **de-identified supporting PDF** (suggested filename `Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf`)
- Content should include: tested firmware / kernel version, comparison of CVE-2026-43499 against the fixed versions, privilege state before and after the exploit
  (uid 2000/Enforcing → uid 0/Permissive/full capabilities), and proof that system/vendor remained read-only.
- **Must not be uploaded**: exploit source code, the ELF, sensitive offsets, raw run logs containing kernel addresses, and the
  device serial number (if using the public version). A private version may be prepared separately and provided once TWCERT confirms confidential handling.

---

## Appendix: Suggested division of labor with the Rokid vendor report

| Channel | Suggested positioning of the content | Timing |
|---|---|---|
| Rokid (security@rokid.com) | Primary report: CVE-2026-43499 affected confirmation + the RS-1~RS-9 hardening list | **Send first** (for the initial report see `11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md`) |
| TWCERT/CC (this form) | Coordination/tracking: report of affected products in the Taiwan market, **explicitly marked as an existing CVE** | After vendor acknowledgment, or escalated if there is no response in 90 days |

> Reminder: both should avoid disclosing vulnerability details through public channels before Rokid publishes its official advisory.
