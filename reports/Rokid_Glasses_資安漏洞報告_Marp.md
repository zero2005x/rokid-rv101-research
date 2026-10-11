---
marp: true
theme: default
paginate: true
size: 16:9
style: |
  section {
    font-size: 20px;
  }
  h1 {
    font-size: 34px;
    color: #dc2626;
  }
  h2 {
    font-size: 26px;
    color: #b91c1c;
  }
  h3 {
    font-size: 22px;
  }
  table {
    font-size: 16px;
  }
  code {
    font-size: 14px;
  }
  .critical { color: #dc2626; font-weight: bold; }
  .high { color: #ea580c; font-weight: bold; }
  .medium { color: #ca8a04; font-weight: bold; }
  .low { color: #16a34a; font-weight: bold; }
---

# 🔒 Rokid Glasses 資訊安全漏洞評估報告

**評估日期：** 2026年1月30日  
**設備型號：** RG-glasses (RV101)  
**韌體版本：** 1.12.009-20260109-150201  
**報告編號：** RKID-SEC-2026-001

---

## 📊 風險等級摘要

| 風險等級               | 數量 | 說明                     |
| ---------------------- | ---- | ------------------------ |
| 🔴 **嚴重 (Critical)** | 3    | 需立即處理的重大安全風險 |
| 🟠 **高風險 (High)**   | 5    | 可能導致系統被入侵的漏洞 |
| 🟡 **中風險 (Medium)** | 6    | 需要關注的安全問題       |
| 🟢 **低風險 (Low)**    | 4    | 建議改善的安全配置       |

**總計發現：18 項安全問題**

---

# 🔴 嚴重風險 (Critical)

---

## CRIT-001: Bootloader 未鎖定

**風險等級：** 🔴 嚴重 | **CVSS：** 9.8

### 發現

```
ro.boot.flash.locked = 0
ro.boot.verifiedbootstate = orange
ro.boot.vbmeta.device_state = unlocked
```

### 影響

- ❌ 攻擊者可刷入惡意韌體
- ❌ 無法保證系統映像完整性
- ❌ 設備重置後可能保留後門
- ❌ 驗證開機 (Verified Boot) 保護失效

---

## CRIT-001: 修補建議

### 立即行動

1. ✅ 在量產設備上鎖定 Bootloader
2. ✅ 確保 OTA 更新驗證機制完整
3. ✅ 出廠前確認 Bootloader 已鎖定
4. ✅ 實施運行時完整性檢查

### 驗證命令

```bash
adb shell getprop ro.boot.flash.locked  # 應返回 1
adb shell getprop ro.boot.verifiedbootstate  # 應返回 green
```

---

## CRIT-002: 安全補丁過期 (超過 6 個月)

**風險等級：** 🔴 嚴重 | **CVSS：** 9.1

### 發現

```
ro.build.version.security_patch = 2024-07-05
當前日期: 2026-01-30
補丁落後: 約 19 個月
```

### 已知受影響的 CVE

| CVE ID         | 嚴重性   | 元件      | 描述         |
| -------------- | -------- | --------- | ------------ |
| CVE-2024-32896 | Critical | Framework | 權限提升     |
| CVE-2024-29748 | Critical | Kernel    | 遠端代碼執行 |
| CVE-2024-23706 | High     | Bluetooth | 資訊洩露     |

---

## CRIT-002: 修補建議

### 緊急措施

1. ✅ **立即發布** 包含最新安全補丁的 OTA 更新
2. ✅ **流程改進** - 建立每月安全補丁更新機制
3. ✅ **合規要求** - 安全補丁不應落後超過 90 天
4. ✅ **回歸測試** - 建立自動化安全測試流程

### 受影響元件

- Android 核心漏洞、Linux 核心漏洞
- Qualcomm 晶片組漏洞
- 藍牙協議棧漏洞、媒體框架漏洞

---

## CRIT-003: 網路服務暴露 (Port 8341)

**風險等級：** 🔴 嚴重 | **CVSS：** 8.6

### 發現

```
State: LISTEN
Local Address: 0.0.0.0:8341
Protocol: TCP
UID: 0 (root)
```

### 風險

- ❌ 允許未經授權的遠端存取
- ❌ 提供網路層攻擊面
- ❌ 可能被利用進行中間人攻擊
- ❌ 以 root 權限運行

---

## CRIT-003: 修補建議

### 立即措施

1. ✅ **識別服務** - 評估該服務的必要性
2. ✅ **網路限制** - 僅綁定到 localhost (127.0.0.1)
3. ✅ **認證機制** - 實施強認證和加密
4. ✅ **防火牆** - 預設阻擋非必要端口
5. ✅ **權限降級** - 避免以 root 權限運行

---

# 🟠 高風險 (High)

---

## HIGH-001: dmesg 未限制存取

**風險等級：** 🟠 高 | **CVSS：** 7.5

### 發現

```
/proc/sys/kernel/dmesg_restrict = 0
```

### 風險

任何應用程式都可讀取核心日誌，可能洩露：

- 記憶體地址資訊
- 載入的核心模組
- 硬體配置細節
- 安全相關事件

### 修補

設置 `kernel.dmesg_restrict = 1`

---

## HIGH-002: 特權應用過度授權

**風險等級：** 🟠 高 | **CVSS：** 7.8

### 發現 - `com.rokid.os.sprite.assistserver`

```
- android.permission.INJECT_EVENTS
- android.permission.READ_LOGS
- android.permission.WRITE_SECURE_SETTINGS
- android.permission.MASTER_CLEAR
- android.permission.INSTALL_PACKAGES
- android.permission.DELETE_PACKAGES
- android.permission.REBOOT
```

**該應用擁有超過 100 項系統權限！**

---

## HIGH-002: 修補建議

### 風險

如果該應用被入侵：

- ❌ 可完全控制設備
- ❌ 可存取所有使用者資料
- ❌ 可安裝惡意軟體
- ❌ 可監聽所有通訊

### 修補

1. ✅ **最小權限原則** - 移除非必要權限
2. ✅ **元件分離** - 拆分到多個低權限元件
3. ✅ **運行時檢查** - 敏感操作二次驗證
4. ✅ **程式碼審計** - 安全程式碼審查

---

## HIGH-003: 調試版應用已安裝

**風險等級：** 🟠 高 | **CVSS：** 7.2

### 發現

```
Package: com.cooper.wheellog.debug
Flags: [ DEBUGGABLE, HAS_CODE, TEST_ONLY, ALLOW_BACKUP ]
```

### 已授予的危險權限

- `ACCESS_BACKGROUND_LOCATION`
- `ACCESS_FINE_LOCATION`
- `BLUETOOTH_SCAN/CONNECT/ADVERTISE`
- `READ_EXTERNAL_STORAGE`

### 修補

卸載所有 debuggable 和 test-only 應用

---

## HIGH-004: 未知來源安裝已啟用

**風險等級：** 🟠 高 | **CVSS：** 6.8

### 發現

```
install_non_market_apps = 1
unknown_sources_default_reversed = 1
```

### 風險

- ❌ 惡意軟體安裝風險
- ❌ 社交工程攻擊面
- ❌ 繞過 Google Play 保護機制

### 修補

1. 預設禁用未知來源安裝
2. 實施應用程式白名單機制

---

## HIGH-005: ADB 持久啟用

**風險等級：** 🟠 高 | **CVSS：** 6.5

### 發現

```
persist.sys.usb.config = adb
persist.vendor.adb = true
adb_enabled = 1
```

### 風險

- ❌ 物理存取攻擊
- ❌ 授權憑證可能被盜
- ❌ 惡意配件利用

### 修補

生產版本預設禁用 ADB，實施連線逾時

---

# 🟡 中風險 (Medium)

---

## MED-001 ~ MED-003

### MED-001: 螢幕鎖定已禁用 (CVSS 5.5)

```
lockscreen.disabled = 1
```

**修補：** 評估眼鏡設備鎖定機制可行性

### MED-002: NetworkSecurityConfig 未配置 (CVSS 5.3)

多個應用使用預設網路安全設定
**修補：** 強制所有應用實施 NetworkSecurityConfig

### MED-003: SELinux 標籤錯誤 (CVSS 5.0)

```
Could not start service 'vendor.face-default': incorrect label
```

**修補：** 修正 SELinux 策略檔案

---

## MED-004 ~ MED-006

### MED-004: 第三方支付應用權限過多 (CVSS 5.5)

支付寶眼鏡版請求敏感權限：

- MANAGE_EXTERNAL_STORAGE
- CAMERA
- START_ACTIVITIES_FROM_BACKGROUND

### MED-005: 藍牙 SCRAM 未啟用 (CVSS 4.8)

```
persist.vendor.qcom.bluetooth.scram.enabled = false
```

### MED-006: 相機隱私 LED 可被禁用 (CVSS 4.6)

存在 `CAMERA_DISABLE_TRANSMIT_LED` 權限

---

# 🟢 低風險 (Low)

---

## LOW-001 ~ LOW-004

| ID      | 風險               | 發現                     | CVSS |
| ------- | ------------------ | ------------------------ | ---- |
| LOW-001 | 開發檔案殘留       | /data/local/tmp/.studio/ | 3.3  |
| LOW-002 | 範例應用程式已安裝 | com.example.\* 應用      | 3.1  |
| LOW-003 | 位置模擬可配置     | location_changer = 1     | 2.8  |
| LOW-004 | 電池優化白名單過寬 | 多個應用可背景運行       | 2.5  |

### 修補建議

- 出廠前清理開發檔案
- 移除所有範例應用
- 強制禁用位置模擬

---

# 🛡️ 安全強化項目 (已實施)

---

## ✅ 已正確配置的安全機制

| 項目           | 狀態           | 說明               |
| -------------- | -------------- | ------------------ |
| SELinux        | ✅ Enforcing   | 強制存取控制已啟用 |
| ASLR           | ✅ 完整 (值=2) | 地址空間隨機化     |
| Kernel Pointer | ✅ 啟用 (值=2) | 核心指針已隱藏     |
| Perf Event     | ✅ 最高 (值=3) | 效能監控已限制     |
| FBE            | ✅ 啟用        | 檔案加密已實施     |
| ADB 認證       | ✅ 啟用        | 需要 RSA 金鑰授權  |
| Root 存取      | ✅ 無          | 未發現 su/magisk   |
| ro.debuggable  | ✅ 禁用        | 系統調試已禁用     |

---

# 📋 修補優先級建議

---

## 第一優先 (7 天內)

| ID       | 風險    | 修補項目                  |
| -------- | ------- | ------------------------- |
| CRIT-001 | 🔴 嚴重 | 鎖定 Bootloader           |
| CRIT-002 | 🔴 嚴重 | 發布安全補丁更新          |
| CRIT-003 | 🔴 嚴重 | 限制或移除 Port 8341 服務 |

---

## 第二優先 (30 天內)

| ID       | 風險  | 修補項目               |
| -------- | ----- | ---------------------- |
| HIGH-001 | 🟠 高 | 限制 dmesg 存取        |
| HIGH-002 | 🟠 高 | 審查並縮減助手服務權限 |
| HIGH-003 | 🟠 高 | 移除調試版應用         |
| HIGH-004 | 🟠 高 | 預設禁用未知來源安裝   |
| HIGH-005 | 🟠 高 | 預設禁用 ADB           |

---

## 第三優先 (90 天內)

| ID                | 風險  | 修補項目           |
| ----------------- | ----- | ------------------ |
| MED-001 ~ MED-006 | 🟡 中 | 實施各項中風險修補 |
| LOW-001 ~ LOW-004 | 🟢 低 | 清理和強化配置     |

---

# 📊 合規性評估

---

## 合規狀態

| 標準            | 狀態        | 問題                       |
| --------------- | ----------- | -------------------------- |
| **GDPR**        | ⚠️ 部分合規 | 相機/位置存取需明確同意    |
| **PCI DSS**     | ❌ 不合規   | 支付應用權限過多、補丁過期 |
| **HIPAA**       | ⚠️ 需評估   | 醫療場景需額外保護         |
| **Android CDD** | ⚠️ 部分合規 | 補丁落後超過 90 天         |

---

# 🔧 建議的安全測試

---

## 後續測試項目

1. **滲透測試**
   - 對 Port 8341 服務進行完整滲透測試

2. **藍牙安全**
   - 測試藍牙協議棧的已知漏洞

3. **應用程式審計**
   - 對 Rokid 專屬應用進行靜態和動態分析

4. **韌體分析**
   - 提取並分析韌體中的硬編碼憑證

5. **隱私影響評估**
   - 評估相機/麥克風的隱私風險

---

# 📝 附錄

---

## A. 使用的掃描工具和命令

```bash
# 系統屬性檢查
adb shell getprop | grep -E "secure|debug|verify|crypto"

# 網路端口掃描
adb shell ss -tlnp

# 權限審計
adb shell dumpsys package <package_name>

# SELinux 狀態
adb shell getenforce

# 核心安全參數
adb shell cat /proc/sys/kernel/dmesg_restrict
adb shell cat /proc/sys/kernel/randomize_va_space
```

---

## B. 參考資料

- Android Security Bulletin
  https://source.android.com/security/bulletin
- CIS Android Benchmark
- OWASP Mobile Security Testing Guide
- NIST SP 800-163: Vetting Mobile Applications

---

# 🔒 報告結束

**報告準備者：** 資安評估專家  
**審核狀態：** 待審核  
**機密等級：** 內部使用

---

_本報告僅供 Rokid 內部安全評估使用，請勿外流。_
