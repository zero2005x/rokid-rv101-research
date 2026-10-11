# Rokid RV101 Root Project Organization Report

> 中文版：[README_ORGANIZED.md](README_ORGANIZED.md)
> ⚠ **This file is a historical layout (2026-08)**: paths such as `assets/`, `binaries/`, `ghostlock_rv101/`, and the RVE01 OTA zip / payload.bin
> **no longer exist in the current workspace**; the file counts (reports 225 / logs 139 / scripts 60) are also outdated.
> For the current layout see [`MANIFEST.md`](MANIFEST.md); this file only preserves the organization thinking of that time.


**Organization date**: 2026-08-22 (last update)
**Target device**: Rokid RV101 (RG-glasses, codename `neo`, NEO LUNA-V2)

> **Major progress in 2026-09 (app productization)**: the project has extended from "acquiring root" to a "dual Android app system".
> For the latest work see: `README.md` (overview and document index), `docs/deploy.md` §9 (Shizuku mode, WiFi media channel,
> localization, icons, wireless adb conclusions), `docs/architecture.md` (dual-channel architecture),
> `reports/shizuku_integration_test_report.md` and `reports/wifi_transfer_test_report.md` (on-device tests).
> Completed: running the exploit directly as the Shizuku UserService shell identity (default path), WiFi media transport (SoftAP + TCP 8555,
> Bluetooth fallback), fully transparent glasses dashboard + remote display configuration from the phone, 14 locales, branding SVG/adaptive icon,
> permanent fix for privacy LED flicker during recording, two-line operation log preview + long-press full window,
> **silent capture (root bind-mounts a silent Ogg over the camera/video camera tone, switchable from the phone in real time)**.

> This document is the project's master index, replacing the older `20260814/` description (that directory no longer exists and its files have been reorganized into top-level directories).
> To understand the current state of the "root research", read **`reports/GHOSTLOCK_HANDOFF_V13.md`** (Clean Root achieved),
> **`reports/GHOSTLOCK_ROOT_操作說明.md`** (v9.60 operation) and **`RV101_Disclosure/00_README.md`** (coordinated disclosure package).

---

## 0. Project Summary in One Sentence

This is a red-team penetration test / root research project targeting the **Rokid RV101 AR glasses**. The goal is to escalate from
`shell` (uid=2000) to **root** on a device running **Android 12 + Linux 5.10.209-perf (Qualcomm neo SoC)**, SELinux Enforcing, user build.

**Current status: root acquired (2026-08-22, v9.59 / v9.60).** The main attack chain is a
**self-built exploit for CVE-2026-43499 (GhostLock, rt_mutex futex PI stack-UAF)**, which has achieved
a clean root shell with `uid=0 gid=0`, SELinux Permissive and full capabilities (evidence:
`reports/ghostlock_runlogs/run_w2_v960_143215.log`, `run_w2_clean_v959_134613.log`).
Subsequent work has shifted from "acquiring root" to preparing **coordinated vulnerability disclosure** (`RV101_Disclosure/`).

---

## 1. Executive Summary

| Item | Status |
|------|------|
| Root privileges | ✅ **Acquired** (2026-08-22, uid=0 gid=0, SELinux Permissive, full capabilities) |
| Bootloader | ✅ Unlocked (orange state, `unlocked:yes`, `secure:no`) |
| Magisk | ✅ v30.6 installed; ❌ not activated (no root binary) |
| Official OTA firmware | ✅ 1.22.009 obtained and fully unpacked |
| Magisk-patched boot | ✅ `magisk_patched-122_009.img` is ready |
| Entering recovery / fastbootd | ❌ Blocked as of 1.22.009 (`adb reboot recovery/bootloader` both ineffective) |
| KASLR leak | ✅ Broken through (`/sys/kernel/tracing/printk_formats`) |
| GhostLock trigger + walk | ✅ Proven (poison test reaches rb_erase) |
| GhostLock reclaim | ✅ **Overcome** (still flaky at ~50%/boot, masked by a boot health gate + automatic retry `boot_run_loop.sh`) |

---

## 2. Directory Structure

```
rokid_root/
├── assets/               # Unpacked assets (2804 files)
│   ├── TZAccessApp/          # TrustZone Access Service APK source
│   ├── RV101_Validation/     # Device validation data collection (2026-08-12)
│   ├── RokidOtaUpgrade_extracted/  # Unpacked OTA client APK
│   ├── vendor_extracted/     # Unpacked vendor.img (lib/lib64 HAL etc.)
│   ├── vendor_output/        # Original vendor.img
│   ├── output_1.19/          # Older firmware 1.19 output (boot/system/vendor etc. .img)
│   └── xlsx_tmp/             # Excel vulnerability inventory worksheets
│
├── binaries/             # Firmware and binaries (3103 files)
│   ├── RVE01-ota-1.22.009-20260710-150201.zip   # Official OTA package (981MB)
│   ├── payload.bin                               # OTA payload (939MB)
│   ├── Magisk-v30.6.apk                          # Magisk App
│   ├── CXRService.apk / seccamservice.apk        # System APKs
│   ├── GateServiced.bin / gate_test.bin          # TrustZone service binaries
│   ├── selinux_policy.bin                        # SELinux policy
│   ├── device_probes/          # ARM64 ELF build artifacts for on-device validation
│   └── extracted_1.22/         # payload unpack (complete firmware)
│       ├── kernel                # Uncompressed ARM64 Image (analysis source)
│       ├── boot.img / vendor_boot.img / recovery.img
│       ├── magisk_patched-122_009.img   # Magisk-patched boot
│       ├── msm_kgsl.ko           # KGSL GPU driver module
│       ├── system.img / system_ext.img / vendor.img / product.img / odm.img
│       ├── modem.img / dtbo.img / uefi.img / xbl.img / xbl_config.img
│       ├── vbmeta.img / vbmeta_system.img / imagefv.img
│       ├── recovery_root/        # Unpacked recovery ramdisk
│       ├── ota_apk/              # Unpacked RokidOtaUpgrade.apk
│       └── dmesg_122009.txt
│
├── reports/              # All reports (.md) + ghostlock run logs (225 files)
│   └── ghostlock_runlogs/       # run_w2_* / exploit execution and diagnostic logs (139 logs)
│
├── research/             # GhostLock reverse-engineering reference material and kernel config (488 files)
│   ├── ghostlock_refs/
│   │   ├── extracted/           # Reference exploit source (OnePlus/Honor/POCO-aresin/
│   │   │                        #   emerald/m3q/selinuxdis/openvz_patch)
│   │   ├── rv101_kallsyms.txt   # Full symbol table (176,575 symbols)
│   │   ├── rv101_disasm/        # Disassembly of key functions (*.txt)
│   │   ├── remove_waiter.disasm.txt   # Unpatched evidence
│   │   └── slub_5.10.209.c
│   └── kernel_configs/         # Working copy of kernel config
│
├── scripts/              # Scripts and test programs (60 files)
│   ├── ghostlock_analysis/      # capstone disassembly tools
│   └── *.py / *.sh / *.ps1      # Analysis/exploitation/collection scripts
│
├── source_code/          # Source code (262 .c files)
│   └── ghostlock_rv101/         # ★ main exploit (see §6)
│
├── tools/                # Third-party tool documentation and helper tools (1 file)
│   └── payload-dumper-go/       # OTA payload unpacking tool documentation
│
├── RV101_Disclosure/     # ★ Coordinated vulnerability disclosure package (created 2026-08-22, see §9)
│   ├── RV101_Vulnerability_Report.md       # Main report (vendor version)
│   ├── CVE_Relationship_Assessment.md      # Existing CVEs vs. independent weakness assessment
│   ├── Evidence_Checklist.md / Evidence_Index.md / SHA256SUMS.txt
│   ├── 11_Vendor_Communication/            # Initial report email + 6 follow-up templates
│   └── confidential/                        # exploit source / offsets / raw evidence (restricted)
│
└── README_ORGANIZED.md   # This document
```

---

## 3. Device Information

| Item | Value |
|------|-----|
| Model | Rokid RV101 (RG-glasses / NEO LUNA-V2) |
| Device serial number | `<DEVICE_SERIAL>` |
| Platform | Qualcomm (`platform=neo`, `soc_id=579`, QCM4290-class 4-core) |
| GPU | Adreno 621v1 (Chip ID 0x6020100) |
| Kernel | `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` (1.22.009) |
| ｜ older version | `5.10.209-perf-gb80a8319d11b` (before 1.19) |
| Android | 12 (SKQ1.240613.001, user build) |
| Security patch | 2024-07-05 |
| Boot partitions | boot_a=`/dev/block/mmcblk0p17`, boot_b=`p18`, recovery_a=`p19`, recovery_b=`p20`, misc=`p59` |
| Security configuration | SELinux Enforcing, seccomp=off, kpti=off, KASLR on |
| ｜ key sysctl | kptr_restrict=2, dmesg_restrict=0, perf_event_paranoid=3 (perf blocked), unprivileged_bpf_disabled=0 |
| ｜ other | bpf_jit_enable=1, bpf_jit_harden=0, `/proc/slabinfo` readable (shell is in the log group) |

---

## 4. Research Timeline

| Date | Milestone |
|------|--------|
| 2026-01-31 ~ 02-03 | Phase 1: a large number of known-CVE attempts (BPF/KGSL/Binder/TIPC/io_uring/Dirty Pipe…), **all failed**. Confirmed that the bootloader was unlocked, Magisk was not activated, and boot.img was unobtainable (`FINAL_STATUS_20260202.md`). Discovered KGSL cross-FD GPU memory access (information disclosure, not privilege escalation). |
| 2026-07-10 | Official OTA `1.22.009` released. |
| 2026-08-12 | Device validation data collection (`assets/RV101_Validation/`). |
| 2026-08-14 | Obtained and unpacked the OTA firmware; reverse analysis (`REVERSE_ANALYSIS_1.22_009.md`). Confirmed that recovery/fastbootd are blocked and that CVE-2026-21385 (KGSL align) is not exploitable. **KASLR leak broken through** (printk_formats). |
| 2026-08-18 ~ 20 | **GhostLock (CVE-2026-43499) exploit development** (handovers V2→V9). Completed the KASLR leak, KernelSnitch mm leak, GhostLock trigger, pselect forgery, punch, and a walk reaching rb_erase (poison proof). |
| 2026-08-20 | `GHOSTLOCK_HANDOFF_V9.md`: fixed the root cause of the run51 kernel panic and landed the W2 FOPS route code. |
| 2026-08-21 | Latest source update (`source_code/ghostlock_rv101/src/*`). |
| 2026-08-22 | **Clean Root achieved**: v9.59 first `uid=0 gid=0` (`run_w2_clean_v959_134613.log`, ~110 s after boot); v9.60 reproduced it and completed before/after state forensics (`run_w2_demo_v960_142523.log`, `run_w2_v960_143215.log`). SELinux Enforcing→Permissive, full capabilities. |
| 2026-08-22 | Created the coordinated vulnerability disclosure package `RV101_Disclosure/` (main report, initial notification email, evidence index, SHA256 manifest, CVE relationship assessment, etc.). |
| 2026-08-23 | Whole-folder audit and corrections (document hash/reference consistency); **complete reverse engineering of the camera white LED mechanism**: the CameraService patch → `lights_ctrl` Binder (EVENT_WARN_ID_CAMERA_OPEN) and the init.rc property, two paths, both validated end-to-end on device (`RV101_CAMERA_LED_FLASH_MECHANISM.md`). |
| 2026-08-24 | **Privacy indicator LED trust boundary audit** (`RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md`: REE-only, no TEE/Secure MCU, no hardware interlock); on the same day, moved the three attack paths (sysfs DAC override / `lights_ctrl` Binder / `session_open` property forgery) from paper analysis to **controlled dynamic validation** (`reports/led_validation/led_test.log`, root + Permissive, without triggering a camera capture); then validated **path D** (P-Sensor/wear-state forgery): the state forgery succeeded (`is_take_on`/`enforce_psensor`→PSOC→extcon→rt600) but **does not control the LED directly** (the LED is controlled only by A/B/C, `reports/led_validation/pathd_test.log`). |

---

## 5. Overview of Vulnerabilities and Attack Surface

### 5.1 Current Focus Vulnerability

| CVE / Technique | Type | Status |
|-----------|------|------|
| **CVE-2026-43499 (GhostLock)** | rt_mutex futex PI **stack-UAF** | 🟢 **Unpatched, exploit in progress (~90%)** |

- Nature: `remove_waiter()` wrongly clears `current->pi_blocked_on` instead of `waiter->task->pi_blocked_on`,
  leaving waiter (A)'s `pi_blocked_on` dangling and pointing at a dead kernel stack `rt_mutex_waiter`.
- Fix commit: mainline `3bfdc63936dd` (7.1); affects 2.6.39 ~ 7.1. Disassembly confirms the RV101 kernel is **unpatched**.
- Exploitation chain: KASLR leak → KernelSnitch mm leak → GhostLock trigger → pselect forgery → punch →
  rb_erase 8-byte write → W1 (selinux_state.enforcing=0) → W2 (cred=init_cred, FOPS route).

### 5.2 Verified but Not Exploitable (this device)

| CVE / Technique | Type | Reason for the verdict |
|-----------|------|---------|
| CVE-2026-21385 | KGSL align sign-extension | userland cannot control the align (the ioctl is closed off) |
| CVE-2026-KGSL-UAF | KGSL syncsource | the SYNCSOURCE ioctl returns ENOTTY, unreachable |
| CVE-2024-36971 | Routing table (dst) UAF | v1-v13, the loopback DST never becomes obsolete |
| CVE-2024-0582 | io_uring PBUF_RING UAF | the PBUF_RING feature does not exist (requires 5.19+) |
| CVE-2024-38401 | KGSL UAF | Patched |
| CVE-2025-21479 / 21480 | KGSL UAF | Context creation is blocked |
| CVE-2024-43047 / 49848 | FastRPC UAF (ITW, NoviSpy) | requires obtaining an fd through HWBinder; shell has no permission |
| CVE-2023-2163 | BPF verifier | Patched |
| CVE-2022-0847 | Dirty Pipe | Patched |
| CVE-2019-2215 | Binder UAF | Patched |
| CVE-2021-22555 | Netfilter x_tables | requires CAP_NET_ADMIN |
| CVE-2024-56720 | BPF sockmap UAF | CONFIG_BPF_STREAM_PARSER is not enabled |
| CVE-2024-1086 / 26581 | nf_tables UAF | CONFIG_NF_TABLES is not enabled |
| CVE-2025-21655 | io_uring eventfd UAF | affects 6.1+ only |
| KGSL Cross-FD access | GPU memory isolation bypass | information disclosure / memory corruption, but cannot reach the kernel slab directly |

> ✅ **This table was re-verified row by row on device with `adb.exe` on 2026-08-21; all 15 rows hold.**
> Evidence: `reports/5.2_VERIFICATION_2026.md` (firmware binary layer),
> `reports/5.2_VERIFICATION_LIVE_ADB_2026.md` (adb on-device layer).
> The on-device `/proc/config.gz` has been archived: `research/kernel_configs/device_config_1.22.009_live.txt`.

### 5.3 Attack Surface (enumerated, re-verified on device 2026-08-21)

| Attack surface | Measured status |
|--------|---------|
| `/dev/kgsl-3d0` | 0666, O_RDWR ✅ (shell can open it) |
| `/dev/i2c-1` | 0666, O_RDWR ✅, ioctl reachable; but the charging IC `mp2724`(0x3f) / fuel gauge `cw221x`(0x64) are already taken by kernel drivers → `I2C_SLAVE` returns EBUSY |
| Binder / hwbinder / vndbinder | 0666, O_RDWR ✅, `BINDER_VERSION`(proto=8) reachable; cross-domain is limited by SELinux |
| `/dev/adsprpc-smd(-secure)` | O_RDONLY ✅, O_RDWR ❌ EACCES (requires obtaining an fd through HWBinder) |
| BPF Socket Filter | `unprivileged_bpf_disabled=0`, `BPF_PROG_LOAD` + `SO_ATTACH_BPF` ✅ |
| SPI Flash / Serial / DIAG | ❌ no `/dev/spidev*` or `/dev/diag` nodes; `/dev/ttyS0-3` are 0600 root and cannot be opened; only the `adb,diag` USB composite (host-side DIAG) |
| `/mnt/vendor/qmcs/` (777, writable), `/data/local/tmp/` | writable |
| GateServiced (root, TCP 0.0.0.0:8341, `u:r:tee:s0`) | missing the drmrpc group, cannot connect directly to `/dev/qseecom` |
| graphics_composer | has the drmrpc group + read/write permission on qmcs |

---

## 6. Main Exploit: GhostLock (CVE-2026-43499)

Location: `source_code/ghostlock_rv101/`

| File | Purpose |
|------|------|
| `target_rv101.h` | all disassembly-derived offsets/constants (single source of truth) |
| `src/main.c` | flow: `--selftest` / `--write1` / the complete W1→W2 skeleton |
| `src/route.c` | trigger thread + pselect forgery + consumer punch + punch_done synchronization |
| `src/util.c` | KASLR leak, KernelSnitch, mm spraying, key-payload reclaim, forensics |
| `src/common.h` | shared definitions |
| `src/mmfree_probe*.c` / `perf_probe.c` | auxiliary probes |
| `Makefile` | build (NDK r27d / WSL clang interop) |
| `build/ghostlock_rv101` | compiled ELF |

**Overcome (2026-08-22)**: reclaim used to be the only blocker (the freed mm slab was not claimed by the spraying carrier,
and writes were silently absorbed by the old slab); it is now reproduced reliably using a "boot health gate (`nr_threads<3000`) + automatic retry
`scripts/boot_run_loop.sh`". The gid fix switched to user-space `setgid(0)` (the kernel-side
repair panics on the 4th reclaim, see `GHOSTLOCK_HANDOFF_V13.md` for details). reclaim is still a
flaky variable at ~50%/boot, but it has been masked by automation and no longer blocks acquiring root. See
`GHOSTLOCK_V5_RECLAIM_MECHANISM.md` and `GHOSTLOCK_HANDOFF_V13.md`.

---

## 7. Tools and Programs

### 7.1 C exploit / probe programs (`source_code/`, 262 .c files, grouped by topic)

- **KGSL** (~80 files): `kgsl_uaf_exploit.c`, `kgsl_cross_fd_exploit.c`, `kgsl_cmd_inject.c`,
  `kgsl_exploit_v2~v9.c`, `kgsl_*_spray.c`, `kgsl_privesc_v*.c` etc.
- **BPF** (~30 files): `bpf_leak_v3.c`, `bpf_ringbuf_oob_v6.c`, `bpf_kaslr_exploit.c`,
  `bpf_verifier_bypass.c`, `cgroup_bpf_exploit.c` etc.
- **CVE-specific**: `cve_2024_36971_v13.c`, `cve_2024_0582_mmap_uaf.c`, `cve-2021-22555_exploit.c`,
  `cve_2023_2163_probe.c`, `cve_2025_21479_*.c`, `dirtypipe_test.c` etc.
- **io_uring / TIPC / Binder / Ashmem / Adsprpc / MSM DRM / GateServiced**: corresponding probes and exploits
- **GhostLock**: `ghostlock_root.c`, `ghostlock_trigger.c` (plus the `ghostlock_rv101/` main body)
- **slabprobe**: `source_code/slabprobe/` (fork/micro/slab probes, with a Makefile)

#### 7.1.1 Tools improved and validated on device on 2026-08-21

The following 5 tools were recompiled with NDK r27d after their bugs were fixed on 2026-08-21 (dynamic linking, 0 warnings under `-Wall -Wextra`),
and re-run for validation on the device (the build artifacts `build2_*` are in `binaries/device_probes/`):

| Tool | Main fix | On-device result |
|------|---------|---------|
| `kgsl_uaf_exploit.c` | switched to the actually usable `KGSL_IOCTL_GPUOBJ_ALLOC`(0x45)+mmapaddr (the original used GPUMEM_ALLOC, which returned ENOTSUP and would not run); removed sigsetjmp/siglongjmp UB; fixed the kernel-pointer check (`(val>>40)==0xFFFFFF`) | UAF SUCCESS + cross-FD overwrite of 20 victims (GPU memory layer) |
| `bpf_leak_v3.c` | fixed the ringbuf mmap size; added unprivileged_bpf_disabled/kptr_restrict/bpf_jit probing | the verifier rejects OOB (patched) |
| `cve_2024_0582_mmap_uaf.c` | added an explicit check of `IORING_FEAT_PBUF_RING` (bit 11) | bit 11 clear + REGISTER_PBUF_RING EINVAL |
| `cve_2021_22555_test.c` | removed `system()`; parses CapEff/CapBnd directly to determine CAP_NET_ADMIN | CapEff=0 → EPERM |
| `adsprpc_secure_exploit.c` | fixed the exaggerated description in the file header; made the O_RDWR permission surface accurate | O_RDWR EACCES (requires HWBinder) |

> Compilation example: `aarch64-linux-android24-clang -O2 -pthread -o <out> <src>.c`
> (Note: `-static` trips the Bionic TLS alignment check and the binary will not run, so dynamic linking must be used.)
>
> 2026-08-22 inventory note: the build artifact `build2_cve_2024_0582_mmap_uaf` is currently not in the workspace;
> the source `source_code/cve_2024_0582_mmap_uaf.c` is still fully preserved and can be recompiled when needed.

### 7.2 Scripts (`scripts/`, 60 files)

- GhostLock analysis: `ghostlock_analysis/disasm_func.py`, `batch_disasm.py`
- Disassembly / symbols: `rv101_disasm.py`, `extract_offsets.py`, `unpack_bootimg.py`
- Device reset / OTA: `sahara_reset.py`, `sahara_reset_v2.py`, `rokid_ota_check.py`, `rokid_ota_auth.py`, `create_flash_zip.py`
- GateServiced attacks: `gate_*.py`, `gate_*.sh`, `GateService_exploit.py`, `frida_hook_gate.py`
- Privilege-escalation enumeration: `full_privesc_enum.sh`, `final_privesc_attack.sh`, `comprehensive_vuln_scan.sh`
- Validation collection: `Collect-RV101-Validation.ps1`, `run_v11.ps1`, `ghostlock_reboot_run.ps1`
- Root: `getroot.sh`, `magisk_activate.sh`

### 7.3 Key Binaries (`binaries/`)

- `magisk_patched-122_009.img` (ready, can be flashed at any time), `msm_kgsl.ko`, `kernel` (uncompressed Image)
- `RVE01-ota-1.22.009-20260710-150201.zip` / `payload.bin`

---

## 8. Index of Key Reports (`reports/`)

### Currently in progress (read first)

| Report | Description |
|------|------|
| `GHOSTLOCK_HANDOFF_V13.md` | **Latest work handover** (2026-08-22, thirteenth: Clean Root uid=0 gid=0 achieved) |
| `CURRENT_STATUS_20260822.md` | Authoritative snapshot of the current state (root achieved, focus shifting to coordinated disclosure) |
| `GHOSTLOCK_ROOT_操作說明.md` | v9.60 root acquisition manual (boot health gate, troubleshooting, rebuild steps) |
| `RV101_CAMERA_LED_FLASH_MECHANISM.md` | Complete reverse engineering of the photo/video white LED mechanism (AW2110x + lights_ctrl Binder, two paths, 2026-08-23) |
| `RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md` | Privacy indicator LED / P-Sensor trust boundary audit (REE-only, no hardware interlock; dynamic validation of paths A/B/C + validation of path D showing "state forgery but not direct LED control", 2026-08-24) |
| `GHOSTLOCK_HANDOVER_PROMPT.md` | Complete handover instructions + full exploitation chain diagram |
| `GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md` | root cause of the fake page panic (reclaim failure) |
| `GHOSTLOCK_V5_RECLAIM_MECHANISM.md` | full diagram of the reclaim mechanism |
| `GHOSTLOCK_W2_CONFIGFS_READ_BLOCKER.md` / `GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md` | the W2 FOPS/configfs route |
| `REVERSE_ANALYSIS_1.22_009.md` | 1.22.009 firmware reverse engineering (including the KASLR breakthrough and the recovery block) |

### 5.2 table verification (added 2026-08-21)

| Report | Description |
|------|------|
| `5.2_VERIFICATION_LIVE_ADB_2026.md` | **Row-by-row adb on-device re-verification of §5.2** (all probes re-run, including compile/deploy steps) |
| `5.2_VERIFICATION_2026.md` | §5.2 verification at the firmware binary layer (kernel config / msm_kgsl.ko / dmesg extraction) |

### Early phase

| Report | Description |
|------|------|
| `FINAL_STATUS_20260202.md` | Final state of phase 1 (all software paths failed) |
| `HANDOVER_SESSION_20260131.md` | Complete session handover |
| `KGSL_FINAL_REPORT.md` | Complete report on the KGSL cross-FD vulnerability |
| `ATTACK_SUMMARY.md` | GateServiced / attack surface summary |
| `CVE_5_10_x_Scan_Report.md` | 5.10.x kernel vulnerability scan |
| `VULN_VERIFICATION_INDEX.md` | Vulnerability ↔ program file cross-reference index |
| `VULN_IMPLEMENTATION_AUDIT.md` | CVE description ↔ repo program audit |
| `PASS_TABLE_EVIDENCE.md` | Traceable evidence table |
| `ROOT_PROCEDURE.md` / `Rokid_Root_Guide_boot_img.md` | Root procedure guides |
| `SESSION_14/15/16_*.md` | research summaries for each session |
| `Medium_Rokid_Root_Article_EN.md` / `Facebook_Rokid_Root_Post.md` | public article drafts |
| `Rokid_Glasses_*.md` | device/security reports (including Marp versions) |

---

## 9. Current Status and Next Steps (in priority order)

Root has been achieved (2026-08-22). The current focus has shifted from "acquiring root" to **coordinated vulnerability disclosure** and optional technical wrap-up:

1. **Complete the pre-disclosure evidence** (see `RV101_Disclosure/00_README.md` for details):
   - Freeze the device session, re-capture a stable `run_w2_v960_143215.log` and re-hash it (the original file is still being appended to,
     see the integrity note in `RV101_Disclosure/Evidence_Index.md`);
   - Fill in the "attack preconditions" evidence (who unlocked the bootloader, the source of ADB authorization, the feasibility of the app scenario);
   - Add a pre-exploit `Cap*` snapshot (symmetric with post-exploit).
2. **Send the initial notification**: `RV101_Disclosure/11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md`
   → security@rokid.com (minimum necessary information only, no weaponization details).
3. **(Optional) reduce reclaim flake** (no longer blocking, only for a more elegant reproduction): reduce the flood/dummy volume, switch the reclaim
   carrier (setxattr / sendmsg / BPF map / keyctl big_key) — see `GHOSTLOCK_HANDOFF_V13.md §6`.
4. **(Optional) fallback path**: flash `magisk_patched-122_009.img` directly over EDL 9008 firehose
   (`scripts/sahara_reset.py`) — this is an independent fallback only and is unrelated to this kernel privilege escalation.
5. **Deliverables**: a single ELF `ghostlock_rv101` that reproduces root (`source_code/ghostlock_rv101/build/`)
   + an operation manual (`reports/GHOSTLOCK_ROOT_操作說明.md`) + the complete disclosure package (`RV101_Disclosure/`).

---

## 10. Notes

- All `.md` reports are collected in `reports/`; source code in `source_code/`; firmware binaries in `binaries/`.
- The GhostLock reference exploit sources (OnePlus/Honor/POCO etc.) are in `research/ghostlock_refs/extracted/`.
- The device reboots automatically after every kernel panic (2-5 minutes); the KASLR slide changes and must be re-leaked. pstore is empty,
  so the only reliable walk-progress probe is the poison bisection.
- The historical handover documents (`GHOSTLOCK_HANDOFF_V2~V13`, `FINAL_STATUS_20260202.md`, `HANDOVER_SESSION_20260131.md`
  and others) are "state at the time" snapshots and have **not been rewritten retroactively**; this document, `reports/GHOSTLOCK_HANDOFF_V13.md`
  and `reports/CURRENT_STATUS_20260822.md` are the basis for the latest state.
