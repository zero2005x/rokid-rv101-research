# Missing / Referenced Binary Inventory — Rokid RV101 (RG_glasses)
> ⚠ **2026-09-13 後續更正**：§4.1／§4.3 將搶救回來的 12 個檔案統稱「執行檔」有誤——
> `fakepayload` 是 **4096 bytes 全零頁**、`min_payload` 是 **24 B `CrAU` OTA payload 標頭殘檔**，兩者皆非 ELF；
> 其餘 10 個確為 aarch64 探針（含原始檔名與 CVE／子系統對應），詳見 `reports/static_re_gate_codes_and_rescue_2026-09-13.md`。


> **Generated:** 2026-09-13 · **Workspace:** `/home/kali/rokid_root` · **Device:** `<DEVICE_SERIAL>` (RG_glasses, Android 12, kernel 5.10.x)

> **Question answered:** historical reports cite ~51 probe/exploit executables plus `binaries/`, `source_code/ghostlock_rv101/build/` and `binaries/device_probes/` that are **not** in the workspace. This document classifies every referenced binary/tool name as **(a) rebuildable, (b) gone, or (c) present only on the device**.


## Method

- **Reference extraction** — scanned all `reports/*.md` (100), `docs/*.md` (16) and every `.sh`/`.py`/`.ps1` under `scripts/` + `deploy/` (114 files) for names appearing after `二進位：`/`binary:`, after `-o `, after `adb push`, in `/data/local/tmp/<name>`, and for `.bin`/`.img`/`.ko`/`.mbn` tokens. A plain word-boundary search then produced the *referenced by* column.
- **In repo?** — exact basename search across the whole workspace (`.git`, `gradle_home`, `.venv`, `.zig-cache`, `build-cache` excluded) **plus a magic-byte scan for real ELF files**.
- **Source to rebuild?** — `✓ exact` = `<name>.c` exists (or a Makefile target builds it); `~ variant` = only a sibling version (e.g. `_v2`, `_test`) survives; `✗ none` = no source at all.
- **On device?** — `ls -la /data/local/tmp/<name>` over adb, batched, plus a **full `ls -la /data/local/tmp/`** capture. `✓ magisk/` = file is at `/data/local/tmp/magisk/<name>` (subdirectory of an in-scope path), not the top level.
- **Read-only**: no workspace file was modified except this report.


## 1. Summary

| Category | Names | Rebuildable (exact src) | Variant src only | No source at all | On device |
|---|---:|---:|---:|---:|---:|
| probe | 125 | 92 | 14 | 19 | 70 |
| exploit | 119 | 77 | 26 | 16 | 19 |
| daemon | 13 | 4 | 0 | 9 | 7 |
| firmware image | 45 | 0 | 0 | 45 | 7 |
| kernel module | 2 | 0 | 0 | 2 | 0 |
| other | 16 | 0 | 0 | 16 | 1 |
| **TOTAL** | **320** | **173** | **40** | **107** | **104** |

**Headline numbers**

- **173 of 320 names (54%) are directly rebuildable** — the `.c` is still in `source_code/` or `deploy/`.
- **40 more are recoverable with adaptation** — only a sibling version's source survives.
- **79 names are truly gone** — no source in the repo, no binary in the repo, and not on the device. These are the real losses (`cve_scanner`, `ashmem_overflow`, `binder_uaf_v2/v3/v4`, `cve40094_test`, `cve_22555_check/test`, `kgsl_exploit_v7`, `kgsl_parasitic_inject`, `shizuku_server`, `hal_proxy_daemon`, `binaries/extracted_1.22/kernel`, `msm_kgsl.ko`, all boot/vendor images, …).
- **27 names exist only on the device** — no source, no repo copy, but a binary sits in `/data/local/tmp`. ⚠️ **These vanish on a factory reset / OTA / `rm -rf /data/local/tmp` and may be stale relative to the reports.** Pull them now if they matter.
- Only **18 names have any copy in the workspace** — the repo is essentially source-only; compiled artifacts were never version-controlled (consistent with `reports/docs_correctness_audit_2026-09-13.md:79`).

**The ~51 figure is confirmed.** `reports/VULN_VERIFICATION_INDEX.md` enumerated 70 names; 26 were already known missing (16 binary-only, 10 source-also-missing). This audit widens the net to every name referenced anywhere and finds the same shape: source survives for the majority, the *binaries* were never committed.


## 2. The three missing artifact directories

| Path | Status | Rebuildable? | Evidence |
|---|---|---|---|
| `binaries/` | **absent** | **partly** — `binaries/extracted_1.22/kernel` (the 47 MB raw ARM64 Image behind every KGSL/BPF disassembly claim) is **gone**. Re-extractable only from a stock OTA/firmware dump. | `reports/5.2_VERIFICATION_2026.md:13,23`, `reports/GHOSTLOCK_HANDOFF_V6.md:20`, `scripts/rv101_disasm.py:11`, `scripts/ghostlock_analysis/batch_disasm.py:7` |
| `source_code/ghostlock_rv101/build/` | **absent** | **yes** — `Makefile` + `src/{main,util,route}.c` + `target_rv101.h` survive; `make` regenerates `build/ghostlock_rv101` and `build/perf_probe` | `source_code/ghostlock_rv101/Makefile`; `reports/GHOSTLOCK_HANDOFF_V9.md:76`, `V12.md:134`, `V13.md:107` |
| `binaries/device_probes/` | **absent** | **no** — held the `build2_*` on-device rebuilds. The `.c` sources survive, but **five of the `build2_*` binaries survive only on the device** | `reports/VULN_VERIFICATION_INDEX.md:172` |

Note: `binaries/extracted_1.22/kernel` is **not** on the device either. Only `/data/local/tmp/magisk/kernel` (47,331,340 B) exists — that is the Magisk boot-image payload, not the clean analysis image.


## 3. Full inventory

Legend — **Source?** `✓ exact` = `<name>.c` exists (or Makefile target) · `~ variant` = sibling version only · `✗ none` · **Device?** = `/data/local/tmp/<name>`, adb-verified 2026-09-13.


### 3.1 probe (125)

| name | in repo? | source? | on device? | referenced by |
|---|---|---|---|---|
| `adsprpc_dmabuf` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:1040`<br>`reports/missing_binaries_inventory_2026-09-13.md:62` |
| `adsprpc_ioctl_test` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:63` |
| `adsprpc_ioctl_v2` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:64` |
| `adsprpc_mmap_probe` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:65` |
| `adsprpc_probe` | ✗ | ~ variant | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:66` |
| `adsprpc_test` | ✗ | ✓ exact | ✗ | `reports/PASS_TABLE_EVIDENCE.md:58`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:193`<br>`reports/VULN_VERIFICATION_INDEX.md:11` |
| `alt_attack_surface` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:68` |
| `alt_attack_surface_v2` | ✗ | ✓ exact | ✗ | `reports/ATTACK_SURFACE_RESEARCH.md:227`<br>`reports/HANDOVER_SESSION_20260131.md:3610`<br>`reports/missing_binaries_inventory_2026-09-13.md:69` |
| `ashmem_blob_test` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:70` |
| `ashmem_ioctl_test` | ✗ | ✓ exact | ✓ | `reports/FACTS.md:107`<br>`reports/GHOSTLOCK_HANDOFF_V10.md:597`<br>`reports/missing_binaries_inventory_2026-09-13.md:71` |
| `ashmem_probe` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2134`<br>`reports/PASS_TABLE_EVIDENCE.md:57`<br>`reports/VULN_VERIFICATION_INDEX.md:11` |
| `ashmem_test` | ✗ | ~ variant | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:73` |
| `attack_surface_probe` | ✗ | ✓ exact | ✓ | `reports/FACTS.md:108`<br>`reports/missing_binaries_inventory_2026-09-13.md:74` |
| `binder_cmd_test` | ✗ | ✓ exact | ✗ | `reports/PRIVESC_REPORT.md:168`<br>`reports/missing_binaries_inventory_2026-09-13.md:75` |
| `binder_fd_steal` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:4807`<br>`reports/missing_binaries_inventory_2026-09-13.md:76` |
| `binder_probe` | ✗ | ✓ exact | ✓ | `reports/HANDOVER_SESSION_20260131.md:2166`<br>`reports/PASS_TABLE_EVIDENCE.md:56`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:157` |
| `binder_simple_test` | ✗ | ✓ exact | ✗ | `reports/PRIVESC_REPORT.md:169`<br>`reports/missing_binaries_inventory_2026-09-13.md:78` |
| `bpf_check` | ✗ | ✓ exact | ✗ | `reports/VULN_VERIFICATION_INDEX.md:30`<br>`reports/missing_binaries_inventory_2026-09-13.md:79` |
| `bpf_verifier_probe` | ✗ | ~ variant | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:80` |
| `bpf_vuln_probe` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:941`<br>`reports/VULN_VERIFICATION_INDEX.md:29`<br>`reports/missing_binaries_inventory_2026-09-13.md:81` |
| `bpf_vuln_scanner` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:82` |
| `cve40094_test` | ✗ | ✗ none | ✗ | `reports/VULN_VERIFICATION_INDEX.md:12`<br>`reports/missing_binaries_inventory_2026-09-13.md:83` |
| `cve_2021_22555_check` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2525`<br>`reports/PASS_TABLE_EVIDENCE.md:52`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:79` |
| `cve_2021_22555_test` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2559`<br>`reports/PASS_TABLE_EVIDENCE.md:52`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:80` |
| `cve_2023_2163_probe` | ✗ | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:81`<br>`reports/CVE-2023-2163_BPF_Verifier_分析.md:323`<br>`reports/PASS_TABLE_EVIDENCE.md:51` |
| `cve_2023_40094_test` | ✗ | ✓ exact | ✗ | `reports/VULN_VERIFICATION_INDEX.md:142`<br>`reports/missing_binaries_inventory_2026-09-13.md:87` |
| `cve_2024_0044_test` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:4037`<br>`reports/Rokid_RedTeam_Internal_Report.md:260`<br>`reports/missing_binaries_inventory_2026-09-13.md:88` |
| `cve_2024_0582_mmap_uaf` | ✗ | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:53`<br>`reports/HANDOVER_SESSION_20260131.md:965`<br>`reports/PASS_TABLE_EVIDENCE.md:50` |
| `cve_2024_0582_uaf_check` | ✗ | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:55`<br>`reports/FACTS.md:106`<br>`reports/HANDOVER_SESSION_20260131.md:964` |
| `cve_2024_43047_verify` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:91`<br>`scripts/build_cve_2024_43047_verify.ps1:25` |
| `cve_2024_56720_probe` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:516`<br>`reports/missing_binaries_inventory_2026-09-13.md:92` |
| `cve_2025_21479_kaslr_leak` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:93` |
| `cve_2025_21479_kaslr_leak_shrinker` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:94` |
| `cve_2025_21479_kaslr_leak_v2` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:95` |
| `cve_2025_21479_probe_v5` | ✗ | ✓ exact | ✓ | `reports/FACTS.md:105`<br>`reports/missing_binaries_inventory_2026-09-13.md:96` |
| `cve_2025_21479_verify_cma` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:97` |
| `cve_2025_21479_verify_uaf` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:98` |
| `cve_22555_check` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:2525`<br>`reports/VULN_VERIFICATION_INDEX.md:12`<br>`reports/missing_binaries_inventory_2026-09-13.md:99` |
| `cve_22555_test` | ✗ | ✗ none | ✗ | `reports/VULN_VERIFICATION_INDEX.md:131`<br>`reports/missing_binaries_inventory_2026-09-13.md:100` |
| `cve_5_10_x_scanner` | ✗ | ✓ exact | ✗ | `reports/CVE_5_10_x_Scan_Report.md:132`<br>`reports/HANDOVER_SESSION_20260131.md:938`<br>`reports/VULN_VERIFICATION_INDEX.md:23` |
| `dirtypipe_test` | ✗ | ✓ exact | ✓ | `reports/5.2_VERIFICATION_2026.md:102`<br>`reports/HANDOVER_SESSION_20260131.md:2248`<br>`reports/PASS_TABLE_EVIDENCE.md:53` |
| `dirtypipe_test_arm64` | ✗ | ~ variant | ✗ | `reports/VULN_VERIFICATION_INDEX.md:123`<br>`reports/missing_binaries_inventory_2026-09-13.md:103` |
| `dirtypipe_test_dyn` | ✗ | ~ variant | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:86`<br>`reports/missing_binaries_inventory_2026-09-13.md:104` |
| `dirtypipe_test_new` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:3429`<br>`reports/missing_binaries_inventory_2026-09-13.md:105` |
| `drm_privesc_probe` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:5034`<br>`reports/missing_binaries_inventory_2026-09-13.md:106` |
| `drm_render` | ✗ | ~ variant | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:107` |
| `dsp_audit_blind` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:108` |
| `dsp_hidl_base_diag` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:109` |
| `dsp_hidl_session_diag` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:110` |
| `dsp_hidl_symbol_probe` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:111` |
| `dsp_poc` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:112` |
| `dsp_poc_standalone` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:113` |
| `dsp_raw_binder` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:114` |
| `dsp_raw_v2` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:115` |
| `forktest` | ✗ | ✓ exact | ✓ | `reports/GHOSTLOCK_HANDOFF_V2.md:140`<br>`reports/GHOSTLOCK_HANDOFF_V3.md:24`<br>`reports/NEXT_AGENT_PROMPT.md:32` |
| `gate_cmd_enum` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:117` |
| `gate_probe` | ✗ | ~ variant | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:118` |
| `gate_proto_probe` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:119` |
| `gate_ta_load` | ✗ | ✗ none | ✗ | `reports/RESEARCH_FINDINGS_20260202.md:135`<br>`reports/missing_binaries_inventory_2026-09-13.md:120` |
| `gate_test` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:3408`<br>`reports/missing_binaries_inventory_2026-09-13.md:121` |
| `gpu_context_bypass` | ✗ | ✓ exact | ✓ | `reports/CVE-2025-21479_Analysis_Report.md:207`<br>`reports/HANDOVER_SESSION_20260131.md:337`<br>`reports/missing_binaries_inventory_2026-09-13.md:122` |
| `gpu_context_bypass_dyn` | ✗ | ~ variant | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:66`<br>`reports/missing_binaries_inventory_2026-09-13.md:123` |
| `i2c_device_explore` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:137`<br>`reports/missing_binaries_inventory_2026-09-13.md:124` |
| `io_uring_capability_probe` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:940`<br>`reports/VULN_VERIFICATION_INDEX.md:42`<br>`reports/missing_binaries_inventory_2026-09-13.md:125` |
| `io_uring_functional_test` | ✗ | ✓ exact | ✗ | `reports/VULN_VERIFICATION_INDEX.md:43`<br>`reports/missing_binaries_inventory_2026-09-13.md:126` |
| `io_uring_lifecycle` | ✗ | ✓ exact | ✗ | `reports/VULN_VERIFICATION_INDEX.md:49`<br>`reports/missing_binaries_inventory_2026-09-13.md:127` |
| `io_uring_probe` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:939`<br>`reports/PASS_TABLE_EVIDENCE.md:50`<br>`reports/VULN_VERIFICATION_INDEX.md:40` |
| `io_uring_uaf_crash` | ✗ | ✓ exact | ✓ | `reports/VULN_VERIFICATION_INDEX.md:53`<br>`reports/missing_binaries_inventory_2026-09-13.md:129` |
| `io_uring_uaf_verify` | ✗ | ✓ exact | ✗ | `reports/FINAL_STATUS_20260202.md:156`<br>`reports/PASS_TABLE_EVIDENCE.md:50`<br>`reports/VULN_VERIFICATION_INDEX.md:51` |
| `io_uring_uaf_verify_v2` | ✗ | ~ variant | ✗ | `reports/VULN_VERIFICATION_INDEX.md:51`<br>`reports/missing_binaries_inventory_2026-09-13.md:131` |
| `io_uring_uaf_verify_v3` | ✗ | ~ variant | ✗ | `reports/VULN_VERIFICATION_INDEX.md:51`<br>`reports/missing_binaries_inventory_2026-09-13.md:132` |
| `io_uring_uaf_verify_v4` | ✗ | ~ variant | ✗ | `reports/VULN_VERIFICATION_INDEX.md:51`<br>`reports/missing_binaries_inventory_2026-09-13.md:133` |
| `kaslr_leak` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:134` |
| `kernelsnitch` | ✗ | ✗ none | ✗ | `reports/APK_TW_Tech_Report_Rokid_Root.md:222`<br>`reports/GHOSTLOCK_HANDOFF_V11.md:9`<br>`reports/GHOSTLOCK_HANDOFF_V3.md:48` |
| `kernelsnitch_bruteforce` | ✗ | ✗ none | ✗ | `reports/kernelsnitch_mm_leak_clean_boot_2026-09-13.md:31`<br>`reports/missing_binaries_inventory_2026-09-13.md:136` |
| `kgsl_auto_scanner` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:240`<br>`reports/missing_binaries_inventory_2026-09-13.md:137` |
| `kgsl_aux_fuzz` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:138` |
| `kgsl_cve_check` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:139` |
| `kgsl_debug_probe` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:239`<br>`reports/missing_binaries_inventory_2026-09-13.md:140` |
| `kgsl_ioctl_probe` | ✗ | ✓ exact | ✗ | `deploy/build_kgsl_probe.sh:16`<br>`reports/missing_binaries_inventory_2026-09-13.md:141` |
| `kgsl_micronode_probe` | ✗ | ✓ exact | ✓ | `docs/deploy.md:441`<br>`reports/FACTS.md:148`<br>`reports/HANDOVER_SESSION_20260131.md:4808` |
| `kgsl_micronode_probe2` | ✗ | ✓ exact | ✓ | `reports/FACTS.md:149`<br>`reports/missing_binaries_inventory_2026-09-13.md:143` |
| `kgsl_mmap_probe` | ✗ | ✓ exact | ✓ | `reports/CVE_2025_21479_EXPLOIT_SUMMARY.md:100`<br>`reports/missing_binaries_inventory_2026-09-13.md:144` |
| `kgsl_mmap_probe_dyn` | ✗ | ~ variant | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:118`<br>`reports/missing_binaries_inventory_2026-09-13.md:145` |
| `kgsl_probe` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:146` |
| `kgsl_race` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:1963`<br>`reports/PASS_TABLE_EVIDENCE.md:55`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:138` |
| `kgsl_scmver` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:148`<br>`scripts/check_1.19_vulns.py:54` |
| `kgsl_size_check` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2397`<br>`reports/VULN_VERIFICATION_INDEX.md:70`<br>`reports/missing_binaries_inventory_2026-09-13.md:149` |
| `kgsl_syncuaf_probe` | ✓ `source_code/kgsl_syncuaf_probe` | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:41`<br>`reports/FACTS.md:99`<br>`reports/missing_binaries_inventory_2026-09-13.md:150` |
| `kgsl_test` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:1959`<br>`reports/VULN_VERIFICATION_INDEX.md:11`<br>`reports/missing_binaries_inventory_2026-09-13.md:151` |
| `kgsl_vermagic` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:152`<br>`scripts/check_1.19_vulns.py:52` |
| `microtest` | ✗ | ✓ exact | ✓ | `reports/GHOSTLOCK_HANDOFF_V3.md:145`<br>`reports/missing_binaries_inventory_2026-09-13.md:153` |
| `mmfree_probe` | ✗ | ✓ exact | ✓ | `reports/GHOSTLOCK_V5_RECLAIM_MECHANISM.md:6`<br>`reports/missing_binaries_inventory_2026-09-13.md:154` |
| `mmfree_probe2` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:155` |
| `mmfree_probe3` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:156` |
| `mmfree_probe5` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:157` |
| `msm_drm_probe` | ✗ | ✓ exact | ✗ | `reports/PRIV_ESC_SESSION_10.md:123`<br>`reports/VULN_VERIFICATION_INDEX.md:11`<br>`reports/docs_correctness_audit_2026-09-13.md:79` |
| `perf_probe` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:159` |
| `privesc_scanner` | ✗ | ✓ exact | ✗ | `reports/PRIVESC_REPORT.md:163`<br>`reports/missing_binaries_inventory_2026-09-13.md:160` |
| `scan_wide` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:5930`<br>`reports/missing_binaries_inventory_2026-09-13.md:161`<br>`scripts/gate_exec_breakthrough.py:37` |
| `slabprobe` | ✗ | ✓ exact | ✓ | `reports/GHOSTLOCK_HANDOFF_V2.md:44`<br>`reports/NEXT_AGENT_PROMPT.md:32`<br>`reports/missing_binaries_inventory_2026-09-13.md:162` |
| `test_0582` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:163` |
| `test_21479` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:164` |
| `test_36971` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:165` |
| `test_adsp` | ✓ `source_code/test_adsp` | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:75`<br>`reports/missing_binaries_inventory_2026-09-13.md:166` |
| `test_align` | ✓ `source_code/test_align` | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:34`<br>`reports/missing_binaries_inventory_2026-09-13.md:167` |
| `test_attach` | ✓ `source_code/test_attach` | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:168` |
| `test_bpf` | ✓ `source_code/test_bpf` | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:169` |
| `test_bpf_prog` | ✓ `source_code/test_bpf_prog` | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:170` |
| `test_bpf_subprog` | ✓ `source_code/test_bpf_subprog` | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:171` |
| `test_bpf_valid` | ✓ `source_code/test_bpf_valid` | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:172` |
| `test_cve41003` | ✓ `source_code/test_cve41003` | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:173` |
| `test_gpumem` | ✓ `source_code/test_gpumem` | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:33`<br>`reports/FACTS.md:103`<br>`reports/missing_binaries_inventory_2026-09-13.md:174` |
| `test_hdr` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:175` |
| `test_import` | ✓ `source_code/test_import` | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:35`<br>`reports/missing_binaries_inventory_2026-09-13.md:176` |
| `test_jitfold` | ✓ `source_code/test_jitfold` | ✓ exact | ✓ | `reports/SESSION_15_SUMMARY.md:55`<br>`reports/missing_binaries_inventory_2026-09-13.md:177` |
| `test_memuninit` | ✓ `source_code/test_memuninit` | ✓ exact | ✓ | `reports/SESSION_15_SUMMARY.md:38`<br>`reports/missing_binaries_inventory_2026-09-13.md:178` |
| `test_recv` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:179` |
| `test_recv2` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:180` |
| `test_sync_errno` | ✓ `source_code/test_sync_errno` | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:40`<br>`reports/FACTS.md:104`<br>`reports/missing_binaries_inventory_2026-09-13.md:181` |
| `test_tailcall` | ✓ `source_code/test_tailcall` | ✓ exact | ✓ | `reports/SESSION_15_SUMMARY.md:80`<br>`reports/missing_binaries_inventory_2026-09-13.md:182` |
| `test_uaf_base` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:183` |
| `test_uaf_read` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:184` |
| `tipc_probe` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2219`<br>`reports/PASS_TABLE_EVIDENCE.md:59`<br>`reports/PRIV_ESC_SESSION_10.md:122` |
| `trigger_warn` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:186` |

### 3.2 exploit (119)

| name | in repo? | source? | on device? | referenced by |
|---|---|---|---|---|
| `adsprpc_secure_exploit` | ✗ | ✓ exact | ✓ | `reports/VULN_VERIFICATION_INDEX.md:174`<br>`reports/missing_binaries_inventory_2026-09-13.md:192` |
| `ashmem_kgsl_bridge` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:4935`<br>`reports/missing_binaries_inventory_2026-09-13.md:193` |
| `ashmem_overflow` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:2210`<br>`reports/VULN_VERIFICATION_INDEX.md:12`<br>`reports/docs_correctness_audit_2026-09-13.md:80` |
| `ashmem_overflow_exploit` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2152`<br>`reports/PASS_TABLE_EVIDENCE.md:57`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:20` |
| `binder_ashmem_attack` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:196` |
| `binder_bpf_exploit` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2417`<br>`reports/PASS_TABLE_EVIDENCE.md:56`<br>`reports/VULN_VERIFICATION_INDEX.md:11` |
| `binder_uaf_v2` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:2966`<br>`reports/VULN_VERIFICATION_INDEX.md:12`<br>`reports/docs_correctness_audit_2026-09-13.md:36` |
| `binder_uaf_v3` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:2967`<br>`reports/VULN_VERIFICATION_INDEX.md:95`<br>`reports/missing_binaries_inventory_2026-09-13.md:199` |
| `binder_uaf_v4` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:2968`<br>`reports/VULN_VERIFICATION_INDEX.md:95`<br>`reports/missing_binaries_inventory_2026-09-13.md:200` |
| `binder_uaf_v5` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2969`<br>`reports/PASS_TABLE_EVIDENCE.md:56`<br>`reports/Rokid_RedTeam_Internal_Report.md:257` |
| `bpf_5_10_exploit` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:3879`<br>`reports/SESSION_14_SUMMARY.md:60`<br>`reports/missing_binaries_inventory_2026-09-13.md:202` |
| `bpf_advanced` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:3882`<br>`reports/SESSION_14_SUMMARY.md:63`<br>`reports/missing_binaries_inventory_2026-09-13.md:203` |
| `bpf_bypass_v2` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:3883`<br>`reports/SESSION_14_SUMMARY.md:64`<br>`reports/missing_binaries_inventory_2026-09-13.md:204` |
| `bpf_deep_probe` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:205` |
| `bpf_info_leak` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:206` |
| `bpf_infoleak_v2` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:207` |
| `bpf_kaslr_exploit` | ✗ | ✓ exact | ✗ | `reports/CVE-2023-2163_BPF_Verifier_分析.md:324`<br>`reports/VULN_VERIFICATION_INDEX.md:11`<br>`reports/docs_correctness_audit_2026-09-13.md:79` |
| `bpf_leak_v3` | ✗ | ✓ exact | ✓ | `reports/HANDOVER_SESSION_20260131.md:12`<br>`reports/VULN_VERIFICATION_INDEX.md:173`<br>`reports/missing_binaries_inventory_2026-09-13.md:209` |
| `bpf_oob_read` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:210` |
| `bpf_ringbuf_exploit` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:3880`<br>`reports/SESSION_14_SUMMARY.md:61`<br>`reports/missing_binaries_inventory_2026-09-13.md:211` |
| `bpf_ringbuf_oob_v1` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:989`<br>`reports/missing_binaries_inventory_2026-09-13.md:212` |
| `bpf_ringbuf_oob_v5` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:4477`<br>`reports/missing_binaries_inventory_2026-09-13.md:213` |
| `bpf_ringbuf_oob_v6` | ✗ | ✓ exact | ✗ | `reports/FINAL_STATUS_20260202.md:154`<br>`reports/HANDOVER_SESSION_20260131.md:990`<br>`reports/missing_binaries_inventory_2026-09-13.md:214` |
| `bpf_sandbox_kaslr_leak` | ✗ | ✓ exact | ✓ | `reports/HANDOVER_SESSION_20260131.md:12`<br>`reports/missing_binaries_inventory_2026-09-13.md:215` |
| `bpf_size_confusion` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:216` |
| `bpf_stack_exploit` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:217` |
| `bpf_verifier_exploit` | ✗ | ✓ exact | ✗ | `reports/ATTACK_SURFACE_RESEARCH.md:388`<br>`reports/HANDOVER_SESSION_20260131.md:3613`<br>`reports/missing_binaries_inventory_2026-09-13.md:218` |
| `build2_adsprpc_secure_exploit` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:219` |
| `build2_bpf_leak_v3` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:220` |
| `build2_cve_2021_22555_test` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:221` |
| `build2_cve_2024_0582_mmap_uaf` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:222` |
| `build2_kgsl_uaf_exploit` | ✗ | ✗ none | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:223` |
| `cgroup_bpf_exploit` | ✗ | ✓ exact | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:224` |
| `cross_cache_exploit` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:3881`<br>`reports/SESSION_14_SUMMARY.md:62`<br>`reports/missing_binaries_inventory_2026-09-13.md:225` |
| `cve_2024_0582_exploit` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:2320`<br>`reports/missing_binaries_inventory_2026-09-13.md:226` |
| `cve_2024_36971_bpf_spray` | ✗ | ✓ exact | ✓ | `reports/5.2_VERIFICATION_2026.md:66`<br>`reports/HANDOVER_SESSION_20260131.md:949`<br>`reports/PASS_TABLE_EVIDENCE.md:54` |
| `cve_2024_36971_cross_cache` | ✗ | ✓ exact | ✓ | `reports/5.2_VERIFICATION_2026.md:66`<br>`reports/missing_binaries_inventory_2026-09-13.md:228` |
| `cve_2024_36971_poc` | ✗ | ✓ exact | ✓ | `reports/5.2_VERIFICATION_LIVE_ADB_2026.md:48`<br>`reports/VULN_VERIFICATION_INDEX.md:59`<br>`reports/missing_binaries_inventory_2026-09-13.md:229` |
| `cve_2024_36971_test` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:3071`<br>`reports/PASS_TABLE_EVIDENCE.md:54`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:120` |
| `cve_2024_36971_v10` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:2349`<br>`reports/missing_binaries_inventory_2026-09-13.md:231` |
| `cve_2024_36971_v11` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:2457`<br>`reports/missing_binaries_inventory_2026-09-13.md:232`<br>`scripts/run_v11.ps1:3` |
| `cve_2024_36971_v12` | ✗ | ~ variant | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:233` |
| `cve_2024_36971_v13` | ✗ | ✓ exact | ✗ | `reports/5.2_VERIFICATION_2026.md:66`<br>`reports/FINAL_STATUS_20260202.md:153`<br>`reports/HANDOVER_SESSION_20260131.md:2925` |
| `cve_2024_36971_v2` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:950`<br>`reports/VULN_VERIFICATION_INDEX.md:12`<br>`reports/docs_correctness_audit_2026-09-13.md:80` |
| `cve_2024_36971_v3` | ✗ | ~ variant | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:236` |
| `cve_2024_36971_v4` | ✗ | ~ variant | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:237` |
| `cve_2024_36971_v5` | ✗ | ~ variant | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:238` |
| `cve_2024_36971_v6` | ✗ | ~ variant | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:239` |
| `cve_2024_36971_v7` | ✗ | ~ variant | ✗ | `reports/CVE-2024-36971_攻堅分析報告.md:382`<br>`reports/missing_binaries_inventory_2026-09-13.md:240` |
| `cve_2024_36971_v8` | ✗ | ~ variant | ✗ | `reports/CVE-2024-36971_攻堅分析報告.md:386`<br>`reports/missing_binaries_inventory_2026-09-13.md:241` |
| `cve_2024_36971_v9` | ✗ | ~ variant | ✗ | `reports/CVE-2024-36971_深度攻堅報告_v2.md:544`<br>`reports/HANDOVER_SESSION_20260131.md:948`<br>`reports/missing_binaries_inventory_2026-09-13.md:242` |
| `cve_2024_36971_wifi` | ✗ | ✓ exact | ✗ | `reports/5.2_VERIFICATION_2026.md:66`<br>`reports/missing_binaries_inventory_2026-09-13.md:243` |
| `cve_36971_v13` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:2926`<br>`reports/VULN_VERIFICATION_INDEX.md:64`<br>`reports/missing_binaries_inventory_2026-09-13.md:244` |
| `cve_36971_v9` | ✗ | ✗ none | ✗ | `reports/VULN_VERIFICATION_INDEX.md:64`<br>`reports/missing_binaries_inventory_2026-09-13.md:245` |
| `drm_msm_exploit` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:4218`<br>`reports/missing_binaries_inventory_2026-09-13.md:246` |
| `dsp_ion_attack` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:1039`<br>`reports/missing_binaries_inventory_2026-09-13.md:247` |
| `exploit_cve_2021_41073` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:248` |
| `exploit_cve_2021_41073_sim` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:249` |
| `exploit_kaslr_leak` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:250` |
| `exploit_kaslr_leak2` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:251` |
| `exploit_kaslr_leak3` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:252` |
| `exploit_kaslr_leak4` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:253` |
| `exploit_kaslr_leak5` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:254` |
| `exploit_kaslr_leak6` | ✗ | ✓ exact | ✓ | `reports/missing_binaries_inventory_2026-09-13.md:255` |
| `gate_adv` | ✗ | ✗ none | ✗ | `reports/APK_TW_Tech_Report_Rokid_Root.md:537`<br>`reports/RESEARCH_FINDINGS_20260202.md:128`<br>`reports/missing_binaries_inventory_2026-09-13.md:256` |
| `gate_exploit` | ✗ | ~ variant | ✗ | `reports/APK_TW_Tech_Report_Rokid_Root.md:536`<br>`reports/HANDOVER_SESSION_20260131.md:3368`<br>`reports/RESEARCH_FINDINGS_20260202.md:127` |
| `gate_fuzzer_v3` | ✗ | ✗ none | ✗ | `reports/GateServiced_LPE_Research_Plan.md:366`<br>`reports/Rokid_RedTeam_Internal_Report.md:800`<br>`reports/missing_binaries_inventory_2026-09-13.md:258` |
| `gate_rce` | ✗ | ✗ none | ✗ | `reports/APK_TW_Tech_Report_Rokid_Root.md:538`<br>`reports/RESEARCH_FINDINGS_20260202.md:129`<br>`reports/missing_binaries_inventory_2026-09-13.md:259` |
| `gate_ta` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:260` |
| `gateserviced_explorer` | ✗ | ✗ none | ✗ | `reports/GateServiced_LPE_Research_Plan.md:492`<br>`reports/missing_binaries_inventory_2026-09-13.md:261` |
| `gateserviced_fuzzer` | ✗ | ✓ exact | ✗ | `reports/APK_Vulnerability_Analysis_Report.md:118`<br>`reports/HANDOVER_SESSION_20260131.md:3413`<br>`reports/Rokid_RedTeam_Internal_Report.md:829` |
| `gateserviced_hook` | ✗ | ✗ none | ✗ | `reports/APK_Vulnerability_Analysis_Report.md:264`<br>`reports/missing_binaries_inventory_2026-09-13.md:263` |
| `gateserviced_uaf_exploit` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:3342`<br>`reports/missing_binaries_inventory_2026-09-13.md:264` |
| `gpu_cmdbuf_spy` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:749`<br>`reports/PARASITIC_INJECT_ATTACK.md:185`<br>`reports/PARASITIC_INJECT_STATUS.md:164` |
| `i2c_attack` | ✗ | ✓ exact | ✗ | `reports/PRIVESC_REPORT.md:167`<br>`reports/missing_binaries_inventory_2026-09-13.md:266` |
| `io_uring_sqpoll_exploit` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:4217`<br>`reports/missing_binaries_inventory_2026-09-13.md:267` |
| `kgsl_advanced_exploit` | ✗ | ✓ exact | ✗ | `reports/KGSL_RESEARCH_SESSION_11.md:236`<br>`reports/missing_binaries_inventory_2026-09-13.md:268` |
| `kgsl_bpf_spray` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2034`<br>`reports/PASS_TABLE_EVIDENCE.md:55`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:139` |
| `kgsl_bpf_spray_v3` | ✗ | ~ variant | ✗ | `reports/VULN_VERIFICATION_INDEX.md:75`<br>`reports/docs_correctness_audit_2026-09-13.md:80`<br>`reports/missing_binaries_inventory_2026-09-13.md:270` |
| `kgsl_bpf_spray_v4` | ✗ | ~ variant | ✗ | `reports/CVE-2024-38401_Test_Report.md:80`<br>`reports/VULN_VERIFICATION_INDEX.md:75`<br>`reports/missing_binaries_inventory_2026-09-13.md:271` |
| `kgsl_bpf_spray_v5` | ✗ | ~ variant | ✗ | `reports/CVE-2024-38401_Test_Report.md:81`<br>`reports/VULN_VERIFICATION_INDEX.md:75`<br>`reports/missing_binaries_inventory_2026-09-13.md:272` |
| `kgsl_bpf_spray_v6` | ✗ | ~ variant | ✗ | `reports/CVE-2024-38401_Test_Report.md:82`<br>`reports/VULN_VERIFICATION_INDEX.md:75`<br>`reports/missing_binaries_inventory_2026-09-13.md:273` |
| `kgsl_bypass_research` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:337`<br>`reports/missing_binaries_inventory_2026-09-13.md:274` |
| `kgsl_cmd_inject` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:337`<br>`reports/KGSL_FINAL_REPORT.md:160`<br>`reports/missing_binaries_inventory_2026-09-13.md:275` |
| `kgsl_cross_fd_exploit` | ✗ | ✓ exact | ✗ | `reports/ATTACK_SUMMARY.md:47`<br>`reports/ATTACK_SURFACE_RESEARCH.md:87`<br>`reports/HANDOVER_SESSION_20260131.md:634` |
| `kgsl_cross_process_attack` | ✗ | ✓ exact | ✗ | `reports/KGSL_FINAL_REPORT.md:300`<br>`reports/missing_binaries_inventory_2026-09-13.md:277` |
| `kgsl_crossfd_exploit` | ✗ | ✓ exact | ✗ | `reports/CVE_2025_21479_EXPLOIT_SUMMARY.md:101`<br>`reports/missing_binaries_inventory_2026-09-13.md:278` |
| `kgsl_deep_research` | ✗ | ✓ exact | ✗ | `reports/PRIVESC_REPORT.md:164`<br>`reports/missing_binaries_inventory_2026-09-13.md:279` |
| `kgsl_exploit_v1` | ✗ | ~ variant | ✗ | `reports/HANDOVER_SESSION_20260131.md:1002`<br>`reports/missing_binaries_inventory_2026-09-13.md:280` |
| `kgsl_exploit_v3` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:1004`<br>`reports/missing_binaries_inventory_2026-09-13.md:281` |
| `kgsl_exploit_v7` | ✗ | ~ variant | ✗ | `reports/VULN_VERIFICATION_INDEX.md:12`<br>`reports/docs_correctness_audit_2026-09-13.md:80`<br>`reports/missing_binaries_inventory_2026-09-13.md:282` |
| `kgsl_exploit_v8` | ✗ | ~ variant | ✗ | `reports/VULN_VERIFICATION_INDEX.md:85`<br>`reports/missing_binaries_inventory_2026-09-13.md:283` |
| `kgsl_exploit_v9` | ✗ | ✓ exact | ✗ | `reports/FINAL_STATUS_20260202.md:155`<br>`reports/VULN_VERIFICATION_INDEX.md:85`<br>`reports/missing_binaries_inventory_2026-09-13.md:284` |
| `kgsl_fd_confusion_exploit` | ✗ | ✓ exact | ✗ | `reports/ATTACK_SURFACE_RESEARCH.md:391`<br>`reports/KGSL_FINAL_REPORT.md:301`<br>`reports/missing_binaries_inventory_2026-09-13.md:285` |
| `kgsl_final_exploit` | ✗ | ✓ exact | ✗ | `reports/KGSL_FINAL_REPORT.md:305`<br>`reports/missing_binaries_inventory_2026-09-13.md:286` |
| `kgsl_gpu_reset_exploit` | ✗ | ✓ exact | ✗ | `reports/KGSL_FINAL_REPORT.md:304`<br>`reports/missing_binaries_inventory_2026-09-13.md:287` |
| `kgsl_keyring_spray` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2206`<br>`reports/PRIV_ESC_SESSION_10.md:121`<br>`reports/VULN_VERIFICATION_INDEX.md:79` |
| `kgsl_massive_spray` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:1006`<br>`reports/missing_binaries_inventory_2026-09-13.md:289` |
| `kgsl_msg_spray` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2089`<br>`reports/VULN_VERIFICATION_INDEX.md:81`<br>`reports/missing_binaries_inventory_2026-09-13.md:290` |
| `kgsl_offset_exploit` | ✗ | ✓ exact | ✗ | `reports/ATTACK_SURFACE_RESEARCH.md:88`<br>`reports/KGSL_FINAL_REPORT.md:302`<br>`reports/missing_binaries_inventory_2026-09-13.md:291` |
| `kgsl_parasitic_inject` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:747`<br>`reports/PARASITIC_INJECT_STATUS.md:162`<br>`reports/missing_binaries_inventory_2026-09-13.md:292` |
| `kgsl_pipe_spray` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2207`<br>`reports/PRIV_ESC_SESSION_10.md:120`<br>`reports/VULN_VERIFICATION_INDEX.md:80` |
| `kgsl_shadow_v2` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:1005`<br>`reports/missing_binaries_inventory_2026-09-13.md:294` |
| `kgsl_syncsource_uaf` | ✗ | ✓ exact | ✗ | `reports/Rokid_RedTeam_Internal_Report.md:261`<br>`reports/missing_binaries_inventory_2026-09-13.md:295` |
| `kgsl_uaf_ebpf_exploit` | ✗ | ✓ exact | ✗ | `reports/CVE_2025_21479_EXPLOIT_SUMMARY.md:103`<br>`reports/missing_binaries_inventory_2026-09-13.md:296` |
| `kgsl_uaf_exploit` | ✗ | ✓ exact | ✗ | `reports/VULN_VERIFICATION_INDEX.md:173`<br>`reports/missing_binaries_inventory_2026-09-13.md:297` |
| `kgsl_uaf_gpu_overlap` | ✗ | ✓ exact | ✗ | `reports/CVE_2025_21479_EXPLOIT_SUMMARY.md:102`<br>`reports/missing_binaries_inventory_2026-09-13.md:298` |
| `kgsl_uaf_v2` | ✗ | ✓ exact | ✗ | `reports/PRIVESC_REPORT.md:165`<br>`reports/missing_binaries_inventory_2026-09-13.md:299` |
| `kgsl_uffd_exploit` | ✗ | ✓ exact | ✗ | `reports/PRIV_ESC_SESSION_10.md:119`<br>`reports/VULN_VERIFICATION_INDEX.md:82`<br>`reports/missing_binaries_inventory_2026-09-13.md:300` |
| `kgsl_wait_exploit` | ✗ | ✓ exact | ✗ | `reports/KGSL_FINAL_REPORT.md:303`<br>`reports/missing_binaries_inventory_2026-09-13.md:301` |
| `netlink_tipc_exploit` | ✗ | ✓ exact | ✗ | `reports/ATTACK_SURFACE_RESEARCH.md:244`<br>`reports/HANDOVER_SESSION_20260131.md:3611`<br>`reports/missing_binaries_inventory_2026-09-13.md:302` |
| `parasitic_inject_v3` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:572`<br>`reports/PARASITIC_INJECT_STATUS.md:165`<br>`reports/missing_binaries_inventory_2026-09-13.md:303` |
| `ringbuf_leak` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:4036`<br>`reports/missing_binaries_inventory_2026-09-13.md:304` |
| `sf_ib_inject_v2` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:572`<br>`reports/PARASITIC_INJECT_STATUS.md:167`<br>`reports/missing_binaries_inventory_2026-09-13.md:305` |
| `spi_serial_attack` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:1038`<br>`reports/missing_binaries_inventory_2026-09-13.md:306` |
| `surfaceflinger_gpu_monitor` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:238`<br>`reports/missing_binaries_inventory_2026-09-13.md:307` |
| `surfaceflinger_ib_hijack` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:748`<br>`reports/PARASITIC_INJECT_ATTACK.md:186`<br>`reports/PARASITIC_INJECT_STATUS.md:163` |
| `tipc_exploit` | ✗ | ✓ exact | ✗ | `reports/HANDOVER_SESSION_20260131.md:2208`<br>`reports/PASS_TABLE_EVIDENCE.md:59`<br>`reports/VULN_IMPLEMENTATION_AUDIT.md:16` |
| `tipc_uaf_exploit` | ✗ | ✓ exact | ✗ | `reports/ATTACK_SURFACE_RESEARCH.md:256`<br>`reports/HANDOVER_SESSION_20260131.md:3612`<br>`reports/missing_binaries_inventory_2026-09-13.md:310` |

### 3.3 daemon (13)

| name | in repo? | source? | on device? | referenced by |
|---|---|---|---|---|
| `ghostlock_bridge` | ✗ | ✗ none | ✗ | `deploy/get_root_now.sh:57`<br>`deploy/lease.sh:56`<br>`deploy/root_bridge.sh:136` |
| `ghostlock_rv101` | ✗ | ✓ exact | ✓ | `deploy/deploy.sh:24`<br>`deploy/get_root_now.sh:2`<br>`deploy/lease.sh:6` |
| `hal_proxy_daemon` | ✗ | ✗ none | ✗ | `reports/GateServiced_LPE_Research_Plan.md:473`<br>`reports/missing_binaries_inventory_2026-09-13.md:318` |
| `lights_ctrl` | ✗ | ✗ none | ✗ | `docs/architecture.md:121`<br>`docs/device-debug-notes.md:272`<br>`reports/RV101_CAMERA_LED_FLASH_MECHANISM.md:76` |
| `magisk` | ✗ | ✗ none | ✓ `magisk/` | `reports/APK_TW_Tech_Report_Rokid_Root.md:514`<br>`reports/HANDOVER_SESSION_20260131.md:1678`<br>`reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md:110` |
| `magiskboot` | ✗ | ✗ none | ✓ `magisk/` | `reports/Rokid_Root_Guide_boot_img.md:210`<br>`reports/missing_binaries_inventory_2026-09-13.md:321` |
| `magiskinit` | ✗ | ✗ none | ✓ `magisk/` | `reports/Rokid_Root_Guide_boot_img.md:232`<br>`reports/missing_binaries_inventory_2026-09-13.md:322` |
| `ring_input_daemon` | ✗ | ✓ exact | ✗ | `deploy/build_ring_input_daemon.sh:11`<br>`reports/missing_binaries_inventory_2026-09-13.md:323` |
| `root_daemon` | ✓ `deploy/root_daemon` | ✓ exact | ✓ | `deploy/build_kgsl_probe.sh:2`<br>`deploy/build_root_daemon.sh:22`<br>`deploy/get_root_now.sh:2` |
| `run_shell` | ✓ `deploy/run_shell` | ✓ exact | ✗ | `docs/deploy.md:201`<br>`docs/shizuku-feasibility-report.md:109`<br>`reports/missing_binaries_inventory_2026-09-13.md:325` |
| `shizuku_server` | ✗ | ✗ none | ✗ | `deploy/start_shizuku.sh:66`<br>`deploy/test_bridge_fallback.sh:30`<br>`deploy/test_keeper_lease_guard.sh:98` |
| `su` | ✗ | ✗ none | ✓ | `docs/shizuku-feasibility-report.md:64`<br>`reports/FACTS.md:195`<br>`reports/GHOSTLOCK_HANDOFF_V13.md:120` |
| `su_gl` | ✗ | ✗ none | ✓ | `docs/device-debug-notes.md:26`<br>`reports/GHOSTLOCK_HANDOFF_V12.md:183`<br>`reports/GHOSTLOCK_ROOT_操作說明.md:107` |

### 3.4 firmware image (45)

| name | in repo? | source? | on device? | referenced by |
|---|---|---|---|---|
| `Ennea.img` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:334`<br>`reports/rootWithoutUnlockBootloaderList_EN_with_link_summaries.md:482` |
| `boot.img` | ✗ | ✗ none | ✓ `magisk/` | `deploy/check_ota.sh:215`<br>`deploy/ghostlock_patch_check.py:8`<br>`docs/device-debug-notes.md:187` |
| `boot_a.img` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:1622`<br>`reports/Rokid_Attack_Vector_Summary.md:192`<br>`reports/Rokid_Root_Guide_boot_img.md:146` |
| `boot_b.img` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:337` |
| `boot_dump.img` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:338` |
| `boot_patched.img` | ✗ | ✗ none | ✗ | `reports/PARASITIC_INJECT_STATUS.md:134`<br>`reports/missing_binaries_inventory_2026-09-13.md:339` |
| `custom_recovery.img` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:1899`<br>`reports/missing_binaries_inventory_2026-09-13.md:340` |
| `fake_ta.bin` | ✗ | ✗ none | ✗ | `reports/GateServiced_Phase2_Exploitation_Playbook.md:252`<br>`reports/missing_binaries_inventory_2026-09-13.md:341` |
| `gate_cmd.bin` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:342` |
| `gate_exploit.bin` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:3375`<br>`reports/missing_binaries_inventory_2026-09-13.md:343` |
| `init_boot` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:1864`<br>`reports/missing_binaries_inventory_2026-09-13.md:344` |
| `kernel` | ✗ | ✗ none | ✓ `magisk/` | `deploy/check_ota.sh:82`<br>`deploy/get_root_now.sh:3`<br>`deploy/ghostlock_patch_check.py:9` |
| `magisk_patched-122_009.img` | ✗ | ✗ none | ✗ | `reports/REVERSE_ANALYSIS_1.22_009.md:79`<br>`reports/SESSION_16_UPDATE_ENGINE_PAYLOAD_GATES.md:9`<br>`reports/docs_correctness_audit_2026-09-13.md:81` |
| `magisk_patched-30600_9C56p.img` | ✗ | ✗ none | ✓ | `docs/device-debug-notes.md:238`<br>`docs/shizuku-feasibility-report.md:125`<br>`reports/HANDOVER_SESSION_20260131.md:19` |
| `magisk_patched.img` | ✗ | ✗ none | ✗ | `reports/PRIV_ESC_SESSION_10.md:136`<br>`reports/missing_binaries_inventory_2026-09-13.md:348` |
| `magisk_patched_boot.img` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:1604`<br>`reports/Rokid_Attack_Vector_Summary.md:52`<br>`reports/Rokid_Root_Guide_boot_img.md:207` |
| `magisk_patched_xxxxx.img` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:350` |
| `new-boot.img` | ✗ | ✗ none | ✓ `magisk/` | `reports/missing_binaries_inventory_2026-09-13.md:351` |
| `output.bin` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:3569`<br>`reports/missing_binaries_inventory_2026-09-13.md:352` |
| `patched.img` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:1146`<br>`reports/missing_binaries_inventory_2026-09-13.md:353` |
| `patched_boot.img` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:4459`<br>`reports/RESEARCH_PROGRESS_20260202.md:59`<br>`reports/missing_binaries_inventory_2026-09-13.md:354` |
| `payload.bin` | ✗ | ✗ none | ✗ | `deploy/ghostlock_patch_check.py:8`<br>`docs/device-debug-notes.md:187`<br>`reports/HANDOVER_SESSION_20260131.md:86` |
| `product.img` | ✗ | ✗ none | ✗ | `reports/REVERSE_ANALYSIS_1.22_009.md:17`<br>`reports/missing_binaries_inventory_2026-09-13.md:356`<br>`scripts/gen_vuln_report_20260523.py:252` |
| `ramdisk.cpio` | ✗ | ✗ none | ✓ `magisk/` | `reports/Rokid_Root_Guide_boot_img.md:227`<br>`reports/missing_binaries_inventory_2026-09-13.md:357` |
| `recovery.img` | ✗ | ✗ none | ✓ `magisk/` | `reports/REVERSE_ANALYSIS_1.22_009.md:12`<br>`reports/Rokid_Root_Guide_boot_img.md:362`<br>`reports/missing_binaries_inventory_2026-09-13.md:358` |
| `repacked.img` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:359`<br>`scripts/unpack_bootimg.py:504` |
| `sepolicy.bin` | ✗ | ✗ none | ✗ | `reports/RG-glasses_RV101_vulnerability-pentest-matrix_v5.md:291`<br>`reports/missing_binaries_inventory_2026-09-13.md:360`<br>`scripts/Collect-RV101-Validation.ps1:315` |
| `source.bin` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:361` |
| `stock_boot.img` | ✗ | ✗ none | ✓ `magisk/` | `reports/missing_binaries_inventory_2026-09-13.md:362` |
| `system.img` | ✗ | ✗ none | ✗ | `reports/ADB_Media_Exfiltration_Report.md:250`<br>`reports/GateServiced_LPE_Research_Plan.md:706`<br>`reports/REVERSE_ANALYSIS_1.22_009.md:18` |
| `twrp.img` | ✗ | ✗ none | ✗ | `reports/FINAL_STATUS_20260202.md:125`<br>`reports/HANDOVER_SESSION_20260131.md:1145`<br>`reports/PARASITIC_INJECT_STATUS.md:133` |
| `twrp_sm6375.img` | ✗ | ✗ none | ✗ | `reports/APK_TW_Tech_Report_Rokid_Root.md:246`<br>`reports/missing_binaries_inventory_2026-09-13.md:365` |
| `uefi.img` | ✗ | ✗ none | ✗ | `reports/REVERSE_ANALYSIS_1.22_009.md:15`<br>`reports/missing_binaries_inventory_2026-09-13.md:366` |
| `update_engine3.bin` | ✗ | ✗ none | ✗ | `reports/SESSION_16_UPDATE_ENGINE_PAYLOAD_GATES.md:15`<br>`reports/docs_correctness_audit_2026-09-13.md:81`<br>`reports/missing_binaries_inventory_2026-09-13.md:367` |
| `vbmeta.img` | ✗ | ✗ none | ✗ | `reports/Rokid_Root_Guide_boot_img.md:258`<br>`reports/missing_binaries_inventory_2026-09-13.md:368` |
| `vbmeta_a.img` | ✗ | ✗ none | ✗ | `reports/Rokid_Root_Guide_boot_img.md:405`<br>`reports/missing_binaries_inventory_2026-09-13.md:369` |
| `vbmeta_disabled.img` | ✗ | ✗ none | ✗ | `reports/Rokid_Root_Guide_boot_img.md:261`<br>`reports/missing_binaries_inventory_2026-09-13.md:370` |
| `vendor.img` | ✗ | ✗ none | ✗ | `reports/GateServiced_LPE_Research_Plan.md:706`<br>`reports/HANDOVER_SESSION_20260131.md:96`<br>`reports/REVERSE_ANALYSIS_1.22_009.md:16` |
| `vendor_boot.img` | ✗ | ✗ none | ✗ | `reports/Rokid_Root_Guide_boot_img.md:172`<br>`reports/missing_binaries_inventory_2026-09-13.md:372`<br>`scripts/unpack_bootimg.py:502` |
| `vendor_boot_a.img` | ✗ | ✗ none | ✗ | `reports/Rokid_Root_Guide_boot_img.md:183`<br>`reports/missing_binaries_inventory_2026-09-13.md:373` |
| `vendor_boot_patched.img` | ✗ | ✗ none | ✗ | `reports/Rokid_Root_Guide_boot_img.md:298`<br>`reports/missing_binaries_inventory_2026-09-13.md:374` |
| `vendor_dlkm.img` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:375`<br>`scripts/check_1.19_vulns.py:49` |
| `verify.bin` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:376` |
| `xbl.img` | ✗ | ✗ none | ✗ | `reports/REVERSE_ANALYSIS_1.22_009.md:15`<br>`reports/missing_binaries_inventory_2026-09-13.md:377` |
| `xbl_config.img` | ✗ | ✗ none | ✗ | `reports/REVERSE_ANALYSIS_1.22_009.md:15`<br>`reports/missing_binaries_inventory_2026-09-13.md:378` |

### 3.5 kernel module (2)

| name | in repo? | source? | on device? | referenced by |
|---|---|---|---|---|
| `msm_kgsl.ko` | ✗ | ✗ none | ✗ | `reports/5.2_VERIFICATION_2026.md:13`<br>`reports/5.2_VERIFICATION_LIVE_ADB_2026.md:69`<br>`reports/ATTACK_SUMMARY.md:20` |
| `qcom-reboot-reason.ko` | ✗ | ✗ none | ✗ | `reports/REVERSE_ANALYSIS_1.22_009.md:164`<br>`reports/missing_binaries_inventory_2026-09-13.md:385` |

### 3.6 other (16)

| name | in repo? | source? | on device? | referenced by |
|---|---|---|---|---|
| `cve_scanner` | ✗ | ✗ none | ✗ | `reports/VULN_VERIFICATION_INDEX.md:12`<br>`reports/docs_correctness_audit_2026-09-13.md:79`<br>`reports/missing_binaries_inventory_2026-09-13.md:391` |
| `fenrir` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:392`<br>`reports/rootWithoutUnlockBootloaderList_EN_with_link_summaries.md:153` |
| `frida-server` | ✓ `frida/frida-server` | ✗ none | ✓ | `docs/device-debug-notes.md:140`<br>`reports/missing_binaries_inventory_2026-09-13.md:393` |
| `gate_analyzer` | ✗ | ✗ none | ✗ | `reports/Rokid_RedTeam_Internal_Report.md:802`<br>`reports/missing_binaries_inventory_2026-09-13.md:394` |
| `gate_diagnostic` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:283`<br>`reports/Rokid_RedTeam_Internal_Report.md:801`<br>`reports/missing_binaries_inventory_2026-09-13.md:395` |
| `gateservice_fuzzer` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:396`<br>`scripts/gateservice_fuzzer.py:410` |
| `lkpatcher` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:397`<br>`reports/rootWithoutUnlockBootloaderList_EN_with_link_summaries.md:538` |
| `location_changer` | ✗ | ✗ none | ✗ | `reports/Rokid_Glasses_Security_Report_EN.md:524`<br>`reports/Rokid_Glasses_資安漏洞報告.md:522`<br>`reports/Rokid_Glasses_資安漏洞報告_Marp.md:373` |
| `msm_kgsl` | ✗ | ✗ none | ✗ | `reports/5.2_VERIFICATION_2026.md:13`<br>`reports/5.2_VERIFICATION_LIVE_ADB_2026.md:69`<br>`reports/ATTACK_SUMMARY.md:20` |
| `run_exec_cmd_ta` | ✗ | ✗ none | ✗ | `reports/APK_TW_Tech_Report_Rokid_Root.md:398`<br>`reports/GateServiced_LPE_Research_Plan.md:107`<br>`reports/GateServiced_Phase1_RE_Report.md:27` |
| `run_load_ta` | ✗ | ✗ none | ✗ | `reports/GateServiced_LPE_Research_Plan.md:106`<br>`reports/GateServiced_Phase1_RE_Report.md:28`<br>`reports/GateServiced_Phase2_Exploitation_Playbook.md:159` |
| `run_unload_ta` | ✗ | ✗ none | ✗ | `reports/GateServiced_LPE_Research_Plan.md:108`<br>`reports/GateServiced_Phase1_RE_Report.md:29`<br>`reports/HANDOVER_SESSION_20260131.md:99` |
| `test_ta.bin` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:403` |
| `unlock-go` | ✗ | ✗ none | ✗ | `reports/HANDOVER_SESSION_20260131.md:1842`<br>`reports/Medium_Rokid_Root_Article_EN_backup.md:109`<br>`reports/missing_binaries_inventory_2026-09-13.md:404` |
| `victim.txt` | ✗ | ✗ none | ✗ | `reports/missing_binaries_inventory_2026-09-13.md:405` |
| `vmlinux-to-elf` | ✗ | ✗ none | ✗ | `deploy/ghostlock_patch_check.py:10`<br>`docs/device-debug-notes.md:187`<br>`reports/GHOSTLOCK_HANDOVER_PROMPT.md:48` |

## 4. Device-only copies — pull these before they are lost

### 4.1 Lost probes / exploits (no source anywhere)

| name | size (B) | category | notes |
|---|---:|---|---|
| `ashmem_blob_test` | 7,400 | probe | ashmem blob test — source never committed |
| `build2_adsprpc_secure_exploit` | 12,256 | exploit | on-device zig rebuild of `adsprpc_secure_exploit.c` |
| `build2_bpf_leak_v3` | 9,184 | exploit | on-device zig rebuild of `bpf_leak_v3.c` |
| `build2_cve_2021_22555_test` | 9,288 | exploit | on-device zig rebuild of `cve_2021_22555_test.c` |
| `build2_cve_2024_0582_mmap_uaf` | 14,000 | exploit | on-device zig rebuild of `cve_2024_0582_mmap_uaf.c` |
| `build2_kgsl_uaf_exploit` | 15,160 | exploit | on-device zig rebuild of `kgsl_uaf_exploit.c` |
| `dsp_audit_blind` | 1,226,928 | probe | DSP/adsprpc audit probe — source never committed |
| `dsp_hidl_base_diag` | 10,776 | probe | DSP HIDL diagnostic — source never committed |
| `dsp_hidl_session_diag` | 7,872 | probe | DSP HIDL session diagnostic — source never committed |
| `dsp_poc_standalone` | 1,288,720 | probe | standalone DSP PoC — source never committed |
| `dsp_raw_v2` | 8,160 | probe | raw DSP binder probe v2 — source never committed |
| `kaslr_leak` | 8,336 | probe | KASLR leak — source never committed |
| `test_0582` | 2,145,128 | probe | CVE-2024-0582 test — source never committed |
| `test_21479` | 16,608 | probe | CVE-2025-21479 test — source never committed |
| `test_36971` | 10,016 | probe | CVE-2024-36971 test — source never committed |

> ⚠️ The five `build2_*` binaries are **rebuildable in principle** — their `.c` sources survive (`adsprpc_secure_exploit.c`, `bpf_leak_v3.c`, `cve_2021_22555_test.c`, `cve_2024_0582_mmap_uaf.c`, `kgsl_uaf_exploit.c`) and `deploy/build_probe_zig.sh` reproduces the exact build recipe. The other ten have **no source at all**.

### 4.2 On-device tooling / boot images (expected provenance, not workspace losses)

| name | size (B) | category |
|---|---:|---|
| `boot.img` | 100,663,296 | firmware image |
| `kernel` | 47,331,340 | firmware image |
| `magisk` | 400,568 | daemon |
| `magisk_patched-30600_9C56p.img` | 100,663,296 | firmware image |
| `magiskboot` | 1,015,160 | daemon |
| `magiskinit` | 254,584 | daemon |
| `new-boot.img` | 100,663,296 | firmware image |
| `ramdisk.cpio` | 3,464,192 | firmware image |
| `recovery.img` | 104,857,600 | firmware image |
| `stock_boot.img` | 100,663,296 | firmware image |
| `su` | 0 | daemon |
| `su_gl` | 60,272 | daemon |

### 4.3 Device artifacts referenced nowhere at all

These exist in `/data/local/tmp` but appear in **none** of the pre-existing `reports/*.md` or `docs/*.md` — they surfaced only because this audit captured a full directory listing. Good news: **all but two are still rebuildable**, because their `.c` sits in `source_code/` even though the binary was never committed.

| name | size (B) | in repo? | source? | risk |
|---|---:|---|---|---|
| `adsprpc_probe` | 2,241,160 | ✗ | ~ variant (`adsprpc_test.c`, `adsprpc_mmap_probe.c`) | low |
| `adsprpc_secure_exploit` | 2,224,536 | ✗ | ✓ exact | none |
| `ashmem_test` | 7,368 | ✗ | ~ variant (`ashmem_probe.c`) | low |
| `dsp_poc` | 7,656 | ✗ | ✓ exact | none |
| `dsp_raw_binder` | 6,688 | ✗ | ✓ exact | none |
| `kgsl_aux_fuzz` | 9,208 | ✗ | ✓ exact | none |
| `kgsl_cve_check` | 11,952 | ✗ | ✓ exact | none |
| `kgsl_probe` | 7,080 | ✗ | ✓ exact | none |
| `exploit_cve_2021_41073` | 2,140,928 | ✗ | ✓ exact | none |
| `exploit_cve_2021_41073_sim` | 2,141,520 | ✗ | ✓ exact | none |
| `exploit_kaslr_leak` + `2`–`6` | ~2.14 MB each | ✗ | ✓ exact (`exploit_kaslr_leak.c` … `6.c`) | none |
| `kgsl_syncuaf_probe` | 2,259,024 | ✓ `source_code/kgsl_syncuaf_probe` (ELF) | ✓ exact | none — binary already in repo |
| `test_attach` | 2,139,856 | ✓ `source_code/test_attach` (ELF) | ✓ exact | none — binary already in repo |
| `test_bpf`, `test_bpf_prog`, `test_bpf_subprog`, `test_bpf_valid` | ~2.14–2.22 MB each | ✓ ELF each | ✓ exact | none — binaries already in repo |
| `test_cve41003`, `test_jitfold`, `test_memuninit`, `test_tailcall` | ~2.14 MB each | ✓ ELF each | ✓ exact | none — binaries already in repo |
| `fakepayload` | 4,096 | ✗ | **✗ none** | **HIGH — only copy** |
| `min_payload` | 24 | ✗ | **✗ none** | **HIGH — only copy** |

**`fakepayload` and `min_payload` are the only genuinely unsourced, unreferenced, device-only files** found by this audit. They are small test payloads, but if their provenance matters they should be pulled now.


## 5. False positives — excluded, not padding the table

These tokens matched a naive heuristic but are **not** binary names. They are listed so the table above can be trusted.

| Token(s) | Why it is not a binary |
|---|---|
| `build`, `extracted`, `output`, `remount`, `user`, `test`, `exploit`, `pwned`, `studio` | bare English words captured after `-o` / `adb push` / `/data/local/tmp/` in prose |
| `C`, `DOES`, `EXIST`, `NOT`, `absent`, `bin`, `su`, `system`, `xbin` | fragments of *"su binary: DOES NOT EXIST (/system/bin/su, /system/xbin/su — absent)"* (`reports/Rokid_RedTeam_Internal_Report.md:655`) |
| `v2`…`v13` | version fragments from `二進位：cve_2024_36971_v2 / v3 / … / v13` (`reports/VULN_VERIFICATION_INDEX.md:63`); the full names are in the table |
| `run_exec_cmd_ta`, `run_load_ta`, `run_unload_ta` | **GateService protocol command names**, not executables — highest raw hit counts (121/78/35), which is exactly why they look like binaries at a glance (`reports/GateServiced_LPE_Research_Plan.md:126-128`) |
| `ashmem_misc`, `ashmem_fops`, `ashmem_open`, `ashmem_ioctl`, `kgsl_ioctl_funcs`, `configfs_read_file`, `simple_read_from_buffer`, `init_cred`, `selinux_state`, `commit_creds`, `prepare_kernel_cred` | **kernel symbols / struct fields**, not userspace binaries |
| `AAAA.mbn`, `ZZZZZ.mbn`, `file.mbn`, `exist.mbn`, `test.mbn`, `gate_resp_` | placeholder / sentinel strings inside fuzzers |
| `*.log`, `*.pid`, `*.sock`, `*.lease`, `helper.alive`, `last.cmd/.done/.out/.progress` | runtime state files, not binaries |
| `cmdtools.zip`, `gradle.zip`, `jdk17.tar.gz`, `rokid_boot_flash.zip` | archives, not executables |

**Qualcomm TA / firmware `.mbn` names (~40)** appear only as *hardcoded candidate path strings inside probe scripts* — `widevine.mbn`, `keymaster64.mbn`, `storsec.mbn`, `playready.mbn`, `cppf.mbn`, `soter.mbn`, `sampleauth.mbn`, `gpqese.mbn`, `kmsvc.mbn`, `tz_ccm.mbn`, `fidocrypto.mbn`, `voiceprint.mbn`, `haventoken.mbn`, `qmiprivsys.mbn`, `secotacl.mbn`, `tzhdcp.mbn`, `spdaemon.mbn`, `securemm.mbn`, `secureindicator.mbn`, `isdbtmm.mbn`, `tbase.mbn`, `macchiato.mbn`, `mldap.mbn`, `gptest.mbn`, `hdcp1.mbn`, `hdcp2p2.mbn`, `km41.mbn`, `smcinvoke.mbn`, `sampleapp.mbn`, `sampleapp64.mbn`, `soter64.mbn`, `chamomile.mbn`, `sec_img_auth.mbn`, `smplap32.mbn`, `smplap64.mbn`, `smpcpyap.mbn`, `wvprov.mbn`, `qcom_eeeprom.mbn`, `rokid.mbn`, `ta.mbn`, `fake_ta.bin`, `fake_ta.mbn`. See `scripts/gate_diagnostic.py:429-457`, `scripts/gate_protocol_test.py:304-326`, `scripts/gate_protocol_test_v2.py:178-207`, `scripts/gate_traversal_oracle.py:263-293`. They are **on-device vendor firmware under `/vendor/firmware_mnt/image/`, never workspace artifacts** — device paths other than `/data/local/tmp` were out of scope for verification, so their presence was not checked.


## 6. Caveats

1. **Device copies are ephemeral.** Everything marked `✓` under *on device* lives in `/data/local/tmp`, wiped by a factory reset, an OTA, or `rm -rf`. It is also **stale-prone**: the `build2_*` rebuilds are timestamped 2026-08-21 while the newest probes (`kgsl_micronode_probe2`) are 2026-09-13.
2. **Device-only ≠ trustworthy.** A binary existing only on the device cannot be audited against source; treat its behaviour as unverified.
3. **`.c` present does not guarantee a byte-identical rebuild.** Toolchain drift (NDK r27d vs zig), `target_rv101.h` offsets and `-D` flags all change the artifact. Rebuild and compare hashes before relying on a reconstruction.
4. **`binaries/extracted_1.22/kernel` is the single most damaging loss** — it underpins the KGSL/BPF disassembly claims, is absent from the repo, and is absent from the device.
5. **Uncertain tokens were not guessed at.** Anything whose status as an executable was genuinely unclear was moved to §5 rather than padded into the table with a `?`.
6. **`kernel` and `ramdisk.cpio`** are classified as *firmware image* because they are boot-image components sitting in `/data/local/tmp/magisk/`, not standalone tools.
7. **Scope.** Only `/data/local/tmp` (and its `magisk/` subdirectory) was queried on the device; `/vendor`, `/system`, `/data/adb` etc. were out of scope per the task.


## 7. Primary sources

- `reports/VULN_VERIFICATION_INDEX.md` — authoritative enumeration of the probe/exploit binaries and the A/B missing classification
- `reports/docs_correctness_audit_2026-09-13.md:79` — `binaries/`, `source_code/ghostlock_rv101/build/`, `binaries/device_probes/` are unversioned
- `reports/FACTS.md:215` — "缺失成品" list
- `source_code/ghostlock_rv101/Makefile` — surviving build definition for `ghostlock_rv101` + `perf_probe`
- `deploy/build_probe_zig.sh` — surviving zig recipe that produced the `build2_*` device binaries
- Device: `adb -s <DEVICE_SERIAL> shell ls -la /data/local/tmp/` (2026-09-13)
