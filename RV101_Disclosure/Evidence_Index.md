# Evidence Index — Rokid RV101 LPE Disclosure

Every artifact below is keyed to its entry in `12_Hashes/SHA256SUMS.txt`. The
"staged" path is the copy inside this package; the "origin" path is where it lives
in the research repo. Device identifier is `<DEVICE_SERIAL>`; firmware/build is
`1.22.009-20260710-150201` unless noted otherwise.

| # | Artifact | SHA-256 (first 12) | Collected | Command / procedure | Raw/Redacted | Origin |
|---|---|---|---|---|---|---|
| E1 | `02_identity_build.txt` | `dc496cccc204` | 2026-08-12 23:07 CST | `getprop` (ro.product.*, ro.build.*, ro.soc.*) via adb collector | Raw | `assets/RV101_Validation/RV101_<DEVICE_SERIAL>_20260812_230700/` |
| E2 | `03_security_baseline.txt` | `c5a5d025466b` | 2026-08-12 23:07 CST | `getenforce` + `getprop` security/boot props | Raw | same dir |
| E3 | `04_kernel_runtime.txt` | `f529593d5400` | 2026-08-12 23:07 CST | `uname -a`; `/proc/version` | Raw | same dir |
| E4 | `17_selinux_runtime.txt` | `d65000958657` | 2026-08-12 23:07 CST | `id` + SELinux runtime status | Raw | same dir |
| E5 | `00_adb_devices.txt` | `eec62ea04a0b` | 2026-08-12 23:07 CST | `adb devices -l` | Raw | same dir |
| E6 | `run_w2_v960_143215.log` | `1b1953ee07c5` | 2026-08-22 14:32–14:39 CST | Exploit v9.60 run + post-exploit state capture | **Raw (sensitive)** | `reports/ghostlock_runlogs/` |
| E7 | `run_w2_demo_v960_142523.log` | `67a945eb19af` | 2026-08-22 14:25 CST | Exploit v9.60 demo run | **Raw (sensitive)** | `reports/ghostlock_runlogs/` |
| E8 | `run_w2_clean_v959_134613.log` | `aa3b05468234` | 2026-08-22 13:46 CST | First clean root (v9.59) | **Raw (sensitive)** | `reports/ghostlock_runlogs/` |
| E9 | `post_exploit_state_REDACTED.txt` | `dcc2f6eee67b` | 2026-08-22 (derived) | Redacted extract of E6 (addresses/exploit internals removed) | **Redacted** | derived from E6 |
| E10 | `GHOSTLOCK_HANDOFF_V13.md` | `a030cd2b0862` | 2026-08-22 | Research handoff (root achieved) | Raw | `reports/` |
| E11 | `GHOSTLOCK_ROOT_操作說明.md` | `df520e15c06e` | 2026-08-23 (updated) | Root operation notes v9.60 (+stdin troubleshooting note) | Raw | `reports/` |
| E12 | `06_Kernel_Logs/kernel_config_1.22.009.txt` | `d7b66eb69b2d` | 2026-08-21 | `/proc/config.gz` dump | Raw | disclosure evidence copy |
| E13 | `dmesg_122009.txt` | `e9cdb7ceae49` | (OTA extraction) | dmesg from firmware | Raw | `binaries/extracted_1.22/` |
| E14 | `dmesg_w2diag_105032.log` | `cc3525b108aa` | 2026-08-22 10:50 CST | dmesg during W2 diagnostic | Raw | `reports/ghostlock_runlogs/` |
| E15 | `ghostlock_rv101` (exploit ELF) | `26575a724704` | build 2026-08-22 14:24 CST | NDK r27d aarch64 build (SHA-1 in run log: `4eeb9817…`) | **Confidential** | `source_code/ghostlock_rv101/build/` |
| E16 | `led_test.log` (privacy-LED suppression validation) | `6ffb39798a69` | 2026-08-24 ~19:59 CST | Root-injected `.ghostlock_root.sh` captured `uid=0(root) … u:r:kernel:s0` then Steps 1–5: sysfs DAC override (`led_current` 2→0), Binder txn#9/#8 (cancel/sendEvent), `session_open=0` spoof → `init: … camera closed!`, plus exact-original safety restore | Raw (**non-sensitive** — no kernel addrs/offsets) | `reports/led_validation/led_test.log` |
| E17 | `scripts/led_test.sh` (reproduction script) | `2fbc82a4dc84` | 2026-08-24 | Exact root-injected script that produced E16; helper `scripts/run_led_orchestrator.sh` (`d85aa992622b`) retries/reboots and injects it in the FOPS window | Raw | `scripts/led_test.sh` |
| E18 | `pathd_test.log` (P-Sensor/wearing-state spoof — Path D) | `9865c70c0252` | 2026-08-24 ~23:34 CST | Root-injected `.ghostlock_root.sh` captured Path-D: camera-active baseline (brightness=255) then forged `is_take_on=0`/`is_spread=0`/`enforce_psensor=0` → **LED stayed 255** (no direct suppression) but state forgery confirmed (`is_take_on=0` held; `enforce_psensor=0` → init → PSOC sysfs → extcon → rt600 `proximity state = 0`); full restore | Raw (**non-sensitive**) | `reports/led_validation/pathd_test.log` |
| E19 | `scripts/pathd_test.sh` (reproduction script) | `e947dea2efd1` | 2026-08-24 | Exact root-injected script that produced E18; helper `scripts/run_pathd_orchestrator.sh` (`fc48c02ab316`) | Raw | `scripts/pathd_test.sh` |

## Notes

- E12 and `06_Kernel_Logs/device_config_1.22.009_live.txt` are byte-identical (`d7b66eb69b2d`); treat
  as the same live kernel-config artifact.
- E15 binary SHA-1 recorded in the successful run log (`4eeb98171ed21113130d5f2676a0bd283c8f56c7`)
  should be re-verified against the exact binary that produced E6 for the final
  package (SHA-1 + SHA-256 of the same file).
- E6 is the single most important artifact: it contains the complete before → after
  transition in one file. Treat it as immutable; do not edit in place.
- E16 (`led_test.log`) is a **secondary finding** artifact (privacy-LED suppression), not
  part of the LPE chain itself. It is non-sensitive (no kernel addresses, offsets, or
  exploit internals) and records the 2026-08-24 validation that converted the privacy-LED
  trust-boundary audit (`reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md`) paths A/B/C
  from theoretical to executed.

## Integrity note (recorded 2026-08-22)

The **in-place original** `reports/ghostlock_runlogs/run_w2_v960_143215.log` was observed
to be **actively appended** during packaging (grew 533 → 687 lines between 14:49 and
15:03 CST; the appended content was additional post-root process-listing output). The
**staged frozen snapshot** `confidential/raw_evidence/run_w2_v960_143215.log`
(SHA-256 `1b1953ee07c5…`, 533 lines) is therefore the authoritative point-in-time copy
used for this package. Before final submission, re-capture a stable, quiesced copy of the
original and re-hash.

## Documentation (added/updated 2026-08-22)

Not device evidence; listed for package completeness and provenance. Hashes in
`12_Hashes/SHA256SUMS.txt`.

| # | File | SHA-256 (first 12) | Purpose |
|---|---|---|---|
| D1 | `RV101_Disclosure/01_Executive_Summary/Executive_Summary_EN.md` | `cefda0ac82a3` | One-page English executive summary |
| D2 | `README_ORGANIZED.md` (repo root) | `89d526f5f272` | Master project index — updated to reflect root achieved + camera-LED research (2026-08-23) + privacy-LED suppression validation (2026-08-24) |
| D3 | `reports/CURRENT_STATUS_20260822.md` | `d6c521d77017` | Current-status snapshot; supersedes stale "not yet root" statements |
| D4 | `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` | `f468da8d2cfa` | Camera white-LED mechanism reverse-engineering (dual path, live verified 2026-08-23; suppression validation §4.1 + P-Sensor non-gating §4.2 added 2026-08-24) |
| D5 | `reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md` | `dab9c41c5bb7` | Privacy-LED / P-Sensor trust-boundary audit (paths A/B/C dynamically validated; path D validated as state-forgery-only, not LED suppression — 2026-08-24); includes RE-target SHA-256 list |
| D6 | `10_Upstream_CVE_Comparison/advisory_almalinux_20260709.md` + `advisory_nvd_osv_record_20260824.md` | see manifest | Archived upstream advisory evidence (AlmaLinux blog full text; NVD 7.8 HIGH kernel.org CNA; OSV git ranges — introduced `8161239a…`, mainline fix `3bfdc639…`, stable fixed **5.10.261** > RV101 5.10.209). Archived 2026-08-24 |
| — | `binaries/RVE01-ota-1.22.009….zip` + `binaries/payload.bin` | see manifest | Original firmware images now hashed in manifest (2026-08-24) |

## Repository relocation note (2026-08-22)

The repository was moved from `/home/kali/rokid_root` to
`/mnt/c/Users/<USER>/Documents/codebase/rokid_root`
(Windows: `C:\Users\<USER>\Documents\codebase\rokid_root`).

- Documentation and scripts were updated to the new path.
- **Raw evidence logs (`.log`) were NOT modified.** Their `/home/kali/rokid_root`
  references are the historical host path at collection time and remain intact so the
  recorded SHA-256 hashes stay valid.

## Correction note (2026-08-23)

During a full-package re-verification (`sha256sum -c 12_Hashes/SHA256SUMS.txt`,
61/61 files OK) three first-12 hash values in the tables above were found to be
stale (captured from pre-finalization drafts) and were corrected to match both the
on-disk files and `SHA256SUMS.txt`:

- E10 `GHOSTLOCK_HANDOFF_V13.md`: `29a774851ae0` → `a030cd2b0862`
- E11 `GHOSTLOCK_ROOT_操作說明.md`: `d4119f257a92` → `22a36ca4bb6f`
- D2 `README_ORGANIZED.md`: `134cb6e4db2c` → `a2ae3f08aa0c`

No raw evidence artifact was touched; all E1–E15 hashes were verified unchanged.

### Sync round (2026-08-23, later same day)

After camera-LED mechanism research, three documentation files were legitimately
edited and re-hashed (raw evidence still untouched):

- E11 `GHOSTLOCK_ROOT_操作說明.md`: content updated (+stdin troubleshooting row) → `df520e15c06e`;
  staged copy `04_Exploit_Execution/GHOSTLOCK_ROOT_操作說明.md` refreshed to match origin.
- D2 `README_ORGANIZED.md`: index/timeline updates → `4559f0d575ab`.
- D4 added: `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` → `79228a6f06e4`.

### Sync round (2026-08-24, privacy-LED suppression validation)

The privacy-LED trust-boundary audit paths were **dynamically validated** (previously
paper-only). Added evidence E16/E17 and updated documentation D2/D4/D5 + the main report:

- E16 added: `reports/led_validation/led_test.log` → `6ffb39798a69` (raw, non-sensitive).
- E17 added: `scripts/led_test.sh` → `2fbc82a4dc84` (+ helper `scripts/run_led_orchestrator.sh` → `d85aa992622b`).
- D2 `README_ORGANIZED.md`: timeline/index updates → `89d526f5f272`.
- D4 `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md`: added §4.1 suppression-validation table → `577dab5924d5`.
- D5 `reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md`: §4 paths A/B/C theoretical→validated,
  new §4.5 evidence table, exec-summary + conclusion/limitations updated → `6ecad7f9ba49`.
- `RV101_Disclosure/RV101_Vulnerability_Report.md`: added RS-8 (privacy-LED trust boundary) +
  remediation bullet → `f4e8cef27a59`.

No LPE raw evidence (E1–E15) was modified.

### Sync round (2026-08-24, later — Path D P-Sensor/wearing-state validation)

Path D (P-Sensor / wearing-state spoofing) was dynamically validated. Result: **state
forgery CONFIRMED, but it does NOT directly suppress the LED** (LED gated only by paths
A/B/C). Added evidence E18/E19 and re-hashed D1/D4/D5:

- E18 added: `reports/led_validation/pathd_test.log` → `9865c70c0252` (raw, non-sensitive).
- E19 added: `scripts/pathd_test.sh` → `e947dea2efd1` (+ helper `scripts/run_pathd_orchestrator.sh` → `fc48c02ab316`).
- D1 `01_Executive_Summary/Executive_Summary_EN.md`: (no content change this round; hash synced) → `cefda0ac82a3`.
- D4 `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md`: added §4.2 P-Sensor non-gating boundary note → `f468da8d2cfa`.
- D5 `reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md`: §2.2 B2/B3/B5 updated, §4 path D
  rewritten (validated), §4.5 + §6 conclusion/limitations/remediation updated → `dab9c41c5bb7`.

No LPE raw evidence (E1–E15) was modified.
