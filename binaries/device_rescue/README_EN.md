# device_rescue/ — Binaries Rescued from the Device (2026-09-13)

> 中文版：[README.md](README.md)

## Why This Directory Exists

The inventory in `reports/missing_binaries_inventory_2026-09-13.md` shows that of the
320 binaries referenced by reports, **27 exist only in `/data/local/tmp` on the
glasses**, and **10 of those have no source code at all** — they would **disappear
permanently** after an OTA, a factory reset, or clearing `/data/local/tmp`.

This pass therefore pulled the 10 "no source code" binaries plus 2 payloads of unknown
origin back into the workspace for safekeeping (4.6 MB in total).

## Contents and Provenance

| File | Size | Source in repo? | Description |
|------|-----:|------------------|------|
| `ashmem_blob_test` | 7,400 | ✗ | ashmem blob test (related to the GhostLock path) |
| `dsp_audit_blind` | 1,226,928 | ✗ | adsprpc/DSP audit (blind test) |
| `dsp_hidl_base_diag` | 10,776 | ✗ | DSP HIDL diagnostics (base) |
| `dsp_hidl_session_diag` | 7,872 | ✗ | DSP HIDL diagnostics (session) |
| `dsp_poc_standalone` | 1,288,720 | ✗ | DSP PoC (standalone version) |
| `dsp_raw_v2` | 8,160 | ✗ | DSP raw channel test v2 |
| `kaslr_leak` | 8,336 | ✗ | KASLR leak test (early version; the current leak goes through `printk_formats`) |
| `test_0582` | 2,145,128 | ✗ | CVE-2024-0582 test (io_uring; not reachable on this device, see FACTS §5 C3) |
| `test_21479` | 16,608 | ✗ | CVE-2025-21479 (KGSL micronode) test |
| `test_36971` | 10,016 | ✗ | CVE-2024-36971 test (not triggerable on this device, see FACTS §5 C2) |
| `fakepayload` | 4,096 | ✗ | **not an executable: a 4096-byte all-zero page** (sha256 `ad7facb2…92ca7` = the standard hash of 4096 zero bytes); it appears in no report and its purpose cannot be determined |
| `min_payload` | 24 | ✗ | **not an executable: a 24 B remnant of a `CrAU` (Android OTA `payload.bin`) header** (the version field is little-endian = hand-crafted); it points at the update_engine/OTA surface, not at a kernel CVE |

- **Source**: `<DEVICE_SERIAL>:/data/local/tmp/<name>` (adb pull, 2026-09-13)
- **Corresponding inventory**: `reports/missing_binaries_inventory_2026-09-13.md` §4.1/§4.3
- **Not included**: the other 17 binaries that "exist only on the device but whose source
  is still in `source_code/`" (they can be rebuilt with `deploy/build_probe_zig.sh` or the
  NDK, so they need not take up version-control space).

## The **Origin** of the Two Non-Executables (traced on 2026-09-13)

| File | mtime (device side) | owner/mode | Origin interpretation |
|------|-----------------|-----------|----------|
| `fakepayload` | 2026-08-14 **17:28:13** | shell / 0666 | all-zero page = a "fake payload" test input |
| `min_payload` | 2026-08-14 **17:28:38** | shell / 0666 | 24 B `CrAU` header remnant = a "minimal payload" test input |

The session on that same day (2026-08-14) is `reports/REVERSE_ANALYSIS_1.22_009.md`:
**update_engine, `applyPayload`, the payload signature chain** and the payload
extraction in `binaries/extracted_1.22/`. The two files are **temporary inputs left
behind by that day's payload gate experiments**; neither the workspace nor any device
script references them → they are retained only as provenance evidence and have **no
research value** (`fakepayload` carries zero information in its content).

## Notes

1. These are **device-side build artifacts** and may not match the report generation
   (most are from 2026-05 through 08); when citing them, note the source and date.
2. **10 of the 12 are aarch64 executables** (the other 2 are in the table above: the
   all-zero page and the OTA header remnant). For classification, sha256, `DT_NEEDED`
   and the CVE/subsystem mapping, see `reports/static_re_gate_codes_and_rescue_2026-09-13.md`.
3. Under the classification principles in `MANIFEST.md`, build artifacts are generally
   not placed under version control; **this directory is the exception for
   "non-rebuildable" items**. Do not put files here that can be rebuilt from source.
