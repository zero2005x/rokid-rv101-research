
import zipfile
import os
import io

# Define the update-binary script content
# Note: Using binary mode for writing to ensure LF line endings if we were writing to file first,
# but zipfile.writestr handles bytes.
update_binary_content = b"""#!/sbin/sh
# Rokid Boot Extractor

FD=$2
ZIP=$3

ui_print() {
  echo "ui_print $1" > /proc/self/fd/$FD
  echo "ui_print" > /proc/self/fd/$FD
}

ui_print "*********************************"
ui_print "   Rokid Boot Extractor v1.0    "
ui_print "*********************************"

ui_print "Mounting partitions..."
# Try mounting both legacy paths and new paths
mount /data
mount /sdcard

# Check where we can write
DEST="/sdcard/boot_dump.img"
if [ ! -d "/sdcard" ]; then
    if [ -d "/data/media/0" ]; then
        DEST="/data/media/0/boot_dump.img"
    elif [ -d "/tmp" ]; then
        DEST="/tmp/boot_dump.img"
        ui_print "WARNING: Saving to /tmp (volatile)!"
    fi
fi

ui_print "Target: $DEST"

# Check for boot device
BOOT_DEV="/dev/block/by-name/boot_a"
if [ ! -e "$BOOT_DEV" ]; then
    BOOT_DEV="/dev/block/bootdevice/by-name/boot_a"
fi

if [ ! -e "$BOOT_DEV" ]; then
   ui_print "Error: Could not find boot_a partition!"
   exit 1
fi

ui_print "Dumping boot_a from $BOOT_DEV..."

if dd if=$BOOT_DEV of=$DEST; then
  ui_print "Success! Boot image dumped."
  chmod 644 $DEST
else
  ui_print "Failed to dump boot image!"
  exit 1
fi

sync
ui_print "Done."
exit 0
"""

updater_script_content = b"# dummy"

zip_filename = "extract_boot.zip"

with zipfile.ZipFile(zip_filename, 'w') as zf:
    # Add update-binary
    zf.writestr('META-INF/com/google/android/update-binary', update_binary_content)
    # Add updater-script
    zf.writestr('META-INF/com/google/android/updater-script', updater_script_content)

print(f"Created {zip_filename}")
