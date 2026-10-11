# Rokid Glasses Device System Data Report

> 中文版：[Rokid_Glasses_系統資料報告.md](Rokid_Glasses_%E7%B3%BB%E7%B5%B1%E8%B3%87%E6%96%99%E5%A0%B1%E5%91%8A.md)

> 📌 **Fact basis**: Whenever this document touches on facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; in case of conflict, that table decides).


**Collection date:** 2026-01-30  
**Device serial:** <DEVICE_SERIAL>

---

## 📱 Basic Device Information

| Item         | Information             |
| ------------ | ---------------- |
| **Product name** | RG-glasses       |
| **Brand**     | Rokid            |
| **Manufacturer**   | Rokid            |
| **Model**     | RG-glasses       |
| **OEM model** | RV101            |
| **OEM ID**   | 101              |
| **Device type** | glasses          |
| **Hardware version** | dvt3             |
| **Serial number**     | <DEVICE_SERIAL> |

---

## 🖥️ System Software Information

| Item                | Information                                                                                |
| ------------------- | ----------------------------------------------------------------------------------- |
| **Android version**    | 12                                                                                  |
| **SDK version**        | 32                                                                                  |
| **Build ID**        | SKQ1.240613.001                                                                     |
| **Build number**           | 1.12.009-20260109-150201                                                            |
| **Build type**      | user                                                                                |
| **Build tags**      | release-keys                                                                        |
| **Security patch**        | 2024-07-05                                                                          |
| **Build date**      | Fri Jan 9 11:38:41 CST 2026                                                         |
| **Build fingerprint**      | Rokid/glasses/glasses:12/SKQ1.240613.001/1.12.009-20260109-150201:user/release-keys |
| **Bootloader state** | unlocked (orange)                                                                   |

---

## 🔧 Kernel and Platform Information

| Item                | Information                                                   |
| ------------------- | ------------------------------------------------------ |
| **Linux kernel version**  | 5.10.209-perf                                          |
| **Full kernel version**    | 5.10.209-perf-gb80a8319d11b-ab1.12.009-20260109-150201 |
| **Kernel build time**    | Wed Dec 31 06:35:01 UTC 2025                           |
| **Architecture**            | aarch64 (ARM64)                                        |
| **Platform**            | neo                                                    |
| **SoC manufacturer**      | QTI (Qualcomm)                                         |
| **SoC ID**          | 579                                                    |
| **Board API Level** | 31                                                     |
| **VNDK version**       | 32                                                     |

---

## 🧠 Processor (CPU) Information

| Item                | Information                            |
| ------------------- | ------------------------------- |
| **CPU architecture**        | ARMv8 (64-bit)                  |
| **CPU core count**      | 4 cores                          |
| **CPU Implementer** | 0x41 (ARM)                      |
| **CPU Part**        | 0xd05 (Cortex-A55)              |
| **CPU Variant**     | 0x2                             |
| **CPU Revision**    | 0                               |
| **BogoMIPS**        | 38.40 (per core)                  |
| **Primary ABI**        | arm64-v8a                       |
| **Supported ABIs**        | arm64-v8a, armeabi-v7a, armeabi |

### CPU Features

- fp, asimd, evtstrm, aes, pmull, sha1, sha2, crc32, atomics
- fphp, asimdhp, cpuid, asimdrdm, lrcpc, dcpop, asimddp

---

## 💾 Memory Information

| Item             | Information                    |
| ---------------- | ----------------------- |
| **Total memory**     | 1,816,600 KB (~1.73 GB) |
| **Available memory**   | 862,732 KB (~843 MB)    |
| **Free memory**   | 201,000 KB (~196 MB)    |
| **Total swap**    | 1,572,860 KB (~1.5 GB)  |
| **Free swap**    | 1,371,388 KB (~1.3 GB)  |
| **Cache**         | 590,736 KB              |
| **Buffers**      | 6,064 KB                |
| **Low-RAM configuration** | yes (Low RAM device)       |

---

## 💽 Storage Information

| Partition            | Size   | Used | Available | Usage | Mount point               |
| --------------- | ------ | ------ | ---- | ------ | -------------------- |
| **System (/)**    | 790M   | 788M   | 0    | 100%   | /                    |
| **System Ext**  | 291M   | 290M   | 0    | 100%   | /system_ext          |
| **Product partition**    | 567M   | 565M   | 0    | 100%   | /product             |
| **Vendor partition**    | 571M   | 569M   | 0    | 100%   | /vendor              |
| **Vendor DLKM** | 61M    | 61M    | 0    | 100%   | /vendor_dlkm         |
| **ODM**         | 988K   | 984K   | 0    | 100%   | /odm                 |
| **Data partition**    | 19G    | 2.9G   | 16G  | 17%    | /data                |
| **Firmware**        | 64M    | 45M    | 19M  | 71%    | /vendor/firmware_mnt |
| **DSP**         | 59M    | 34M    | 24M  | 59%    | /vendor/dsp          |
| **BT firmware**     | 64M    | 1.2M   | 63M  | 2%     | /vendor/bt_firmware  |
| **Persist**     | 27M    | 960K   | 25M  | 4%     | /mnt/vendor/persist  |
| **Total eMMC capacity** | ~30 GB | -      | -    | -      | mmcblk0              |

### Partition Characteristics

- **A/B update system:** yes
- **Dynamic partitions:** yes
- **Virtual A/B:** yes
- **Encryption type:** file-based encryption (FBE)
- **Verified boot:** **orange (not enforced / unlocked)** (**originally written as enforcing, which contradicted L37/L348 of the same file; corrected 2026-09-13**)

---

## 📺 Display Information

| Item               | Information            |
| ------------------ | --------------- |
| **Physical resolution**     | 480 x 640 pixels  |
| **Override resolution**     | 481 x 640 pixels  |
| **Screen density**       | 240 DPI         |
| **Refresh rate**           | 60 Hz           |
| **Display orientation**       | Portrait |
| **Color mode**       | 9               |
| **HDR display**       | no              |
| **Wide color gamut display**       | no              |
| **Background blur**       | supported            |
| **GPU composition**       | forced        |
| **OpenGL ES version** | 3.2 (196610)    |

### GPU Information

| Item         | Information                        |
| ------------ | --------------------------- |
| **EGL**      | Adreno                      |
| **Vulkan**   | Adreno                      |
| **Available frequencies** | 644, 570, 450, 320, 235 MHz |

---

## 🔋 Battery Information

| Item         | Information            |
| ------------ | --------------- |
| **Battery type** | Li-ion |
| **Battery health** | Good     |
| **Charge level**     | 100%            |
| **Voltage**     | 4,507 mV        |
| **Temperature**     | 26.5°C          |
| **Charge cycles** | 36           |
| **USB powered** | yes              |
| **AC powered**  | no              |
| **Wireless charging** | no              |

---

## 📡 Connectivity

### Bluetooth

| Item               | Information                                      |
| ------------------ | ----------------------------------------- |
| **State**           | ON                                        |
| **Bluetooth name**       | <DEVICE_ALIAS>                              |
| **MAC address**       | AC:86:D1:55:D6:F9                         |
| **Connection state**       | STATE_CONNECTED                           |
| **Connected device**       | Redmi Note 11 Pro+ 5G (4C:E0:DB:7D:4E:D5) |
| **A2DP Sink**      | supported                                      |
| **HFP Client**     | supported                                      |
| **PBAP Client**    | supported                                      |
| **A2DP Offload**   | sbc-aac-aptx-aptxhd-ldac                  |
| **A2DP Sink codecs** | sbc-aac                                   |
| **SOC type**       | hamilton                                  |
| **MFI support**       | yes (iOS device compatible)                         |

### Wi-Fi

| Item                 | Information        |
| -------------------- | ----------- |
| **Interface**             | wlan0       |
| **Wi-Fi Aware interface** | wifi-aware0 |
| **Country code**         | CN          |
| **State**             | not connected      |

### USB

| Item              | Information                         |
| ----------------- | ---------------------------- |
| **Controller**        | a600000.dwc3                 |
| **Current configuration**      | adb                          |
| **MTP support**      | yes                           |
| **ConfigFS version** | 2                            |
| **Product string**      | RG-glasses-IDP \_SN:D380BE29 |

---

## 📷 Camera Information

| Item             | Information                 |
| ---------------- | -------------------- |
| **Camera count**     | 1                    |
| **Camera ID**      | 0                    |
| **Facing**         | rear (Back)          |
| **Orientation angle**     | 270°                 |
| **Flash**       | none                   |
| **HAL version**    | 3.7                  |
| **Resource cost**     | 50                   |
| **Autofocus mode** | not supported (fixed focus)    |
| **Supported frame rates**   | 15, 24, 30, 60 fps   |
| **Exposure compensation range** | -24 ~ +24 (step 1/6) |
| **Digital zoom range** | 1.0x ~ 8.0x          |
| **Video stabilization**     | supported                 |
| **AE/AWB lock**  | supported                 |
| **Tuning version**     | 4.1.2                |
| **Power frequency**     | 60 Hz (anti-flicker)       |

---

## 📲 Sensor Information

| Sensor             | Vendor                      | Type                       | Max update rate |
| ------------------ | ------------------------- | -------------------------- | ---------- |
| **Accelerometer**       | TDK-Invensense (ICM4x6xx) | accelerometer              | 500 Hz     |
| **Gyroscope**         | TDK-Invensense (ICM4x6xx) | gyroscope                  | 500 Hz     |
| **Proximity sensor**       | Sensortek (UCS146E0)      | proximity                  | On-change  |
| **Gravity sensor**       | QTI                       | gravity                    | 200 Hz     |
| **Linear acceleration**       | QTI                       | linear_acceleration        | 200 Hz     |
| **Game rotation vector**   | QTI                       | game_rotation_vector       | 200 Hz     |
| **Uncalibrated gyroscope**   | TDK-Invensense            | gyroscope_uncalibrated     | 500 Hz     |
| **Uncalibrated accelerometer** | TDK-Invensense            | accelerometer_uncalibrated | 500 Hz     |

**Total sensor count:** 16 hardware sensors

---

## 🔊 Audio Features

### Audio Characteristics

- **Fluence noise reduction:** supported (speaker/voice call/voice communication)
- **A2DP Offload:** supported
- **A2DP Sink:** supported
- **HFP (hands-free):** supported
- **LDAC codec:** supported
- **AAC VBR:** supported
- **USB audio offload:** supported
- **Burst Mode:** supported
- **Concurrent capture:** supported
- **Compressed capture:** supported
- **Dynamic ECNS:** supported
- **FM:** supported
- **HDMI passthrough:** supported

### Audio Configuration

| Item                   | Value    |
| ---------------------- | ------- |
| **Client heap size**   | 7168 KB |
| **Flinger standby time**   | 2000 ms |
| **Offload buffer size** | 32 KB   |
| **Period size**           | 192     |
| **Period multiplier**       | 3       |
| **Input period size**       | 144     |

---

## 📦 System Applications

### Rokid-Specific Applications

| Application                     | Package name                         |
| ------------------------ | -------------------------------- |
| **Rokid system configuration**       | com.rokid.sysconfig              |
| **Rokid OTA upgrade**       | com.rokid.glass.ota              |
| **Rokid sprite launcher**     | com.rokid.os.sprite.launcher     |
| **Rokid sprite assistant server** | com.rokid.os.sprite.assistserver |
| **Rokid screen recording**       | com.rokid.os.master.screenstream |
| **Rokid stream receiver**       | com.rokid.stream.receiver        |
| **CXR service**       | com.rokid.cxrservice             |

### Third-Party Integrated Applications

| Application             | Package name                      |
| ---------------- | ----------------------------- |
| **Alipay glasses edition** | com.eg.android.AlipayGGlasses |
| **JD Pay**     | com.jd.jr.joyaibuy            |
| **Ant Pay AR**  | com.iap.mobile.ar_pay         |
| **M365 HUD**     | com.m365hud.glass             |
| **WheelLog**     | com.cooper.wheellog.debug     |

### Preinstalled System Applications

- Camera, settings, Bluetooth, Wi-Fi related
- Android core services
- Qualcomm-specific services

**Total installed applications:** about 100

---

## ⚙️ Special Features and Configuration

### Glasses-Specific Features

| Feature             | State                    |
| ---------------- | ----------------------- |
| **Panel support**     | yes (glassesWithPanel=1) |
| **MFI support**     | yes (glassesWithMfi=1)   |
| **Touch firmware**     | enabled                    |
| **Camera LED**     | enabled                    |
| **Glasses unfolded state** | yes (is_spread=1)        |
| **Glasses worn state** | yes (is_take_on=1)       |
| **Local TTS**     | enabled                    |
| **TTS language**     | Chinese (language_id=1)    |
| **TTS speed**     | 1.0                     |

### System Configuration

| Item                   | Value                      |
| ---------------------- | ------------------------- |
| **Time zone**               | Asia/Taipei               |
| **Language**               | zh-CN                     |
| **Shutdown timeout**           | 1 second                      |
| **Low-RAM configuration**       | yes                        |
| **Main keys (virtual buttons)**    | enabled (qemu.hw.mainkeys=1) |
| **OTA server**         | https://ota.rokid.com     |
| **CXR service version**       | 1.103                     |
| **Proximity sensor forced on** | yes                       |

### Shutdown Reason Log

- Most recent shutdown reason: leg_fold_timeout (leg fold timeout)

---

## 🔐 Security Information

| Item             | State            |
| ---------------- | --------------- |
| **SELinux**      | Enforcing (boot default; Permissive after privilege escalation) |
| **Verified boot**     | **orange (unlocked)** |
| **Bootloader**   | unlocked        |
| **Verified boot state** | orange          |
| **Encryption state**     | encrypted (FBE) |
| **Security patch level** | 2024-07-05      |
| **ADB secure mode** | enabled            |
| **Performance hardening**     | enabled            |
| **OEM unlock**     | supported            |
| **KeyMint**      | Qualcomm        |

---

## 🏗️ Partition Layout

The system uses an A/B seamless update mechanism; the main partitions:

- **boot_a/boot_b** - boot images
- **vbmeta_a/vbmeta_b** - verified boot metadata
- **super** - dynamic partitions (containing system, vendor, product, etc.)
- **userdata** - user data (~19.7 GB)
- **persist** - persistent data
- **misc** - miscellaneous configuration
- **frp** - factory reset protection

**Total eMMC capacity:** about 30 GB

---

## 📊 System Runtime State

| Service                        | State            |
| --------------------------- | --------------- |
| **Rokid Config Service**    | running          |
| **Rokid OTA Check Service** | running          |
| **Rokid Instruct Service**  | running          |
| **Bluetooth service**                | running (connected) |
| **Camera service**                | running          |
| **Audio service**                | running          |
| **SurfaceFlinger**          | running          |
| **Thermal Engine**          | running          |
| **Sensor HAL**              | running          |

---

## 📝 Technical Specification Summary

```
┌─────────────────────────────────────────────────────────────┐
│                    Rokid Glasses technical specs             │
├─────────────────────────────────────────────────────────────┤
│  Processor: Qualcomm Neo platform (4x Cortex-A55 @ 38.4 BogoMIPS) │
│  Memory: 1.73 GB RAM + 1.5 GB Swap                           │
│  Storage: 30 GB eMMC (16 GB available)                       │
│  Display: 480x640 @ 60Hz, 240 DPI                            │
│  Camera: single lens, rear, fixed focus, up to 60fps         │
│  Battery: Li-ion battery, 36 charge cycles                   │
│  Connectivity: Bluetooth 5.x, Wi-Fi, USB-C (ADB)             │
│  Sensors: accelerometer, gyroscope, proximity sensor (16 total) │
│  System: Android 12 (API 32)                                 │
│  Kernel: Linux 5.10.209                                      │
│  Highlights: A2DP Sink, HFP Client, MFI support, local TTS   │
└─────────────────────────────────────────────────────────────┘
```

---

_Report generation complete_
