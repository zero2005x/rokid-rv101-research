# Documentation Correctness Audit Report (whole workspace, 2026-09-13)

> 中文版：[docs_correctness_audit_2026-09-13.md](docs_correctness_audit_2026-09-13.md)

- Scope: `README.md`, `MANIFEST.md`, `README_ORGANIZED.md`, `docs/*.md` (15), `reports/*.md` (97)
- Method: 4 subagents deep-dived 70 historical research reports in shards + 2 subagents checked the authoritative documents' paths/commands/numbers/structure +
  automated consistency checks by me on the core documents and implementation.
- Principle: **a dated record of its time is not itself an error**; only things that "would mislead someone acting today" or "documents that contradict themselves / contain broken references" need fixing.

---

## 1. Fixed (by severity)

### 1.1 Would lead someone to do the wrong thing (highest)
| Document | Problem | Action |
|------|------|------|
| `docs/deploy.md` §9.3, troubleshooting, `docs/device-debug-notes.md`, `deploy/root_bridge.sh`/`setup_direct.sh` output | Told people to change `USE_ENGINE`/`USE_BRIDGE` and rebuild the APK | Changed to describe **runtime automatic probing**; the script messages were synchronized |
| 6 reports (`CVE-2024-38401_Test_Report`, `CVE-2024-36971_analysis`, `CVE_PROBE_REPORT`, `RESEARCH_FINDINGS`, `Rokid_Attack_Vector_Summary`, `CVE_5_10_x_Scan_Report`, `PRIV_ESC_SESSION_10`, `OTA_Authentication_Analysis_Report`, `SESSION_16`, `NEW_CVE_ANALYSIS`, `ADB_Media_Exfiltration_Report`, `Rokid_Attack_Vector_Summary`) | Still recommended **Fastboot/Magisk/flashing boot.img** as the "most reliable root path" | All annotated with "this route is not feasible; root has already been obtained via the GhostLock LPE" |
| `docs/protocol.md` | **`SET_RING` was not documented at all** | Added it; also updated the `ENABLE_WIFI_ADB` fields (`persistPort`/`autoAfterReboot`) |
| `docs/root-from-zero-guide.md` | Told people to edit `ADB=`/`SERIAL=` in `get_root_now.sh` (already centralized into `deploy/env.sh`) | Changed to the env-override form |
| `reports/GHOSTLOCK_HANDOFF_V2` | **"RV101 has no VMAP_STACK" is a factual error** (the real device has `CONFIG_VMAP_STACK=y`), which invalidates the §2.2 mechanism | Marked as an error and pointed out the correct config |
| `reports/VULN_VERIFICATION_INDEX` | The opening claimed "it has been confirmed the files exist", but measurement shows **26/70 names are not in the workspace** | Added a re-check table: 16 are missing only as binaries (the .c files remain), 10 are missing as source too |

### 1.2 Externally disclosed documents (Rokid letter / English reports)
| Document | Problem | Action |
|------|------|------|
| `Rokid_Glasses_Security_Report_EN.md`, `Rokid_Glasses_資安漏洞報告.md` | "SELinux Enforcing/Root Access: None" does not match the current state; "Over 6 Months" contradicts the 19 months in the same section | Added a status-correction banner + row-by-row corrections |
| The same + `email.md`/`Rokid_漏洞報告信件.md` | Contradictory CVSS labels (8.6 labeled Critical, 7.5 vs 4.6); the 90-day deadline date was off by one day (should be 2026-05-20) | Marked for unification; the date was already marked |
| `Rokid_Glasses_系統資料報告(.md/_Marp.md)` | "verified boot enforcing" contradicts "orange/unlocked" in the same document; 16 sensors but only 8 listed in the table | Corrected to orange + noted that the table is a subset |

### 1.3 Internal self-contradictions (one document disagreeing with itself)
| Document | Contradiction | Action |
|------|------|------|
| `GHOSTLOCK_HANDOFF_V10` | The table marks W1/W2 ❌, while appendix E/AA already verified ✅ | Table corrected + the to-do marked "already done, do not redo" |
| `GHOSTLOCK_V5_RECLAIM_MECHANISM` | "payload is in place" vs V6 proving a 64 B difference | Added a correction note |
| `KGSL_RESEARCH_REPORT` | GPUMEM_ALLOC "✅ succeeded" vs measured ENOTSUP | Marked as deprecated and pointed to the measured report |
| `CVE-2024-36971_攻堅分析報告` | `CONFIG_RCU_FAST_NO_HZ=n` (actually y) | Corrected |
| `PASS_TABLE_EVIDENCE` | "15 rows" when it is actually 10 rows; `binder_uaf_v2~v5` actually only has v5 | Corrected |
| `ATTACK_SURFACE_RESEARCH` | Line-count table outdated (415→704 etc.) | Corrected and noted the measurement date |
| `zero_light_desktop_test_report` | i18n 191/2674, 149/44, 8 cores | Corrected with desktop-tool measurements: **228/3192, 134/28/26, 4 cores** |
| `kernelsnitch_mm_leak_clean_boot_2026-09-13` | Sample count, MemFree, keepalive seconds, external path | Corrected (path points to the desktop-tool project, sample 23) |
| `5.2_VERIFICATION_2026`, `CVE_5_10_x_Scan_Report` | Row count 16→15; report year 2025→2026 | Corrected |
| `KGSL_FINAL_REPORT` | "ERESTARTSYS (524)" is actually ENOTSUPP | Corrected |

### 1.4 Historical-positioning annotations (14 + 27 documents)
- 14 reports that present themselves as current but are outdated (`FINAL_STATUS_20260202`, `NEXT_AGENT_PROMPT*`, `便紀錄`, `PRIVESC_REPORT`, etc.) → added a "historical document (not the current state)" banner.
- `ROOT_PROCEDURE.md`, `Rokid_Root_Guide_boot_img.md`, `CURRENT_STATUS_20260822.md`, the two handoff prompts, `GHOSTLOCK_ROOT_操作說明.md`, `GHOSTLOCK_HANDOFF_V8.md`, `shizuku-feasibility-report.md`, `README_ORGANIZED.md`, the video production spec → each annotated with "superseded / external project / path changed".

---

## 2. Cross-file contradiction list (not yet fully resolved; for later unification)

> These are cases where **different reports give different answers about the same fact**. This round only annotated individual files rather than rewriting them one by one; unification is recommended under the principle "measured reports take precedence".

| # | Topic | Differing statements (files) | Recommended authoritative source |
|---|------|------------------|--------------|
| 1 | CVE-2024-36971 patched version | 5.10.219 (`深度攻堅報告_v2`, `analysis`, `5.2_VERIFICATION`) vs **5.10.220+** (`CVE_5_10_x_Scan_Report`) | Use the scan report + online CVE records |
| 2 | CVE-2024-36971 exploitability | "~1–5%" / "needs verification" / "verified affected" vs **"not exploitable (no routes/ICMP)"** (`PRIV_ESC_SESSION_10`, `5.2_VERIFICATION_LIVE_ADB_2026` §1.3) | Measured (LIVE_ADB) takes precedence |
| 3 | CVE-2024-0582 | "affects 5.10–6.4 / may be unpatched" vs "PBUF_RING needs ≥5.19, unreachable on this device" | The latter (measured ioctl EINVAL) |
| 4 | `msgsnd` spray | Recommended by several reports vs `CONFIG_SYSVIPC` not set | The latter (real-device config) |
| 5 | GhostLock symbol offsets | V3 (`0x2aca0140`) vs V7/V11 (`0x02ca0140`, `0x02202058`) | V7/V11 (newer) |
| 6 | `data_addr` alias | V7 "no impact" vs V10 "maps to the wrong PA, must change to `text_addr`" | V10 (demonstrated) |
| 7 | How panics are captured | V3 "no ramoops, can only use dmesg -w" vs V11 "ramoops is configured, console=ttyMSM0" | V11 |
| 8 | miscdevice fops offset | V3 "+0x18" vs W2 blocker "+0x10 (0x29bcbc0)" | W2 blocker |
| 9 | reclaim uptime threshold | V7 ">150s" vs W2 blocker ">~400s" | W2 blocker |
| 10 | GateServiced error propagation / capabilities | "all commands return 300c" vs "timeout=error"; capabilities "almost all" vs "4 named" | Needs a single measurement to pin down |
| 11 | i2c-1 identity | Letter "0x08 PMIC / 0x30 display" vs LED audit "0x30 = AW2110x LED / 0x08 = PSOC-TP-R" | LED audit (sysfs-demonstrated) |
| 12 | CVE-2025-21479/21480 attribution | "cross-FD = 21479" vs CVE records "micronode"; 21480 Android applicability unproven | CVE records + `5.2_VERIFICATION` §2.6 |
| 13 | CVE-2023-2163 status | "may be unpatched, prioritize" vs "patched (≥5.10.179)" | The latter |
| 14 | Device/firmware generation | 1.12.009 / kernel `gb80a8319d11b` vs **1.22.009 / `gd908d71a0404`** | The latter (current); the former's reports should be generation-tagged |
| 15 | KASLR / kernel-exploit feasibility | "cannot be bypassed / kernel exploit infeasible" vs currently feasible (leak→root 86–155s) | Current state |
| 16 | USB config value | `adb` vs `adb,diag` | Needs measurement |
| 17 | GHOSTLOCK V5 vs V6 | "reproducible 2/2", "pad 64", "v9.19" vs "not reproduced once", "block+0", "v9.60" | V6/V13 |

---

## 3. Broken references and missing artifacts (systemic)

| Type | Description |
|------|------|
| **Build products not under version control** | `binaries/`, `source_code/ghostlock_rv101/build/`, `binaries/device_probes/`, `cve_scanner`, `bpf_kaslr_exploit`, `io_uring_*`, `tipc_*`, `ashmem_*`, `msm_drm_probe`, etc. — **~51 binaries** are all absent from the workspace (most `.c` files are in `source_code/` and can be rebuilt) |
| **Source not retained** | `binder_uaf_v2~v4.c`, `cve_2024_36971_v2~v12.c`, `kgsl_bpf_spray_v3~v6.c`, `kgsl_exploit_v7/v8.c`, `ashmem_overflow.c`, `cve_scanner.c`, etc. |
| **External/host artifacts** | `RVE01-ota-*.zip`, `payload.bin`, `magisk_patched-122_009.img`, `update_engine3.bin`, `msm_kgsl.ko`, `vendor_extracted/`, `RideFlux/`, `recovery_root/`, `assets/` (removed), `RV101_Edit_Project/` (desktop editing project) |
| **Hashes cannot be re-checked** | the binary md5/sha1 in each report (V4 `01bd9e97…`, V5 `3CCAAAB6…`, V11 sha1s, V13 sha1s), `init.rokid.rc` SHA-256 |
| **Already annotated** | `VULN_VERIFICATION_INDEX` annotated with the 26 missing items by class; `MANIFEST` already corrected counts and sizes; the `deploy` script index completed with 25 entries |

---

## 4. Cannot be verified (needs the device or external resources)

- Device-side runtime values (ioctl return values, port 8341, sysfs, PID, `/proc` contents, GPU counters) — no device was attached for re-checking in this round.
- External URLs/CVE records (NVD, CodeLinaro, GitHub, community posts) and their details (patch dates, commits, CVE ranges).
- Artifacts that have gone offline (see §3) and their hashes.
- Figures in the desktop-tool report such as "x64 813 KB / helper 1957 bytes" that no longer match the current files (the current exe is 900,697 B).

---

## 5. Consistency re-check (automated, all green)

| Check | Result |
|------|------|
| i18n: `phone_keys.tsv` 193 keys, 284 strings per locale across 14 locales | ✅ zero missing |
| `protocol.md` covers 37/37 message types, 26/26 `Status` fields | ✅ |
| Bottom navigation 4 tabs, album/local 3 columns, `USE_ENGINE` has no effective references | ✅ |
| Build commands, module names, JDK path, APK artifact paths | ✅ match reality |
| `deploy/*.sh`, `scripts/*.sh` `bash -n`; i18n validators | ✅ all pass |
| `deploy` script index covers 25 entries | ✅ |
| MANIFEST references 23 files | ✅ all exist |

---

## 6. Parallel workstream (important background)

During the audit **another workstream was found editing the same workspace at the same time** (the ZeroLightRokid Desktop tool line):

- It added `deploy/lease.sh`, `deploy/proc_util.sh`, `deploy/buddy_watch.sh`, `deploy/test_lease.sh`, and added a
  **cross-tool shared lease** (`/data/local/tmp/ghostlock_op.lease`) plus a TOCTOU pre-check to `RootDaemonKeeper.kt`.
- The §1 corrections in this report are **based on the current file contents**; if the two lines edit the same file simultaneously, re-run this audit using the latest mtime.
- The lease mechanism and new scripts have been added to `docs/deploy.md` (P0 section) and `docs/architecture.md`.

---

## 7. Follow-up recommendations (ordered by value)

1. **Unify the 17 sets of cross-file contradictions** (§2): adding a `FACTS.md` under `reports/` is recommended (a single authoritative fact table: firmware generation, CVE status and patched versions, available ioctls, symbol offsets, reclaim threshold), with each report changed to cite it.
2. **Add a "generation stamp" to every report**: `fw=1.22.009 / kernel=gd908d71a0404 / date`, so that "different conclusions for different generations" are no longer contradictions.
3. **Write the rule that build products are not version-controlled into MANIFEST** (§3 already annotated): if a report cites a binary, it must always also state "how to rebuild it".
4. **Do another fact-freeze pass on the external documents (Rokid letter / English reports)**: CVSS labels, SELinux/root status, dates, checked item by item before publication.


---

# Rev.2 (late night 2026-09-13) — re-audit after the transport-architecture change

## Trigger
Later that day "media always goes over Bluetooth" was fixed (`reports/wifi_transfer_fix_2026-09-13.md` Rev.1–6) and three behavior blocks were changed:
**transport routing, transport indication, and the glasses media index** ⇒ any document describing these behaviors may be outdated.

## Method (repeatable)
1. Link check: whether the markdown links in `README.md`/`MANIFEST.md`/`dist/README.md`/`docs/*.md` exist (21 links, 0 broken).
2. Path check: extract all references of the form `` `xxx.sh|py|kt|c|cmd|tsv|apk|json` `` and check one by one whether the **full path or basename** exists in the workspace
   (excluding device-side paths and external-project references).
3. Code cross-check: whether all 37 `MsgType` constants in `Protocol.kt` appear in `docs/protocol.md`.
4. Number re-check: i18n key count, APK versions, directory counts and sizes.
5. Manual re-reading of specified sections: `docs/deploy.md` §9.4, `docs/architecture.md` §1/§4/§7, `docs/protocol.md` §1, `dist/README.md`.

## This round's fixes (things that would mislead someone acting today)

| # | File | Error | Fix |
|---|------|------|------|
| 1 | `README.md` §Transport | "control commands go over Bluetooth RFCOMM; photos/videos go over WiFi" | Changed to "both control and media prefer WiFi" + the mutual-exclusion reason and measured numbers |
| 2 | `README.md` §Directory structure | missing `dist/`, `binaries/`, `build-cache/` (only 20 of the 23 top-level items were listed) | Added the three directories and the principle "formal evidence → `reports/`, temporary → `build-cache/`" |
| 3 | `docs/architecture.md` §1 | "control commands go over Bluetooth RFCOMM; media prefers WiFi" + architecture-diagram labels | Changed to WiFi-first; added a **channel mutual-exclusion** warning |
| 4 | `docs/architecture.md` §4 | old description of `Remote.mediaClient()`/`wifiMode` | Changed to `link()`/`ensureMediaLink()`/three-state display/`onTransportChanged` |
| 5 | `docs/architecture.md` §7 | "WiFi **media** channel" | Changed to "WiFi channel (carries both control and media)" + risk pointer to FACTS P10 |
| 6 | `docs/deploy.md` §9.4 | the title and content still said "manually enable WiFi transport", `~60KB/s` | Whole section rewritten: automatic setup, persisted preference, mutual-exclusion warning, **measured 3.0–3.8s vs 324s** |
| 7 | `docs/protocol.md` §1 | "control commands **always stay on Bluetooth**" | Changed to the two channels side by side + mutual-exclusion explanation + routing rules |
| 8 | `docs/architecture.md`/`deploy.md` | i18n "**193** keys" | Corrected to **203 keys** (the current value from `validate_translations.py`) |
| 9 | `dist/README.md` | `app-glasses-v1.0-safe.apk` appeared in **two duplicate rows**, and both rows said "this is the version currently installed on the glasses" | Rewrote the table: one row per each of the 7 APKs, current versions being glasses `1.0-safe.3`/phone `1.3` |
| 10 | `MANIFEST.md` §2 | missing `dist/`/`binaries/`/`build-cache/`/`.zig-cache/`; sizes and file counts outdated (4.0GB→4.7GB, reports 293→331…) | Added four verdict rows + re-measured all counts; §2.1 added `1.0-safe.3`/`v1.3` and the two new evidence directories |
| 11 | `MANIFEST.md` §2.1 | cited the non-existent `add_p2x.py` | Listed the actual filenames instead |

## Confirmed "no change needed" after this round's checks
- The message table in `docs/protocol.md`: **all** 37 message constants in the code are documented (including `ACK`/`HELLO_ACK`).
- `PROTO_VERSION=1` in `docs/protocol.md` matches the code.
- The build commands in `docs/sdk-setup.md` (`JAVA_HOME=tools/jdk/jdk-17.0.20.1+1`, `GRADLE_USER_HOME=gradle_home`)
  match how the build was actually done today.
- `design.py`/`story.json`, etc. referenced by `docs/RV101_RERENDER_SPEC_v2.md`/`VIDEO_PRODUCTION_PLAN_v2.md`
  belong to an **external editing project** (the documents already state they are not in this workspace); `assets/cei.json`/
  `endpoints.json` in `docs/privacy-audit-report.md` are actually located in `analysis/phone/`; `.ghostlock_root.sh`/`last.cmd` are **device-side** paths. None of the above are broken references.
- The "communications changed to Bluetooth" passage in `docs/device-debug-notes.md` is a **historical record** (that file is already marked historical at the top), and by principle history is not rewritten.

## Execution notes (recorded faithfully)
This round originally dispatched one subagent dedicated to the `docs/` audit, but that subagent **failed to execute and produced no report**;
the results in the table above were instead completed by me using four classes of automated checks (links/paths/code cross-check/numbers) plus re-reading of specified sections.
There was also one **adversarial verification agent** (targeting the FACTS P27–P30 added that evening) that successfully produced a report and caught 3 real gaps,
which have been fixed and recorded in `reports/wifi_transfer_fix_2026-09-13.md` Rev.6.
