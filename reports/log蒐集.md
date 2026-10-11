# Rokid Glasses ADB 命令蒐集記錄

**蒐集日期：** 2026年1月30日  
**設備序號：** <DEVICE_SERIAL>  
**用途：** 系統資料報告 & 資安漏洞報告

---

## 📱 系統資料報告 - 使用的命令

### 1. 設備連接確認

```bash
# 確認 ADB 設備連接
adb devices
```

### 2. 基本設備資訊

```bash
# 取得所有系統屬性
adb shell getprop

# 特定屬性查詢
adb shell getprop ro.product.name
adb shell getprop ro.product.brand
adb shell getprop ro.product.manufacturer
adb shell getprop ro.product.model
adb shell getprop ro.product.odm.model
adb shell getprop ro.serialno
adb shell getprop ro.product.device
adb shell getprop ro.hardware
```

### 3. 系統軟體資訊

```bash
# Android 版本
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

### 4. 核心與平台資訊

```bash
# Linux 核心版本
adb shell cat /proc/version
adb shell uname -a

# 平台資訊
adb shell getprop ro.board.platform
adb shell getprop ro.soc.manufacturer
adb shell getprop ro.soc.model
adb shell getprop ro.product.board
adb shell getprop ro.board.api_level
adb shell getprop ro.vndk.version
```

### 5. CPU 資訊

```bash
# CPU 詳細資訊
adb shell cat /proc/cpuinfo

# CPU 架構
adb shell getprop ro.product.cpu.abi
adb shell getprop ro.product.cpu.abilist
adb shell getprop ro.product.cpu.abilist32
adb shell getprop ro.product.cpu.abilist64
```

### 6. 記憶體資訊

```bash
# 記憶體詳細資訊
adb shell cat /proc/meminfo

# 記憶體配置
adb shell getprop ro.config.low_ram
```

### 7. 儲存空間資訊

```bash
# 磁碟使用情況
adb shell df -h

# 分區資訊
adb shell cat /proc/partitions

# 掛載資訊
adb shell mount

# 分區特性
adb shell getprop ro.boot.slot_suffix
adb shell getprop ro.boot.dynamic_partitions
adb shell getprop ro.virtual_ab.enabled
adb shell getprop ro.crypto.state
adb shell getprop ro.boot.veritymode
```

### 8. 顯示器資訊

```bash
# 顯示服務資訊
adb shell dumpsys display

# 顯示相關屬性
adb shell getprop ro.sf.lcd_density
adb shell getprop ro.opengles.version

# GPU 資訊
adb shell getprop ro.hardware.egl
adb shell getprop ro.hardware.vulkan
adb shell dumpsys SurfaceFlinger
```

### 9. 電池資訊

```bash
# 電池服務資訊
adb shell dumpsys battery

# 電池詳細狀態
adb shell cat /sys/class/power_supply/battery/status
adb shell cat /sys/class/power_supply/battery/health
adb shell cat /sys/class/power_supply/battery/capacity
adb shell cat /sys/class/power_supply/battery/voltage_now
adb shell cat /sys/class/power_supply/battery/temp
adb shell cat /sys/class/power_supply/battery/cycle_count
```

### 10. 藍牙資訊

```bash
# 藍牙管理器服務
adb shell dumpsys bluetooth_manager

# 藍牙相關屬性
adb shell getprop bluetooth.device.class_of_device
adb shell getprop persist.bluetooth.a2dp_offload.disabled
adb shell getprop persist.bluetooth.bluetooth_audio_hal.disabled
```

### 11. Wi-Fi 資訊

```bash
# Wi-Fi 服務資訊
adb shell dumpsys wifi

# Wi-Fi 配置
adb shell getprop wifi.interface
```

### 12. USB 資訊

```bash
# USB 配置
adb shell getprop sys.usb.config
adb shell getprop sys.usb.controller
adb shell getprop sys.usb.configfs
adb shell getprop sys.usb.ffs.ready
```

### 13. 相機資訊

```bash
# 相機服務資訊
adb shell dumpsys media.camera

# 相機配置
adb shell getprop vendor.camera.aux.packagelist
```

### 14. 感測器資訊

```bash
# 感測器服務資訊
adb shell dumpsys sensorservice
```

### 15. 音訊資訊

```bash
# 音訊服務資訊
adb shell dumpsys audio

# 音訊配置屬性
adb shell getprop | grep audio
adb shell getprop | grep sound
```

### 16. 安裝的應用程式

```bash
# 列出所有已安裝的套件
adb shell pm list packages

# 列出系統應用
adb shell pm list packages -s

# 列出第三方應用
adb shell pm list packages -3

# 取得特定套件資訊
adb shell dumpsys package <package_name>
```

### 17. 系統服務狀態

```bash
# 列出所有運行的服務
adb shell dumpsys activity services

# 特定服務狀態
adb shell service list
```

### 18. Rokid 特殊功能

```bash
# Rokid 相關屬性
adb shell getprop | grep rokid
adb shell getprop | grep glasses
adb shell getprop | grep rkd

# TTS 設定
adb shell getprop persist.vendor.boot.tts.flag
adb shell getprop persist.vendor.rkd.local.tts.param
adb shell getprop persist.vendor.rkd.local.tts.speed
adb shell getprop persist.vendor.tts.init.flag

# 眼鏡狀態
adb shell getprop vendor.rkd.glasses.is_spread
adb shell getprop vendor.rkd.glasses.is_take_on
```

---

## 🔒 資安漏洞報告 - 使用的命令

### 1. SELinux 狀態

```bash
# SELinux 模式
adb shell getenforce

# SELinux 相關屬性
adb shell getprop ro.boot.selinux
```

### 2. 安全屬性檢查

```bash
# 安全相關屬性
adb shell getprop ro.debuggable
adb shell getprop ro.secure
adb shell getprop ro.adb.secure
adb shell getprop ro.boot.flash.locked
adb shell getprop ro.boot.verifiedbootstate
adb shell getprop ro.boot.vbmeta.device_state
adb shell getprop ro.crypto.state

# 過濾安全相關屬性
adb shell getprop | grep -E "secure|debug|verify|crypto"
```

### 3. 網路端口掃描

```bash
# 監聽的網路端口
adb shell netstat -tlnp
adb shell ss -tlnp

# 所有網路連接
adb shell netstat -an
adb shell ss -an
```

### 4. 可調試應用檢查

```bash
# 檢查 debuggable 應用
adb shell pm list packages -f | while read pkg; do
  adb shell dumpsys package $(echo $pkg | cut -d= -f2) | grep -E "flags|pkgFlags"
done

# 檢查特定應用的 flags
adb shell dumpsys package <package_name> | grep flags
```

### 5. 危險權限審計

```bash
# 取得應用的所有權限
adb shell dumpsys package <package_name> | grep permission

# 檢查特定危險權限
adb shell dumpsys package <package_name> | grep -E "CAMERA|LOCATION|MICROPHONE|STORAGE|CONTACTS|SMS|CALL"

# 列出所有已授予的權限
adb shell dumpsys package <package_name> | grep "granted=true"
```

### 6. 核心安全參數

```bash
# dmesg 限制
adb shell cat /proc/sys/kernel/dmesg_restrict

# 核心指針限制
adb shell cat /proc/sys/kernel/kptr_restrict

# 效能事件限制
adb shell cat /proc/sys/kernel/perf_event_paranoid

# ASLR 狀態
adb shell cat /proc/sys/kernel/randomize_va_space
```

### 7. 安全日誌分析

```bash
# 讀取核心日誌 (dmesg)
adb shell dmesg | tail -100

# 讀取 logcat 中的安全相關訊息
adb shell logcat -d | grep -iE "security|selinux|permission|denied"

# SELinux 拒絕日誌
adb shell logcat -d | grep "avc: denied"
```

### 8. Root 存取檢查

```bash
# 檢查 su 二進制
adb shell which su
adb shell ls -la /system/bin/su
adb shell ls -la /system/xbin/su

# 檢查 Magisk
adb shell ls -la /data/adb/magisk
adb shell pm list packages | grep magisk

# 檢查 root 相關屬性
adb shell getprop ro.build.selinux
```

### 9. 未知來源安裝設定

```bash
# 檢查未知來源設定
adb shell settings get secure install_non_market_apps
adb shell settings get secure unknown_sources_default_reversed
```

### 10. ADB 設定

```bash
# ADB 相關設定
adb shell settings get global adb_enabled
adb shell getprop persist.sys.usb.config
adb shell getprop persist.vendor.adb
adb shell getprop sys.usb.config
```

### 11. 螢幕鎖定設定

```bash
# 螢幕鎖定狀態
adb shell settings get secure lockscreen.disabled
adb shell settings get system screen_off_timeout
```

### 12. 開發檔案檢查

```bash
# 檢查臨時目錄
adb shell ls -la /data/local/tmp/

# 檢查開發工具殘留
adb shell ls -la /data/local/tmp/.studio/
```

### 13. 位置模擬設定

```bash
# 位置模擬設定
adb shell settings get secure mock_location
adb shell settings get secure location_changer
```

### 14. 網路安全配置

```bash
# 檢查 NetworkSecurityConfig
adb shell logcat -d | grep "NetworkSecurityConfig"
```

### 15. SELinux 標籤檢查

```bash
# 檢查 SELinux 標籤錯誤
adb shell logcat -d | grep "incorrect label"
adb shell dmesg | grep "avc:"
```

### 16. 電池優化白名單

```bash
# 電池優化豁免清單
adb shell dumpsys deviceidle whitelist
```

### 17. KeyMint/Keymaster 狀態

```bash
# 硬體安全模組狀態
adb shell getprop ro.hardware.keystore
adb shell getprop ro.hardware.gatekeeper
adb shell service list | grep -E "keymaster|keymint"
```

---

## 📊 綜合查詢命令

### 批量屬性查詢

```bash
# 所有屬性匯出
adb shell getprop > device_properties.txt

# 特定關鍵字過濾
adb shell getprop | grep -E "secure|debug|verify|crypto|selinux"
adb shell getprop | grep -E "rokid|glasses|rkd"
adb shell getprop | grep -E "bluetooth|wifi|usb"
```

### 系統服務匯出

```bash
# 完整 dumpsys 匯出 (警告：檔案很大)
adb shell dumpsys > full_dumpsys.txt

# 特定服務匯出
adb shell dumpsys battery > battery_info.txt
adb shell dumpsys display > display_info.txt
adb shell dumpsys bluetooth_manager > bluetooth_info.txt
adb shell dumpsys sensorservice > sensor_info.txt
adb shell dumpsys media.camera > camera_info.txt
```

### 檔案系統檢查

```bash
# 系統分區結構
adb shell ls -la /system/
adb shell ls -la /vendor/
adb shell ls -la /product/
adb shell ls -la /data/

# 權限敏感目錄
adb shell ls -la /data/local/tmp/
adb shell ls -la /data/data/
```

---

## 🔧 實用腳本範例

### 完整系統資訊蒐集腳本

```bash
#!/bin/bash
# 建立輸出目錄
mkdir -p rokid_analysis

# 基本資訊
adb shell getprop > rokid_analysis/properties.txt
adb shell cat /proc/cpuinfo > rokid_analysis/cpuinfo.txt
adb shell cat /proc/meminfo > rokid_analysis/meminfo.txt
adb shell df -h > rokid_analysis/storage.txt

# 服務資訊
adb shell dumpsys battery > rokid_analysis/battery.txt
adb shell dumpsys display > rokid_analysis/display.txt
adb shell dumpsys bluetooth_manager > rokid_analysis/bluetooth.txt
adb shell dumpsys sensorservice > rokid_analysis/sensors.txt
adb shell dumpsys media.camera > rokid_analysis/camera.txt

# 應用程式
adb shell pm list packages > rokid_analysis/packages.txt

echo "蒐集完成！"
```

### 安全掃描腳本

```bash
#!/bin/bash
# 建立輸出目錄
mkdir -p rokid_security

# SELinux 狀態
adb shell getenforce > rokid_security/selinux.txt

# 安全屬性
adb shell getprop | grep -E "secure|debug|verify|crypto" > rokid_security/security_props.txt

# 網路端口
adb shell ss -tlnp > rokid_security/open_ports.txt

# 核心安全參數
echo "dmesg_restrict: $(adb shell cat /proc/sys/kernel/dmesg_restrict)" > rokid_security/kernel_security.txt
echo "kptr_restrict: $(adb shell cat /proc/sys/kernel/kptr_restrict)" >> rokid_security/kernel_security.txt
echo "randomize_va_space: $(adb shell cat /proc/sys/kernel/randomize_va_space)" >> rokid_security/kernel_security.txt
echo "perf_event_paranoid: $(adb shell cat /proc/sys/kernel/perf_event_paranoid)" >> rokid_security/kernel_security.txt

# SELinux 日誌
adb shell logcat -d | grep "avc:" > rokid_security/selinux_denials.txt

echo "安全掃描完成！"
```

---

## 📝 注意事項

1. **ADB 授權**：首次連接需在設備上確認 ADB 授權
2. **USB 調試**：確保設備已啟用 USB 調試模式
3. **權限限制**：部分命令可能需要 root 權限才能執行
4. **資料敏感性**：蒐集的資料可能包含敏感資訊，請妥善保管
5. **設備相容性**：某些命令可能因 Android 版本或設備差異而有所不同

---

_命令蒐集完畢_
