# 【技術分享】Rokid AR Glasses Root 權限攻略 - Bootloader 已解鎖仍遇阻的深度研究

> English: [APK_TW_Tech_Report_Rokid_Root_EN.md](APK_TW_Tech_Report_Rokid_Root_EN.md)

> ⚠ **2026-09-13 狀態更正（對外文件尤須注意）**：本檔寫作時 **root 尚未取得**；**2026-08-22 起已由 GhostLock LPE（CVE-2026-43499）取得執行期 root**（`uid=0(root)`、`context=u:r:kernel:s0`、**SELinux Permissive**）。文中「SELinux Enforcing／Root Access: None」僅代表當時狀態；現行流程見 `docs/deploy.md`（每 boot 以 adb 武裝一次）。


> **作者**: <USER>  
> **發布日期**: 2026-01-31  
> **適用設備**: Rokid AR Glasses (RG-glasses / RV101)  
> **難度等級**: ★★★★☆ (進階)  
> **研究狀態**: ~~進行中 - 尋求社群協助~~ → **已解決：2026-08-22 由 GhostLock LPE（CVE-2026-43499）取得執行期 root**（本文保留為 2026-01/02 紀錄）

---

## 📱 前言

大家好！

最近入手了一副 **Rokid AR 眼鏡**，想要對它進行 Root 以便安裝自定義應用和調整系統設定。經過幾天的深入研究，發現這個設備雖然 **Bootloader 已經解鎖**，但要取得 Root 權限卻沒有想像中簡單。

這篇文章將分享我的研究過程、遇到的困難，以及目前找到的可能解決方案。希望能幫助到同樣對這款設備有興趣的朋友，也歡迎有經驗的大大提供建議！

---

## 🔧 設備規格

| 項目             | 規格                                                       |
| ---------------- | ---------------------------------------------------------- |
| **型號**         | Rokid RG-glasses (RV101)                                   |
| **Android 版本** | 12 (API 32)                                                |
| **Linux 內核**   | 5.10.209-perf-gb80a8319d11b                                |
| **處理器**       | Qualcomm neo (SM6375 系列)                                 |
| **架構**         | AArch64 (ARM64 Cortex-A55 x4)                              |
| **安全補丁**     | 2024-07-05                                                 |
| **Build 號**     | Rokid/glasses/glasses:12/SKQ1.240613.001/1.12.009-20260109 |
| **分區架構**     | A/B 動態分區 (boot_a / boot_b)                             |

### 🎉 好消息：Bootloader 已解鎖！

```
ro.boot.flash.locked = 0           # 已解鎖
ro.boot.verifiedbootstate = orange # 橙色狀態 = 可刷自訂系統
ro.boot.vbmeta.device_state = unlocked
```

這表示我們可以直接用 `fastboot flash boot` 刷入修補過的 boot.img！

---

## 🛠️ 我的 Root 策略

既然 Bootloader 已解鎖，最直接的方式就是：

```
1. 提取原廠 boot.img
2. 使用 Magisk 修補 boot.img
3. 用 Fastboot 刷入修補後的 boot.img
4. 重啟 = Root 成功！
```

聽起來很簡單對吧？但問題就出在**第一步**... 😢

---

## 🚧 遇到的困難

### 問題核心：無法提取 boot.img

我嘗試了以下所有方法，全部失敗：

#### ❌ 方法 1：ADB shell dd 提取

```bash
adb shell "dd if=/dev/block/by-name/boot_a of=/sdcard/boot.img"
```

**結果**：`Permission denied` - shell 用戶沒有讀取 block device 的權限

#### ❌ 方法 2：Fastboot fetch 命令

```bash
fastboot fetch boot_a boot.img
```

**結果**：`Device does not support fetch command`

這個設備的 Bootloader 太基礎了，不支援 fetch 命令。

#### ❌ 方法 3：Fastboot OEM 命令

嘗試了各種 OEM 命令：

| 命令                                 | 結果               |
| ------------------------------------ | ------------------ |
| `fastboot oem dump boot_a`           | ❌ unknown command |
| `fastboot oem read-partition boot_a` | ❌ unknown command |
| `fastboot oem readback boot_a`       | ❌ unknown command |
| `fastboot oem sha1sum boot_a`        | ❌ unknown command |
| `fastboot oem reboot-edl`            | ❌ unknown command |

Qualcomm 基礎 Bootloader 沒有任何擴展命令。

#### ❌ 方法 4：Recovery Sideload

創建了一個 sideload zip 想在 Recovery 中執行 dd 命令：

```bash
adb reboot recovery
# 選擇 "Apply update from ADB"
adb sideload extract_boot.zip
```

**結果**：設備卡在 sideload 模式！

原因是 sideload 模式使用不同的 USB PID (D001)，Windows 會錯誤識別為 COM 連接埠，導致驅動問題。長按電源鍵 30 秒也無法強制重啟。

> ⚠️ **警告**：不建議隨便嘗試 sideload，很容易卡住！

**解決方法**：在設備管理員中解除安裝錯誤驅動（記得勾選移除驅動程式），然後重新插拔 USB。

#### ❌ 方法 5：進入 fastbootd (Userspace Fastboot)

```bash
fastboot reboot fastboot
```

**結果**：`Failed to boot into userspace fastboot`

設備無法進入 fastbootd 模式，GKI 的現代功能不可用。

#### ❌ 方法 6：Magisk Direct Install

安裝了 Magisk v30.6 並用 scrcpy 操作眼鏡 UI：

```bash
adb install Magisk-v30.6.apk
scrcpy --window-title "Rokid Glasses"
```

**結果**：Magisk 只顯示 "Select and Patch a File" 選項，"Direct Install" 需要已有 Root 權限。

這是個雞生蛋的問題... 需要 root 才能 root。

---

## ✅ 成功的部分

### 進入 Fastboot 模式

經過多次嘗試，找到了正確的進入方式：

```bash
# 步驟 1：進入 Recovery
adb reboot recovery

# 步驟 2：在眼鏡螢幕上操作
# - 按任意鍵切換選項 (Any button cycles highlight)
# - 選擇 "Enter fastboot"
# - 長按確認 (Long-press activates)

# 步驟 3：驗證連接
fastboot devices
# 輸出：33115a5c         fastboot
```

### Fastboot 設備資訊

```
(bootloader) max-download-size: 805306368
(bootloader) current-slot:a
(bootloader) slot-count:2
(bootloader) unlocked:yes            # ← 已解鎖！
(bootloader) secure:no               # ← 安全啟動關閉！
(bootloader) hw-revision:10000
(bootloader) variant:SSG EMMC
(bootloader) off-mode-charge:0
(bootloader) battery-soc-ok:yes
```

### boot_a 分區資訊

```
partition-size:boot_a: 0x6000000   # 96MB
partition-type:boot_a: raw
slot-retry-count:a: 6
slot-unbootable:a: no
slot-successful:a: yes
```

---

## 🔬 額外研究：內核漏洞探索

既然正規方法行不通，我也嘗試了內核漏洞：

### CVE-2024-36971 (Route Cache UAF)

這個 UAF 漏洞理論上影響 5.10.x 內核，但實際測試發現：

- ✅ BPF Heap Spray 成功（unprivileged_bpf_disabled=0）
- ❌ UAF 無法觸發 - 需要真實的網路介面和 ICMP 錯誤
- ❌ 設備沒有 WiFi 連接，只有 loopback

### CVE-2024-0582 (io_uring mmap UAF)

```
結果：功能不存在
原因：PBUF_RING 是 Linux 5.19+ 才有的功能
```

### CVE-2023-2163 (BPF Verifier)

```
結果：已修補
原因：5.10.209 包含反向移植的安全修復
```

### KASLR 繞過

```
kptr_restrict = 2（最嚴格）
/proc/kallsyms 所有地址都是 0
/proc/modules 所有地址都是 0
~~結論：無法繞過~~ → **2026-09-13：KASLR 已可洩漏（kernelsnitch mm leak），leak→root 約 86–155 秒**
```

---

## 📋 USB ID 對照表

這個很重要！不同模式用不同的 USB PID：

| 模式       | VID  | PID  | 說明                   |
| ---------- | ---- | ---- | ---------------------- |
| ADB (正常) | 18D1 | 4EE7 | 正常 Android 系統      |
| Fastboot   | 18D1 | D00D | Bootloader 模式        |
| Sideload   | 18D1 | D001 | Recovery sideload 模式 |

如果遇到驅動問題，可以用 **Zadig** 強制安裝 WinUSB 驅動。

---

## 🎯 目前可行的方案

### 方案 A：fastboot boot 臨時 TWRP ⭐推薦

```bash
fastboot boot twrp_sm6375.img
# 在 TWRP 中執行
dd if=/dev/block/by-name/boot_a of=/sdcard/boot.img
adb pull /sdcard/boot.img
```

**問題**：需要找到相容 Qualcomm SM6375/khaje 的 TWRP

### 方案 B：獲取 Rokid 官方固件

如果能取得官方固件包（通常是 `.zip` 或 `.tar`），可以用 `payload-dumper` 解出 boot.img

**管道**：

- Rokid 客服
- XDA/4PDA/酷安
- Telegram 群組
- 攔截 OTA 更新

### 方案 C：硬體 EDL 模式

Qualcomm Emergency Download Mode：

1. 拆機找到測試點
2. 短接 EDL 測試點
3. 使用 QFIL 工具讀取分區

**風險**：需要拆機，可能導致保固失效

---

## 🤝 尋求協助

如果你有以下資源，歡迎私訊我：

1. **Rokid AR Glasses 官方固件** (任何版本)
2. **相容 SM6375/khaje 的 TWRP recovery**
3. **Rokid 設備的 EDL firehose programmer**
4. **其他 Root 經驗分享**

---

## 📦 工具準備清單

給想要嘗試的朋友：

1. **ADB & Fastboot** (版本 >= 34.0.0)
   - [Android SDK Platform-Tools](https://developer.android.com/studio/releases/platform-tools)

2. **Magisk** (推薦 v26.x+)
   - [Magisk Releases](https://github.com/topjohnwu/Magisk/releases)

3. **Zadig** (USB 驅動安裝)
   - [Zadig](https://zadig.akeo.ie/)

4. **scrcpy** (鏡像眼鏡螢幕)
   - [scrcpy Releases](https://github.com/Genymobile/scrcpy/releases)

---

## 📊 研究總結

| 方法                  | 狀態 | 備註              |
| --------------------- | ---- | ----------------- |
| ADB shell dd          | ❌   | Permission denied |
| fastboot fetch        | ❌   | 不支持此命令      |
| fastboot oem dump     | ❌   | unknown command   |
| Recovery sideload     | ⚠️   | 驅動問題嚴重      |
| fastbootd             | ❌   | 無法進入          |
| Magisk Direct Install | ❌   | 需已有 root       |
| CVE-2024-36971        | ❌   | 需真實網路        |
| CVE-2024-0582         | ❌   | 功能不存在        |
| fastboot boot TWRP    | 🔄   | 需要相容 image    |
| 官方固件              | 🔄   | 需要取得          |

---

## 🔐 設備安全配置參考

給有興趣研究的朋友，這是設備的安全配置：

```bash
# 安全相關
ro.secure = 1
ro.debuggable = 0
ro.adb.secure = 1
kernel.kptr_restrict = 2
kernel.perf_event_paranoid = 3
kernel.unprivileged_bpf_disabled = 0  # BPF 可用！

# SELinux
getenforce = Enforcing

# 分區
boot_a → /dev/block/mmcblk0p17 (當前使用)
boot_b → /dev/block/mmcblk0p18
vendor_boot_a → /dev/block/mmcblk0p24
vbmeta_a → /dev/block/mmcblk0p78
```

---

## 💬 結語

這個設備的 Root 之路比預期的困難許多。雖然 Bootloader 已解鎖是個巨大優勢，但沒有辦法提取 boot.img 就無法使用 Magisk。

目前最有希望的方案是找到相容的 TWRP 或官方固件。

如果有任何進展會再來更新，也歡迎大家分享經驗！

---

## 📝 更新日誌

- **2026-02-02**: 新增 GateServiced 協議逆向分析
- **2026-01-31**: 初版發布

---

## 🔓 GateServiced 協議逆向分析 (2026-02-02 新增)

### 關鍵發現

經過深入分析，發現 GateServiced 服務具有以下特徵：

| 項目                | 值                          |
| ------------------- | --------------------------- |
| **進程 PID**        | 1787                        |
| **UID/GID**         | 0 (root) / 0 (root)         |
| **監聽埠**          | TCP 8341 (0.0.0.0)          |
| **SELinux Context** | u:r:tee:s0                  |
| **Capabilities**    | 000001ffffffffff (幾乎全部) |

### 協議格式

根據 logcat 分析，GateServiced 使用以下協議格式：

```
+--------+--------+------------+
| cmd_id | rsv_len| payload    |
| 4 bytes| 4 bytes| variable   |
+--------+--------+------------+
```

- **cmd_id**: 前 4 字節直接作為命令 ID (little-endian ASCII)
- **rsv_len**: 完整數據長度
- **input_len**: rsv_len - 4

### 日誌分析範例

```
GateServiced: [GATE CA][INFO][handle_client][53]:"Received, cmd_id: 6c6c6568, rsv_len:5"
GateServiced: [GATE CA][INFO][run_exec_cmd_ta][231]:"cmd_id = 6c6c6568, input_len = 1, output_len = 20480"
GateServiced: [GATE CA][ERRO][run_exec_cmd_ta][234]:"TA is not already loaded."
GateServiced: [GATE CA][ERRO][handle_client][59]:"cmd_dispatch failed, ret:300c"
```

解析: `cmd_id: 6c6c6568` = "lleh" (little-endian "hell" from "hello")

### 問題

**"TA is not already loaded"** - GateServiced 期望 TrustZone Trusted Application 已經載入，但目前：

- `ssgqmigd` 服務不存在
- `qseeproxydaemon` 服務不存在

這表示 TrustZone 相關服務可能未完整配置。

### 測試腳本

在設備上運行的 shell 測試腳本：

```bash
# 位置: /data/local/tmp/gate_exploit.sh
# 用法:
/data/local/tmp/gate_exploit.sh -admin    # 測試管理命令
/data/local/tmp/gate_exploit.sh -binary   # 測試二進制命令 ID
/data/local/tmp/gate_exploit.sh -all      # 執行所有測試
```

### 攻擊向量評估

| 向量        | 狀態 | 說明                    |
| ----------- | ---- | ----------------------- |
| 命令注入    | ❌   | 未發現                  |
| 路徑遍歷    | ❌   | 未發現                  |
| 格式字符串  | ❌   | 未發現                  |
| 緩衝區溢出  | ⚠️   | 待進一步測試            |
| TA 載入繞過 | 🔄   | 需要研究 QSEE HIDL 接口 |

### 下一步

1. **研究 QSEE HIDL 服務**: `vendor.qti.hardware.qseecom@1.0::IQSEECom/default`
2. **TZAccessService 利用**: exported=true, 無權限限制
3. **Frida 動態 Hook**: 需要 root 或 SELinux permissive

---

## 🔬 研究記錄更新 (2026-02-02)

### 1. GateServiced 協議深度分析

經過詳細測試，確認協議格式：

```
[cmd_id:4B little-endian ASCII][rsv_len:4B][payload:variable]
```

測試的命令標識符：
| ASCII String | Hex cmd_id | 結果 |
|-------------|------------|------|
| ping | 676e6970 | ret:300c |
| stat | 74617473 | ret:300c |
| info | 6f666e69 | ret:300c |
| vers | 73726576 | ret:300c |
| GATE | 45544147 | ret:300c |
| help | 706c6568 | ret:300c |

**核心問題**: 所有命令返回 `300c` 錯誤碼 = "TA is not already loaded"

### 2. QSEE 子系統訪問分析

```bash
# QSEE 內核模組
qseecom 1003520 - Live 0x0000000000000000 (O)

# QSEECOM 版本
cat /sys/bus/platform/drivers/qseecom/version
# Output: 0x1402000

# qseecomd 進程
# PID 553-554, u:r:tee:s0 context

# HIDL 服務
vendor.qti.hardware.qseecom@1.0::IQSEECom/default
```

**Socket 權限限制**:

```
/dev/socket/notify-topology: srw-rw---- system drmrpc
```

- 需要 `drmrpc` 組權限 (GID 1019)
- shell 用戶僅有: shell, input, log, adb, sdcard_rw, sdcard_r, inet

### 3. TrustZoneAccessService 研究

APK 位置: `/vendor/app/TrustZoneAccessService/`
Package: `com.qualcomm.qti.qms.service.trustzoneaccess`

**AndroidManifest 分析**:

- `exported="true"` - 可從任何 app 調用
- 無 permission 要求 - 潛在攻擊面

**測試結果**:

```bash
am start-foreground-service -n com.qualcomm.qti.qms.service.trustzoneaccess/...
# 結果: ForegroundServiceDidNotStartInTimeException
```

服務需要前台通知但未提供，需要自建 APK 來正確綁定。

### 4. Magisk 發現

```bash
pm list packages | grep magisk
# package:com.topjohnwu.magisk

dumpsys package com.topjohnwu.magisk | grep version
# versionCode=30600 versionName=30.6
```

**狀態**: App 已安裝但未激活（無 root 二進制，可能是遺留安裝）

### 5. 攻擊向量優先級更新

| 優先級 | 向量                 | 可行性 | 阻礙                       |
| ------ | -------------------- | ------ | -------------------------- |
| 🔴 1   | GateServiced TA 載入 | 高潛力 | 需找到載入觸發器           |
| 🟡 2   | TZAccessService 綁定 | 中等   | 需要 APK 開發              |
| 🟡 3   | qmcs 分區注入        | 未知   | 需要分析 graphics_composer |
| 🟢 4   | DRM 服務利用         | 低     | 需要更多研究               |

### 6. 已部署的測試腳本

```
/data/local/tmp/
├── gate_exploit.sh    # 基礎協議測試
├── gate_adv.sh        # 進階 TA 載入測試
├── gate_rce.sh        # RCE 向量測試
├── tz_call.sh         # TZ 服務調用
├── priv_enum.sh       # 權限枚舉
└── ta_monitor.sh      # TA 載入監控
```

---

**#Rokid #AR眼鏡 #Root #Magisk #Fastboot #Android12 #技術分享**

---

> 💡 **免責聲明**：本文僅供技術研究與學習用途。Root 設備可能導致保固失效，請自行評估風險。
