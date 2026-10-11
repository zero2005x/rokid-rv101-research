# Rokid Glasses Device System Report
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。

> ⚠ **2026-09-13 註**：本文快照為 **1.12.009-20260109-150201**；裝置現行為 **1.22.009-20260710-150201**（歷史蒐集，非現況）。


**Collection Date:** January 30, 2026  
**Device Serial:** <DEVICE_SERIAL>

---

## 📱 Basic Device Information

| Item                 | Information      |
| -------------------- | ---------------- |
| **Product Name**     | RG-glasses       |
| **Brand**            | Rokid            |
| **Manufacturer**     | Rokid            |
| **Model**            | RG-glasses       |
| **OEM Model**        | RV101            |
| **OEM ID**           | 101              |
| **Device Type**      | glasses          |
| **Hardware Version** | dvt3             |
| **Serial Number**    | <DEVICE_SERIAL> |

---

## 🖥️ System Software Information

| Item                  | Information                                                                         |
| --------------------- | ----------------------------------------------------------------------------------- |
| **Android Version**   | 12                                                                                  |
| **SDK Version**       | 32                                                                                  |
| **Build ID**          | SKQ1.240613.001                                                                     |
| **Version Number**    | 1.12.009-20260109-150201                                                            |
| **Build Type**        | user                                                                                |
| **Build Tags**        | release-keys                                                                        |
| **Security Patch**    | 2024-07-05                                                                          |
| **Build Date**        | Fri Jan 9 11:38:41 CST 2026                                                         |
| **Build Fingerprint** | Rokid/glasses/glasses:12/SKQ1.240613.001/1.12.009-20260109-150201:user/release-keys |
| **Bootloader Status** | unlocked (orange)                                                                   |

---

## 🔧 Kernel & Platform Information

| Item                     | Information                                            |
| ------------------------ | ------------------------------------------------------ |
| **Linux Kernel Version** | 5.10.209-perf                                          |
| **Full Kernel Version**  | 5.10.209-perf-gb80a8319d11b-ab1.12.009-20260109-150201 |
| **Kernel Build Time**    | Wed Dec 31 06:35:01 UTC 2025                           |
| **Architecture**         | aarch64 (ARM64)                                        |
| **Platform**             | neo                                                    |
| **SoC Manufacturer**     | QTI (Qualcomm)                                         |
| **SoC ID**               | 579                                                    |
| **Board API Level**      | 31                                                     |
| **VNDK Version**         | 32                                                     |

---

## 🧠 Processor (CPU) Information

| Item                 | Information                     |
| -------------------- | ------------------------------- |
| **CPU Architecture** | ARMv8 (64-bit)                  |
| **CPU Cores**        | 4 cores                         |
| **CPU Implementer**  | 0x41 (ARM)                      |
| **CPU Part**         | 0xd05 (Cortex-A55)              |
| **CPU Variant**      | 0x2                             |
| **CPU Revision**     | 0                               |
| **BogoMIPS**         | 38.40 (per core)                |
| **Primary ABI**      | arm64-v8a                       |
| **Supported ABIs**   | arm64-v8a, armeabi-v7a, armeabi |

### CPU Features

- fp, asimd, evtstrm, aes, pmull, sha1, sha2, crc32, atomics
- fphp, asimdhp, cpuid, asimdrdm, lrcpc, dcpop, asimddp

---

## 💾 Memory Information

| Item                 | Information             |
| -------------------- | ----------------------- |
| **Total Memory**     | 1,816,600 KB (~1.73 GB) |
| **Available Memory** | 862,732 KB (~843 MB)    |
| **Free Memory**      | 201,000 KB (~196 MB)    |
| **Swap Total**       | 1,572,860 KB (~1.5 GB)  |
| **Swap Free**        | 1,371,388 KB (~1.3 GB)  |
| **Cached**           | 590,736 KB              |
| **Buffers**          | 6,064 KB                |
| **Low RAM Config**   | Yes (Low RAM Device)    |

---

## 💽 Storage Information

| Partition       | Size   | Used | Available | Usage | Mount Point          |
| --------------- | ------ | ---- | --------- | ----- | -------------------- |
| **System (/)**  | 790M   | 788M | 0         | 100%  | /                    |
| **System Ext**  | 291M   | 290M | 0         | 100%  | /system_ext          |
| **Product**     | 567M   | 565M | 0         | 100%  | /product             |
| **Vendor**      | 571M   | 569M | 0         | 100%  | /vendor              |
| **Vendor DLKM** | 61M    | 61M  | 0         | 100%  | /vendor_dlkm         |
| **ODM**         | 988K   | 984K | 0         | 100%  | /odm                 |
| **Data**        | 19G    | 2.9G | 16G       | 17%   | /data                |
| **Firmware**    | 64M    | 45M  | 19M       | 71%   | /vendor/firmware_mnt |
| **DSP**         | 59M    | 34M  | 24M       | 59%   | /vendor/dsp          |
| **BT Firmware** | 64M    | 1.2M | 63M       | 2%    | /vendor/bt_firmware  |
| **Persist**     | 27M    | 960K | 25M       | 4%    | /mnt/vendor/persist  |
| **eMMC Total**  | ~30 GB | -    | -         | -     | mmcblk0              |

### Partition Features

- **A/B Update System:** Yes
- **Dynamic Partitions:** Yes
- **Virtual A/B:** Yes
- **Encryption Type:** File-Based Encryption (FBE)
- **Verified Boot:** enforcing

---

## 📺 Display Information

| Item                    | Information      |
| ----------------------- | ---------------- |
| **Physical Resolution** | 480 x 640 pixels |
| **Override Resolution** | 481 x 640 pixels |
| **Screen Density**      | 240 DPI          |
| **Refresh Rate**        | 60 Hz            |
| **Display Orientation** | Portrait         |
| **Color Mode**          | 9                |
| **HDR Display**         | No               |
| **Wide Color Gamut**    | No               |
| **Background Blur**     | Supported        |
| **GPU Composition**     | Forced           |
| **OpenGL ES Version**   | 3.2 (196610)     |

### GPU Information

| Item                      | Information                 |
| ------------------------- | --------------------------- |
| **EGL**                   | Adreno                      |
| **Vulkan**                | Adreno                      |
| **Available Frequencies** | 644, 570, 450, 320, 235 MHz |

---

## 🔋 Battery Information

| Item                  | Information          |
| --------------------- | -------------------- |
| **Battery Type**      | Li-ion (Lithium-ion) |
| **Battery Health**    | Good                 |
| **Battery Level**     | 100%                 |
| **Voltage**           | 4,507 mV             |
| **Temperature**       | 26.5°C               |
| **Charge Cycles**     | 36                   |
| **USB Powered**       | Yes                  |
| **AC Powered**        | No                   |
| **Wireless Charging** | No                   |

---

## 📡 Connectivity Features

### Bluetooth

| Item                 | Information                               |
| -------------------- | ----------------------------------------- |
| **Status**           | ON                                        |
| **Bluetooth Name**   | <DEVICE_ALIAS>                              |
| **MAC Address**      | AC:86:D1:55:D6:F9                         |
| **Connection State** | STATE_CONNECTED                           |
| **Connected Device** | Redmi Note 11 Pro+ 5G (4C:E0:DB:7D:4E:D5) |
| **A2DP Sink**        | Supported                                 |
| **HFP Client**       | Supported                                 |
| **PBAP Client**      | Supported                                 |
| **A2DP Offload**     | sbc-aac-aptx-aptxhd-ldac                  |
| **A2DP Sink Codecs** | sbc-aac                                   |
| **SoC Type**         | hamilton                                  |
| **MFI Support**      | Yes (iOS Compatible)                      |

### Wi-Fi

| Item                      | Information  |
| ------------------------- | ------------ |
| **Interface**             | wlan0        |
| **Wi-Fi Aware Interface** | wifi-aware0  |
| **Country Code**          | CN           |
| **Status**                | Disconnected |

### USB

| Item                 | Information                  |
| -------------------- | ---------------------------- |
| **Controller**       | a600000.dwc3                 |
| **Current Config**   | adb                          |
| **MTP Support**      | Yes                          |
| **ConfigFS Version** | 2                            |
| **Product String**   | RG-glasses-IDP \_SN:D380BE29 |

---

## 📷 Camera Information

| Item                            | Information                 |
| ------------------------------- | --------------------------- |
| **Camera Count**                | 1                           |
| **Camera ID**                   | 0                           |
| **Facing**                      | Back                        |
| **Orientation**                 | 270°                        |
| **Flash**                       | None                        |
| **HAL Version**                 | 3.7                         |
| **Resource Cost**               | 50                          |
| **Auto Focus Mode**             | Not Supported (Fixed Focus) |
| **Supported Frame Rates**       | 15, 24, 30, 60 fps          |
| **Exposure Compensation Range** | -24 to +24 (step 1/6)       |
| **Digital Zoom Range**          | 1.0x to 8.0x                |
| **Video Stabilization**         | Supported                   |
| **AE/AWB Lock**                 | Supported                   |
| **Tuning Version**              | 4.1.2                       |
| **Power Frequency**             | 60 Hz (Anti-flicker)        |

---

## 📲 Sensor Information

| Sensor                         | Vendor                    | Type                       | Max Rate  |
| ------------------------------ | ------------------------- | -------------------------- | --------- |
| **Accelerometer**              | TDK-Invensense (ICM4x6xx) | accelerometer              | 500 Hz    |
| **Gyroscope**                  | TDK-Invensense (ICM4x6xx) | gyroscope                  | 500 Hz    |
| **Proximity Sensor**           | Sensortek (UCS146E0)      | proximity                  | On-change |
| **Gravity Sensor**             | QTI                       | gravity                    | 200 Hz    |
| **Linear Acceleration**        | QTI                       | linear_acceleration        | 200 Hz    |
| **Game Rotation Vector**       | QTI                       | game_rotation_vector       | 200 Hz    |
| **Uncalibrated Gyroscope**     | TDK-Invensense            | gyroscope_uncalibrated     | 500 Hz    |
| **Uncalibrated Accelerometer** | TDK-Invensense            | accelerometer_uncalibrated | 500 Hz    |

**Total Sensor Count:** 16 hardware sensors（**2026-09-13 註：下表僅列 8 個；16 為裝置回報總數**）

---

## 🔊 Audio Features

### Audio Characteristics

- **Fluence Noise Reduction:** Supported (Speaker/Voice Call/Voice Comm)
- **A2DP Offload:** Supported
- **A2DP Sink:** Supported
- **HFP (Hands-Free):** Supported
- **LDAC Codec:** Supported
- **AAC VBR:** Supported
- **USB Audio Offload:** Supported
- **Burst Mode:** Supported
- **Concurrent Capture:** Supported
- **Compressed Capture:** Supported
- **Dynamic ECNS:** Supported
- **FM:** Supported
- **HDMI Passthrough:** Supported

### Audio Configuration

| Item                     | Value   |
| ------------------------ | ------- |
| **Client Heap Size**     | 7168 KB |
| **Flinger Standby Time** | 2000 ms |
| **Offload Buffer Size**  | 32 KB   |
| **Period Size**          | 192     |
| **Period Multiplier**    | 3       |
| **Input Period Size**    | 144     |

---

## 📦 System Applications

### Rokid Proprietary Apps

| Application                    | Package Name                     |
| ------------------------------ | -------------------------------- |
| **Rokid System Config**        | com.rokid.sysconfig              |
| **Rokid OTA Upgrade**          | com.rokid.glass.ota              |
| **Rokid Sprite Launcher**      | com.rokid.os.sprite.launcher     |
| **Rokid Sprite Assist Server** | com.rokid.os.sprite.assistserver |
| **Rokid Screen Record**        | com.rokid.os.master.screenstream |
| **Rokid Stream Receiver**      | com.rokid.stream.receiver        |
| **CXR Service**                | com.rokid.cxrservice             |

### Third-Party Integrated Apps

| Application                | Package Name                  |
| -------------------------- | ----------------------------- |
| **Alipay Glasses Edition** | com.eg.android.AlipayGGlasses |
| **JD Pay**                 | com.jd.jr.joyaibuy            |
| **Ant Pay AR**             | com.iap.mobile.ar_pay         |
| **M365 HUD**               | com.m365hud.glass             |
| **WheelLog**               | com.cooper.wheellog.debug     |

### Pre-installed System Apps

- Camera, Settings, Bluetooth, Wi-Fi related
- Android Core Services
- Qualcomm Proprietary Services

**Total Installed Apps:** Approximately 100

---

## ⚙️ Special Features & Configuration

### Glasses-Specific Features

| Feature                  | Status                   |
| ------------------------ | ------------------------ |
| **Panel Support**        | Yes (glassesWithPanel=1) |
| **MFI Support**          | Yes (glassesWithMfi=1)   |
| **Touch Firmware**       | Enabled                  |
| **Camera LED**           | Enabled                  |
| **Glasses Spread State** | Yes (is_spread=1)        |
| **Glasses Wear State**   | Yes (is_take_on=1)       |
| **Local TTS**            | Enabled                  |
| **TTS Language**         | Chinese (language_id=1)  |
| **TTS Speed**            | 1.0                      |

### System Configuration

| Item                            | Value                        |
| ------------------------------- | ---------------------------- |
| **Timezone**                    | Asia/Taipei                  |
| **Language**                    | zh-CN                        |
| **Shutdown Timeout**            | 1 second                     |
| **Low RAM Config**              | Yes                          |
| **Main Keys (Virtual Buttons)** | Enabled (qemu.hw.mainkeys=1) |
| **OTA Server**                  | https://ota.rokid.com        |
| **CXR Service Version**         | 1.103                        |
| **Force Proximity Sensor**      | Yes                          |

### Shutdown Reason Log

- Recent Shutdown Reason: leg_fold_timeout (Leg Fold Timeout)

---

## 🔐 Security Information

| Item                      | Status          |
| ------------------------- | --------------- |
| **SELinux**               | enforcing       |
| **Verified Boot**         | enforcing       |
| **Bootloader**            | unlocked        |
| **Verified Boot State**   | orange          |
| **Encryption State**      | encrypted (FBE) |
| **Security Patch Level**  | 2024-07-05      |
| **ADB Secure Mode**       | Enabled         |
| **Performance Hardening** | Enabled         |
| **OEM Unlock**            | Supported       |
| **KeyMint**               | Qualcomm        |

---

## 🏗️ Partition Layout

The system uses A/B seamless update mechanism. Main partitions:

- **boot_a/boot_b** - Boot images
- **vbmeta_a/vbmeta_b** - Verified boot metadata
- **super** - Dynamic partition (contains system, vendor, product, etc.)
- **userdata** - User data (~19.7 GB)
- **persist** - Persistent data
- **misc** - Miscellaneous configuration
- **frp** - Factory Reset Protection

**Total eMMC Capacity:** Approximately 30 GB

---

## 📊 System Running Status

| Service                     | Status              |
| --------------------------- | ------------------- |
| **Rokid Config Service**    | Running             |
| **Rokid OTA Check Service** | Running             |
| **Rokid Instruct Service**  | Running             |
| **Bluetooth Service**       | Running (Connected) |
| **Camera Service**          | Running             |
| **Audio Service**           | Running             |
| **SurfaceFlinger**          | Running             |
| **Thermal Engine**          | Running             |
| **Sensor HAL**              | Running             |

---

## 📝 Technical Specifications Summary

```
┌─────────────────────────────────────────────────────────────┐
│                 Rokid Glasses Technical Specs               │
├─────────────────────────────────────────────────────────────┤
│  Processor: Qualcomm Neo (4x Cortex-A55 @ 38.4 BogoMIPS)    │
│  Memory: 1.73 GB RAM + 1.5 GB Swap                          │
│  Storage: 30 GB eMMC (16 GB available)                      │
│  Display: 480x640 @ 60Hz, 240 DPI                           │
│  Camera: Single lens, rear-facing, fixed focus, up to 60fps │
│  Battery: Li-ion, 36 charge cycles                          │
│  Connectivity: Bluetooth 5.x, Wi-Fi, USB-C (ADB)            │
│  Sensors: Accelerometer, Gyroscope, Proximity (16 total)    │
│  OS: Android 12 (API 32)                                    │
│  Kernel: Linux 5.10.209                                     │
│  Features: A2DP Sink, HFP Client, MFI Support, Local TTS    │
└─────────────────────────────────────────────────────────────┘
```

---

_Report Generation Complete_
