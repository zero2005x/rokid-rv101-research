# TWCERT "De-identified Supporting PDF" Content Checklist for Upload

> 中文版：[TWCERT_佐證PDF_內容清單.md](TWCERT_%E4%BD%90%E8%AD%89PDF_%E5%85%A7%E5%AE%B9%E6%B8%85%E5%96%AE.md)

> Purpose: to assemble a supporting PDF (or ZIP) that **can be uploaded directly to the TWCERT/CC CVE report form**.
> Corresponding form field: "4. Supporting material upload" — **files limited to under 10MB, formats limited to PDF, ZIP**.
> This checklist is consistent with `Evidence_Index.md`, `Evidence_Checklist.md` and `Redaction_Checklist.md`,
> but is stricter than the latter: **it includes only de-identified content that is "safe to publish/submit"**, and removes the device serial number and partition layout.

---

## 0. Objective and constraints

| Item | Value |
|---|---|
| Artifact | A single PDF (text-based, under 1MB, far below the 10MB limit) |
| Suggested filename | `Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf` |
| Locale | Traditional Chinese section titles + explanations; command output **is preserved verbatim in the original English** |
| Positioning | "Affected-product confirmation + exploitability validation" of the existing CVE-2026-43499; **not a new CVE, not a zero-day, not remote** |

> If you do not want to produce a PDF, you can also assemble the sections below into a single Markdown/HTML file and have a tool output it;
> `weasyprint` and `chromium` are already available locally for offline generation (see §6 Output method).

---

## 1. PDF section content checklist (in order)

### Cover / statement page
- Title: `Rokid Glasses RV101 — CVE-2026-43499 受影響產品佐證（去識別化版）`
- One-sentence statement: "This document is the affected-product confirmation and local privilege escalation exploitability validation of the existing CVE-2026-43499 (GhostLock) on the Rokid Glasses RV101; no new CVE, zero-day or remote exploitation is claimed."
- Reporter: 〔姓名/alias〕; disclose publicly: No
- Date: 2026-08-25
- Evidence correspondence: the empirical attachment for form field 3 (vulnerability description/conditions/method/CVSS/CWE).

### §1 Tested product identification
- Sources: `02_Device_Information/02_identity_build.txt` (E1), `03_security_baseline.txt` (E2), `04_kernel_runtime.txt` (E3)
- Verbatim output retained:
  - `ro.product.manufacturer=Rokid`, `ro.product.model=RG-glasses`, `ro.product.device=glasses`, `ro.board.platform=neo`
  - `ro.build.version.release=12`, `ro.build.version.sdk=32`, `ro.build.id=SKQ1.240613.001`
  - `ro.build.fingerprint=Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys`
  - `ro.build.type=user`, `ro.build.tags=release-keys`
  - `uname`/`/proc/version`: `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201 … aarch64`
- De-identification: **do not list the device serial number**; delete the `__collector_last_exit=*` line.
- Evidence strength: CONFIRMED.

### §2 Security baseline (pre-exploit environment)
- Source: `03_Pre_Exploit_State/pre_exploit_security_baseline.txt`
- Verbatim output retained:
  - `getenforce` → `Enforcing`
  - `ro.build.version.security_patch=2024-07-05` (build + vendor)
  - `ro.debuggable=0`, `ro.secure=1`, `ro.adb.secure=1`
  - `ro.boot.veritymode=enforcing`, `ro.boot.avb_version=1.2`, `ro.boot.selinux=enforcing`, `selinux_enforce=1`
  - `ro.boot.verifiedbootstate=orange`, `ro.boot.vbmeta.device_state=unlocked`, `ro.boot.flash.locked=0`
    (**annotate**: bootloader unlock is an "environmental condition of the test environment" and has not been proven to be a necessary precondition for CVE-2026-43499)
  - `persist.sys.usb.config=adb,diag` (annotate: the acquisition path for the uid-2000 shell)
- De-identification: delete `__collector_last_exit=*`.

### §3 Initial privilege state (pre-exploit uid/SELinux)
- Source: `03_Pre_Exploit_State/pre_exploit_uid_selinux.txt`
- Verbatim output retained (one of the core proofs):
  - `uid=2000(shell) gid=2000(shell) groups=…,3012(readtracefs) context=u:r:shell:s0`
    (`3012(readtracefs)` is precisely the privilege source of the RS-1 KASLR leak, **deliberately retained** to support the incidental findings)
- De-identification: the `policy_bytes/policyvers/mls/policy hash` lines may be deleted (irrelevant to this case); delete `__collector_last_exit=*`.

### §4 Vulnerability root cause and CVE comparison (version-comparison proof)
- Sources: `10_Upstream_CVE_Comparison/advisory_nvd_osv_record_20260824.md`, `CVE_Relationship_Assessment.md`
- Content (all public information, no de-identification needed):
  - CVE-2026-43499 summary: `rtmutex: Use waiter::task instead of current in remove_waiter()`
  - Root cause in one sentence: remove_waiter() mistakenly operates on `current` during proxy-lock rollback, the waiter task's `pi_blocked_on` is not cleared → dangling pointer UAF
  - Affected range: introduced in 2.6.39; the 5.10.x branch **fixed in 5.10.261**
  - Tested kernel `5.10.209-perf` < `5.10.261` → **affected** (established by version comparison alone)
  - Fix commit: `3bfdc63936dd`; CWE-416; CNA CVSS 3.1 `AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H` (7.8 High)
  - Upstream references: Red Hat RHSB-2026-010, AlmaLinux, CloudLinux, NVD links

### §5 Privilege escalation success state (post-exploit, core proof)
- Source: `05_Post_Exploit_State/post_exploit_state_REDACTED.txt` (E9, already de-identified)
- Verbatim output retained:
  - BEFORE: `getenforce` → `Enforcing`; `startup: pid=… uid=2000 euid=2000 gid=2000`
  - AFTER: `whoami` → `root`
  - `id` → `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0`
  - `id -Z` → `u:r:kernel:s0`
  - `getenforce` → `Permissive`
  - `/proc/self/status`: `Uid: 0 0 0 0`, `Gid: 0 0 0 0`, `CapPrm/CapEff/CapBnd = 000001ffffffffff` (full effective capabilities)
  - Non-destructive validation: create `/data/root_proof` (root:root), read the root-only `/data/adb` (0700)
  - Mount state (**retain only the concluding sentence**): `system_ext`/`vendor`/`vendor_dlkm` remain `ro` (dm-verity) — they did not become writable due to root
- De-identification (**this file still needs further processing**):
  - ❌ Delete the **device serial number `<DEVICE_SERIAL>`** on line 2 of the header and at the end of the file (do not include it in the public version)
  - ❌ Delete the `ls -l /dev/block/by-name` block and the **partition layout** items such as `mmcblk0p51/p76/p80`, `dm-13/15/16` (the Redaction_Checklist explicitly requires keeping these in raw/confidential)
  - ❌ Delete the enumeration of writable mounts for `/mnt/vendor/persist|qmcs|logfs` (also part of the partition layout)

### §6 (optional) Incidental findings summary RS-1~RS-9
- Source: `RV101_Vulnerability_Report.md` §Potential Rokid-Specific Security Issues
- Content: a table listing the **one-sentence description + status** of RS-1~RS-9, all annotated "requires independent vendor assessment; no new CVE is claimed".
- Highlight in particular: RS-8 (the entire privacy-indicator control chain during camera use is in the REE and can be suppressed by root through three paths: sysfs/Binder/session_open)
  and RS-9 (wearing/proximity state can be forged) — **both are "after obtaining root" or defense-in-depth issues**.
- De-identification: do not attach the original text of `led_test.log`/`pathd_test.log` (although they contain no addresses themselves, they include the root-injection script path and
  the `/data/local/tmp/.ghostlock_root.sh` name, which are exploit internals); describe them only in words as "validated on device".

### §7 Integrity and hash statement
- Source: `12_Hashes/SHA256SUMS.txt`
- Content: a one-page table mapping each PDF section to the original evidence filename + SHA-256 (the first 12 characters suffice), for the reviewing party to cross-check afterwards.
- Note: the complete hashes of the raw evidence remain in the private version; the public PDF lists only the de-identified correspondence table.

---

## 2. Must-remove checklist (check each item before submission)

| # | Item | Where it appears |
|---|---|---|
| R1 | Device serial number `<DEVICE_SERIAL>` | post_exploit_state_REDACTED.txt header/footer, 00_adb_devices.txt |
| R2 | Kernel virtual addresses / KASLR base-slide / `init_cred` / `selinux_state` / fops / `0xffff…` | raw run log (**does not go into the PDF**) |
| R3 | Exploit source code, ELF (ghostlock_rv101), `target_rv101.h` offsets, reclaim/`pselect`/`rb_erase` punch parameters | **never goes into the PDF** |
| R4 | Complete reproduction commands that reveal the above details (`.ghostlock_root.sh` injection flow, FOPS window) | **never goes into the PDF** |
| R5 | Partition layout: `/dev/block/by-name`, `mmcblk0pN`, `dm-N`, `/mnt/vendor/persist|qmcs|logfs` | post_exploit_state_REDACTED.txt §MOUNT |
| R6 | Researcher host paths / Windows username (`C:\Users\<USER>\…`) | operation notes document (**does not go into the PDF**) |
| R7 | Third-party keys/secrets (OTA keys, ADB keys, if they appear in any log) | all excluded |
| R8 | Full partition dumps / `ls -l /dev/block/by-name` | all excluded |

## 3. Safe-to-retain checklist

- `id` / `id -Z` / `getenforce` / `Uid|Gid|Cap*` lines (core proof)
- Firmware fingerprint, Android version, kernel version string, security patch level
- The concluding sentence that "system/vendor remained read-only"
- The `/data/root_proof` non-destructive demonstration
- CVE-2026-43499 and the affected/unpatched version comparison
- `uid=2000 … context=u:r:shell:s0` (including the `readtracefs` gid, supporting RS-1)

---

## 4. Pre-submission verification steps (run after producing the PDF)

```bash
# 1) Confirm there is no sensitive content (run on the text extracted from the PDF)
pdftotext Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf - 2>/dev/null | \
  grep -nE '<DEVICE_SERIAL>|0xffff|slide=|init_cred|target_rv101|C:\\\\Users|mmcblk0p|dm-1[0-9]|/dev/block/by-name|ghostlock_root' \
  && echo "!! 發現需再移除的內容" || echo "OK：無敏感內容"

# 2) Confirm the file size and format
ls -l Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf   # should be < 1MB
file  Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf   # PDF document

# 3) Cross-check the original evidence hashes (not required, but recommended)
sha256sum -c 12_Hashes/SHA256SUMS.txt 2>&1 | tail -1
```

---

## 5. Output method (can be completed offline on this machine)

Since pandoc is not available locally, but `weasyprint` (or `chromium --headless`) is, the recommended path is: **Markdown/HTML → PDF**.

```bash
# Method A: HTML + weasyprint (supports CJK, font Droid Sans Fallback)
#  1. Per §1, assemble the sections into evidence.html (wrap command output in <pre>)
#  2. weasyprint evidence.html Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf

# Method B: HTML + Chromium headless
#  chromium --headless --no-sandbox --print-to-pdf=out.pdf --no-pdf-header-footer evidence.html
```

> The alignment of command output (tabs, spaces) must use a monospace font (`<pre>` + `font-family: monospace`),
> so that `Uid:\t0\t0\t0\t0` in `/proc/self/status` is preserved verbatim.

---

## 6. One-page mapping (evidence file → PDF section → de-identification action)

| Evidence file (Evidence_Index ID) | PDF section | De-identification action |
|---|---|---|
| 02_identity_build.txt (E1) | §1 | remove serial, remove `__collector_last_exit` |
| 03_security_baseline.txt (E2) | §2 | remove `__collector_last_exit`; annotate bootloader unlock as an "environmental condition" |
| 04_kernel_runtime.txt (E3) | §1 | retain uname/version; `cmdline: Permission denied` may be removed |
| pre_exploit_uid_selinux.txt (E4) | §3 | remove the policy hash line, remove `__collector_last_exit` |
| pre_exploit_security_baseline.txt | §2 | remove `__collector_last_exit` |
| post_exploit_state_REDACTED.txt (E9) | §5 | **further remove serial + partition layout + writable-mount enumeration** |
| advisory_nvd_osv_record (D6) | §4 | none (all public information) |
| RV101_Vulnerability_Report.md RS table | §6 | text summary only, do not attach the original log |
| 12_Hashes/SHA256SUMS.txt | §7 | list only the correspondence table (serial removed) |

---

## 7. Final check before sending to TWCERT

- [ ] Filename `Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf`, < 10MB, PDF format
- [ ] No serial number, no partition layout, no kernel addresses, no exploit details, no host paths (all of §2 passes)
- [ ] §1~§5 complete, verbatim output, readable standalone (the reviewing party can judge without seeing private material)
- [ ] Explicitly annotated "existing CVE, not a new CVE / not a zero-day / not remote"
- [ ] If TWCERT requests the raw PoC, prepare a separate private PDF/ZIP and provide it privately after confirming confidential handling (**do not upload it to the public form**)
