> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets and product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (a single authoritative fact table; that table decides any conflict).

---
marp: true
theme: default
paginate: true
size: 16:9
style: |
  section {
    font-size: 22px;
  }
  h1 {
    font-size: 36px;
    color: #2563eb;
  }
  h2 {
    font-size: 28px;
    color: #1e40af;
  }
  h3 {
    font-size: 24px;
  }
  table {
    font-size: 18px;
  }
  code {
    font-size: 16px;
  }
---

# 📱 Rokid Glasses Device System Data Report

> 中文版：[Rokid_Glasses_系統資料報告_Marp.md](Rokid_Glasses_%E7%B3%BB%E7%B5%B1%E8%B3%87%E6%96%99%E5%A0%B1%E5%91%8A_Marp.md)

**Collection Date:** January 30, 2026  
**Device Serial:** <DEVICE_SERIAL>

![bg right:30%](https://via.placeholder.com/300x400/2563eb/ffffff?text=Rokid+Glasses)

---

## 📱 Basic Device Information

| Item              | Information      |
| ----------------- | ---------------- |
| **Product Name**  | RG-glasses       |
| **Brand / Manufacturer** | Rokid     |
| **Model**         | RG-glasses       |
| **OEM Model**     | RV101            |
| **Device Type**   | glasses          |
| **Hardware Version** | dvt3          |
| **Serial Number** | <DEVICE_SERIAL> |

---

## 🖥️ System Software Information

| Item                | Information                 |
| ------------------- | --------------------------- |
| **Android Version** | 12 (API 32)                 |
| **Build ID**        | SKQ1.240613.001             |
| **Build Number**    | 1.12.009-20260109-150201    |
| **Build Type**      | user / release-keys         |
| **Security Patch**  | 2024-07-05                  |
| **Build Date**      | Fri Jan 9 11:38:41 CST 2026 |
| **Bootloader State** | unlocked (orange)          |

---

## 🔧 Kernel and Platform Information

| Item                | Information                  |
| ------------------- | ---------------------------- |
| **Linux Kernel Version** | 5.10.209-perf           |
| **Kernel Build Time** | Wed Dec 31 06:35:01 UTC 2025 |
| **Architecture**    | aarch64 (ARM64)              |
| **Platform**        | neo                          |
| **SoC Manufacturer** | QTI (Qualcomm)              |
| **SoC ID**          | 579                          |
| **Board API Level** | 31                           |
| **VNDK Version**    | 32                           |

---

## 🧠 Processor (CPU) Information

| Item           | Information                     |
| -------------- | ------------------------------- |
| **CPU Architecture** | ARMv8 (64-bit)            |
| **CPU Core Count** | 4 cores                      |
| **CPU Model**  | Cortex-A55 (0xd05)              |
| **BogoMIPS**   | 38.40 (per core)                |
| **Primary ABI** | arm64-v8a                      |
| **Supported ABIs** | arm64-v8a, armeabi-v7a, armeabi |

### CPU Features

fp, asimd, aes, pmull, sha1, sha2, crc32, atomics, lrcpc, asimddp

---

## 💾 Memory Information

| Item             | Information             |
| ---------------- | ----------------------- |
| **Total Memory** | 1,816,600 KB (~1.73 GB) |
| **Available Memory** | 862,732 KB (~843 MB) |
| **Free Memory**  | 201,000 KB (~196 MB)    |
| **Total Swap**   | 1,572,860 KB (~1.5 GB)  |
| **Available Swap** | 1,371,388 KB (~1.3 GB) |
| **Cache**        | 590,736 KB              |
| **Low-RAM Configuration** | yes (Low RAM device) |

---

## 💽 Storage Information

| Partition       | Size   | Used   | Available | Usage |
| --------------- | ------ | ------ | ---- | ------ |
| **System (/)**  | 790M   | 788M   | 0    | 100%   |
| **System Ext**  | 291M   | 290M   | 0    | 100%   |
| **Product Partition** | 567M | 565M | 0    | 100%   |
| **Vendor Partition** | 571M | 569M | 0    | 100%   |
| **Data Partition** | 19G  | 2.9G   | 16G  | 17%    |
| **eMMC Total Capacity** | ~30 GB | - | -   | -      |

---

## 💽 Partition Characteristics

- ✅ **A/B update system:** yes
- ✅ **Dynamic partitions:** yes
- ✅ **Virtual A/B:** yes
- 🔐 **Encryption type:** file-based encryption (FBE)
- ✅ **Verified boot:** orange (bootloader unlocked / not enforced) (**originally written enforcing, contradicting L62/L323 of this same file; corrected 2026-09-13**)

### Main Partitions

- boot_a/boot_b - boot images
- vbmeta_a/vbmeta_b - verified boot metadata
- super - dynamic partition
- userdata - user data (~19.7 GB)

---

## 📺 Display Information

| Item               | Information         |
| ------------------ | ------------------- |
| **Physical Resolution** | 480 x 640 pixels |
| **Screen Density** | 240 DPI             |
| **Refresh Rate**   | 60 Hz               |
| **Display Orientation** | Portrait       |
| **HDR / Wide Color Gamut** | no       |
| **Background Blur** | supported          |
| **GPU Composition** | forced             |
| **OpenGL ES Version** | 3.2             |
| **GPU**            | Adreno (EGL/Vulkan) |

---

## 🔋 Battery Information

| Item              | Information     |
| ----------------- | --------------- |
| **Battery Type**  | Li-ion          |
| **Battery Health** | Good           |
| **Charge Level**  | 100%            |
| **Voltage**       | 4,507 mV        |
| **Temperature**   | 26.5°C          |
| **Charge Cycles** | 36              |
| **USB Power**     | yes             |
| **AC / Wireless Charging** | no     |

---

## 📡 Bluetooth Connection

| Item             | Information              |
| ---------------- | ------------------------ |
| **State**        | ON                       |
| **Bluetooth Name** | <DEVICE_ALIAS>         |
| **MAC Address**  | AC:86:D1:55:D6:F9        |
| **Connection State** | STATE_CONNECTED      |
| **Connected Device** | Redmi Note 11 Pro+ 5G |
| **A2DP Sink**    | supported                |
| **HFP Client**   | supported                |
| **A2DP Offload** | sbc-aac-aptx-aptxhd-ldac |
| **MFI Support**  | yes (iOS device compatible) |

---

## 📡 Wi-Fi / USB Connection

### Wi-Fi

| Item         | Information |
| ------------ | ------ |
| **Interface** | wlan0 |
| **Country Code** | CN  |
| **State**    | not connected |

### USB

| Item         | Information  |
| ------------ | ------------ |
| **Controller** | a600000.dwc3 |
| **Current Configuration** | adb |
| **MTP Support** | yes       |

---

## 📷 Camera Information

| Item           | Information        |
| -------------- | ------------------ |
| **Camera Count** | 1                |
| **Facing**     | rear (Back)        |
| **Orientation Angle** | 270°        |
| **Flash**      | none               |
| **HAL Version** | 3.7               |
| **Autofocus**  | not supported (fixed focus) |
| **Supported Frame Rates** | 15, 24, 30, 60 fps |
| **Digital Zoom** | 1.0x ~ 8.0x      |
| **Video Stabilization** | supported |

---

## 📲 Sensor Information

| Sensor           | Vendor                    | Maximum Update Rate |
| ---------------- | ------------------------- | ---------- |
| **Accelerometer** | TDK-Invensense (ICM4x6xx) | 500 Hz    |
| **Gyroscope**    | TDK-Invensense (ICM4x6xx) | 500 Hz     |
| **Proximity Sensor** | Sensortek (UCS146E0)  | On-change  |
| **Gravity Sensor** | QTI                     | 200 Hz     |
| **Linear Acceleration** | QTI                | 200 Hz     |
| **Game Rotation Vector** | QTI              | 200 Hz     |

**Total Sensor Count:** 16 hardware sensors

---

## 🔊 Audio Features

### Audio Characteristics

| Feature                 | Status  |
| ----------------------- | ------- |
| **Fluence Noise Reduction** | ✅ supported |
| **A2DP Offload / Sink** | ✅ supported |
| **HFP (hands-free)**    | ✅ supported |
| **LDAC / AAC VBR**      | ✅ supported |
| **USB Audio Offload**   | ✅ supported |
| **Concurrent Capture**  | ✅ supported |
| **HDMI Pass-through**   | ✅ supported |

---

## 📦 Rokid-Exclusive Apps

| App                      | Package Name                     |
| ------------------------ | -------------------------------- |
| **Rokid System Config**  | com.rokid.sysconfig              |
| **Rokid OTA Upgrade**    | com.rokid.glass.ota              |
| **Rokid Sprite Launcher** | com.rokid.os.sprite.launcher    |
| **Rokid Sprite Assistant Server** | com.rokid.os.sprite.assistserver |
| **Rokid Screen Recording** | com.rokid.os.master.screenstream |
| **Rokid Stream Receiver** | com.rokid.stream.receiver       |
| **CXR Service**          | com.rokid.cxrservice             |

---

## 📦 Third-Party Integrated Apps

| App              | Package Name                  |
| ---------------- | ----------------------------- |
| **Alipay Glasses Edition** | com.eg.android.AlipayGGlasses |
| **JD Pay**       | com.jd.jr.joyaibuy            |
| **Ant Pay AR**   | com.iap.mobile.ar_pay         |
| **M365 HUD**     | com.m365hud.glass             |
| **WheelLog**     | com.cooper.wheellog.debug     |

**Total Installed App Count:** about 100

---

## ⚙️ Glasses-Specific Features

| Feature          | Status         |
| ---------------- | -------------- |
| **Panel Support** | ✅ yes        |
| **MFI Support**  | ✅ yes          |
| **Touch Firmware** | ✅ enabled    |
| **Camera LED**   | ✅ enabled      |
| **Glasses Unfolded State** | ✅ yes |
| **Glasses Worn State** | ✅ yes     |
| **Local TTS**    | ✅ enabled (Chinese) |
| **TTS Speed**    | 1.0            |

---

## ⚙️ System Configuration

| Item             | Value                 |
| ---------------- | --------------------- |
| **Time Zone**    | Asia/Taipei           |
| **Language**     | zh-CN                 |
| **Shutdown Timeout** | 1 second          |
| **Low-RAM Configuration** | yes          |
| **OTA Server**   | https://ota.rokid.com |
| **CXR Service Version** | 1.103          |

### Most Recent Shutdown Reason

leg_fold_timeout (leg fold timeout)

---

## 🔐 Security Information

| Item             | Status             |
| ---------------- | ------------------ |
| **SELinux**      | Enforcing (**boot default**; Permissive after escalation) |
| **Verified Boot** | ✅ orange (unlocked) |
| **Bootloader**   | ⚠️ unlocked        |
| **Verified Boot State** | ⚠️ orange   |
| **Encryption State** | ✅ encrypted (FBE) |
| **Security Patch Level** | 2024-07-05   |
| **ADB Secure Mode** | ✅ enabled      |
| **KeyMint**      | Qualcomm           |

---

## 📊 System Runtime Status

| Service                     | Status             |
| --------------------------- | ------------------ |
| **Rokid Config Service**    | ✅ running         |
| **Rokid OTA Check Service** | ✅ running         |
| **Rokid Instruct Service**  | ✅ running         |
| **Bluetooth Service**       | ✅ running (connected) |
| **Camera Service**          | ✅ running         |
| **Audio Service**           | ✅ running         |
| **SurfaceFlinger**          | ✅ running         |
| **Thermal Engine**          | ✅ running         |

---

## 📝 Technical Specification Summary

| Specification | Value                        |
| ---------- | ---------------------------- |
| **Processor** | Qualcomm Neo (4x Cortex-A55) |
| **Memory** | 1.73 GB RAM + 1.5 GB Swap    |
| **Storage** | 30 GB eMMC (16 GB available) |
| **Display** | 480x640 @ 60Hz, 240 DPI      |
| **Camera** | single lens, fixed focus, up to 60fps |
| **Battery** | Li-ion, 36 charge cycles    |
| **Connectivity** | Bluetooth 5.x, Wi-Fi, USB-C |
| **Sensors** | 16 (accelerometer, gyroscope, etc.) |
| **System** | Android 12 / Linux 5.10.209  |

---

# 📱 End of Report

**Device:** Rokid Glasses RG-glasses (RV101)  
**Collection Date:** January 30, 2026

---

_Report generation complete_
