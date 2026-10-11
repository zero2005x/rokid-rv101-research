# Rokid OTA API 認證機制完整分析報告
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


**日期**: 2026-02-11  
**狀態**: ✅ **認證機制完全破解**

---

## 1. 認證機制概覽

### 1.1 關鍵類與組件

| 類名           | 路徑                                         | 功能             |
| -------------- | -------------------------------------------- | ---------------- |
| `CheckService` | `com.rokid.glass.ota.component.CheckService` | OTA 檢查服務主類 |
| `DeviceProp`   | `com.rokid.glass.ota.basic.bean.DeviceProp`  | 設備屬性配置     |
| `MD5Utils`     | `com.rokid.glass.ota.utils.MD5Utils`         | MD5 哈希工具     |
| `DeviceUtil`   | `com.rokid.glass.ota.basic.util.DeviceUtil`  | 設備版本獲取     |

### 1.2 認證流程

```
1. 獲取 Unix 時間戳 (秒)
2. 構建簽名字符串
3. 計算 MD5 哈希
4. 打包 Authorization Header
5. 構建 JSON 請求體
6. POST 到 OTA 端點
```

---

## 2. 簽名生成算法

### 2.1 簽名字符串格式 (CheckService.generateSignature)

```java
"key=" + DeviceProp.key +
"&device_type_id=" + DeviceProp.deviceTypeId +
"&device_id=" + DeviceProp.deviceId +
"&service=" + DeviceProp.service +
"&version=" + DeviceProp.version +
"&time=" + timestamp +
"&secret=" + DeviceProp.secret
```

### 2.2 實際範例

```
key=<REDACTED_OTA_KEY>&device_type_id=<REDACTED_OTA_DEVICETYPEID>&device_id=<DEVICE_SERIAL>&service=ota&version=1.0&time=1770775144&secret=<REDACTED_OTA_SECRET>
```

### 2.3 簽名計算

```python
sign = MD5(signature_string).hexdigest()  # 全小寫
# 例: 99e8c789bd36d2150f32241619babf1f
```

---

## 3. Authorization Header 格式

### 3.1 格式 (CheckService.packageAuthorization)

```
version={version};time={timestamp};sign={md5_sign};key={key};device_type_id={deviceTypeId};device_id={deviceId};service={service}
```

### 3.2 實際範例

```
version=1.0;time=1770775144;sign=99e8c789bd36d2150f32241619babf1f;key=<REDACTED_OTA_KEY>;device_type_id=<REDACTED_OTA_DEVICETYPEID>;device_id=<DEVICE_SERIAL>;service=ota
```

---

## 4. 設備參數獲取

### 4.1 系統屬性來源 (DeviceProp.init)

| 參數             | 系統屬性                 | 本設備值                           |
| ---------------- | ------------------------ | ---------------------------------- |
| `key`            | `ro.boot.key`            | `<REDACTED_OTA_KEY>` |
| `secret`         | `ro.boot.secret`         | `<REDACTED_OTA_SECRET>` |
| `device_type_id` | `ro.boot.devicetypeid`   | `<REDACTED_OTA_DEVICETYPEID>` |
| `device_id`      | `ro.serialno`            | `<DEVICE_SERIAL>`                 |
| `version`        | (固定值)                 | `1.0`                              |
| `service`        | (固定值)                 | `ota`                              |
| `check_url`      | `ro.rokid.ota.check_url` | `https://ota.rokid.com`            |
| `check_api`      | `ro.rokid.ota.check_api` | `/v1/extended/ota/check`           |

### 4.2 系統版本獲取 (DeviceUtil.getSystemVersion)

```java
// 從 Build.FINGERPRINT 解析
// Fingerprint: Rokid/glasses/glasses:12/SKQ1.240613.001/1.12.009-20260109-150201:user/release-keys
String[] split = Build.FINGERPRINT.split(":")[1].split("/");
String version = split[split.length - 1];
// 結果: 1.12.009-20260109-150201
```

---

## 5. API 請求規範

### 5.1 端點

```
POST https://ota.rokid.com/v1/extended/ota/check
```

### 5.2 請求頭

```http
Content-Type: application/json;charset=utf-8
Authorization: version=1.0;time={timestamp};sign={md5};key=...;device_type_id=...;device_id=...;service=ota
User-Agent: okhttp/4.9.3
```

### 5.3 請求體 (JSON)

```json
{
  "version": "1.12.009-20260109-150201",
  "osType": "",
  "cpuType": ""
}
```

---

## 6. API 響應解析

### 6.1 成功響應 (有更新)

```json
{
  "code": "OK",
  "authorize": true,
  "version": "1.14.006-20260204-150203",
  "imageUrl": "https://ota-g.rokidcdn.com/YodaOS-Sprite/OS/Rokid/1.14.006-20260204-150203/RVE01-ota-1.14.006-20260204-150203.zip",
  "checksum": "0f52a85e7a25704e7a2e5cf679a32b3a",
  "isForceUpdate": false,
  "changelog": "...",
  "packageChoice": "1"
}
```

### 6.2 無更新響應

```json
{
  "code": "NO_IMAGE",
  "errorMessage": "..."
}
```

### 6.3 認證失敗響應

```json
{
  "success": false,
  "code": "C100401",
  "message": "认证不通过"
}
```

---

## 7. 完整認證腳本

參見 `rokid_ota_auth.py`

### 7.1 核心實現

```python
import hashlib
import time
import requests
import json

def md5_hash(s):
    return hashlib.md5(s.encode('utf-8')).hexdigest()

def generate_signature(key, device_type_id, device_id, service, version, timestamp, secret):
    sig_string = (
        f"key={key}"
        f"&device_type_id={device_type_id}"
        f"&device_id={device_id}"
        f"&service={service}"
        f"&version={version}"
        f"&time={timestamp}"
        f"&secret={secret}"
    )
    return md5_hash(sig_string)

def package_authorization(version, timestamp, sign, key, device_type_id, device_id, service):
    return (
        f"version={version}"
        f";time={timestamp}"
        f";sign={sign}"
        f";key={key}"
        f";device_type_id={device_type_id}"
        f";device_id={device_id}"
        f";service={service}"
    )
```

---

## 8. 已獲取的固件資訊

| 項目           | 值                                                                                                                  |
| -------------- | ------------------------------------------------------------------------------------------------------------------- |
| **最新版本**   | `1.14.006-20260204-150203`                                                                                          |
| **下載 URL**   | `https://ota-g.rokidcdn.com/YodaOS-Sprite/OS/Rokid/1.14.006-20260204-150203/RVE01-ota-1.14.006-20260204-150203.zip` |
| **MD5 校驗和** | `0f52a85e7a25704e7a2e5cf679a32b3a`                                                                                  |
| **強制更新**   | No                                                                                                                  |
| **更新類型**   | A/B OTA (payload.bin)                                                                                               |

---

## 9. 安全發現

### 9.1 認證弱點

1. **對稱金鑰暴露**: `key` 和 `secret` 儲存在設備系統屬性中，可通過 ADB 讀取
2. **簡單 MD5 簽名**: 使用簡單的 MD5 哈希，無 HMAC 或非對稱簽名
3. **無 nonce**: 時間戳提供一定的重放保護，但無服務端 nonce
4. **設備識別洩露**: device_id 和 device_type_id 可被提取

### 9.2 為何之前嘗試失敗

之前的嘗試失敗原因：

1. 缺少 `ro.boot.key` 和 `ro.boot.secret` 這兩個關鍵參數
2. 嘗試在請求體中放置認證參數，而非 Header
3. 簽名字符串格式順序錯誤
4. 版本號格式不正確（需要完整三段式：`x.xx.xxx-yyyymmdd-hhmmss`）

---

## 10. 下一步

1. ✅ 下載固件 ZIP 檔案
2. ✅ 提取 payload.bin
3. ✅ 使用 payload-dumper 提取 boot.img
4. ✅ 使用 Magisk 修補 boot.img
5. ~~⏳ 通過 RPi4 刷入 patched boot.img~~ → **2026-09-13：此步從未可行（bootloader 移除 flash/boot/erase，且取不到 boot.img）**

---

**報告完成日期**: 2026-02-11 02:00
