#!/system/bin/sh
# Boot Partition Extraction Attempt
# Uses various methods to try reading boot partition

BOOT_PART="/dev/block/mmcblk0p17"
OUTPUT="/data/local/tmp/boot_a.img"

echo "[*] Attempting to extract boot partition..."
echo "[*] Target: $BOOT_PART"
echo "[*] Output: $OUTPUT"

# Method 1: Direct dd
echo "[1] Direct dd..."
dd if=$BOOT_PART of=$OUTPUT bs=4096 count=24576 2>&1

if [ -s "$OUTPUT" ]; then
    echo "[+] Success! Size: $(ls -l $OUTPUT | awk '{print $5}') bytes"
    xxd $OUTPUT | head -5
    exit 0
fi

# Method 2: Through /dev/block/by-name symlink
echo "[2] Via by-name symlink..."
dd if=/dev/block/by-name/boot_a of=$OUTPUT bs=4096 count=24576 2>&1

if [ -s "$OUTPUT" ]; then
    echo "[+] Success! Size: $(ls -l $OUTPUT | awk '{print $5}') bytes"
    exit 0
fi

# Method 3: Through bootdevice
echo "[3] Via bootdevice..."
dd if=/dev/block/bootdevice/by-name/boot_a of=$OUTPUT bs=4096 count=24576 2>&1

if [ -s "$OUTPUT" ]; then
    echo "[+] Success!"
    exit 0
fi

# Method 4: Run as system app context
echo "[4] Checking current context..."
id
cat /proc/self/attr/current 2>/dev/null

echo "[-] All methods failed. Need root access to read boot partition."
echo "[*] Consider using fastboot to boot a custom recovery image."
