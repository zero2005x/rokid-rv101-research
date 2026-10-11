#!/usr/bin/env python3
"""
Create a flashable ZIP for stock Android Recovery
===================================================
This creates an update.zip that flashes Magisk-patched boot.img
to boot_a partition via stock recovery's "Apply update from SD card".

The ZIP contains:
  - META-INF/com/google/android/update-binary  (shell script installer)
  - META-INF/com/google/android/updater-script (dummy)
  - boot.img (the Magisk-patched boot image)
"""

import zipfile
import os
import sys
import shutil

# Configuration
PATCHED_BOOT = "magisk_patched-30600_9C56p.img"
OUTPUT_ZIP = "rokid_boot_flash.zip"

# The update-binary shell script
# Stock recovery on unlocked-bootloader devices often supports this format
UPDATE_BINARY = r"""#!/sbin/sh
#
# Rokid Boot Flasher - update-binary
# Flashes Magisk-patched boot.img to boot_a partition
#

OUTFD=/proc/self/fd/$2
ZIPFILE="$3"

ui_print() {
  echo "ui_print $1" > $OUTFD
  echo "ui_print" > $OUTFD
}

ui_print "========================================"
ui_print "  Rokid Magisk Boot Flasher"
ui_print "========================================"
ui_print ""

# Create temp directory
TMPDIR=/tmp/rokid_flash
rm -rf $TMPDIR
mkdir -p $TMPDIR

# Extract boot.img from zip
ui_print "Extracting boot.img from zip..."
unzip -o "$ZIPFILE" boot.img -d $TMPDIR
if [ ! -f "$TMPDIR/boot.img" ]; then
    ui_print "ERROR: Failed to extract boot.img!"
    exit 1
fi

BOOT_SIZE=$(stat -c%s "$TMPDIR/boot.img" 2>/dev/null || ls -l "$TMPDIR/boot.img" | awk '{print $5}')
ui_print "Boot image size: $BOOT_SIZE bytes"

# Find boot_a partition
BOOT_PART=""
if [ -e /dev/block/by-name/boot_a ]; then
    BOOT_PART="/dev/block/by-name/boot_a"
elif [ -e /dev/block/platform/soc/4804000.ufshc/by-name/boot_a ]; then
    BOOT_PART="/dev/block/platform/soc/4804000.ufshc/by-name/boot_a"
elif [ -e /dev/block/bootdevice/by-name/boot_a ]; then
    BOOT_PART="/dev/block/bootdevice/by-name/boot_a"
else
    # Fallback: mmcblk0p17 (known from device probe)
    BOOT_PART="/dev/block/mmcblk0p17"
fi

ui_print "Target partition: $BOOT_PART"

if [ ! -e "$BOOT_PART" ]; then
    ui_print "ERROR: Boot partition not found!"
    exit 1
fi

# Flash boot.img
ui_print ""
ui_print "Flashing boot.img to $BOOT_PART..."
dd if="$TMPDIR/boot.img" of="$BOOT_PART" bs=4096
RESULT=$?

if [ $RESULT -eq 0 ]; then
    ui_print ""
    ui_print "SUCCESS! Boot image flashed!"
    ui_print ""
    ui_print "Verifying..."
    # Quick verify by reading back first 16 bytes
    dd if="$BOOT_PART" of="$TMPDIR/verify.bin" bs=16 count=1 2>/dev/null
    dd if="$TMPDIR/boot.img" of="$TMPDIR/source.bin" bs=16 count=1 2>/dev/null
    if cmp -s "$TMPDIR/verify.bin" "$TMPDIR/source.bin"; then
        ui_print "Verification PASSED!"
    else
        ui_print "WARNING: Verification mismatch (may be OK)"
    fi
else
    ui_print "ERROR: dd failed with code $RESULT"
    exit 1
fi

# Cleanup
rm -rf $TMPDIR

ui_print ""
ui_print "========================================"
ui_print "  Flash complete! Reboot to enjoy root!"
ui_print "========================================"
ui_print ""

exit 0
"""

# Dummy updater-script (required by recovery but content doesn't matter for shell-based update-binary)
UPDATER_SCRIPT = ""

def create_flash_zip():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    patched_boot_path = os.path.join(script_dir, PATCHED_BOOT)
    output_path = os.path.join(script_dir, OUTPUT_ZIP)

    if not os.path.exists(patched_boot_path):
        print(f"[-] Error: {PATCHED_BOOT} not found!")
        print(f"    Expected at: {patched_boot_path}")
        sys.exit(1)

    boot_size = os.path.getsize(patched_boot_path)
    print(f"[*] Patched boot image: {PATCHED_BOOT} ({boot_size / 1024 / 1024:.1f} MB)")

    print(f"[*] Creating flashable ZIP: {OUTPUT_ZIP}")

    with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_STORED) as zf:
        # Add update-binary (shell script)
        zf.writestr("META-INF/com/google/android/update-binary", UPDATE_BINARY)
        print("    + META-INF/com/google/android/update-binary")

        # Add updater-script (dummy)
        zf.writestr("META-INF/com/google/android/updater-script", UPDATER_SCRIPT)
        print("    + META-INF/com/google/android/updater-script")

        # Add the patched boot.img (stored, no compression for block device write)
        print(f"    + boot.img ({boot_size / 1024 / 1024:.1f} MB) ...")
        zf.write(patched_boot_path, "boot.img")

    output_size = os.path.getsize(output_path)
    print(f"\n[+] Created: {OUTPUT_ZIP} ({output_size / 1024 / 1024:.1f} MB)")
    print(f"\n[*] Next steps:")
    print(f"    1. Reboot glasses to normal Android")
    print(f"    2. adb push {OUTPUT_ZIP} /sdcard/")
    print(f"    3. adb reboot recovery")
    print(f"    4. Select 'Apply update from SD card'")
    print(f"    5. Navigate to {OUTPUT_ZIP}")

if __name__ == "__main__":
    create_flash_zip()
