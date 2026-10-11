# Complete Analysis Report: Rokid OTA API Authentication Mechanism

> 中文版：[OTA_Authentication_Analysis_Report.md](OTA_Authentication_Analysis_Report.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; it decides any conflict).


**Date**: 2026-02-11  
**Status**: ✅ **authentication mechanism completely broken**

---

## 1. Authentication Mechanism Overview

### 1.1 Key Classes and Components

| Class name     | Path                                         | Function         |
| -------------- | -------------------------------------------- | ---------------- |
| `CheckService` | `com.rokid.glass.ota.component.CheckService` | Main OTA check service class |
| `DeviceProp`   | `com.rokid.glass.ota.basic.bean.DeviceProp`  | Device property configuration |
| `MD5Utils`     | `com.rokid.glass.ota.utils.MD5Utils`         | MD5 hash utility |
| `DeviceUtil`   | `com.rokid.glass.ota.basic.util.DeviceUtil`  | Device version retrieval |

### 1.2 Authentication Flow

```
1. Obtain a Unix timestamp (seconds)
2. Build the signature string
3. Compute the MD5 hash
4. Package the Authorization Header
5. Build the JSON request body
6. POST to the OTA endpoint
```

---

## 2. Signature Generation Algorithm

### 2.1 Signature String Format (CheckService.generateSignature)

```java
"key=" + DeviceProp.key +
"&device_type_id=" + DeviceProp.deviceTypeId +
"&device_id=" + DeviceProp.deviceId +
"&service=" + DeviceProp.service +
"&version=" + DeviceProp.version +
"&time=" + timestamp +
"&secret=" + DeviceProp.secret
```

### 2.2 Actual Example

```
key=<REDACTED_OTA_KEY>&device_type_id=<REDACTED_OTA_DEVICETYPEID>&device_id=<DEVICE_SERIAL>&service=ota&version=1.0&time=1770775144&secret=<REDACTED_OTA_SECRET>
```

### 2.3 Signature Computation

```python
sign = MD5(signature_string).hexdigest()  # all lowercase
# e.g.: 99e8c789bd36d2150f32241619babf1f
```

---

## 3. Authorization Header Format

### 3.1 Format (CheckService.packageAuthorization)

```
version={version};time={timestamp};sign={md5_sign};key={key};device_type_id={deviceTypeId};device_id={deviceId};service={service}
```

### 3.2 Actual Example

```
version=1.0;time=1770775144;sign=99e8c789bd36d2150f32241619babf1f;key=<REDACTED_OTA_KEY>;device_type_id=<REDACTED_OTA_DEVICETYPEID>;device_id=<DEVICE_SERIAL>;service=ota
```

---

## 4. Obtaining Device Parameters

### 4.1 System Property Sources (DeviceProp.init)

| Parameter        | System property          | Value on this device               |
| ---------------- | ------------------------ | ---------------------------------- |
| `key`            | `ro.boot.key`            | `<REDACTED_OTA_KEY>` |
| `secret`         | `ro.boot.secret`         | `<REDACTED_OTA_SECRET>` |
| `device_type_id` | `ro.boot.devicetypeid`   | `<REDACTED_OTA_DEVICETYPEID>` |
| `device_id`      | `ro.serialno`            | `<DEVICE_SERIAL>`                 |
| `version`        | (fixed value)            | `1.0`                              |
| `service`        | (fixed value)            | `ota`                              |
| `check_url`      | `ro.rokid.ota.check_url` | `https://ota.rokid.com`            |
| `check_api`      | `ro.rokid.ota.check_api` | `/v1/extended/ota/check`           |

### 4.2 Obtaining the System Version (DeviceUtil.getSystemVersion)

```java
// Parsed from Build.FINGERPRINT
// Fingerprint: Rokid/glasses/glasses:12/SKQ1.240613.001/1.12.009-20260109-150201:user/release-keys
String[] split = Build.FINGERPRINT.split(":")[1].split("/");
String version = split[split.length - 1];
// Result: 1.12.009-20260109-150201
```

---

## 5. API Request Specification

### 5.1 Endpoint

```
POST https://ota.rokid.com/v1/extended/ota/check
```

### 5.2 Request Headers

```http
Content-Type: application/json;charset=utf-8
Authorization: version=1.0;time={timestamp};sign={md5};key=...;device_type_id=...;device_id=...;service=ota
User-Agent: okhttp/4.9.3
```

### 5.3 Request Body (JSON)

```json
{
  "version": "1.12.009-20260109-150201",
  "osType": "",
  "cpuType": ""
}
```

---

## 6. API Response Parsing

### 6.1 Success Response (update available)

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

### 6.2 No-Update Response

```json
{
  "code": "NO_IMAGE",
  "errorMessage": "..."
}
```

### 6.3 Authentication-Failure Response

```json
{
  "success": false,
  "code": "C100401",
  "message": "认证不通过"
}
```

---

## 7. Complete Authentication Script

See `rokid_ota_auth.py`

### 7.1 Core Implementation

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

## 8. Firmware Information Obtained

| Item           | Value                                                                                                               |
| -------------- | ------------------------------------------------------------------------------------------------------------------- |
| **Latest version** | `1.14.006-20260204-150203`                                                                                     |
| **Download URL**   | `https://ota-g.rokidcdn.com/YodaOS-Sprite/OS/Rokid/1.14.006-20260204-150203/RVE01-ota-1.14.006-20260204-150203.zip` |
| **MD5 checksum**   | `0f52a85e7a25704e7a2e5cf679a32b3a`                                                                             |
| **Forced update**  | No                                                                                                             |
| **Update type**    | A/B OTA (payload.bin)                                                                                          |

---

## 9. Security Findings

### 9.1 Authentication Weaknesses

1. **Symmetric key exposure**: `key` and `secret` are stored in device system properties and can be read over ADB
2. **Simple MD5 signature**: a plain MD5 hash is used, with no HMAC or asymmetric signature
3. **No nonce**: the timestamp provides some replay protection, but there is no server-side nonce
4. **Device-identity disclosure**: device_id and device_type_id can be extracted

### 9.2 Why Earlier Attempts Failed

Reasons earlier attempts failed:

1. The two key parameters `ro.boot.key` and `ro.boot.secret` were missing
2. The authentication parameters were placed in the request body instead of the header
3. The signature string format was in the wrong order
4. The version number format was incorrect (the full three-part form is required: `x.xx.xxx-yyyymmdd-hhmmss`)

---

## 10. Next Steps

1. ✅ Download the firmware ZIP file
2. ✅ Extract payload.bin
3. ✅ Extract boot.img with payload-dumper
4. ✅ Patch boot.img with Magisk
5. ~~⏳ Flash the patched boot.img via RPi4~~ → **2026-09-13: this step was never feasible (the bootloader removes flash/boot/erase, and boot.img cannot be obtained)**

---

**Report completion date**: 2026-02-11 02:00
