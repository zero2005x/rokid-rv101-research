# Session 16 Research Summary - Rokid RV101 Update Engine Payload Gates RE

> 中文版：[SESSION_16_UPDATE_ENGINE_PAYLOAD_GATES.md](SESSION_16_UPDATE_ENGINE_PAYLOAD_GATES.md)

> ⚠ **2026-09-13 correction**: the **Fastboot/Magisk/flash-boot.img route assumed in this document is not feasible** (boot.img cannot be obtained, there is no Magisk, and there is no persistent root); **root has been acquired at runtime via the GhostLock LPE (CVE-2026-43499)**.


## Target Device
- **Model**: Rokid AR Glasses RV101
- **OS**: Android 12
- **Target Binary**: `/system/bin/update_engine` (AArch64 ELF64 Little-Endian)
- **Goal**: Drive `update_engine` via Binder `ApplyPayload` to flash Magisk-patched boot image (`magisk_patched-122_009.img`) without full bootloader unlock.

---

## Executive Summary

This session focused on reverse engineering `update_engine` (analyzing stripped binary `update_engine3.bin`) to trace and resolve the execution gates inside `UpdateAttempterAndroid::ApplyPayload` and `DeltaPerformer`.

### Key Outcomes
1. **Header Parsing Confirmed**: Verified that `ParseHeaders` splits on `=` and matches fixed `.rodata` key strings. Passed `--headers` keys MUST be exact uppercase.
2. **Metadata Size Gate Resolved**: Confirmed `METADATA_SIZE` is parsed from headers, stored in `InstallPlan::Payload`, and passed to `DeltaPerformer`. Passing the correct `METADATA_SIZE=<manifest_size>` bypasses the mandatory metadata size check.
3. **`hash_checks_mandatory` Gate Pinpointed**: Located the write to `install_plan_.hash_checks_mandatory` at `0xcb140–0xcb154`. It is set by calling `hardware_->IsOfficialBuild()` (virtual method at vtable offset `+0x10`), making it **build-property-derived** (via `ro.debuggable` / `ro.build.type`), NOT controllable via payload headers and NOT hardcoded.
4. **Header Key Table Mapped**: Mapped exact `.rodata` string locations for all recognized payload header parameters.

---

## Detailed Technical Findings

### 1. Header Keys & Parser Architecture
- **Parser Location**: `0xcc550` (`ParseHeaders`)
- **Key Table (`.rodata`)**:
  | Header Key | Address | Description / Impact |
  | :--- | :--- | :--- |
  | `FILE_SIZE` | `0x2efa6` | Total payload byte size |
  | `FILE_HASH` | `0x2efb0` | SHA-256 hash of entire payload |
  | `METADATA_SIZE` | `0x2efba` | Manifest size (bytes); populates payload `metadata_size` |
  | `METADATA_HASH` | `0x2efc8` | SHA-256 hash of manifest header |
  | `POWERWASH` | `0x2efef` | Controls data wipe on update |
  | `SWITCH_SLOT_ON_REBOOT` | `0x2f004` | Switch active A/B slot on reboot |
  | `RUN_POST_INSTALL` | `0x2f01a` | Execute post-install script on target partition |
  | `NETWORK_ID` | `0x2f03c` | Network interface binding |
  | `AUTHORIZATION` | `0x2f047` | Auth header token for HTTP fetcher |
  | `USER_AGENT` | `0x2f055` | Custom HTTP User-Agent string |

### 2. `InstallPlan` & `DeltaPerformer` Field Layout
- **`UpdateAttempterAndroid` offsets**:
  - `this + 0x48`: `hardware_` subsystem pointer (`HardwareAndroid` / `HardwareInterface`)
  - `this + 0x80`: `install_plan_` (`InstallPlan` struct)
  - `this + 0xb8`: `install_plan_.payloads` (`std::vector<InstallPlan::Payload>`)
  - `this + 0xf0`: `install_plan_.hash_checks_mandatory` (offset `+0x70` from `install_plan_` base)
  - `this + 0xf1`: `install_plan_.powerwash_required`
  - `this + 0xf2`: `install_plan_.switch_slot_on_reboot`
  - `this + 0xf3`: `install_plan_.run_post_install`
  - `this + 0xf6`: `install_plan_.is_resume`

- **`InstallPlan::Payload` offsets (size = `0x98` bytes)**:
  - `+0x18`: `size` (`FILE_SIZE`)
  - `+0x20`: `metadata_size` (`METADATA_SIZE`)
  - `+0x28`: `metadata_signature`

### 3. Disassembly Analysis of Key Sites

#### A. Header Extraction & Size Assignment (`0xcb0c0–0xcb130`)
```assembly
cb0dc: bl   base::StringToUint64   ; Parse METADATA_SIZE string to uint64
cb0f4: tbnz w23, #0, 0xcb0fc       ; If parse succeeded, keep value; else set 0
cb0f8: stur xzr, [x29, #-0x80]     ; payload.metadata_size = 0 (on failure)
cb10c: sub  x1, x29, #0xa0         ; payload struct on stack
cb110: bl   vector::push_back      ; push to install_plan_.payloads
```

#### B. `hash_checks_mandatory` Assignment (`0xcb140–0xcb154`)
```assembly
cb140: ldr  x0, [x19, #0x48]       ; x0 = this->hardware_
cb144: ldr  x8, [x0]               ; x8 = vtable pointer
cb148: ldr  x8, [x8, #0x10]        ; x8 = vtable[2] -> HardwareAndroid::IsOfficialBuild()
cb14c: blr  x8                     ; Call IsOfficialBuild()
cb150: and  w8, w0, #0x1           ; Mask return value (bool)
cb154: strb w8, [x19, #0xf0]       ; install_plan_.hash_checks_mandatory = is_official
```

#### C. Static String Verification
- `ro.debuggable` is present in binary @ `0x1a076`.
- `ro.build.type` / `userdebug` literals are NOT present in `.rodata`, indicating `IsOfficialBuild()` keys off `ro.debuggable` or system properties via `android::base::GetBoolProperty`.

---

## 4-Point Action Plan & Status

| Step | Action Description | Current Status | Findings / Next Actions |
| :--- | :--- | :--- | :--- |
| **1** | **Device Test (Size Check)**: Send Binder `ApplyPayload` with exact uppercase `--headers="METADATA_SIZE=<manifest_size>"` | **Ready for Execution** | Header parser confirmed; `METADATA_SIZE` key verified @ `0x2efba`. |
| **2** | **Test Next Gate**: Observe if execution reaches signature / hash verification | **Pending Step 1** | Awaiting logcat output from Step 1 on-device run. |
| **3** | **Parallel RE**: Identify setting point of `hash_checks_mandatory` and public key loading | **Completed** | Proved `hash_checks_mandatory` is derived from `hardware_->IsOfficialBuild()` @ `0xcb140–0xcb154`. |
| **4** | **Payload Construction / Fallback Pivot**: If gates pass, construct `REPLACE boot` payload with Magisk; if blocked, pivot to LPE or fastboot | **Pending Steps 1 & 2** | `magisk_patched-122_009.img` ready for packaging if gates clear. |

---

## Required Parameters for Device Testing

To execute Step 1 on-device, the following parameters are active:
1. **Target Header Format**:
   ```
   FILE_SIZE=<payload_file_size>
   FILE_HASH=<base64_or_hex_sha256>
   METADATA_SIZE=<exact_manifest_protobuf_byte_size>
   METADATA_HASH=<base64_or_hex_manifest_sha256>
   SWITCH_SLOT_ON_REBOOT=1
   RUN_POST_INSTALL=0
   ```
2. **Payload URL**: `file:///data/local/tmp/payload.bin` or HTTP local server URL.
3. **Logcat Filter**: `adb logcat -s update_engine:V update_engine_client:V update_engine_sideload:V`
