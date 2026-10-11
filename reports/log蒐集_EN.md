# Rokid Glasses ADB Command Collection Log

> 中文版：[log蒐集.md](log%E8%92%90%E9%9B%86.md)

**Collection Date:** January 30, 2026  
**Device Serial:** <DEVICE_SERIAL>  
**Purpose:** System data report & security vulnerability report

---

## 📱 System Data Report - Commands Used

### 1. Device Connection Confirmation

```bash
# confirm the ADB device connection
adb devices
```

### 2. Basic Device Information

```bash
# collect all system properties
adb shell getprop

# query specific properties
adb shell getprop ro.product.name
adb shell getprop ro.product.brand
adb shell getprop ro.product.manufacturer
adb shell getprop ro.product.model
adb shell getprop ro.product.odm.model
adb shell getprop ro.serialno
adb shell getprop ro.product.device
adb shell getprop ro.hardware
```

### 3. System Software Information

```bash
# Android version
adb shell getprop ro.build.version.release
adb shell getprop ro.build.version.sdk
adb shell getprop ro.build.id
adb shell getprop ro.build.display.id
adb shell getprop ro.build.type
adb shell getprop ro.build.tags
adb shell getprop ro.build.version.security_patch
adb shell getprop ro.build.date
adb shell getprop ro.build.fingerprint
adb shell getprop ro.boot.flash.locked
adb shell getprop ro.boot.verifiedbootstate
```

### 4. Kernel and Platform Information

```bash
# Linux kernel version
adb shell cat /proc/version
adb shell uname -a

# platform information
adb shell getprop ro.board.platform
adb shell getprop ro.soc.manufacturer
adb shell getprop ro.soc.model
adb shell getprop ro.product.board
adb shell getprop ro.board.api_level
adb shell getprop ro.vndk.version
```

### 5. CPU Information

```bash
# detailed CPU information
adb shell cat /proc/cpuinfo

# CPU architecture
adb shell getprop ro.product.cpu.abi
adb shell getprop ro.product.cpu.abilist
adb shell getprop ro.product.cpu.abilist32
adb shell getprop ro.product.cpu.abilist64
```

### 6. Memory Information

```bash
# detailed memory information
adb shell cat /proc/meminfo

# memory configuration
adb shell getprop ro.config.low_ram
```

### 7. Storage Information

```bash
# disk usage
adb shell df -h

# partition information
adb shell cat /proc/partitions

# mount information
adb shell mount

# partition characteristics
adb shell getprop ro.boot.slot_suffix
adb shell getprop ro.boot.dynamic_partitions
adb shell getprop ro.virtual_ab.enabled
adb shell getprop ro.crypto.state
adb shell getprop ro.boot.veritymode
```

### 8. Display Information

```bash
# display service information
adb shell dumpsys display

# display-related properties
adb shell getprop ro.sf.lcd_density
adb shell getprop ro.opengles.version

# GPU information
adb shell getprop ro.hardware.egl
adb shell getprop ro.hardware.vulkan
adb shell dumpsys SurfaceFlinger
```

### 9. Battery Information

```bash
# battery service information
adb shell dumpsys battery

# detailed battery state
adb shell cat /sys/class/power_supply/battery/status
adb shell cat /sys/class/power_supply/battery/health
adb shell cat /sys/class/power_supply/battery/capacity
adb shell cat /sys/class/power_supply/battery/voltage_now
adb shell cat /sys/class/power_supply/battery/temp
adb shell cat /sys/class/power_supply/battery/cycle_count
```

### 10. Bluetooth Information

```bash
# bluetooth manager service
adb shell dumpsys bluetooth_manager

# bluetooth-related properties
adb shell getprop bluetooth.device.class_of_device
adb shell getprop persist.bluetooth.a2dp_offload.disabled
adb shell getprop persist.bluetooth.bluetooth_audio_hal.disabled
```

### 11. Wi-Fi Information

```bash
# Wi-Fi service information
adb shell dumpsys wifi

# Wi-Fi configuration
adb shell getprop wifi.interface
```

### 12. USB Information

```bash
# USB configuration
adb shell getprop sys.usb.config
adb shell getprop sys.usb.controller
adb shell getprop sys.usb.configfs
adb shell getprop sys.usb.ffs.ready
```

### 13. Camera Information

```bash
# camera service information
adb shell dumpsys media.camera

# camera configuration
adb shell getprop vendor.camera.aux.packagelist
```

### 14. Sensor Information

```bash
# sensor service information
adb shell dumpsys sensorservice
```

### 15. Audio Information

```bash
# audio service information
adb shell dumpsys audio

# audio configuration properties
adb shell getprop | grep audio
adb shell getprop | grep sound
```

### 16. Installed Applications

```bash
# list all installed packages
adb shell pm list packages

# list system apps
adb shell pm list packages -s

# list third-party apps
adb shell pm list packages -3

# get information about a specific package
adb shell dumpsys package <package_name>
```

### 17. System Service Status

```bash
# list all running services
adb shell dumpsys activity services

# status of a specific service
adb shell service list
```

### 18. Rokid Special Features

```bash
# Rokid-related properties
adb shell getprop | grep rokid
adb shell getprop | grep glasses
adb shell getprop | grep rkd

# TTS settings
adb shell getprop persist.vendor.boot.tts.flag
adb shell getprop persist.vendor.rkd.local.tts.param
adb shell getprop persist.vendor.rkd.local.tts.speed
adb shell getprop persist.vendor.tts.init.flag

# glasses state
adb shell getprop vendor.rkd.glasses.is_spread
adb shell getprop vendor.rkd.glasses.is_take_on
```

---

## 🔒 Security Vulnerability Report - Commands Used

### 1. SELinux Status

```bash
# SELinux mode
adb shell getenforce

# SELinux-related properties
adb shell getprop ro.boot.selinux
```

### 2. Security Property Checks

```bash
# security-related properties
adb shell getprop ro.debuggable
adb shell getprop ro.secure
adb shell getprop ro.adb.secure
adb shell getprop ro.boot.flash.locked
adb shell getprop ro.boot.verifiedbootstate
adb shell getprop ro.boot.vbmeta.device_state
adb shell getprop ro.crypto.state

# filter security-related properties
adb shell getprop | grep -E "secure|debug|verify|crypto"
```

### 3. Network Port Scan

```bash
# listening network ports
adb shell netstat -tlnp
adb shell ss -tlnp

# all network connections
adb shell netstat -an
adb shell ss -an
```

### 4. Debuggable App Check

```bash
# check debuggable apps
adb shell pm list packages -f | while read pkg; do
  adb shell dumpsys package $(echo $pkg | cut -d= -f2) | grep -E "flags|pkgFlags"
done

# check the flags of a specific app
adb shell dumpsys package <package_name> | grep flags
```

### 5. Dangerous Permission Audit

```bash
# get all permissions of an app
adb shell dumpsys package <package_name> | grep permission

# check specific dangerous permissions
adb shell dumpsys package <package_name> | grep -E "CAMERA|LOCATION|MICROPHONE|STORAGE|CONTACTS|SMS|CALL"

# list all granted permissions
adb shell dumpsys package <package_name> | grep "granted=true"
```

### 6. Kernel Security Parameters

```bash
# dmesg restriction
adb shell cat /proc/sys/kernel/dmesg_restrict

# kernel pointer restriction
adb shell cat /proc/sys/kernel/kptr_restrict

# performance event restriction
adb shell cat /proc/sys/kernel/perf_event_paranoid

# ASLR state
adb shell cat /proc/sys/kernel/randomize_va_space
```

### 7. Security Log Analysis

```bash
# read the kernel log (dmesg)
adb shell dmesg | tail -100

# read security-related messages from logcat
adb shell logcat -d | grep -iE "security|selinux|permission|denied"

# SELinux denial log
adb shell logcat -d | grep "avc: denied"
```

### 8. Root Access Check

```bash
# check for the su binary
adb shell which su
adb shell ls -la /system/bin/su
adb shell ls -la /system/xbin/su

# check for Magisk
adb shell ls -la /data/adb/magisk
adb shell pm list packages | grep magisk

# check root-related properties
adb shell getprop ro.build.selinux
```

### 9. Unknown-Source Installation Setting

```bash
# check the unknown-source settings
adb shell settings get secure install_non_market_apps
adb shell settings get secure unknown_sources_default_reversed
```

### 10. ADB Settings

```bash
# ADB-related settings
adb shell settings get global adb_enabled
adb shell getprop persist.sys.usb.config
adb shell getprop persist.vendor.adb
adb shell getprop sys.usb.config
```

### 11. Screen Lock Settings

```bash
# screen lock state
adb shell settings get secure lockscreen.disabled
adb shell settings get system screen_off_timeout
```

### 12. Development File Check

```bash
# check the temporary directory
adb shell ls -la /data/local/tmp/

# check for leftovers from development tools
adb shell ls -la /data/local/tmp/.studio/
```

### 13. Location Mocking Settings

```bash
# location mocking settings
adb shell settings get secure mock_location
adb shell settings get secure location_changer
```

### 14. Network Security Configuration

```bash
# check NetworkSecurityConfig
adb shell logcat -d | grep "NetworkSecurityConfig"
```

### 15. SELinux Label Check

```bash
# check for SELinux label errors
adb shell logcat -d | grep "incorrect label"
adb shell dmesg | grep "avc:"
```

### 16. Battery Optimization Allowlist

```bash
# battery optimization exemption list
adb shell dumpsys deviceidle whitelist
```

### 17. KeyMint/Keymaster Status

```bash
# hardware security module status
adb shell getprop ro.hardware.keystore
adb shell getprop ro.hardware.gatekeeper
adb shell service list | grep -E "keymaster|keymint"
```

---

## 📊 Combined Query Commands

### Batch Property Queries

```bash
# export all properties
adb shell getprop > device_properties.txt

# filter by specific keywords
adb shell getprop | grep -E "secure|debug|verify|crypto|selinux"
adb shell getprop | grep -E "rokid|glasses|rkd"
adb shell getprop | grep -E "bluetooth|wifi|usb"
```

### System Service Export

```bash
# full dumpsys export (warning: the file is large)
adb shell dumpsys > full_dumpsys.txt

# export a specific service
adb shell dumpsys battery > battery_info.txt
adb shell dumpsys display > display_info.txt
adb shell dumpsys bluetooth_manager > bluetooth_info.txt
adb shell dumpsys sensorservice > sensor_info.txt
adb shell dumpsys media.camera > camera_info.txt
```

### Filesystem Checks

```bash
# system partition structure
adb shell ls -la /system/
adb shell ls -la /vendor/
adb shell ls -la /product/
adb shell ls -la /data/

# permission-sensitive directories
adb shell ls -la /data/local/tmp/
adb shell ls -la /data/data/
```

---

## 🔧 Practical Script Examples

### Full System Information Collection Script

```bash
#!/bin/bash
# create the output directory
mkdir -p rokid_analysis

# basic information
adb shell getprop > rokid_analysis/properties.txt
adb shell cat /proc/cpuinfo > rokid_analysis/cpuinfo.txt
adb shell cat /proc/meminfo > rokid_analysis/meminfo.txt
adb shell df -h > rokid_analysis/storage.txt

# service information
adb shell dumpsys battery > rokid_analysis/battery.txt
adb shell dumpsys display > rokid_analysis/display.txt
adb shell dumpsys bluetooth_manager > rokid_analysis/bluetooth.txt
adb shell dumpsys sensorservice > rokid_analysis/sensors.txt
adb shell dumpsys media.camera > rokid_analysis/camera.txt

# applications
adb shell pm list packages > rokid_analysis/packages.txt

echo "蒐集完成！"
```

### Security Scan Script

```bash
#!/bin/bash
# create the output directory
mkdir -p rokid_security

# SELinux state
adb shell getenforce > rokid_security/selinux.txt

# security properties
adb shell getprop | grep -E "secure|debug|verify|crypto" > rokid_security/security_props.txt

# network ports
adb shell ss -tlnp > rokid_security/open_ports.txt

# kernel security parameters
echo "dmesg_restrict: $(adb shell cat /proc/sys/kernel/dmesg_restrict)" > rokid_security/kernel_security.txt
echo "kptr_restrict: $(adb shell cat /proc/sys/kernel/kptr_restrict)" >> rokid_security/kernel_security.txt
echo "randomize_va_space: $(adb shell cat /proc/sys/kernel/randomize_va_space)" >> rokid_security/kernel_security.txt
echo "perf_event_paranoid: $(adb shell cat /proc/sys/kernel/perf_event_paranoid)" >> rokid_security/kernel_security.txt

# SELinux logs
adb shell logcat -d | grep "avc:" > rokid_security/selinux_denials.txt

echo "安全掃描完成！"
```

---

## 📝 Notes

1. **ADB authorization**: the first connection requires confirming ADB authorization on the device
2. **USB debugging**: make sure USB debugging mode is enabled on the device
3. **Permission limits**: some commands may require root permission to execute
4. **Data sensitivity**: the collected data may contain sensitive information; keep it safe
5. **Device compatibility**: some commands may differ depending on the Android version or device

---

_Command collection complete_
