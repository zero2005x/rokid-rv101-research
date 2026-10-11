# Vulnerability Detection Verification Index

> 中文版：[VULN_VERIFICATION_INDEX.md](VULN_VERIFICATION_INDEX.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; it decides any conflict).


> ## ⚠ 2026-09-13 Re-Check Result (the opening claim that "the files are confirmed to exist" **does not hold**)
>
> After checking each one with `rglob`: of the 70 names mentioned in this index, **26 are not in the workspace**, in two categories:
>
> | Category | Count | Notes |
> |------|------|------|
> | A. Only the **binary** is missing (the `.c` source is still present) | 16 | `adsprpc_test`, `ashmem_probe`, `binder_bpf_exploit`, `binder_probe`, `bpf_kaslr_exploit`, `cve_2023_2163_probe`, `dirtypipe_test`, `io_uring_*`(5), `kgsl_bpf_spray`, `kgsl_test`, `msm_drm_probe`, `tipc_probe` → **compiled artifacts were never under version control anyway**; rebuild them yourself with the NDK |
> | B. **The source is missing too** | 10 | `ashmem_overflow`, `binder_uaf_v2(.c)`, `cve40094_test`, `cve_2024_36971_v2(.c)`, `cve_22555_check`, `cve_scanner`, `kgsl_exploit_v7(.c)` → these were the experimental files from 2026-01 and **were not retained**; this document is only a historical index |
>
> Before using this index, first confirm that the corresponding file exists (or recompile from the `.c` files under `source_code/`).

This index organizes the project files corresponding to every "vulnerability/attack-surface detection" in HANDOVER_SESSION_20260131.md. ~~The files are confirmed to exist (according to the project list)~~ **that "confirmation" was overturned by the 2026-09-13 re-check; see the note above**. This index does not include the actual execution steps.

## 1) Kernel/System Baseline Detection

- Kernel/system information gathering
  - Reference: HANDOVER_SESSION_20260131.md
- 5.10.x vulnerability scanner
  - Source: cve_5_10_x_scanner.c
  - Binary: cve_scanner

## 2) BPF-Related

- BPF feature/vulnerability probing
  - Source: bpf_vuln_probe.c
  - Source: bpf_check.c
  - Source: bpf_research.c

- BPF KASLR research/testing
  - Source: bpf_kaslr_exploit.c
  - Binary: bpf_kaslr_exploit

## 3) io_uring-Related (CVE-2024-0582)

- io_uring baseline feature/capability probing
  - Source: io_uring_probe.c
  - Binary: io_uring_probe
  - Source: io_uring_capability_probe.c
  - Source: io_uring_functional_test.c
  - Binary: io_uring_functional_test

- CVE-2024-0582 detection/verification
  - Source: cve_2024_0582_uaf_check.c
  - Source: cve_2024_0582_mmap_uaf.c
  - Source: io_uring_lifecycle.c
  - Binary: io_uring_lifecycle
  - Source: io_uring_uaf_verify.c / io_uring_uaf_verify_v2.c / io_uring_uaf_verify_v3.c / io_uring_uaf_verify_v4.c
  - Binary: io_uring_uaf_verify / io_uring_uaf_verify_v2 / io_uring_uaf_verify_v3 / io_uring_uaf_verify_v4
  - Source: io_uring_uaf_crash.c
  - Binary: io_uring_uaf_crash

## 4) Net/Route (CVE-2024-36971)

- PoC/test versions
  - Source: cve_2024_36971_poc.c
  - Source: cve_2024_36971_test.c
  - Source: cve_2024_36971_bpf_spray.c
  - Source: cve_2024_36971_v2.c ~ cve_2024_36971_v13.c
  - Binary: cve_2024_36971_v2 / v3 / v4 / v5 / v6 / v9 / v10 / v11 / v12 / v13 (matching filenames)
  - Other: cve_36971_v9, cve_36971_v13 (compiled files)

## 5) KGSL GPU (CVE-2024-38401)

- Probing/size/race
  - Source: kgsl_test.c
  - Source: kgsl_size_check.c
  - Source: kgsl_race.c
  - Binary: kgsl_test / kgsl_size_check / kgsl_race

- BPF/multi-strategy spray versions
  - Source: kgsl_bpf_spray.c, kgsl_bpf_spray_v3.c, kgsl_bpf_spray_v4.c, kgsl_bpf_spray_v5.c, kgsl_bpf_spray_v6.c
  - Binary: kgsl_bpf_spray / kgsl_bpf_spray_v3 / kgsl_bpf_spray_v4 / kgsl_bpf_spray_v5 / kgsl_bpf_spray_v6

- Other spray strategies
  - Source: kgsl_keyring_spray.c
  - Source: kgsl_pipe_spray.c
  - Source: kgsl_msg_spray.c
  - Source: kgsl_uffd_exploit.c

- In-depth versions
  - Source: kgsl_exploit_v7.c / kgsl_exploit_v8.c / kgsl_exploit_v9.c
  - Binary: kgsl_exploit_v7 / kgsl_exploit_v8 / kgsl_exploit_v9

## 6) Binder

- Probing
  - Source: binder_probe.c
  - Binary: binder_probe

- UAF attempt versions
  - Source: binder_uaf_v2.c / binder_uaf_v3.c / binder_uaf_v4.c / binder_uaf_v5.c
  - Binary: binder_uaf_v2 / binder_uaf_v3 / binder_uaf_v4 / binder_uaf_v5

- BPF-assisted version
  - Source: binder_bpf_exploit.c
  - Binary: binder_bpf_exploit

## 7) Ashmem

- Baseline probing and race
  - Source: ashmem_probe.c
  - Binary: ashmem_probe

- Integer-overflow verification
  - Source: ashmem_overflow_exploit.c
  - Binary: ashmem_overflow

## 8) TIPC

- Probing/attempts
  - Source: tipc_probe.c
  - Source: tipc_exploit.c
  - Binary: tipc_probe / tipc_exploit

## 9) Dirty Pipe (CVE-2022-0847)

- Test
  - Source: dirtypipe_test.c
  - Binary: dirtypipe_test / dirtypipe_test_arm64

## 10) CVE-2021-22555 (Netfilter)

- Detection/test
  - Source: cve_2021_22555_check.c
  - Source: cve_2021_22555_test.c
  - Source: cve-2021-22555_exploit.c
  - Binary: cve_22555_check / cve_22555_test

## 11) CVE-2023-2163 (BPF Verifier)

- Probing
  - Source: cve_2023_2163_probe.c
  - Binary: cve_2023_2163_probe

## 12) CVE-2023-40094

- Test
  - Source: cve_2023_40094_test.c
  - Binary: cve40094_test

## 13) Adsprpc

- Probing/test
  - Source: adsprpc_test.c
  - Binary: adsprpc_test

## 14) MSM DRM

- Probing
  - Source: msm_drm_probe.c
  - Binary: msm_drm_probe

---

## Supplementary

- Other reports and summaries:
  - CVE_5_10_x_Scan_Report.md
  - Rokid_Attack_Vector_Summary.md
  - Rokid*Glasses*資安漏洞報告.md
  - CVE-2024-36971\_\* 分析報告

## Added 2026-08-21

- §5.2 table verification reports:
  - `5.2_VERIFICATION_LIVE_ADB_2026.md` — row-by-row §5.2 adb re-verification on device (probe re-run + build/on-device steps)
  - `5.2_VERIFICATION_2026.md` — §5.2 firmware binary-layer verification (kernel config / msm_kgsl.ko / dmesg extraction)
- Tools improved and verified on device (the surviving compiled artifacts `build2_*` are in `binaries/device_probes/`):
  - `kgsl_uaf_exploit.c` (switched to the GPUOBJ_ALLOC 0x45 path), `bpf_leak_v3.c`,
    `cve_2024_0582_mmap_uaf.c`, `cve_2021_22555_test.c`, `adsprpc_secure_exploit.c`
- On-device config saved: `research/kernel_configs/device_config_1.22.009_live.txt` (from `/proc/config.gz`)
