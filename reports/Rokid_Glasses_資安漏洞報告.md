# Rokid Glasses 資訊安全漏洞評估報告

> English: [Rokid_Glasses_資安漏洞報告_EN.md](Rokid_Glasses_%E8%B3%87%E5%AE%89%E6%BC%8F%E6%B4%9E%E5%A0%B1%E5%91%8A_EN.md)

> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


**評估日期：** 2026年1月30日  
**設備型號：** RG-glasses (RV101)  
**韌體版本：** 1.12.009-20260109-150201  
**Android 版本：** 12 (API 32)  
**評估類型：** ADB 遠端安全掃描  
**報告編號：** RKID-SEC-2026-001

---

## 📊 風險等級摘要

| 風險等級               | 數量 | 說明                     |
| ---------------------- | ---- | ------------------------ |
| 🔴 **嚴重 (Critical)** | 3    | 需立即處理的重大安全風險 |
| 🟠 **高風險 (High)**   | 5    | 可能導致系統被入侵的漏洞 |
| 🟡 **中風險 (Medium)** | 6    | 需要關注的安全問題       |
| 🟢 **低風險 (Low)**    | 4    | 建議改善的安全配置       |

---

## 🔴 嚴重風險 (Critical)

### CRIT-001: Bootloader 未鎖定

**風險等級：** 🔴 嚴重  
**CVSS 評分：** 9.8 (Critical)  
**影響範圍：** 設備完整性、資料保護

#### 發現

```
ro.boot.flash.locked = 0
ro.boot.verifiedbootstate = orange
ro.boot.vbmeta.device_state = unlocked
```

#### 描述

設備的 Bootloader 處於解鎖狀態，驗證開機狀態為「orange」（表示自訂韌體）。這意味著：

- 攻擊者可以刷入惡意韌體
- 無法保證系統映像的完整性
- 即使設備重置也可能保留後門
- 驗證開機 (Verified Boot) 保護失效

#### 修補建議

1. **立即行動：** 在量產設備上鎖定 Bootloader
2. **韌體更新：** 確保 OTA 更新驗證機制完整
3. **製程改進：** 在出廠前確認 Bootloader 已鎖定
4. **監控機制：** 實施運行時完整性檢查

#### 驗證命令

```bash
adb shell getprop ro.boot.flash.locked  # 應返回 1
adb shell getprop ro.boot.verifiedbootstate  # 應返回 green
```

---

### CRIT-002: 安全補丁過期 (超過 6 個月)

**風險等級：** 🔴 嚴重  
**CVSS 評分：** 9.1 (Critical)  
**影響範圍：** 系統所有元件

#### 發現

```
ro.build.version.security_patch = 2024-07-05
當前日期: 2026-01-30
補丁落後: 約 19 個月
```

#### 描述

設備安全補丁級別為 2024-07-05，距今已超過 19 個月。期間 Google 已發布多個關鍵安全修補，包括：

- Android 核心漏洞 (CVE-2024-xxxxx 系列)
- Linux 核心漏洞 (核心版本 5.10.209)
- Qualcomm 晶片組漏洞
- 藍牙協議棧漏洞
- 媒體框架漏洞

#### 已知受影響的 CVE (部分列表)

| CVE ID         | 嚴重性   | 元件      | 描述         |
| -------------- | -------- | --------- | ------------ |
| CVE-2024-32896 | Critical | Framework | 權限提升漏洞 |
| CVE-2024-29748 | Critical | Kernel    | 遠端代碼執行 |
| CVE-2024-23706 | High     | Bluetooth | 資訊洩露     |
| CVE-2024-23707 | High     | System    | 權限繞過     |

#### 修補建議

1. **緊急：** 立即發布包含最新安全補丁的 OTA 更新
2. **流程改進：** 建立每月安全補丁更新機制
3. **合規要求：** 安全補丁不應落後超過 90 天
4. **回歸測試：** 建立自動化安全測試流程

---

### CRIT-003: 網路服務暴露 (Port 8341)

**風險等級：** 🔴 嚴重  
**CVSS 評分：** 8.6 (High)  
**影響範圍：** 網路安全、遠端存取

#### 發現

```
State: LISTEN
Local Address: 0.0.0.0:8341
Protocol: TCP
UID: 0 (root)
```

#### 描述

設備在所有網路介面 (0.0.0.0) 上監聽 TCP 端口 8341，且以 root 權限運行。這可能：

- 允許未經授權的遠端存取
- 提供攻擊面給網路層攻擊
- 被利用進行中間人攻擊
- 該服務可能是 Rokid 專屬的串流或控制服務

#### 修補建議

1. **立即：** 識別並評估該服務的必要性
2. **網路限制：** 僅綁定到 localhost (127.0.0.1) 或必要介面
3. **認證機制：** 實施強認證和加密
4. **防火牆：** 預設阻擋非必要端口
5. **權限降級：** 避免以 root 權限運行服務

---

## 🟠 高風險 (High)

### HIGH-001: dmesg 未限制存取

**風險等級：** 🟠 高  
**CVSS 評分：** 7.5 (High)  
**影響範圍：** 資訊洩露

#### 發現

```
/proc/sys/kernel/dmesg_restrict = 0
```

#### 描述

任何應用程式都可以讀取核心日誌 (dmesg)，可能洩露：

- 記憶體地址資訊
- 載入的核心模組
- 硬體配置細節
- 安全相關事件
- 潛在的漏洞利用資訊

#### 修補建議

1. 設置 `kernel.dmesg_restrict = 1`
2. 在 `/system/etc/sysctl.conf` 或 init 腳本中配置

---

### HIGH-002: 特權應用過度授權

**風險等級：** 🟠 高  
**CVSS 評分：** 7.8 (High)  
**影響範圍：** 權限控制

#### 發現

`com.rokid.os.sprite.assistserver` 擁有以下高危權限：

```
- android.permission.INJECT_EVENTS (模擬輸入事件)
- android.permission.READ_LOGS (讀取系統日誌)
- android.permission.WRITE_SECURE_SETTINGS (修改安全設定)
- android.permission.MASTER_CLEAR (恢復原廠設定)
- android.permission.INSTALL_PACKAGES (安裝應用)
- android.permission.DELETE_PACKAGES (刪除應用)
- android.permission.BLUETOOTH_PRIVILEGED (藍牙特權)
- android.permission.MODIFY_PHONE_STATE (修改電話狀態)
- android.permission.MANAGE_USERS (管理使用者)
- android.permission.REBOOT (重新啟動設備)
```

#### 描述

Rokid 助手服務擁有超過 100 項系統權限，形成單點故障風險。如果該應用被入侵：

- 可完全控制設備
- 可存取所有使用者資料
- 可安裝惡意軟體
- 可監聽所有通訊

#### 修補建議

1. **最小權限原則：** 審查並移除非必要權限
2. **元件分離：** 將功能拆分到多個較低權限的元件
3. **運行時檢查：** 實施敏感操作的二次驗證
4. **程式碼審計：** 對該應用進行安全程式碼審查

---

### HIGH-003: 調試版應用已安裝

**風險等級：** 🟠 高  
**CVSS 評分：** 7.2 (High)  
**影響範圍：** 應用程式安全

#### 發現

```
Package: com.cooper.wheellog.debug
Flags: [ DEBUGGABLE, HAS_CODE, TEST_ONLY, ALLOW_BACKUP ]
```

已授予的危險權限：

- `ACCESS_BACKGROUND_LOCATION` (背景位置存取)
- `ACCESS_FINE_LOCATION` (精確位置)
- `BLUETOOTH_SCAN/CONNECT/ADVERTISE` (完整藍牙存取)
- `READ_EXTERNAL_STORAGE` (讀取儲存空間)

#### 描述

設備上安裝了具有 `DEBUGGABLE` 和 `TEST_ONLY` 標誌的應用程式。這些應用：

- 可被調試器附加
- 可能包含額外的日誌記錄
- 可能存在測試後門
- 不應出現在生產設備上

#### 修補建議

1. **移除：** 卸載所有 debuggable 和 test-only 應用
2. **政策：** 禁止在生產環境安裝調試版應用
3. **掃描：** OTA 前自動檢測調試版應用

---

### HIGH-004: 未知來源安裝已啟用

**風險等級：** 🟠 高  
**CVSS 評分：** 6.8 (High)  
**影響範圍：** 應用程式安全

#### 發現

```
install_non_market_apps = 1
unknown_sources_default_reversed = 1
```

#### 描述

設備允許從未知來源安裝應用程式，增加了：

- 惡意軟體安裝風險
- 社交工程攻擊面
- 繞過 Google Play 保護機制

#### 修補建議

1. 預設禁用未知來源安裝
2. 實施應用程式白名單機制
3. 要求企業憑證簽章的應用才能安裝

---

### HIGH-005: ADB 持久啟用

**風險等級：** 🟠 高  
**CVSS 評分：** 6.5 (High)  
**影響範圍：** 設備存取控制

#### 發現

```
persist.sys.usb.config = adb
persist.vendor.adb = true
adb_enabled = 1
sys.usb.config = adb
```

#### 描述

ADB (Android Debug Bridge) 預設啟用且持久化。雖然 `ro.adb.secure=1`（需要授權），但仍存在風險：

- 物理存取攻擊
- 授權設備的憑證可能被盜
- 可能被惡意配件利用

#### 修補建議

1. **生產版本：** 預設禁用 ADB
2. **條件啟用：** 僅在開發者選項啟用時開放
3. **逾時機制：** 實施 ADB 連線逾時
4. **日誌記錄：** 記錄所有 ADB 連線事件

---

## 🟡 中風險 (Medium)

### MED-001: 螢幕鎖定已禁用

**風險等級：** 🟡 中  
**CVSS 評分：** 5.5 (Medium)

#### 發現

```
lockscreen.disabled = 1
```

#### 描述

設備未配置螢幕鎖定，任何人都可直接存取設備。對於具有相機和麥克風的穿戴設備，這增加了隱私風險。

#### 修補建議

1. 評估眼鏡設備的鎖定機制可行性
2. 考慮替代方案如藍牙配對鎖定
3. 實施佩戴檢測鎖定

---

### MED-002: NetworkSecurityConfig 未配置

**風險等級：** 🟡 中  
**CVSS 評分：** 5.3 (Medium)

#### 發現

```
Log: No Network Security Config specified, using platform default
受影響應用: 多個第三方應用
```

#### 描述

多個應用程式未配置自訂網路安全策略，使用預設設定。這可能：

- 允許明文 HTTP 流量
- 接受無效憑證
- 缺乏憑證釘選

#### 修補建議

1. 強制所有應用實施 NetworkSecurityConfig
2. 預設阻擋明文 HTTP
3. 實施憑證釘選

---

### MED-003: SELinux 標籤錯誤

**風險等級：** 🟡 中  
**CVSS 評分：** 5.0 (Medium)

#### 發現

```
Could not start service 'vendor.face-default': incorrect label
Could not start service 'qguard': incorrect label (u:object_r:vendor_file:s0)
```

#### 描述

部分廠商服務的 SELinux 標籤配置錯誤，導致服務無法正確啟動或可能存在權限問題。

#### 修補建議

1. 修正 SELinux 策略檔案
2. 為所有廠商服務定義正確的 domain transition
3. 執行完整的 SELinux 政策審計

---

### MED-004: 第三方支付應用權限過多

**風險等級：** 🟡 中  
**CVSS 評分：** 5.5 (Medium)

#### 發現

支付寶眼鏡版 (`com.eg.android.AlipayGGlasses`) 請求的敏感權限：

```
- MANAGE_EXTERNAL_STORAGE (完整儲存存取)
- SYSTEM_ALERT_WINDOW (懸浮視窗，被拒絕)
- WRITE_SECURE_SETTINGS (修改安全設定，被拒絕)
- CAMERA (相機存取)
- READ_PRIVILEGED_PHONE_STATE (特權電話狀態)
- START_ACTIVITIES_FROM_BACKGROUND (背景啟動)
```

#### 描述

支付類應用擁有過多權限，增加了金融資料洩露風險。

#### 修補建議

1. 與支付寶合作最小化權限需求
2. 實施支付操作的額外驗證
3. 隔離支付應用的資料存取

---

### MED-005: 藍牙 SCRAM 未啟用

**風險等級：** 🟡 中  
**CVSS 評分：** 4.8 (Medium)

#### 發現

```
persist.vendor.qcom.bluetooth.scram.enabled = false
```

#### 描述

藍牙 SCRAM (Secure Connection Required for Audio/HID Mode) 未啟用，可能允許較弱的藍牙連線安全性。

#### 修補建議

1. 啟用 SCRAM 強制安全連線
2. 更新藍牙韌體至最新版本
3. 實施藍牙配對驗證

---

### MED-006: 相機隱私 LED 可被禁用

**風險等級：** 🟡 中  
**CVSS 評分：** 4.6 (Medium)

#### 發現

```
權限: android.permission.CAMERA_DISABLE_TRANSMIT_LED (存在於系統中)
vendor.rkd.camera.led.enable = 1 (目前啟用)
```

#### 描述

系統存在禁用相機 LED 指示燈的權限，如果被惡意應用獲取，可能在用戶不知情下錄影。

#### 修補建議

1. 移除或嚴格限制 `CAMERA_DISABLE_TRANSMIT_LED` 權限
2. 硬體層級強制 LED 開啟
3. 增加軟體層級的指示器

---

## 🟢 低風險 (Low)

### LOW-001: 開發檔案殘留

**風險等級：** 🟢 低  
**CVSS 評分：** 3.3 (Low)

#### 發現

```
/data/local/tmp/.studio/ (Android Studio 調試檔案)
```

#### 描述

設備上殘留開發工具的臨時檔案，可能洩露開發資訊。

#### 修補建議

1. 出廠前清理開發檔案
2. 定期清理臨時目錄

---

### LOW-002: 範例應用程式已安裝

**風險等級：** 🟢 低  
**CVSS 評分：** 3.1 (Low)

#### 發現

```
com.example.rokidglasses
com.example.wifi
```

#### 描述

設備上安裝了以 "com.example" 開頭的應用，這通常是範例或測試應用。

#### 修補建議

1. 移除所有非正式發布的應用
2. 建立應用白名單制度

---

### LOW-003: 位置模擬已禁用但可配置

**風險等級：** 🟢 低  
**CVSS 評分：** 2.8 (Low)

#### 發現

```
mock_location = 0
location_changer = 1
```

#### 描述

雖然位置模擬目前禁用，但 `location_changer` 設定可能允許重新配置。

#### 修補建議

1. 強制禁用位置模擬
2. 移除相關設定選項

---

### LOW-004: 電池優化白名單可能過寬

**風險等級：** 🟢 低  
**CVSS 評分：** 2.5 (Low)

#### 發現

多個系統應用在電池優化白名單中，可持續在背景運行。

#### 修補建議

1. 審查白名單必要性
2. 實施細粒度的背景活動控制

---

## 🛡️ 安全強化項目 (已實施)

以下安全機制已正確配置：

| 項目                       | 狀態           | 說明                     |
| -------------------------- | -------------- | ------------------------ |
| SELinux                    | Enforcing（開機）→ **Permissive（提權後）** | 強制存取控制已啟用；**2026-09-13 更正** |
| ASLR                       | ✅ 完整 (值=2) | 地址空間配置隨機化已啟用 |
| Kernel Pointer Restriction | ✅ 啟用 (值=2) | 核心指針已隱藏           |
| Perf Event Paranoid        | ✅ 最高 (值=3) | 效能監控已限制           |
| File-Based Encryption      | ✅ 啟用        | 檔案加密已實施           |
| ADB 認證                   | ✅ 啟用        | 需要 RSA 金鑰授權        |
| Root 存取                  | ✅ 無          | 未發現 su/magisk 二進制  |
| ro.debuggable              | ✅ 禁用 (值=0) | 系統調試已禁用           |
| ro.secure                  | ✅ 啟用 (值=1) | 安全模式已啟用           |
| KeyMint HAL                | ✅ 運行中      | 硬體金鑰管理已啟用       |

---

## 📋 修補優先級建議

### 第一優先 (7 天內)

| ID       | 風險 | 修補項目                  |
| -------- | ---- | ------------------------- |
| CRIT-001 | 嚴重 | 鎖定 Bootloader           |
| CRIT-002 | 嚴重 | 發布安全補丁更新          |
| CRIT-003 | 嚴重 | 限制或移除 Port 8341 服務 |

### 第二優先 (30 天內)

| ID       | 風險 | 修補項目               |
| -------- | ---- | ---------------------- |
| HIGH-001 | 高   | 限制 dmesg 存取        |
| HIGH-002 | 高   | 審查並縮減助手服務權限 |
| HIGH-003 | 高   | 移除調試版應用         |
| HIGH-004 | 高   | 預設禁用未知來源安裝   |
| HIGH-005 | 高   | 預設禁用 ADB           |

### 第三優先 (90 天內)

| ID                | 風險 | 修補項目           |
| ----------------- | ---- | ------------------ |
| MED-001 ~ MED-006 | 中   | 實施各項中風險修補 |
| LOW-001 ~ LOW-004 | 低   | 清理和強化配置     |

---

## 📊 合規性評估

| 標準            | 狀態        | 問題                           |
| --------------- | ----------- | ------------------------------ |
| **GDPR**        | ⚠️ 部分合規 | 相機/位置存取需要明確同意機制  |
| **PCI DSS**     | ❌ 不合規   | 支付應用權限過多、安全補丁過期 |
| **HIPAA**       | ⚠️ 需評估   | 如用於醫療場景需額外保護       |
| **Android CDD** | ⚠️ 部分合規 | 安全補丁落後超過 90 天限制     |

---

## 🔧 建議的安全測試

1. **滲透測試：** 對 Port 8341 服務進行完整滲透測試
2. **藍牙安全：** 測試藍牙協議棧的已知漏洞
3. **應用程式審計：** 對 Rokid 專屬應用進行靜態和動態分析
4. **韌體分析：** 提取並分析韌體中的硬編碼憑證
5. **隱私影響評估：** 評估相機/麥克風的隱私風險

---

## 📝 附錄

### A. 使用的掃描工具和命令

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

### B. 參考資料

- Android Security Bulletin: https://source.android.com/security/bulletin
- CIS Android Benchmark
- OWASP Mobile Security Testing Guide
- NIST SP 800-163: Vetting Mobile Applications

---

**報告準備者：** 資安評估專家  
**審核狀態：** 待審核  
**機密等級：** 內部使用

---

_本報告僅供 Rokid 內部安全評估使用，請勿外流。_
