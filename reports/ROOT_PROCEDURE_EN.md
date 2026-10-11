# Rokid AR Glasses Root Procedure

> 中文版：[ROOT_PROCEDURE.md](ROOT_PROCEDURE.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; it decides any conflict).

> ## ⚠ 2026-09-13 Current-State Guidance (this document is a historical record from 2026-02)
> 
> - **root was acquired long ago** (since 2026-08-22, `uid=0(root) context=u:r:kernel:s0`, SELinux Permissive);
>   the current one-shot flows are `deploy/root_bridge.sh` (bridge helper) or `deploy/start_shizuku.sh` (Shizuku) + `deploy/get_root_now.sh`.
> - The boot.img blocker described in this document **only affects "persistent root across reboots"** (which needs a Magisk-patched boot), and does not affect runtime privilege escalation.
> - Privilege escalation must be armed once per boot over adb; once acquired, running `deploy/install_root_keepalive.sh` is recommended so the daemon survives the whole session.
> - For the latest architecture and operations see `docs/deploy.md` (§9 and § Privilege-Escalation Operations P0–P3) and `reports/su_escalation_roi_assessment.md`.


> **🔴 Current status (2026-02-02): root privileges have not yet been obtained**
> **Blocking cause: the boot.img file cannot be obtained**

## Current Status

| Item               | Status                                                     |
| ------------------ | -------------------------------------------------------- |
| **Root privileges**      | ❌ **not obtained**                                            |
| **Device**           | RG-glasses (Serial: <DEVICE_SERIAL>)                    |
| **Android**        | 12                                                       |
| **Kernel**         | 5.10.209-perf                                            |
| **Bootloader**     | ✅ **Unlocked** (flash.locked=0, verifiedbootstate=orange) |
| **Magisk app**     | ✅ 30.6 installed, ❌ boot image **not patched**                 |
| **SELinux**        | Enforcing                                                |
| **Current boot slot** | A (boot_a → /dev/block/mmcblk0p17)                       |

---

## 🔴 Blocker: boot.img Cannot Be Obtained

| Method tried                       | Result | Notes                          |
| ------------------------------ | ---- | ----------------------------- |
| `dd /dev/block/mmcblk0p17`     | ❌   | Permission denied (root required) |
| `fastboot fetch boot_a`        | ❌   | The device does not support this command              |
| `fastboot oem dump-partitions` | ❌   | unknown command               |
| Recovery Sideload              | ❌   | Driver issues caused it to hang              |
| Custom Recovery (TWRP)         | ❌   | No compatible version found                |
| Official OTA firmware                  | ❌   | No download source found                |

---

## Root Steps (boot.img must be obtained first)

### Step 1: Extract the Boot Image ❌ blocked

```bash
# Method A: read directly (requires root - currently unavailable)
adb shell "dd if=/dev/block/mmcblk0p17 of=/data/local/tmp/boot.img" 2>&1

# Method B: extract from an OTA package (a firmware image must be found)
payload-dumper-go -o extracted/ firmware.zip

# Method C: extract via TWRP (a compatible TWRP must be found)
fastboot boot twrp.img
adb shell dd if=/dev/block/mmcblk0p17 of=/sdcard/boot.img
adb pull /sdcard/boot.img
```

### Step 2: Patch with the Magisk App

1. Transfer boot.img to the device
2. Open the Magisk app
3. Choose "Install" → "Select and Patch a File"
4. Select boot.img
5. Wait for patching to finish; magisk_patched-xxx.img is produced

### Step 3: Flash the Patched Boot Image

```bash
# Enter fastboot mode (the correct way)
adb reboot recovery
# On the glasses screen choose "Enter fastboot"

# Verify the connection
fastboot devices

# Flash the patched boot image
fastboot flash boot_a magisk_patched-xxx.img

# Reboot
fastboot reboot
```

### Step 4: Verify Root

```bash
adb shell su -c id
# should show uid=0(root)
```

---

## Viable Alternatives

### Option A: Find official Rokid firmware (best)

1. Contact Rokid support to obtain the firmware
2. Look for firmware shared on forums/communities
3. Unpack payload.bin with `payload-dumper-go`

### Option B: Find a compatible TWRP

1. Look for TWRP for the SM6375/khaje platform
2. Boot it temporarily with `fastboot boot twrp.img`
3. dd the boot partition out from within TWRP

### Option C: EDL mode (requires disassembly)

1. Short the test points on the device to enter EDL mode
2. Read the partitions with QFIL/Qualcomm tools
3. The correct firehose file must be found

---

## Software Attack Paths Confirmed Failed

All kernel exploit attempts have failed; see [FINAL_STATUS_20260202.md](./FINAL_STATUS_20260202.md) for details

| Vulnerability                       | Status               |
| -------------------------- | ------------------ |
| CVE-2024-36971 (Route UAF) | ❌ v1-v13 all failed |
| CVE-2024-0582 (io_uring)   | ❌ feature does not exist      |
| BPF information leak               | ❌ all failed        |
| KGSL exploitation                  | ❌ v1-v9 all failed  |
| Dirty Pipe                 | ❌ patched          |
