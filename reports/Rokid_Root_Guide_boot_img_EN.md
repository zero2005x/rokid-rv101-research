# Rokid AR Glasses Root Guide: Flashing a Custom boot.img

> 中文版：[Rokid_Root_Guide_boot_img.md](Rokid_Root_Guide_boot_img.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets and product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (a single authoritative fact table; that table decides any conflict).

> ⚠ **Historical document / non-current path (2026-09-13 note)**: this guide describes the **flashing a custom boot.img** (Magisk) route,
> which is blocked because boot.img cannot be obtained. **The current root method is the GhostLock LPE** (`deploy/root_bridge.sh`/`start_shizuku.sh`),
> and on success SELinux is **Permissive** (not the Enforcing described in this document).


## 📋 Prerequisite Checks

### Device State Confirmation

```bash
# 1. Confirm the bootloader is unlocked
adb shell getprop ro.boot.flash.locked
# expected output: 0 (unlocked)

# 2. Confirm the device state
adb shell getprop ro.boot.verifiedbootstate
# expected output: orange (unlocked state)

# 3. Confirm the currently used partition slot
adb shell getprop ro.boot.slot_suffix
# expected output: _a or _b

# 4. Confirm the Android version
adb shell getprop ro.build.version.release
# expected output: 12
```

### Device Information

| Item         | Value                        |
| ------------ | ------------------------- |
| Model         | Rokid RG-glasses (RV101)  |
| Android version | 12 (API 32)               |
| Partition scheme     | A/B dynamic partitions              |
| Bootloader   | **unlocked** (orange state) |
| Kernel version     | 5.10.209-perf             |
| Security patch     | 2024-07-05                |

---

## ⚠️ Important Warnings

1. **Flashing a custom boot.img may make the device unbootable**
2. **Make sure all important data has been backed up**
3. **A working Fastboot environment is required**
4. **It is recommended to back up the stock boot.img first so it can be restored**

---

## 📦 Tool Preparation

### Required Tools

1. **ADB & Fastboot**
   - Download: [Android SDK Platform-Tools](https://developer.android.com/studio/releases/platform-tools)
   - Make sure the version is >= 34.0.0

2. **Magisk Manager** (used to patch boot.img)
   - Download: [Magisk GitHub Releases](https://github.com/topjohnwu/Magisk/releases)
   - Recommended version: 26.x or newer

3. **Storage**
   - About 500MB of free space on the computer
   - About 200MB of internal device storage

4. **Qualcomm USB driver** (Windows users)
   - Download: Qualcomm HS-USB Driver
   - Used for Fastboot/EDL mode connections

---

## 🔌 Entering Fastboot Mode (Rokid-specific)

### Method 1: ADB Command

```bash
adb reboot bootloader
# wait about 5-10 seconds
fastboot devices
```

### Method 2: Hardware Button Combination

Since the Rokid glasses may not have conventional buttons, try:

```bash
# Method A: after a full shutdown
# 1. Long-press the power button for 10 seconds to shut down
# 2. Connect USB
# 3. Hold "power button" + "volume down" simultaneously (if present)
# 4. Hold for 5 seconds until the Fastboot screen appears

# Method B: use ADB
adb reboot bootloader

# Method C: through Recovery
adb reboot recovery
# In Recovery select "Reboot to bootloader"
```

### Fastboot Connection Troubleshooting

```bash
# If fastboot devices produces no output:

# 1. Check Windows Device Manager
#    - Look for "Android Bootloader Interface"
#    - If it shows an unknown device, install the Qualcomm driver

# 2. Try a different USB port (connect directly to a motherboard USB port)

# 3. Run the terminal with administrator privileges

# 4. Check the Fastboot version
fastboot --version
```

---

## 🔧 Step 1: Extract the stock boot.img

### Method A: Extract directly from the device (recommended)

```bash
# 1. Confirm the current active slot
adb shell getprop ro.boot.slot_suffix
# assume the output is _a

# 2. Find the boot partition path
adb shell ls -la /dev/block/by-name/ | grep boot
# expected output similar to:
# boot_a -> /dev/block/sda10
# boot_b -> /dev/block/sda11

# 3. Extract the boot_a partition (adjust according to the current slot)
adb shell "dd if=/dev/block/by-name/boot_a of=/sdcard/boot_a.img bs=4096"

# 4. Pull it to the computer
adb pull /sdcard/boot_a.img .

# 5. Verify the file
# Windows:
certutil -hashfile boot_a.img MD5
# Linux/Mac:
md5sum boot_a.img

# Save this MD5 value for backup verification!
```

### Method B: Extract from the OTA package

```bash
# If you have an OTA update package (update.zip)
# use payload-dumper-go to extract

# 1. Download payload-dumper-go
# https://github.com/ssut/payload-dumper-go/releases

# 2. Unzip payload.bin from the OTA package
unzip update.zip payload.bin

# 3. Extract boot.img
payload-dumper-go -o . payload.bin
# boot.img will be generated
```

---

## 🔧 Step 2: Extract vendor_boot.img (required on Android 12)

```bash
# Android 12 uses GKI (Generic Kernel Image)
# the vendor_boot partition must be handled as well

# 1. Extract vendor_boot
adb shell "dd if=/dev/block/by-name/vendor_boot_a of=/sdcard/vendor_boot_a.img bs=4096"
adb pull /sdcard/vendor_boot_a.img .

# 2. Back up the MD5 as well
certutil -hashfile vendor_boot_a.img MD5
```

---

## 🔧 Step 3: Patch boot.img with Magisk

### Method A: Patch on the device (recommended)

```bash
# 1. Install the Magisk Manager APK
adb install Magisk-v26.4.apk

# 2. Push boot.img to the device
adb push boot_a.img /sdcard/Download/

# 3. On the device:
#    - open Magisk Manager
#    - tap "Install" -> "Select and Patch a File"
#    - select /sdcard/Download/boot_a.img
#    - wait for patching to complete
#    - the patched file is saved as: /sdcard/Download/magisk_patched_xxxxx.img

# 4. Pull the patched file
adb pull /sdcard/Download/magisk_patched_*.img magisk_patched_boot.img
```

### Method B: Patch on the computer (using magiskboot)

```bash
# requires a Linux environment or WSL

# 1. Download Magisk and unzip it
unzip Magisk-v26.4.apk -d magisk_tmp

# 2. Find magiskboot (ARM64 version)
# path: magisk_tmp/lib/arm64-v8a/libmagiskboot.so
# rename it to magiskboot

# 3. Unpack boot.img
./magiskboot unpack boot_a.img

# 4. The following files will be generated:
#    - kernel (the kernel)
#    - ramdisk.cpio (initramfs)
#    - dtb (device tree, possibly)

# 5. Patch the ramdisk
./magiskboot cpio ramdisk.cpio "mkdir 0750 overlay.d" \
    "add 0644 overlay.d/sbin/magiskinit magiskinit"

# 6. Repack
./magiskboot repack boot_a.img magisk_patched_boot.img
```

---

## 🔧 Step 4: Handle vbmeta (critical step!)

**⚠️ This step is very important!** Android Verified Boot checks the signature of boot.img.
If vbmeta verification is not disabled, flashing the patched boot.img will make the device unbootable.

### Option A: Flash a verification-disabled vbmeta (recommended)

```bash
# Enter Fastboot mode
adb reboot bootloader

# Wait for the device to enter fastboot
fastboot devices
# should display the device serial number

# Flash vbmeta and disable verification
# --disable-verity: disable dm-verity
# --disable-verification: disable signature verification
fastboot flash vbmeta vbmeta.img --disable-verity --disable-verification

# If there is no stock vbmeta.img, a blank one can be created:
# dd if=/dev/zero of=vbmeta_disabled.img bs=256 count=64
# fastboot flash vbmeta vbmeta_disabled.img --disable-verity --disable-verification
```

### Option B: Use Fastboot parameters (supported by some devices)

```bash
# some devices support disabling directly
fastboot flashing unlock_critical  # if needed
fastboot oem disable-verity
fastboot oem disable-dm-verity
```

---

## 🔧 Step 5: Flash the patched boot.img

```bash
# Make sure you are still in Fastboot mode
fastboot devices

# Flash the patched boot.img to the current slot
# if the current slot is _a:
fastboot flash boot_a magisk_patched_boot.img

# Or use the generic command (automatically selects the current slot):
fastboot flash boot magisk_patched_boot.img

# Confirm the flash succeeded; the output should look like:
# Sending 'boot_a' (xxxxx KB)   OKAY [  1.xxx s]
# Writing 'boot_a'              OKAY [  0.xxx s]
```

### Android 12 special handling: vendor_boot

```bash
# If Magisk's output says vendor_boot needs patching, flash it as well:
fastboot flash vendor_boot_a vendor_boot_patched.img

# Note: in most cases only boot.img needs to be flashed
# only when Magisk explicitly says vendor_boot must be patched is it needed
```

---

## 🔧 Step 6: Reboot and verify

```bash
# Reboot to the system
fastboot reboot

# Wait for the system to start (it may take a long time)
# the first boot may show a warning message; this is normal

# After the system starts, verify the Root state
adb shell

# In the shell run:
su
# if successful, the prompt becomes #

# Verify the UID
id
# should show uid=0(root) gid=0(root)

# Verify the Magisk version
magisk -v

# Check the SELinux state
getenforce
# should show Enforcing (Magisk does not break SELinux)
```

---

## 🆘 Disaster Recovery

### Case A: Cannot boot but can enter Fastboot

```bash
# Enter fastboot
# long-press power + volume down (the exact combination depends on the device)

# Flash back the stock boot.img
fastboot flash boot_a boot_a.img

# Flash back the stock vbmeta
fastboot flash vbmeta vbmeta.img

# Reboot
fastboot reboot
```

### Case B: Completely unable to boot

```bash
# 1. Try to force entry into Fastboot
#    - cut power completely (if possible)
#    - hold the key combination while powering on

# 2. If there is a recovery partition
fastboot boot recovery.img

# 3. Last resort: reflash the complete firmware
#    requires the full stock flashing package
```

### Case C: Recovery via EDL mode is needed

```bash
# some Qualcomm devices support EDL (Emergency Download) mode
# this is the lowest-level recovery method

# To enter EDL mode:
adb reboot edl
# or hold a specific key combination

# Recover using QFIL or the edl tool
# requires the device's stock firehose programmer and partition images
```

---

## 📋 Complete Command Sequence (quick reference)

```bash
# ========== Backup phase ==========
adb shell getprop ro.boot.slot_suffix          # confirm the slot
adb shell "dd if=/dev/block/by-name/boot_a of=/sdcard/boot_a.img bs=4096"
adb shell "dd if=/dev/block/by-name/vendor_boot_a of=/sdcard/vendor_boot_a.img bs=4096"
adb shell "dd if=/dev/block/by-name/vbmeta_a of=/sdcard/vbmeta_a.img bs=4096"
adb pull /sdcard/boot_a.img .
adb pull /sdcard/vendor_boot_a.img .
adb pull /sdcard/vbmeta_a.img .

# ========== Patching phase ==========
adb install Magisk-v26.4.apk
adb push boot_a.img /sdcard/Download/
# patch using Magisk on the device...
adb pull /sdcard/Download/magisk_patched*.img magisk_patched_boot.img

# ========== Flashing phase ==========
adb reboot bootloader
fastboot devices
fastboot flash vbmeta vbmeta_a.img --disable-verity --disable-verification
fastboot flash boot_a magisk_patched_boot.img
fastboot reboot

# ========== Verification phase ==========
adb shell su -c id
adb shell su -c "getenforce"
adb shell su -c "magisk -v"
```

---

## 🔍 Troubleshooting

### Problem: Fastboot cannot recognize the device

```bash
# Check the driver
# Windows: install the Google USB Driver or the Qualcomm Driver
# check Device Manager for unknown devices

# Try a different USB cable/port
# make sure USB debugging is enabled
```

### Problem: Stuck on the boot screen after flashing

```bash
# this may be a vbmeta problem
# re-enter fastboot and make sure verification is disabled
fastboot flash vbmeta vbmeta_a.img --disable-verity --disable-verification
fastboot reboot
```

### Problem: SELinux becomes Permissive

```bash
# this usually means Magisk was not installed correctly
# try reflashing or use a newer version of Magisk
```

### Problem: Some apps detect Root

```bash
# use Magisk's DenyList (formerly MagiskHide)
# in Magisk Manager:
# Settings -> Configure DenyList -> add the apps that need to be hidden
```

---

## 📝 Additional Notes

### About the A/B partition system

Rokid uses an A/B partition scheme:

- `boot_a` / `boot_b`
- `system_a` / `system_b`
- `vendor_boot_a` / `vendor_boot_b`

**Advantages**:

- OTA updates are safer
- you can fall back to the previous slot

**Notes**:

- flashing boot only affects the current slot
- if the system receives an OTA update, it may overwrite the modification

### About GKI (Generic Kernel Image)

Android 12 uses the GKI architecture:

- `boot.img` contains the generic kernel
- `vendor_boot.img` contains device-specific kernel modules

In most cases Magisk only needs to patch `boot.img`.
Only in specific cases does `vendor_boot.img` need patching.

---

## ✅ Signs of Success

After a successful root you should be able to:

1. ✅ Run `adb shell su` to obtain a root shell
2. ✅ Magisk Manager shows installed
3. ✅ SELinux is still Enforcing
4. ✅ The system runs normally with no obvious anomalies
5. ✅ Apps that require root permission can be installed

---

## 🔗 Related Resources

- [Magisk official documentation](https://topjohnwu.github.io/Magisk/)
- [Android Verified Boot documentation](https://source.android.com/security/verifiedboot)
- [Fastboot command reference](https://source.android.com/setup/build/running)

---

**Last updated**: 2026-01-31
**Applicable device**: Rokid RG-glasses (RV101) / Android 12
