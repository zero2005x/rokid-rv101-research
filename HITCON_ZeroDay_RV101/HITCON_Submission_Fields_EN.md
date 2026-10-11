# HITCON ZeroDay Report Field Draft

> 中文版：[HITCON_Submission_Fields.md](HITCON_Submission_Fields.md)

> Organized according to the actual fields on the report page after login on 2026-08-22. Not yet submitted. Text in square brackets must be handled before sending.

## Title

```text
{Rokid} RV101 未修補 CVE-2026-43499 可由 ADB shell 提權至 root
```

## Organization

```text
[先請 HITCON 確認：Rokid／鼎榮匯股份有限公司（Rokid 樂奇台灣銷售與服務窗口）]
```

Note: RV101 firmware is maintained by Rokid's original manufacturer; the footer of the official Taiwan sales website lists 鼎榮匯股份有限公司 (unified number 60711556). It must not be implied that 鼎榮匯 wrote or introduced the upstream kernel vulnerability.

## Introduction

```text
在本人所有且已授權測試的 Rokid Glasses RV101 上，已實證既有 Linux kernel 漏洞 CVE-2026-43499 可由 uid 2000 ADB shell 提升為執行期 uid 0、完整有效 capabilities 與 SELinux Permissive。
```

## Type

```text
Kernel 提權 (Kernel Privilege Escalation)
```

## Risk

```text
高
```

Scoring note: the CVSS 3.1 listed by the kernel.org CNA is `7.8 High (AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H)`. The currently demonstrated entry point in this case is an authorized uid 2000 ADB shell; whether physical access, Developer Options, ADB RSA authorization, and an unlocked bootloader are necessary conditions has not yet been fully isolated and verified, so please have HITCON re-check based on the actual preconditions.

## Related URLs

```text
https://www.rokid.com.tw/zh-TW/products/rokid-glasses
https://nvd.nist.gov/vuln/detail/CVE-2026-43499
https://git.kernel.org/stable/c/3bfdc63936dd4773109b7b8c280c0f3b5ae7d349
```

The first URL is used only to identify the tested product sold in Taiwan; the vulnerability is in the device firmware's Linux kernel, not in that web page itself.

## Description

The following content can be pasted directly into the platform; please complete all `[to be filled]` before sending:

---

### I. Authorization and Test Scope

This test was conducted only on a Rokid Glasses RV101 that I own / am explicitly authorized to test. No third-party devices, services, or accounts were tested, and no third-party personal data was obtained or retained. Validation used non-destructive methods; no persistent firmware was modified, and no attempt was made to defeat Secure Boot, AVB, or dm-verity.

### II. Affected Product and Version

- Product: Rokid Glasses RV101 (system model `RG-glasses`; Android device `glasses`)
- Firmware: `1.22.009-20260710-150201`
- Build fingerprint: `Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys`
- Android: 12 (SDK 32)
- Android security patch: `2024-07-05`
- Kernel: `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201`
- Number of units tested: 1

### III. Nature of the Vulnerability and Its Relationship to the Existing CVE

The kernel root cause used in this case is the already-assigned `CVE-2026-43499`, a use-after-free (CWE-416) in the futex PI / rt-mutex `remove_waiter()` path. I do not claim that this is a new CVE, nor do I claim that this case is a zero-day. The added value of this report is: confirming that the vulnerability can be turned into a working uid 0 root shell on the above RV101 firmware, and not merely a crash or DoS.

Offline disassembly comparison of `remove_waiter()` in the RV101 shipped kernel shows that it does not contain the corresponding stable fix. The complete disassembly and exploit engineering details can be provided to HITCON / the vendor for validation through a secure private channel, but will not be placed in fields that may become public in the future.

### IV. Demonstrated Privilege Transition

BEFORE:

```text
# getenforce
Enforcing

# exploit entry privilege
startup: pid=[REDACTED] uid=2000 euid=2000 gid=2000
```

AFTER:

```text
# whoami
root

# id
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0

# id -Z
u:r:kernel:s0

# getenforce
Permissive

# grep -E '^(Uid|Gid|CapInh|CapPrm|CapEff|CapBnd):' /proc/self/status
Uid:    0       0       0       0
Gid:    0       0       0       0
CapInh: 0000000000000000
CapPrm: 000001ffffffffff
CapEff: 000001ffffffffff
CapBnd: 000001ffffffffff
```

Non-destructive proof includes: creating an empty proof file in `/data` (root:root), and confirming the ability to list the mode 0700 root-only `/data/adb`. `system_ext`, `vendor`, and `vendor_dlkm` remain read-only mounts after the exploit.

### V. Reproduction Overview

1. On an RV101 of the same version, obtain an ADB shell from an authorized host; confirm that the initial `id` is uid 2000 and that SELinux is Enforcing.
2. Place the private validation PoC compiled for that firmware into `/data/local/tmp` and run it from the uid 2000 shell.
3. The PoC triggers the futex PI / rt-mutex stack UAF of CVE-2026-43499, and through controlled kernel-memory modification disables SELinux enforcement and modifies the current process credentials.
4. In the newly spawned shell, run `whoami`, `id`, `id -Z`, `getenforce`, and the `/proc/self/status` capability check, obtaining the AFTER output listed above.
5. Stop testing after performing the non-destructive proof file and read-only mount checks.

The complete PoC ELF, source, offsets, KASLR, and kernel addresses are not attached to the initial platform report; if HITCON review or vendor reproduction requires them, I am willing to provide them after confirming that they will not be published automatically and that an appropriate private transfer method exists. The latest PoC succeeds roughly 50% per boot on the tested firmware, and on failure a reboot and retry may be required; this reliability figure comes from a limited number of research runs and does not represent a statistical guarantee.

### VI. Impact

It has been confirmed that the attacking process obtains uid 0 at runtime, full effective Linux capabilities, and SELinux Permissive, thereby crossing the Android shell and kernel / root security boundary. This is sufficient to compromise the confidentiality and integrity of data within the running system.

There is currently no evidence supporting persistence across reboots, Secure Boot / AVB / dm-verity bypass, a writable system/vendor, remote exploitation, direct exploitability by any Android App, or all RV101 firmware being affected.

### VII. Attack Preconditions

The demonstrated entry point is a uid 2000 ADB shell. USB debugging / ADB was enabled on the test device at the time, and the bootloader state was unlocked / orange; these are test-environment observations and do not amount to proof that they are all necessary conditions for the CVE. Whether it can be reached from a normal App context, whether physical access / an existing ADB RSA authorization is required, and whether bootloader unlocking is necessary all still await isolated verification.

### VIII. Items Requested for Coordination

1. Please confirm which RV101 firmware versions are affected by CVE-2026-43499.
2. Please backport the kernel.org stable fix and provide the fixed firmware version and timeline.
3. Please confirm the patching coordination channel between the Taiwan sales / service contact window and Rokid's original manufacturer.
4. Please position this case as an RV101 affected-product / exploit validation of an existing CVE; if a product-specific weakness that can be fixed independently is separately confirmed, evaluate it separately.
5. Please do not publish the exploit source, binary, offset, or raw log before patching coordination is complete.

Date of discovery / first complete success evidence: 2026-08-22 (UTC+8).  
Direct vendor notification status: [to be filled: 尚未寄送／已於 YYYY-MM-DD 寄送，ticket ...].

---

## Remediation Recommendations

```text
1. 將 CVE-2026-43499 對應的 kernel.org stable 修補 backport 至 RV101 5.10 kernel，並由 OTA 發布。
2. 修補後在同版硬體重跑私密 PoC；預期 UAF trigger 無法再建立 dangling pi_blocked_on／controlled write，且 shell 維持 uid 2000、SELinux Enforcing。
3. 獨立檢查 uid 2000 對 /sys/kernel/tracing/printk_formats 的可讀性；此條件在本 exploit chain 中被用於 KASLR defeat，屬可獨立 harden 的候選弱點，但尚不主張為新的 CVE。
4. 重新檢視 ADB 預設啟用與授權政策，並更新 Android security patch level。
```

## Uploaded Images

There are currently no native screenshots that can be safely uploaded. Recommended preparation before sending:

1. `IMG#1`: firmware, Android security patch, kernel (serial number redacted).
2. `IMG#2`: BEFORE: uid 2000, `u:r:shell:s0`, Enforcing.
3. `IMG#3`: AFTER: uid 0, `u:r:kernel:s0`, Permissive, full capability mask.

Screenshots of the raw run log must not be uploaded, to avoid leaking kernel addresses, offsets, host paths, or the device serial number.

## Anonymous Reporting

```text
[由研究者決定；若希望公開後獲得署名，通常不要勾匿名，並先確認平台顯示的帳號名稱是否合適。]
```
