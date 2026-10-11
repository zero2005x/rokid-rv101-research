# Reverse Engineering Report - RVE01 1.22.009 Firmware Package

> 中文版：[REVERSE_ANALYSIS_1.22_009.md](REVERSE_ANALYSIS_1.22_009.md)

**Date**: 2026-08-14
**Goal**: Reverse engineer the downloaded `RVE01-ota-1.22.009-20260710-150201.zip` to identify usable root attack surfaces and potential weaknesses.

---

## 1. Reverse Engineering Scope

| Item | Source | Result |
|------|------|------|
| Recovery ramdisk | recovery.img | fully unpacked locally to `recovery_root\fs\` |
| fastbootd binary | recovery ramdisk `/system/bin/fastbootd` | 1288832 B, complete command table |
| recovery binary | recovery ramdisk `/system/bin/recovery` | 1923816 B, sideload signature verification confirmed |
| XBL / UEFI | xbl.img / uefi.img / xbl_config.img | bootloader fastboot confirmed stripped down |
| vendor.img | 7z ext4 | GateServiced, init rc analysis |
| product.img | 7z ext4 | RokidOtaUpgrade.apk reverse engineering |
| system.img | payload extraction | update_engine, build.prop |
| vbmeta / vbmeta_system | payload extraction | AVB flags confirmed |

---

## 2. Key Findings (Weakness Surface)

### 2.1 Fastbootd (userspace fastboot) — ⭐ Most Promising Path
- **fastbootd's restrictions apply only to locked devices**: `Flashing is not allowed on locked devices`, `Erase is not allowed on locked devices`, `Download is not allowed on locked devices`, `set_active command is not allowed on locked devices`.
- This device's **bootloader is already unlocked** (`ro.boot.flash.locked=0`, `verifiedbootstate=orange`, `Unlock = Yes` inside xbl_config).
- Therefore, if `fastboot flash boot_b` can be executed **in fastbootd mode**, the locked-device check will not fire.
- The recovery ramdisk contains `service fastbootd /system/bin/fastbootd`, and recovery supports the `--fastboot` argument (`Entering fastboot`).
- **Historical blocker**: earlier tests of `adb reboot fastboot` and `fastboot reboot fastboot` both failed. Those go through the bootloader fastboot, and the bootloader's fastboot has been stripped down (flash/boot/erase/oem unlock/flashing unlock all return `unknown command`).
- **Unverified breakthrough point**: the built-in fastbootd in recovery has never actually been triggered. It needs testing by manually running fastbootd inside recovery, or writing the misc bootloader_message `command=boot-fastboot` + the `--fastboot` argument.

### 2.2 Bootloader fastboot — Completely Stripped Down
- The fastboot at the XBL (Qualcomm) layer retains only `getvar`, `download`, `reboot`, `reboot-recovery`; all other write commands have been removed.
- xbl_config.img contains `Unlock = Yes` and `EnableUfsIOC = 1` (UFS I/O enabled).
- uefi.img (2.6MB) is the core DXE and contains no fastboot command table strings (fastboot lives in another DXE that was removed).

### 2.3 OTA Client (RokidOtaUpgrade.apk)
- Package path: `product/app/RokidOtaUpgrade/RokidOtaUpgrade.apk` (3,615,458 B, `classes.dex` 6,992,392 B, 5453 classes).
- **Verification chain**: `MyUpdateEngine.applyPayload()` → `android.os.UpdateEngineService` (runs `update_engine` as root).
- **Download target**: `/sdcard/update.zip`.
- **Key properties**:
  - `ro.rokid.ota.check_url=https://ota.rokid.com`
  - `ro.rokid.ota.check_api=/v1/extended/ota/check`
  - `ro.product.ab_ota_partitions=system,system_ext,product,vbmeta_system`
- The update.zip downloaded by the OTA client is ultimately verified by update_engine for signature (payload signature) and serial number. **The verification chain is complete and cannot be exploited directly**.

### 2.4 Recovery sideload — Signature Verification Complete
- The recovery binary contains `RSA_verify` / `ECDSA_verify` / `X509_get_signature_nid` (the whole libmincrypt family).
- It uses `otacerts.zip` (containing Rokid releasekey.x509.pem, public key, SHA-256 with RSA).
- **Additional check**: `Package is for serial ...` — recovery verifies the serial number inside the OTA package to prevent cross-device flashing.
- `bootreason is in the blocklist; skip OTA installation` — there is a bootreason denylist mechanism.
- **Conclusion**: sideload cannot bypass the signature (the private key is not on the device).

### 2.5 GateServiced — TrustZone Communication (Fully Studied Previously)
- Runs as root (`init.gateserviced.rc`: user root, group root system, disabled, started after `sys.boot_completed=1`).
- AArch64 ELF (20304 B), depends on `CreateDmabufHeapBufferAllocator`, `TZCom_getClientEnvObject` (TrustZone).
- Contains `run_exec_cmd_ta` (load/unload TA), DMA-Buf heap allocation, socket listening, `cmd_dispatch`.
- Previous research already tested DMA overflow and similar approaches on device; all failed, no usable vulnerability.

### 2.6 update_engine
- `/system/bin/update_engine` (2,083,864 B) runs as **root** (`user root group root system wakelock inet cache media_rw`).
- `update_engine.rc`: `on property:ro.boot.slot_suffix=*` → `enable update_engine`.
- Triggered by the OTA APK through the binder `applyPayload`; payload signature verification is complete.

### 2.7 Other System Services (root Privilege Surface)
- `bootconfigs` (/system/xbin/bootconfigs): root, seclabel `u:r:init:s0`, executed at post-fs.
- `backup_keys` (/system/xbin/backupkeys): root, seclabel `u:r:init:s0`, executed at boot_completed.
- `init.rokid.rc`: a large number of `write /sys/.../uart_en` and similar, UART console control (enabled when `ro.bootimage.build.type=userdebug`).

---

## 3. Extracted/Downloaded Assets (Local)

| File | Location |
|------|------|
| OTA zip | `binaries\RVE01-ota-1.22.009-20260710-150201.zip` |
| payload.bin + extracted contents | `binaries\extracted_1.22\` (boot/vendor_boot/vbmeta/recovery/vendor/system/product/system_ext/xbl/uefi etc.) |
| Magisk patched boot | `binaries\extracted_1.22\magisk_patched-122_009.img` |
| Recovery ramdisk | `binaries\extracted_1.22\recovery_root\fs\` |
| vendor unpack | `binaries\extracted_1.22\vendor\extract\` |
| OTA APK unpack | `binaries\extracted_1.22\ota_apk\` |
| product unpack | `binaries\extracted_1.22\product\extract\` |

---

## 4. Weakness Assessment Summary

| Attack surface | Exploitability | Notes |
|--------|---------|------|
| **Fastbootd (built into recovery)** | 🟡 **unverified, most promising** | an unlocked device allows flashing, but fastbootd has never been entered successfully |
| Bootloader fastboot | 🔴 stripped down | flash/boot/erase all removed |
| Recovery sideload | 🔴 signature complete | Rokid releasekey verification + serial number check |
| OTA update_engine | 🔴 signature complete | payload verification performed by the root update_engine |
| GateServiced | 🔴 no usable vulnerability | TrustZone communication, previous research failed |
| Known CVE exploits | 🔴 all failed | see FINAL_STATUS_20260202.md |

### Main Attack Path Assessment
1. **⭐ Enter fastbootd** (top priority): find a way to start the fastbootd built into recovery on an unlocked device, then `fastboot flash boot_b magisk_patched-122_009.img`.
   - Option A: manually start fastbootd inside recovery.
   - Option B: write the misc bootloader_message (requires root or a vulnerability).
   - Option C: recovery menu (if "Enter fastboot" enters the bootloader rather than fastbootd, another route must be found).
2. **Magisk patched boot is ready**: `magisk_patched-122_009.img` (sha1 `730c3f89...`) has been pulled locally and can be flashed at any time.

---

## 5. Analysis of Write Paths to the misc Partition (bootloader_message)

### 5.1 misc Partition State
- Location: `/dev/block/mmcblk0p59` (by-name `misc`, 1024 sectors = 512KB).
- Permissions: `brw------- 0600 root:root`, SELinux `u:object_r:misc_block_device:s0`.
- shell (uid=2000) **can neither read nor write it** (`dd` measured as Permission denied).
- recovery_a=p19, recovery_b=p20, boot_a=p17, boot_b=p18, misc=p59 (GPT mapping confirmed).

### 5.2 bootloader_message (BCB) Structure
Standard AOSP `bootloader_message` structure (used by libbootloader_message.so / libboot_control_qti.so):
```
offset 0x000: command[32]        // "boot-fastboot" / "boot-recovery" / "reboot"
offset 0x020: status[32]
offset 0x040: recovery[768]      // recovery command
offset 0x340: reserved[224]
offset 0x420: (A/B extension area)
  slot_suffix[32], update_channel[128], recovery_api_version[32], compatible[32]
offset 0x1000: BootloaderControl (A/B slot metadata, BootCtrl magic 0x0BADB1DE + CRC)
```
- recovery supports the `--fastboot` argument: writing `command=boot-fastboot` + placing `--fastboot` in `recovery[]` makes the next boot enter **userspace fastbootd**.
- libboot_control_qti.so exports `write_reboot_bootloader`, `write_bootloader_message_to`, `SetMiscBlockDeviceForTest`.

### 5.3 Write Path Assessment

| Path | Privilege required | Usability |
|------|---------|--------|
| `dd` directly writing misc (p59) | root + misc_block_device | 🔴 shell has no permission |
| update_engine (root) | needs a trigger with an officially signed payload | 🔴 signature verification complete |
| boot HAL `android.hardware.boot@1.2-service` | runs as root, binder call | 🟡 requires root to trigger, no shell interface |
| libboot_control `SetMiscBlockDeviceForTest` | internal root test interface | 🔴 no external trigger surface |
| recovery `--fastboot` | requires entering recovery first | 🟡 **feasible, but requires entering recovery** |
| misc_writer (vendor) | SELinux `vendor_misc_writer_exec` | 🔴 this tool does not exist on the device |

### 5.4 Conclusion
- **misc cannot be written from the Android shell** (no root, SELinux enforcing, misc_block_device is root-only).
- **The shortest path remains starting fastbootd after entering recovery**: the recovery ramdisk init has `service fastbootd /system/bin/fastbootd`, and the recovery binary supports `--fastboot`.
- Writing misc through update_engine/boot HAL both require the already-verified OTA flow and cannot be exploited directly.
- **The only unverified practical attack surface**: in the recovery environment (where adbd runs as root), manually run `/system/bin/fastbootd` or write misc to set `command=boot-fastboot`.

---

## 6. On-Device Verification: Paths into recovery / fastbootd (2026-08-14, this session)

### 6.1 Measured Results

| Test | Result |
|------|------|
| `adb reboot recovery` | ❌ boots straight back to Android (uptime 28s, bootreason=`reboot,shell`) |
| `adb reboot bootloader` | ❌ boots straight back to Android (uptime 77s) |
| `/system/bin/fastbootd` in the normal system | ❌ does not exist (only in the recovery ramdisk) |
| `/system/bin/recovery` in the normal system | ❌ does not exist |
| misc partition (p59) read/write | ❌ Permission denied |
| recovery partition | ✅ exists (p19/p20, recovery.img contents correct, contains fastbootd) |

### 6.2 Conclusion: The Bootloader Has Blocked the Software Paths into recovery/fastbootd

- `adb reboot recovery` and `adb reboot bootloader` are both ignored by the bootloader (XBL), which boots normal Android directly.
- The kernel cmdline has no `androidboot.recovery` parameter; the reason read by `qcom-reboot-reason.ko` is `reboot,shell` (the recovery/bootloader parameters did not take effect).
- The `/system/bin/reboot` tool supports the `recovery`/`bootloader`/`fastboot` arguments, but the bootloader ignores them after the reboot.
- The device has only a power button (qpnp_pon / PM8150) and a touchpad, no volume keys, and no hardware key combination to enter recovery.
- Earlier reports (older firmware 1.14/1.19) recorded that `adb reboot recovery` could enter the Recovery menu; **1.22.009 has blocked this path**.

### 6.3 Impact

- **The fastbootd path is unreachable**: recovery with fastbootd cannot be entered through `adb reboot recovery/fastboot` or a key combination.
- **The misc write path is not viable**: writing misc requires root (and root is exactly what we are trying to obtain).
- **The only remaining paths**:
  1. Write misc / start fastbootd after obtaining root (chicken-and-egg problem).
  2. EDL (Emergency Download) 9008 mode (Qualcomm firehose, flashing partitions directly while bypassing bootloader verification) — requires entering EDL by hardware means or a flashing cable.
  3. Leverage the already-unlocked bootloader (orange state) + the existing Magisk patched boot, through another unblocked flashing channel.

---

## 7. KASLR Leak Breakthrough (2026-08-14 kernel/userspace 0-day session)

### 7.1 printk_formats Leaks Kernel Addresses

**`/sys/kernel/tracing/printk_formats` is readable and leaks 202 kernel virtual addresses.**

```bash
shell@neo:/ $ cat /sys/kernel/tracing/printk_formats | head
0xffffffd23e82ac0c : "Rescheduling interrupts"
0xffffffd23e8ca100 : "Function call interrupts"
...
```

- shell uid=2000 is in the `readtracefs` (3012) group and can read `/sys/kernel/tracing/`.
- `printk_formats` is `r--r--r--` under `/sys/kernel/tracing/`, fully readable.
- **Kernel base ≈ 0xffffffd23e820000** (derived from the lowest leaked address 0xffffffd23e82ac0c, with `_text` at `_stext+0`).
- dmesg confirms `KASLR enabled`, `kpti=off` (kernel global mapping, not strongly isolated).
- All previous research listed "KASLR bypass blocked by kptr_restrict=2" as the main obstacle; **this is now removed**.

### 7.2 Other Newly Confirmed Configurations

| Configuration | Value | Meaning |
|------|-----|------|
| `dmesg_restrict` | 0 | dmesg is fully readable (oops/warning address leaks are feasible) |
| `kptr_restrict` | 2 | kallsyms/iomem are masked, but printk_formats is not masked |
| `bpf_jit_enable` | 1 | BPF JIT enabled |
| `bpf_jit_harden` | 0 | **JIT not hardened** |
| `unprivileged_bpf_disabled` | 0 | unprivileged BPF is available |
| `/proc/slabinfo` | readable by root:log | shell is in the log group → **heap layout leak is feasible** |
| kernel cmdline | `kpti=off`, `kasan=off`, `mem auto-init: heap alloc:on/free:on` | kernel global mapping; SLUB auto-init enabled |
| USB config | `adb,diag` | DIAG interface exposed (used by host-side QXDM) |
| ftrace | `trace` is 666 and writable, `kprobe_events`/`set_event` are not writable | cannot write kprobes, but can read the trace buffer |

### 7.3 The Remaining Piece: A Write Primitive

KASLR is solved, but a **kernel write/memory-corruption primitive** is still missing. All previous corruption attempts (BPF verifier, KGSL, binder UAF, TIPC) were blocked by CFI/SCS/SLAB auto-init. The primitives available are:

1. **KGSL cross-FD UAF** (CVE-2025-21479): can keep a freed GPU buffer mapping alive, but is limited by SMMU/GPU VA isolation and cannot reach the kernel slab directly.
2. **BPF heap spray + known KASLR**: can precisely predict JIT code locations, but a verifier vulnerability is required in order to write.
3. **slabinfo leak**: allows precise heap feng shui (combined with any OOB write).

---

## 8. Recommended Follow-Up Actions

1. **EDL 9008 mode** (most promising): Qualcomm devices can be put into EDL (Emergency Download) by specific means, then firehose can flash partitions directly, bypassing the bootloader's fastboot/recovery blocking. Previous research already produced the `sahara_reset.py` tool and EDL entry experience, so entering EDL by hardware means can be attempted.
2. **Re-examine the earlier EDL test records**: `Rokid_RedTeam_Internal_Report.md` mentions "EDL 9008 entry method + recovery tool"; confirm how EDL was entered at the time and what firehose flashing could do.
3. **Exploit older firmware with an earlier bootloader**: older firmware (1.14/1.19) could enter recovery; if the bootloader behavior of the old firmware or a downgrade path can be obtained, it can be reproduced.
4. **Analyze the EDL trigger logic of uefi.img/xbl.img**: check the PMIC/SPMI key combinations or USB plug/unplug conditions that trigger EDL.
5. If EDL is also blocked, the remaining paths are: an unlocked bootloader + finding an unblocked flashing channel, or continuing to hunt for kernel/userspace 0-days.

---

## 9. KGSL In-Depth Analysis: CVE-2026-21385 Verification (2026-08-14, this session)

### 9.1 KGSL ioctl dispatch Table (Actually Registered on 1.22.009, 47 in Total)

The complete registered ioctl set extracted from `kgsl_ioctl_funcs` (0x98f8, size 0x600) in `msm_kgsl.ko` `.rodata`:

| ioctl | Function | Status |
|-------|------|------|
| `0xc0180920` (GPUMEM_ALLOC) | allocate GPU memory | 🔴 **ENOTSUP (95)** feature disabled |
| `0xc0080913` (DRAWCTXT_CREATE) | create GPU context | 🔴 previously confirmed blocked |
| `0xc0100958` (GPUOBJ_IMPORT) | import user memory | 🟡 usable, **align is forcibly overwritten** |
| `0xc0300957` (GPUMEM_BIND_RANGES/SPARSE) | sparse/VBO bind | 🟡 usable, no align injection |
| `0xc0200956` (GPUOBJ_SYNC) | gpuobj sync | 🟡 usable |
| `0xc0300930` (SYNCSOURCE_CREATE) | syncsource | 🔴 **ENOTTY (25)** |
| `0xc0180932` (SYNCSOURCE_CREATE_FENCE) | fence creation | 🔴 **ENOTTY (25)** |
| `0xc0300938` (GPUOBJ_ALLOC) | gpuobj allocation | 🔴 **not registered** (EINVAL) |
| `0xc008093a` (GPUOBJ_SET_INFO) | configure gpuobj | 🔴 **not registered** |

### 9.2 CVE-2026-21385 (alignment sign-extension) Verification Conclusion

**Nature of the vulnerability** (CodeLinaro commit `91a0db3aa438bffbd235df8301bf188dbab218b7` "kgsl: Prevent sign extension on alignments"):
- `kgsl_memdesc_get_align` return type `int` → `u32`.
- `align = max_t(uint64_t, 1 << kgsl_memdesc_get_align(memdesc), PAGE_SIZE)` in `kgsl_iommu_get_gpuaddr` → `align = kgsl_get_align(memdesc)`.

**Device state determination (disassembly evidence)**:
- Inside `kgsl_iommu_get_gpuaddr` (0xa6170), `0xa61d0: ubfx x11,x8,#0x10,#0x10; lsl w11,w12,w11` (`1 << align` inlined, **no capping, no `bl kgsl_get_align`**) → **the iommu path is in the pre-patch state**.
- There is no global `kgsl_get_align` symbol.
- `kgsl_get_unmapped_area` (0x10e00) has `0x10fb8: ubfx w9,w3,#0x10,#8; cmp w9,#0x14; ... csel` (SZ_2M/1M/64K capping) → this capping is the **pre-existing** `get_align()` and does not mean the fix has been applied.

**Conclusion**: the CVE-2026-21385 patch has **not been applied completely** (the iommu path is still the old version).

### 9.3 Exploitability Verification (On Device) — Not Exploitable

| Path | Result |
|------|------|
| GPUOBJ_ALLOC align injection | 🔴 ioctl not registered (EINVAL), cannot reach the align check |
| GPUMEM_ALLOC align injection | 🔴 ENOTSUP, feature disabled |
| GPUOBJ_IMPORT align injection | 🟡 **can be set to 0-255**, but `kgsl_ioctl_gpuobj_import` at `0x80ac-0x80d8` **forcibly overwrites align** based on size (size≥1MB→align=20, size≥64K→align=16), so the userland value has no effect |
| SPARSE_BIND (0x57) | 🟡 creates a sparse obj (kzalloc 0x50), no align injection |
| SYNCSOURCE_CREATE/FENCE | 🔴 ENOTTY, CVE-2026-KGSL-UAF unreachable |

**Determination**: the device's KGSL is a "trimmed-down version"; every ioctl that could be used to inject align and control GPU VA placement has been closed off. **CVE-2026-21385 is not exploitable on this device** (even though the iommu path is unpatched, userland cannot control align to reach `kgsl_iommu_get_gpuaddr`).

### 9.4 Appendix: Other KGSL Analysis Notes

- `kgsl_ioctl_gpuobj_import` (0x7e98): type check (0x7ed0 `cmp w8,#2`), offset mask `0x1ffffff009d00000` (0x7f48, the CVE-2023-33107 fix), flags mask `0x3_1ff_ff08` (0x7f9c).
- `gpumem_alloc_entry` (0x9fc0): align check `0xa00c ubfx w8,w19,#0x10,#8; cmp w8,#0x19` (clamps to 25 when align ≥25, rather than rejecting).
- PAC enabled (`paciasp`/`autiasp` prologue/epilogue in every function).
- The earlier report's "6 usable ioctls (0x38-0x3a, 0x40, 0x56, 0x58)" is **incorrect** (based on an old build or a misjudgement); 1.22.009 actually has only 47 registered, and the memory-allocation surface is largely closed off.

---

## 10. Updated Path Assessment

| Path | Status | Notes |
|------|------|------|
| recovery/fastbootd | 🔴 blocked | §6 on-device verification confirmed |
| CVE-2026-21385 (KGSL align) | 🔴 not exploitable | §9 disassembly + on-device verification confirm userland cannot control align |
| CVE-2026-KGSL-UAF (syncsource) | 🔴 unreachable | ENOTTY |
| **EDL 9008 firehose** | 🟡 **most promising** | bypass bootloader verification and flash patched boot directly |
| Other kernel 0-days | 🟡 not exhausted | requires a new attack-surface scan |

**Next practical action**: return to EDL 9008 mode (`scripts/sahara_reset.py` + firehose), or, building on the existing KASLR leak, hunt for other reachable memory-corruption primitives in drivers.
