# Workspace Layout Inventory (MANIFEST)

> 中文版：[MANIFEST.md](MANIFEST.md)

> Purpose: to explain the role and the "correct home" of every top-level item in `rokid_root/`, as the criterion for where files should be placed.
> Last inventory: **2026-09-13 23:xx** (re-check: sizes and file counts re-measured; the roles and verdicts of four newly added top-level items `dist/`, `binaries/`, `build-cache/`, `.zig-cache/` added; the APK versions and evidence directories of §2.1 filled in). Total roughly **4.7 GB** (23 top-level items; of which about 4.3 GB is toolchain / research input / build artifacts, not project content).

## 1. Classification Principles

| Category | Home | Description |
|------|------|------|
| **Project content** (should be under version control) | `android/` `docs/` `deploy/` `scripts/` `README.md` `MANIFEST.md` | Code, documentation, deployment scripts |
| **Research output** (can be version-controlled, mind the size) | `reports/` `RV101_Disclosure/` `HITCON_ZeroDay_RV101/` `research/` `source_code/` | Reports, evidence, external references, probe source code |
| **Research input / large data** (not version-controlled) | `apks/` `analysis/` | Device APKs, packet captures and decompilation output |
| **Toolchain / environment** (not version-controlled, rebuildable) | `tools/` `gradle_home/` `.venv/` `frida/` | JDK, jadx, Gradle cache, Python venv, frida-server |
| **Build artifacts** (not version-controlled, rebuildable) | `android/*/build/` `android/.gradle/` | APKs and intermediate artifacts |
| **Backup / scratch** (currently **none**) | — | Both `rokid_root_20260904_backup.zip` and `wm_work/` have been moved out (see §5) |

## 2. Current State of Top-Level Items

| Item | Size | Contents | Verdict |
|------|------|------|------|
| `android/` | 122M | Gradle multi-module (shared/app-glasses/app-phone/branding/adbreachprobe/**usbdebug-hider**/**debugdetectprobe**) + about 110M of `build/` artifacts (2,310 files) | ✅ Correct (`build/` is already listed in .gitignore) |
| `apks/` | 1.1G | 17 vendor APKs pulled from the device | ✅ Research input (large, do not version-control) |
| `analysis/` | 807M | Packet captures (`cap1/2.pcap`), `jadx_out/`, `strings/`, `phone/`, `device_data/` | ⚠️ Research input, but missing a README; suggest renaming / moving to `research/device_analysis/` |
| `tools/` | 439M | `jdk/` (for builds), `jadx/` (already extracted), **`framework/` (Xposed framework installers for APatch: NeoZygisk v2.4-289, Vector v2.2-3080)** | ✅ `jadx.zip` has been deleted (see §5); the framework zips are third-party tools (can be re-downloaded) |
| `.venv/` | 654M | Python virtual environment (for scripts) | ✅ Environment (do not version-control) |
| `gradle_home/` | 1.2G | Gradle user cache (`GRADLE_USER_HOME`) | ✅ Environment (do not version-control) |
| `research/` | 71M | `ghostlock_refs/` (7 public exploit references: 7 zips + extracted directories), `kernel_configs/` (device config), `shizuku/` (Shizuku source + research + the predecessor of branding) | ✅ Correct location for external references |
| `frida/` | 51M | frida-server binary + hook scripts (OTA/intent) | ⚠️ Tools and scripts mixed together; the scripts can stay, the binary belongs with the tools |
| `source_code/` | 36M | 262 `.c` probe/exploit source files (286 files in the directory in total) (adsprpc/binder/kgsl/ashmem etc.) | ✅ Correct (clear division of labor with the app daemons in `deploy/*.c`) |
| `reports/` | 53M | 331 files: research reports, on-device test reports, `ghostlock_runlogs/`(148), `media/`(32), `ui_v11/`(7), `zero_light_desktop_ui/`(7), `ui_transport_2026-09-13/`(5), `media_index_repair_2026-09-13/`(6), `led_validation/`(2), **`media_usbdebug_hider/`(6: USB debugging anti-detection A/B evidence)** | ✅ Correct location for research output (**canonical location for formal evidence**) |
| `deploy/` | 2.4M | 40+ files: scripts (`.sh`/`.py`/`.cmd`) + `.c` (root_daemon / run_shell / ring_input_daemon / **mini_sqlite3**) + compiled ELF; **USB debugging anti-detection: `usbdebug_hider_ctl.sh` / `usbdebug_hider.conf` / `build_debugmask_so.sh` / `build_mini_sqlite3.sh`**; plus runtime `*.log` (excluded by `.gitignore`, formal evidence is in `reports/`) | ✅ Correct |
| `scripts/` | 920K | 74 files: development-time tools and orchestrators (**canonical location**) + `i18n/` (phone-side translation workflow: `phone_keys.tsv` / `out/` / 4 scripts for normalization, checking, applying, and migration) | ✅ Correct |
| `RV101_Disclosure/` | 2.0M | Vendor coordinated-disclosure package (52 files) | ✅ Correct |
| `HITCON_ZeroDay_RV101/` | 72K | HITCON submission package (14 files, redacted attachments) | ✅ Correct (**different recipients** from the item above, see §4) |
| `docs/` | 496K | 15 `.md` files (+1 PDF) of design / protocol / deployment / research documentation | ✅ Correct |
| `README.md` / `README_ORGANIZED.md` | 26K | Project overview / historical master index | ✅ Correct |
| `dist/` | 39M | 12 APK files + `README.md`: glasses `1.0-safe.6` (current) / `1.0-safe.5` / `1.0-safe.4` / `1.0-safe.3` / `1.0-safe.2` / `1.0-safe` / `1.0-original`, phone `1.3` (current) / `1.2` / `1.1`, **`usbdebug-hider-v1.1` (LSPosed module) / `debugdetectprobe-v1.0` (control-group probe)**, with a version comparison table and install commands | ✅ Correct (**deliverables**; `*.apk` is excluded by `.gitignore` and must be restored via `README.md`) |
| `binaries/` | 4.6M | 13 files: `device_rescue/` — binaries **rescued from the device that cannot be rebuilt** (10 probes with no source + 2 payloads) + `README.md` | ✅ Correct (**not rebuildable, most in need of preservation**) |
| `build-cache/` | ~2.3G | 3,140+ files: **scratch / rebuildable** — zig caches (`zig-global/`, `zig-local/`), probe scratch (`gate_re/`, `ta_re/`, `ta_load/`, `tb/`), screenshot workspace (`shots/`), third-party delivery test material (`ringtest/`), one-off file-fixing scripts (`add_p21.py` / `add_p23.py` / `add_p24.py` / `add_p27.py` / `fix_p24.py`); **`ota_122009/`, `ota_125012/` = OTA analysis workspaces for two versions** (about 2.1G each: OTA zip + `payload.bin` / kernel Image / ELF; the zip and payload **can be re-downloaded from the vendor CDN, do not treat them as the only copy**; raw reports containing absolute addresses and kernel.elf stay only here and are not sent out) | ⚠️ Scratch by nature (**route formal evidence to `reports/`**); suggest adding to `.gitignore` |
| `.zig-cache/` | 72M | zig's local cache (overlaps with `build-cache/zig-local/`) | ⚠️ Redundant cache; can be deleted (it just rebuilds), or switch to `build-cache/zig-local` |

## 2.1 Files Generated by Scripts (rebuildable, do not edit by hand)

| Generated file | Generator | Input |
|--------|--------|------|
| `android/usbdebug-hider/…/EmbeddedSo.java` + `BakedDefaults.java` | `android/usbdebug-hider/build.gradle.kts` (generated automatically at build time) | `android/usbdebug-hider/src/main/jniLibs/arm64-v8a/libdebugmask.so` (`deploy/build_debugmask_so.sh`) + `-PmaskTargets=` |
| `android/app-glasses/…/ghost/SilentPayloads.kt` | `scripts/gen_silent_payload.py` | `reports/media/silent_camera.ogg` (0.3 s silent Ogg Vorbis produced by ffmpeg) |
| the i18n blocks of `android/app-phone/…/res/values*/strings.xml` | `scripts/i18n/apply_phone_strings.py` (re-runnable) | `scripts/i18n/phone_keys.tsv` (zh-TW/en) + `scripts/i18n/out/*.xml` (the other 12 locales) |

New files related to ring control (canonical location):

| File | Role |
|------|------|
| `android/shared/…/ring/{RingScanner,RingBonder,RingGatt,RingWatcher,RingGestureDetector}.kt` | BLE discovery / pairing / GATT probing / watching / gesture recognition shared by both sides |
| `android/app-phone/…/ring/RingController.kt` | Path A: phone-side gesture interception and action execution (+ UI state) |
| `android/app-glasses/…/ring/{RingEvdevReader,RingControlManager}.kt` | Path B: root reads evdev directly and captures locally |
| `deploy/ring_input_daemon.c` + `build_ring_input_daemon.sh` | Fallback implementation (native evdev reader; **not adopted**, this version uses `getevent`) |

Files related to hard-coded value governance:

| File | Role |
|------|------|
| `scripts/audit_hardcode.py` | Hard-coded value audit tool (strings / paths / layout / scripts / **component reachability / credential literals / native `chmod`**, re-runnable) |
| `reports/FACTS.md` | **Single authoritative fact table** (adjudicates 17 sets of cross-file contradictions; reports cite it instead of stating things independently) |
| `binaries/device_rescue/` | **Non-rebuildable binaries rescued from the device** (10 probes with no source + 2 payloads; see that directory's `README.md`) |
| `reports/missing_binaries_inventory_2026-09-13.md` | **Complete inventory of missing / referenced binaries** (320 names: rebuildable / variant only / entirely lost / device only) |
| `reports/gateserviced_ta_load_2026-09-13.txt` | Measured transcript of GateServiced **TA loading and a successful EXEC_CMD** (corresponds to FACTS §5.2 S10–S13) |
| `build-cache/empty_file_probe.py` | Empty file → `3001` measured probe (cleaned up after use; corresponds to FACTS §5.2 S16) |
| `reports/static_re_gate_codes_and_rescue_2026-09-13.md` | **Instruction-level RE** of GateServiced error codes + classification of the rescued binaries (corresponds to FACTS §5.2 S14/S15, §9.5) |
| `reports/gate_dma_alloc_heapname_probe_2026-09-13.txt` | **On-device falsification probe for the `dma_alloc` heap name / fd slot** (overturns "heap name = stack pointer" and "uninitialized fd grooming"; corresponds to FACTS §5.2 S19/S20) |
| `reports/gate_300a_and_dma_unalloc_2026-09-13.md` | Instruction-by-instruction RE of the GateServiced **`0x300A` hard-coded value** + **`dma_unalloc` return space** (corresponds to FACTS §5.2 S18–S20; Rev.2 has withdrawn both side conclusions) |
| `reports/dtx_ta_command_space_2026-09-13.md` | **Complete static enumeration of the DTX / gate TA command space** (id→handler, `op` subcommands, SFS objects, risk rating; **no command was executed**; corresponds to FACTS §5.2 S21/S22) |
| `reports/ring_capture_security_audit_2026-09-13.md` | **Source authorization and hard-coded security audit of ring-controlled photo/video capture** (assessment of the pairing confirmation mechanism, exported debug receiver, `:8555` with zero authentication, hard-coded AP password, root daemon socket `0777`; corresponds to FACTS §7 P7–P14, §9-E) |
| `reports/wifi_transfer_fix_2026-09-13.md` | **WiFi media channel fixes (Rev.1–5)**: ①`NEARBY_WIFI_DEVICES` was not requested at runtime + ②the TCP socket was not bound to the AP network (P27) ③channel mutual exclusion caused media to silently fall back to Bluetooth + WiFi was not created automatically before media (P28) ④race condition in the UI transport indicator ⑤silent loss of the glasses media index (P29) ⑥unfinalized videos and `.part` leftovers (P30) |
| `reports/camera_ring_failure_remote_diagnosis_2026-09-13.md` | **Remote diagnosis with the cable unplugged**: `camera_not_ready` (the service was stopped by app idle) / `ringEnabled=false` / `ringDevice` pointing at the ring; includes the fix for the real bug of "silently reusing an invalid device path" (corresponds to FACTS §7 P20) |
| `dist/README.md` | **Installable APK list** (safe version / original version / phone version, install and rebuild commands) |
| `reports/arm_safety_policy_2026-09-13.md` | Implementation and on-device validation of the **four protections for arming privilege escalation safely** (boot-loop circuit breaker / retry cap + backoff / charging only / delay after boot); includes how to tell the two APK versions apart (corresponds to FACTS §7 P19) |
| `dist/app-glasses-v1.0-safe.3.apk` | **Current glasses-side version** (`1.0-safe.3`, code 5): four protections + **bidirectional** index correction (per-entry fault tolerance including albums, disk self-repair, automatic removal of externally deleted files) + atomic writes (corresponds to FACTS §7 P29) |
| `dist/app-glasses-v1.0-safe.2.apk` | Previous glasses-side version (`1.0-safe.2`, code 4): one-way index repair (per-entry fault tolerance for the `media` array only) |
| `dist/app-glasses-v1.0-safe.apk` | Previous glasses-side version (`1.0-safe`, code 3): all four protections on, behavior before the index fix (for A/B comparison) |
| `dist/app-glasses-v1.0-original.apk` | **Original version** (versionName `1.0`) for A/B comparison |
| `dist/app-phone-v1.3.apk` | **Current phone-side version** (`1.3`, code 4): on top of 1.2, fixes the regression where "`PROGRESS` pushes were dropped while WiFi was the current channel" (centralized registration in `Remote.onPush`), immediate disconnection notification to the UI, `@Volatile`, dead code removed. **Currently installed on the phone** |
| `dist/app-phone-v1.2.apk` | Previous phone-side version (`1.2`, code 3): WiFi-preferred transport + automatic WiFi channel creation + live transport indicator updates + `.part` cleanup |
| `dist/app-phone-v1.1.apk` | Previous phone-side version (`1.1`, code 2) kept for comparison |
| `reports/glasses_reboot_after_adb_unplug_2026-09-13.md` | **Root-cause analysis of the glasses rebooting after adb is unplugged** (main cause = delayed consequence of an exploit panic; ring / power loss / adb reboot ruled out; BOOT_COMPLETED is blocked; list of possible fixes; corresponds to FACTS §7 P18) |
| `reports/rgr06_pairing_failure_2026-09-13.md` | **RGR06 pairing failure forensics** (phone: the ring aborts SMP; glasses: after fixing the `RingBonder` crash, pairing succeeds and it becomes an HID device; corresponds to FACTS §7 P17) |
| `reports/ring_p0_p1_fixes_test_2026-09-13.md` | **Implementation and on-device testing of the ring/camera P0 + P1 fixes** (busy_recording, recording watchdog, burst throttling, media key pass-through, paired HID list; corresponds to FACTS §7 P16) |
| `reports/aivela_ring_connection_failure_2026-09-13.md` | **Forensics and remediation for the AIVELA ring "connection failed" log** (LMK → FGS denied → recording stuck → service stopped; includes verified immediate recovery steps; corresponds to FACTS §7 P15) |
| `reports/ring_reachability_test_2026-09-13.txt` | **End-to-end proof of a third-party app delivering to `DebugReceiver`** (purpose-built minimal app, uid 10346 / 10064; removed after testing, material kept in `build-cache/ringtest/`) |
| `reports/ui_transport_2026-09-13/` | **On-device screenshot evidence for the transport indicator fix** (4 images + README: Bluetooth → WiFi connecting… → WiFi connected, without manually switching tabs at any point; corresponds to FACTS §7 P28) |
| `reports/media_index_repair_2026-09-13/` | **A/B experiment samples for the glasses media index** (index after repair, index before deletion, deliberately corrupted index, victim filenames, deletion list + README; corresponds to FACTS §7 P29/P30) |
| `reports/url_check_2026-09-13.txt` | Spot check of external URL reachability (including the finding that the OTA zip is downloadable, corresponds to FACTS §9.6) |
| `reports/gateserviced_probe_2026-09-13.txt` | **Measured transcript** of the GateServiced protocol / capabilities (corresponds to FACTS §5.2) |
| `reports/device_facts_ramoops_uptime_2026-09-13.txt` | ramoops / uptime conditions / system baseline forensics (corresponds to FACTS §6.1) |
| `reports/kgsl_ioctl_retest_2026-09-13.txt` | **Raw measured output for this generation** of KGSL / peripheral ioctls (6 probes; corresponds to FACTS §5.1) |
| `deploy/kgsl_micronode_probe.c` / `kgsl_micronode_probe2.c` | **Dedicated probes for the 0x58 micronode** (nr×size matrix + semantic validation; measurements in FACTS §5.1.1) |
| `deploy/build_probe_zig.sh` | **Cross-compile** device probes with zig (aarch64-linux-musl static; no Windows NDK needed) |
| `reports/kgsl_micronode_retest_2026-09-13.txt` | Raw output of the micronode probes (evidence) |
| `deploy/kgsl_ioctl_probe.c` / `build_kgsl_probe.sh` | Self-built ioctl probes (**could not be compiled this time**: the Windows mount was read-only + no Linux cross toolchain; kept for use when a toolchain is available) |
| `reports/hardcode_audit.md` | Audit results and fix record (including deliberately retained items and open questions) |
| `reports/docs_correctness_audit_2026-09-13.md` | **Documentation correctness audit** of the whole workspace (sharding method, cross-file contradictions, missing artifacts, list of fixed items) |
| `android/app-glasses/…/GlassesPaths.kt` | **Single** source of truth for glasses-side device paths (`/data/local/tmp`, `/sys/class/leds`) |
| `deploy/env.sh` | `ADB` / `SERIAL` shared by the scripts (the single source of defaults, overridable via environment variables) |
| `deploy/lease.sh` | **Shared lease** primitive for the deploy path (shares `/data/local/tmp/ghostlock_op.lease` with the app watchdog and the desktop tool) |
| `deploy/proc_util.sh` | Safe process-killing utility (replaces `pkill -f`, which self-matches / wrongly kills `*_desktop` variants) |
| `deploy/test_lease.sh` | Lease test suite (9 items; **requires no root and does not trigger the exploit**, must be run after changing lease code) |
| `deploy/test_keeper_lease_guard.sh` | End-to-end test of the keeper lease guard (15 items; induces "root lost" and observes the guard log) |
| `deploy/root_restore_delayed.sh` | Delayed recovery process (stays behind as root first; brings the daemon back after testing, **no need to re-run the exploit**) |
| `reports/deploy_lease_fix_2026-09-13.md` | Record of the deploy lease fix + two new defects (the same owner is not mutually exclusive, `pkill` misses `_desktop`) |
| `reports/index_resilience_verification_2026-09-16.md` | **Index / channel catch-up validation report** (A fault tolerance for a bad album, B forward catch-up registration, C reverse removal, D control and push over TCP, E UI update on disconnection; all passed; includes three methodology pitfalls: "`adb pull` does not work for `/sdcard/Android/data`", "the glasses logcat tag is `GlassesDebugReceiver`", "always check the foreground before taking a screenshot") |
| `scripts/ota_kernel_diff.py` | **Compare `remove_waiter()` between two kernel versions**: symbol location → Capstone disassembly → **byte-by-byte comparison** → output a list of function-relative offsets (call targets symbolized). Used to independently re-check the CVE-2026-43499 backport; measured: both versions are `0x28c` / 163 instructions and differ by only one instruction at `+0x040` |
| `scripts/gen_redacted_pdf.py` | **Produce the PDF sent to Rokid from `docs/ghostlock-before-after-redacted.md`** (weasyprint). Must be re-run after editing the .md so the PDF and the .md do not fall out of sync (there was a case where the PDF still contained `[TBD]`). Layout notes are written at the top of the file |
| `scripts/check_index_state.sh` | **Index state check / repair** (`--fix` restores the album name changed by the validation flow back to `Camera`; and verifies that the index / disk / phone entry counts agree). Used for cleanup after an interrupted validation; read-only mode is safe to run first |
| `scripts/verify_index_resilience.sh` | **Validation script for bidirectional self-repair of the glasses media index** (A fault tolerance for a bad album / B present on disk but missing from the index → catch-up registration / C present in the index but missing on disk → removal / D push over WiFi / E channel-closed notification); it only touches self-created test files and backs up the index first, is re-runnable, and writes a transcript to `reports/` |
| `deploy/buddy_watch.sh` | Sampling of `/proc/buddyinfo` and `/proc/meminfo` (KernelSnitch mm leak diagnosis) |

Privilege-escalation operations layer (P0–P3, 2026-09-13):

| File | Role |
|------|------|
| `android/app-glasses/…/ghost/RootDaemonKeeper.kt` | root daemon watchdog (30 s): stale socket cleanup, heartbeat probe, automatic rebuild (5-minute cooldown), status reporting |
| `android/app-glasses/…/ghost/AutoGhostlockEngine.kt` | Runtime engine probing / delegation (Shizuku ↔ bridge), automatically switching routes on failure |
| `android/shared/…/protocol/RootState.kt` | `RootStates` status codes + `RootMetrics` privilege-escalation metrics |
| `android/app-phone/…/ui/RootText.kt` | root status / hints / metrics → phone-locale text |
| `deploy/install_root_keepalive.sh` | Install keepalive + OOM protection on top of any existing root (for the deploy path) |

## 2.2 External Deliverables (not in this workspace)

| Deliverable | Location | Description |
|--------|------|------|
| **ZeroLightRokid Desktop** (Windows GUI tool) | `C:\Users\<USER>\Documents\codebase\ZeroLightRokidDesktop` (Linux development copy `~/ZeroLightRokidDesktop`) | A single Win32 C exe that wraps this project's root flow into buttons; 14 locales (mirroring `app-phone`). **The research output stays in this workspace**: the on-device test report is in `reports/zero_light_desktop_test_report.md` and screenshots are in `reports/zero_light_desktop_ui/`. The tool itself is version-controlled separately and is not merged into `rokid_root/` (different language / build system from the Gradle project). |

## 3. Confirmed "illogical placement" items (redundant copies)

The following are duplicate files that are **byte-for-byte identical** (root directory vs canonical location), left over from historical operations:

| Root-directory file | canonical location | Status |
|-----------|---------------|------|
| `led_test.sh` | `scripts/led_test.sh` | Exactly identical (matching md5) |
| `pathd_test.sh` | `scripts/pathd_test.sh` | Exactly identical |
| `run_led_orchestrator.sh` | `scripts/run_led_orchestrator.sh` | Exactly identical |
| `run_pathd_orchestrator.sh` | `scripts/run_pathd_orchestrator.sh` | Exactly identical |
| `led_test.log` | `reports/led_validation/led_test.log` | Exactly identical (matching md5) |
| `pathd_test.log` | `reports/led_validation/pathd_test.log` | Exactly identical |

Other locations still to be adjusted:
- `all_pkgs.txt` (device package list) → suggest `analysis/device_data/all_pkgs.txt`.
- ~~`rokid_root_20260904_backup.zip` (24M) → move out of the project~~ **Done** (currently not in the root directory).
- ~~`wm_work/` (35M) → suggest `reports/media/`~~ **Done** (contents have been merged into `reports/media/`).

## 4. Relationship between the two disclosure directories (easily confused, not duplicates)

- `RV101_Disclosure/`: coordinated disclosure to the **Rokid vendor** (complete evidence chain, timeline, CVE relationship assessment).
- `HITCON_ZeroDay_RV101/`: submission to the **HITCON ZeroDay platform** (redacted attachments, risk notes, submission letter).
The two have different recipients and different red lines, so **keeping both is correct**; each has a `00_README.md` explaining it.

## 5. Cleanup execution record (executed 2026-09-11)

```bash
# ✅ Done: delete redundant copies in the root directory (canonical copies are in scripts/ and reports/led_validation/; md5 verified identical)
rm -f led_test.sh pathd_test.sh run_led_orchestrator.sh run_pathd_orchestrator.sh led_test.log pathd_test.log
# ✅ Done: delete the redundant archive (tools/jadx/ was already extracted, contents equivalent)
rm -f tools/jadx.zip
# ✅ Done: put back in place
mv all_pkgs.txt analysis/device_data/
mkdir -p reports/media && mv wm_work/* reports/media/ && rmdir wm_work
# ⏳ Manual step pending (the sandbox cannot write outside the workspace): move the compressed backup out of the project
mv rokid_root_20260904_backup.zip ~/backups/     # ✅ Done (this file is no longer in the root directory)
```

> Result: the root directory went from 26 items down to 19; the **2026-09-13 23:xx re-check shows 23 items** — the additions are `dist/` (APK deliverables),
> `binaries/` (rescued binaries), `build-cache/` and `.zig-cache/` (caches / scratch), all produced by later work rather than old redundant copies returning;
> the root directory still holds only 4 **files** (`.gitignore`, `MANIFEST.md`, `README.md`, `README_ORGANIZED.md`) with no stray files.
> References in the documentation all point at canonical locations (`scripts/…`, `reports/led_validation/…`) and are unaffected.
