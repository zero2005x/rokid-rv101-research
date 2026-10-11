# Rokid Glasses RV101 Local Kernel Privilege Escalation Report

> 中文版：[HITCON_RV101_Report_ZH-TW.md](HITCON_RV101_Report_ZH-TW.md)

> Submitted to: HITCON ZeroDay vulnerability review team / notified organization  
> Document status: Draft, not yet submitted  
> Confidentiality level: Restricted; this document deliberately omits weaponization details  
> Researcher: [To be filled: name or alias / contact email]

## 1. Summary

On a Rokid Glasses RV101 that I own and am authorized to test, I have successfully demonstrated that the shipped firmware `1.22.009-20260710-150201` can be affected by the existing Linux kernel vulnerability `CVE-2026-43499`. The attack starts from an Android ADB uid 2000 shell with SELinux Enforcing, and ultimately obtains a working runtime uid 0 root shell, full effective Linux capabilities, the `u:r:kernel:s0` context, and SELinux Permissive.

This is not merely a version determination based on a crash / DoS, but an actual crossing of a security boundary. However, this case does not claim persistent root, Secure Boot / AVB / dm-verity bypass, or remote exploitation, nor does it claim that CVE-2026-43499 was introduced by Rokid or that a second CVE should be assigned.

## 2. Tested Product

| Field | Observed Value | Evidence Status |
|---|---|---|
| Product | Rokid Glasses RV101; system model `RG-glasses` | CONFIRMED |
| Device codename | `glasses` | CONFIRMED |
| Firmware | `1.22.009-20260710-150201` | CONFIRMED |
| Build fingerprint | `Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys` | CONFIRMED |
| Android | 12 / SDK 32 | CONFIRMED |
| Security patch | `2024-07-05` | CONFIRMED |
| Kernel | `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` | CONFIRMED |
| Test scope | 1 device owned by / authorized to me | CONFIRMED (researcher declaration) |

Public / platform-version documents do not disclose the device serial number; the immutable raw evidence still retains the original serial number for private verification.

## 3. Vulnerability Classification and Relationship to the Existing CVE

- Type: Kernel Privilege Escalation
- CVE: CVE-2026-43499 (already existing)
- CWE: CWE-416 Use After Free
- Component: Linux kernel futex PI / rt-mutex `remove_waiter()`
- Boundary: uid 2000 `u:r:shell:s0` / Enforcing → uid 0 `u:r:kernel:s0` / Permissive

The kernel.org CVE description states that in the proxy-lock rollback scenario, `waiter::task` is not equal to `current`, yet the old implementation operated on `current`, which could leave the waiter task's `pi_blocked_on` uncleared and thus leave a dangling pointer behind. The CNA CVSS 3.1 is 7.8 High.

Offline disassembly of the RV101 kernel image shows that the corresponding function does not contain the stable fix; the observed behavior of the successful exploit is also consistent with that root cause. Therefore:

- The CVE exists in the tested RV101 firmware: CONFIRMED.
- The successful chain exploits the same CVE: STRONGLY SUPPORTED.
- This case requires a new CVE: not claimed.
- There is an additional Rokid-specific new vulnerability: not yet confirmed.

## 4. Attack Preconditions

| Condition | Classification | Status |
|---|---|---|
| Local code execution at uid 2000 shell | Entry point of the successful run | CONFIRMED |
| ADB / USB debugging enabled | Test environment | CONFIRMED |
| Authorized ADB host | Whether required every time is still to be confirmed | UNKNOWN |
| Physical USB access | Whether it is the only entry point is still to be confirmed | UNKNOWN |
| Bootloader unlocked / orange | Test environment | CONFIRMED; not proven to be a necessary condition |
| Triggerable by a normal Android App | Not tested | UNKNOWN / not claimed |
| Network access | Not seen used by the chain; no formal isolation test performed | INFERRED not necessary |
| Root persists after reboot | Not tested | UNKNOWN / not claimed |

## 5. Technical Summary

Described at a level that avoids weaponization, the successful chain is:

```text
CVE-2026-43499 kernel stack UAF
→ controlled kernel-memory modification
→ disable SELinux enforcement
→ modify target process credentials
→ uid 0, full capabilities, root shell
```

The chain additionally uses the environmental condition that uid 2000 can read `/sys/kernel/tracing/printk_formats` to obtain the information needed for KASLR. This readability can be independently patched / hardened and is labeled "Potential independently fixable vulnerability — requires vendor validation", but it is not currently claimed to have an independent CVE root cause.

## 6. Success Evidence

### BEFORE EXPLOIT

```text
Enforcing
uid=2000(shell) gid=2000(shell) ... context=u:r:shell:s0
startup: pid=[REDACTED] uid=2000 euid=2000 gid=2000
```

### AFTER EXPLOIT

```text
root
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
u:r:kernel:s0
Permissive
Uid:    0       0       0       0
Gid:    0       0       0       0
CapInh: 0000000000000000
CapPrm: 000001ffffffffff
CapEff: 000001ffffffffff
CapBnd: 000001ffffffffff
```

Non-destructive privilege verification was additionally performed by creating an empty `/data/root_proof` (root:root) and listing the mode 0700 `/data/adb`. The dm-verity-protected `system_ext` / `vendor` / `vendor_dlkm` remain `ro`.

## 7. How to Reproduce

The initial platform report provides a verifiable process, and does not provide publicly weaponizable content:

1. Prepare an RV101 of the same model and the same firmware, and record the initial `id`, `id -Z`, `getenforce`, and capabilities from a uid 2000 ADB shell.
2. Run the private PoC built for this firmware.
3. After the PoC succeeds, record `whoami`, `id`, `id -Z`, `getenforce`, `/proc/self/status` capabilities, and the process SELinux context.
4. Perform the non-destructive proof file and mount read-only checks.
5. Compare against the BEFORE / AFTER values in this report.

The PoC ELF, source, sensitive offsets, kernel addresses, and the complete raw run log can be provided privately to reviewers or the vendor for reproduction after the platform confirms confidential handling and postponed publication.

## 8. Impact and Risk

Runtime uid 0 together with full effective capabilities and SELinux Permissive means the attacking process has crossed the Android shell and kernel / root boundary, and can compromise the confidentiality and integrity of data within the running operating system. Availability may also be affected by a root process, but no destructive validation was performed in this research.

It is recommended that the platform fill in the initial risk as "High", on the grounds that the kernel.org CNA's CVSS 3.1 is 7.8 High; HITCON should re-check based on the ADB / physical access preconditions that are still to be confirmed.

## 9. Unsupported Claims

| Claim | Evidence Status |
|---|---|
| persistent root | UNKNOWN; not claimed |
| Secure Boot / AVB / dm-verity bypass | No evidence; not claimed |
| system/vendor writable | The observation is the opposite; still `ro` |
| remote exploit | No evidence; not claimed |
| Exploitable by any App | Not tested; not claimed |
| All RV101 firmware affected | Only 1 device / 1 version tested; not claimed |
| New CVE / zero-day | The CVE already exists; not claimed |

## 10. Remediation Recommendations

1. Backport the kernel.org stable fix corresponding to CVE-2026-43499 to the RV101 5.10 branch, and release it via OTA.
2. Compare before and after the patch using the same private PoC; after patching, uid 2000 and SELinux Enforcing should be maintained, and the UAF controlled write should no longer occur.
3. Restrict uid 2000's read access to tracefs pointer material (especially `/sys/kernel/tracing/printk_formats`).
4. Review the ADB default enablement / authorization policy and the Android security patch update cycle.

## 11. Coordination Requests

- Please have HITCON first confirm whether this case falls within the Taiwan-region acceptance scope, and whether it should be assigned to Rokid's original manufacturer or to the Taiwan sales / service contact window.
- Please confirm that Rokid has a reasonable patching window before the platform publishes automatically; if publication cannot be postponed, please do not first require the upload of exploit source / binary / offset / raw logs.
- If the vendor publishes an advisory or release note, I would like the researcher name / alias `[to be filled]` to be acknowledged for verifying that CVE-2026-43499 is exploitable on RV101.
- If the vendor additionally confirms a Rokid-specific weakness that can be fixed independently, then evaluate the CVE and researcher attribution separately.

## 12. Evidence Cross-Reference

| Claim | Evidence | Confidence |
|---|---|---|
| Tested firmware / kernel | `01_Attachments/A01_Device_Baseline_REDACTED.txt` | CONFIRMED |
| Initial uid 2000 / Enforcing | A01 + `A02_Privilege_Transition_REDACTED.txt` | CONFIRMED |
| uid 0 / full caps / Permissive | A02 | CONFIRMED |
| system/vendor remain read-only | A02 | CONFIRMED |
| Exploit is CVE-2026-43499 | Private disassembly + raw run logs | STRONGLY SUPPORTED |
| persistence / boot-chain bypass | None | UNKNOWN / not claimed |
