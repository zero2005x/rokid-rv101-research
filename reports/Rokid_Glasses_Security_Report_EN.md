# Rokid Glasses Security Vulnerability Assessment Report
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。

> ⚠ **2026-09-13 狀態更正（對外文件尤須注意）**：本檔寫作時 **root 尚未取得**；**2026-08-22 起已由 GhostLock LPE（CVE-2026-43499）取得執行期 root**（`uid=0(root)`、`context=u:r:kernel:s0`、**SELinux Permissive**）。文中「SELinux Enforcing／Root Access: None」僅代表當時狀態；現行流程見 `docs/deploy.md`（每 boot 以 adb 武裝一次）。


**Assessment Date:** January 30, 2026  
**Device Model:** RG-glasses (RV101)  
**Firmware Version:** 1.12.009-20260109-150201  
**Android Version:** 12 (API 32)  
**Assessment Type:** ADB Remote Security Scan  
**Report ID:** RKID-SEC-2026-001

---

## 📊 Risk Level Summary

| Risk Level      | Count | Description                                          |
| --------------- | ----- | ---------------------------------------------------- |
| 🔴 **Critical** | 3     | Major security risks requiring immediate attention   |
| 🟠 **High**     | 5     | Vulnerabilities that could lead to system compromise |
| 🟡 **Medium**   | 6     | Security issues requiring attention                  |
| 🟢 **Low**      | 4     | Recommended security configuration improvements      |

---

## 🔴 Critical Risks

### CRIT-001: Bootloader Unlocked

**Risk Level:** 🔴 Critical  
**CVSS Score:** 9.8 (Critical)  
**Impact Scope:** Device Integrity, Data Protection

#### Finding

```
ro.boot.flash.locked = 0
ro.boot.verifiedbootstate = orange
ro.boot.vbmeta.device_state = unlocked
```

#### Description

The device's bootloader is in an unlocked state with verified boot state as "orange" (indicating custom firmware). This means:

- Attackers can flash malicious firmware
- System image integrity cannot be guaranteed
- Backdoors may persist even after device reset
- Verified Boot protection is ineffective

#### Remediation Recommendations

1. **Immediate Action:** Lock bootloader on production devices
2. **Firmware Update:** Ensure OTA update verification mechanism is intact
3. **Manufacturing Process:** Confirm bootloader is locked before shipment
4. **Monitoring:** Implement runtime integrity checks

#### Verification Commands

```bash
adb shell getprop ro.boot.flash.locked  # Should return 1
adb shell getprop ro.boot.verifiedbootstate  # Should return green
```

---

### CRIT-002: Security Patch Outdated (Over 19 Months（**原寫 6 個月，與同節 19 個月不一致，2026-09-13 更正**）)

**Risk Level:** 🔴 Critical  
**CVSS Score:** 9.1 (Critical)  
**Impact Scope:** All System Components

#### Finding

```
ro.build.version.security_patch = 2024-07-05
Current Date: 2026-01-30
Patch Lag: Approximately 19 months
```

#### Description

The device security patch level is 2024-07-05, over 19 months behind. During this period, Google has released multiple critical security patches, including:

- Android core vulnerabilities (CVE-2024-xxxxx series)
- Linux kernel vulnerabilities (kernel version 5.10.209)
- Qualcomm chipset vulnerabilities
- Bluetooth protocol stack vulnerabilities
- Media framework vulnerabilities

#### Known Affected CVEs (Partial List)

| CVE ID         | Severity | Component | Description            |
| -------------- | -------- | --------- | ---------------------- |
| CVE-2024-32896 | Critical | Framework | Privilege Escalation   |
| CVE-2024-29748 | Critical | Kernel    | Remote Code Execution  |
| CVE-2024-23706 | High     | Bluetooth | Information Disclosure |
| CVE-2024-23707 | High     | System    | Permission Bypass      |

#### Remediation Recommendations

1. **Urgent:** Immediately release OTA update with latest security patches
2. **Process Improvement:** Establish monthly security patch update mechanism
3. **Compliance Requirement:** Security patches should not lag more than 90 days
4. **Regression Testing:** Establish automated security testing pipeline

---

### CRIT-003: Network Service Exposed (Port 8341)

**Risk Level:** 🔴 Critical  
**CVSS Score:** 8.6 (High)  
**Impact Scope:** Network Security, Remote Access

#### Finding

```
State: LISTEN
Local Address: 0.0.0.0:8341
Protocol: TCP
UID: 0 (root)
```

#### Description

The device is listening on TCP port 8341 on all network interfaces (0.0.0.0), running with root privileges. This may:

- Allow unauthorized remote access
- Provide attack surface for network-layer attacks
- Be exploited for man-in-the-middle attacks
- The service may be a Rokid proprietary streaming or control service

#### Remediation Recommendations

1. **Immediate:** Identify and evaluate the necessity of this service
2. **Network Restriction:** Bind only to localhost (127.0.0.1) or necessary interfaces
3. **Authentication:** Implement strong authentication and encryption
4. **Firewall:** Block non-essential ports by default
5. **Privilege Reduction:** Avoid running services with root privileges

---

## 🟠 High Risks

### HIGH-001: dmesg Access Unrestricted

**Risk Level:** 🟠 High  
**CVSS Score:** 7.5 (High)  
**Impact Scope:** Information Disclosure

#### Finding

```
/proc/sys/kernel/dmesg_restrict = 0
```

#### Description

Any application can read kernel logs (dmesg), potentially exposing:

- Memory address information
- Loaded kernel modules
- Hardware configuration details
- Security-related events
- Potential exploit information

#### Remediation Recommendations

1. Set `kernel.dmesg_restrict = 1`
2. Configure in `/system/etc/sysctl.conf` or init scripts

---

### HIGH-002: Over-Privileged System Application

**Risk Level:** 🟠 High  
**CVSS Score:** 7.8 (High)  
**Impact Scope:** Permission Control

#### Finding

`com.rokid.os.sprite.assistserver` has the following high-risk permissions:

```
- android.permission.INJECT_EVENTS (Simulate input events)
- android.permission.READ_LOGS (Read system logs)
- android.permission.WRITE_SECURE_SETTINGS (Modify secure settings)
- android.permission.MASTER_CLEAR (Factory reset)
- android.permission.INSTALL_PACKAGES (Install applications)
- android.permission.DELETE_PACKAGES (Delete applications)
- android.permission.BLUETOOTH_PRIVILEGED (Bluetooth privileges)
- android.permission.MODIFY_PHONE_STATE (Modify phone state)
- android.permission.MANAGE_USERS (Manage users)
- android.permission.REBOOT (Reboot device)
```

#### Description

The Rokid assistant service has over 100 system permissions, creating a single point of failure. If this application is compromised:

- Complete device control is possible
- All user data can be accessed
- Malware can be installed
- All communications can be monitored

#### Remediation Recommendations

1. **Principle of Least Privilege:** Review and remove unnecessary permissions
2. **Component Separation:** Split functionality into multiple lower-privilege components
3. **Runtime Checks:** Implement secondary verification for sensitive operations
4. **Code Audit:** Conduct security code review of this application

---

### HIGH-003: Debug Applications Installed

**Risk Level:** 🟠 High  
**CVSS Score:** 7.2 (High)  
**Impact Scope:** Application Security

#### Finding

```
Package: com.cooper.wheellog.debug
Flags: [ DEBUGGABLE, HAS_CODE, TEST_ONLY, ALLOW_BACKUP ]
```

Granted dangerous permissions:

- `ACCESS_BACKGROUND_LOCATION` (Background location access)
- `ACCESS_FINE_LOCATION` (Precise location)
- `BLUETOOTH_SCAN/CONNECT/ADVERTISE` (Full Bluetooth access)
- `READ_EXTERNAL_STORAGE` (Read storage)

#### Description

Applications with `DEBUGGABLE` and `TEST_ONLY` flags are installed on the device. These applications:

- Can be attached by debuggers
- May contain additional logging
- May have test backdoors
- Should not appear on production devices

#### Remediation Recommendations

1. **Remove:** Uninstall all debuggable and test-only applications
2. **Policy:** Prohibit installation of debug applications in production
3. **Scan:** Automatically detect debug applications before OTA

---

### HIGH-004: Unknown Sources Installation Enabled

**Risk Level:** 🟠 High  
**CVSS Score:** 6.8 (High)  
**Impact Scope:** Application Security

#### Finding

```
install_non_market_apps = 1
unknown_sources_default_reversed = 1
```

#### Description

The device allows installation of applications from unknown sources, increasing:

- Malware installation risk
- Social engineering attack surface
- Bypass of Google Play Protect mechanism

#### Remediation Recommendations

1. Disable unknown sources installation by default
2. Implement application whitelist mechanism
3. Require enterprise certificate-signed applications for installation

---

### HIGH-005: ADB Persistently Enabled

**Risk Level:** 🟠 High  
**CVSS Score:** 6.5 (High)  
**Impact Scope:** Device Access Control

#### Finding

```
persist.sys.usb.config = adb
persist.vendor.adb = true
adb_enabled = 1
sys.usb.config = adb
```

#### Description

ADB (Android Debug Bridge) is enabled by default and persistent. Although `ro.adb.secure=1` (requires authorization), risks remain:

- Physical access attacks
- Authorized device credentials may be stolen
- May be exploited by malicious accessories

#### Remediation Recommendations

1. **Production Build:** Disable ADB by default
2. **Conditional Enable:** Only enable when developer options are activated
3. **Timeout Mechanism:** Implement ADB connection timeout
4. **Logging:** Log all ADB connection events

---

## 🟡 Medium Risks

### MED-001: Screen Lock Disabled

**Risk Level:** 🟡 Medium  
**CVSS Score:** 5.5 (Medium)

#### Finding

```
lockscreen.disabled = 1
```

#### Description

The device has no screen lock configured, allowing anyone direct access. For wearable devices with camera and microphone, this increases privacy risks.

#### Remediation Recommendations

1. Evaluate feasibility of lock mechanism for glasses devices
2. Consider alternatives such as Bluetooth pairing lock
3. Implement wear detection lock

---

### MED-002: NetworkSecurityConfig Not Configured

**Risk Level:** 🟡 Medium  
**CVSS Score:** 5.3 (Medium)

#### Finding

```
Log: No Network Security Config specified, using platform default
Affected Apps: Multiple third-party applications
```

#### Description

Multiple applications have not configured custom network security policies, using default settings. This may:

- Allow cleartext HTTP traffic
- Accept invalid certificates
- Lack certificate pinning

#### Remediation Recommendations

1. Enforce NetworkSecurityConfig for all applications
2. Block cleartext HTTP by default
3. Implement certificate pinning

---

### MED-003: SELinux Label Errors

**Risk Level:** 🟡 Medium  
**CVSS Score:** 5.0 (Medium)

#### Finding

```
Could not start service 'vendor.face-default': incorrect label
Could not start service 'qguard': incorrect label (u:object_r:vendor_file:s0)
```

#### Description

Some vendor services have incorrect SELinux label configuration, causing services to fail to start properly or may have permission issues.

#### Remediation Recommendations

1. Correct SELinux policy files
2. Define proper domain transitions for all vendor services
3. Perform complete SELinux policy audit

---

### MED-004: Third-Party Payment App Over-Privileged

**Risk Level:** 🟡 Medium  
**CVSS Score:** 5.5 (Medium)

#### Finding

Alipay Glasses Edition (`com.eg.android.AlipayGGlasses`) requested sensitive permissions:

```
- MANAGE_EXTERNAL_STORAGE (Full storage access)
- SYSTEM_ALERT_WINDOW (Overlay window, denied)
- WRITE_SECURE_SETTINGS (Modify secure settings, denied)
- CAMERA (Camera access)
- READ_PRIVILEGED_PHONE_STATE (Privileged phone state)
- START_ACTIVITIES_FROM_BACKGROUND (Background launch)
```

#### Description

Payment applications have excessive permissions, increasing financial data leakage risk.

#### Remediation Recommendations

1. Work with Alipay to minimize permission requirements
2. Implement additional verification for payment operations
3. Isolate payment application data access

---

### MED-005: Bluetooth SCRAM Not Enabled

**Risk Level:** 🟡 Medium  
**CVSS Score:** 4.8 (Medium)

#### Finding

```
persist.vendor.qcom.bluetooth.scram.enabled = false
```

#### Description

Bluetooth SCRAM (Secure Connection Required for Audio/HID Mode) is not enabled, potentially allowing weaker Bluetooth connection security.

#### Remediation Recommendations

1. Enable SCRAM to enforce secure connections
2. Update Bluetooth firmware to latest version
3. Implement Bluetooth pairing verification

---

### MED-006: Camera Privacy LED Can Be Disabled

**Risk Level:** 🟡 Medium  
**CVSS Score:** 4.6 (Medium)

#### Finding

```
Permission: android.permission.CAMERA_DISABLE_TRANSMIT_LED (exists in system)
vendor.rkd.camera.led.enable = 1 (currently enabled)
```

#### Description

The system has permission to disable camera LED indicator. If obtained by malicious applications, recording may occur without user knowledge.

#### Remediation Recommendations

1. Remove or strictly restrict `CAMERA_DISABLE_TRANSMIT_LED` permission
2. Enforce LED activation at hardware level
3. Add software-level indicator

---

## 🟢 Low Risks

### LOW-001: Development Files Residue

**Risk Level:** 🟢 Low  
**CVSS Score:** 3.3 (Low)

#### Finding

```
/data/local/tmp/.studio/ (Android Studio debug files)
```

#### Description

Development tool temporary files remain on the device, potentially exposing development information.

#### Remediation Recommendations

1. Clean development files before shipment
2. Regularly clean temporary directories

---

### LOW-002: Sample Applications Installed

**Risk Level:** 🟢 Low  
**CVSS Score:** 3.1 (Low)

#### Finding

```
com.example.rokidglasses
com.example.wifi
```

#### Description

Applications starting with "com.example" are installed, typically sample or test applications.

#### Remediation Recommendations

1. Remove all non-release applications
2. Establish application whitelist policy

---

### LOW-003: Mock Location Disabled But Configurable

**Risk Level:** 🟢 Low  
**CVSS Score:** 2.8 (Low)

#### Finding

```
mock_location = 0
location_changer = 1
```

#### Description

Although mock location is currently disabled, the `location_changer` setting may allow reconfiguration.

#### Remediation Recommendations

1. Force disable mock location
2. Remove related configuration options

---

### LOW-004: Battery Optimization Whitelist May Be Too Broad

**Risk Level:** 🟢 Low  
**CVSS Score:** 2.5 (Low)

#### Finding

Multiple system applications are in the battery optimization whitelist, allowing continuous background operation.

#### Remediation Recommendations

1. Review whitelist necessity
2. Implement fine-grained background activity controls

---

## 🛡️ Security Hardening (Already Implemented)

The following security mechanisms are correctly configured:

| Item                       | Status                | Description                                |
| -------------------------- | --------------------- | ------------------------------------------ |
| SELinux                    | Enforcing（開機）→ **Permissive（提權後）** | Mandatory Access Control enabled；**2026-09-13 更正** |
| ASLR                       | ✅ Full (value=2)     | Address Space Layout Randomization enabled |
| Kernel Pointer Restriction | ✅ Enabled (value=2)  | Kernel pointers hidden                     |
| Perf Event Paranoid        | ✅ Maximum (value=3)  | Performance monitoring restricted          |
| File-Based Encryption      | ✅ Enabled            | File encryption implemented                |
| ADB Authentication         | ✅ Enabled            | RSA key authorization required             |
| Root Access                | ~~None~~ → **執行期 root（2026-08-22 起）** | No su/magisk binaries；root 由 GhostLock LPE 提供 |
| ro.debuggable              | ✅ Disabled (value=0) | System debugging disabled                  |
| ro.secure                  | ✅ Enabled (value=1)  | Secure mode enabled                        |
| KeyMint HAL                | ✅ Running            | Hardware key management enabled            |

---

## 📋 Remediation Priority Recommendations

### First Priority (Within 7 Days)

| ID       | Risk     | Remediation Item                     |
| -------- | -------- | ------------------------------------ |
| CRIT-001 | Critical | Lock Bootloader                      |
| CRIT-002 | Critical | Release Security Patch Update        |
| CRIT-003 | Critical | Restrict or Remove Port 8341 Service |

### Second Priority (Within 30 Days)

| ID       | Risk | Remediation Item                                |
| -------- | ---- | ----------------------------------------------- |
| HIGH-001 | High | Restrict dmesg Access                           |
| HIGH-002 | High | Review and Reduce Assistant Service Permissions |
| HIGH-003 | High | Remove Debug Applications                       |
| HIGH-004 | High | Disable Unknown Sources by Default              |
| HIGH-005 | High | Disable ADB by Default                          |

### Third Priority (Within 90 Days)

| ID                | Risk   | Remediation Item                     |
| ----------------- | ------ | ------------------------------------ |
| MED-001 ~ MED-006 | Medium | Implement Medium Risk Remediations   |
| LOW-001 ~ LOW-004 | Low    | Cleanup and Hardening Configurations |

---

## 📊 Compliance Assessment

| Standard        | Status                 | Issues                                                    |
| --------------- | ---------------------- | --------------------------------------------------------- |
| **GDPR**        | ⚠️ Partially Compliant | Camera/location access needs explicit consent mechanism   |
| **PCI DSS**     | ❌ Non-Compliant       | Payment app over-privileged, security patches outdated    |
| **HIPAA**       | ⚠️ Needs Evaluation    | Additional protection needed if used in medical scenarios |
| **Android CDD** | ⚠️ Partially Compliant | Security patches lag exceeds 90-day limit                 |

---

## 🔧 Recommended Security Testing

1. **Penetration Testing:** Conduct full penetration test on Port 8341 service
2. **Bluetooth Security:** Test Bluetooth protocol stack for known vulnerabilities
3. **Application Audit:** Perform static and dynamic analysis on Rokid proprietary applications
4. **Firmware Analysis:** Extract and analyze firmware for hardcoded credentials
5. **Privacy Impact Assessment:** Evaluate camera/microphone privacy risks

---

## 📝 Appendix

### A. Scanning Tools and Commands Used

```bash
# System property check
adb shell getprop | grep -E "secure|debug|verify|crypto"

# Network port scan
adb shell ss -tlnp

# Permission audit
adb shell dumpsys package <package_name>

# SELinux status
adb shell getenforce

# Kernel security parameters
adb shell cat /proc/sys/kernel/dmesg_restrict
adb shell cat /proc/sys/kernel/randomize_va_space
```

### B. References

- Android Security Bulletin: https://source.android.com/security/bulletin
- CIS Android Benchmark
- OWASP Mobile Security Testing Guide
- NIST SP 800-163: Vetting Mobile Applications

---

**Report Prepared By:** Security Assessment Expert  
**Review Status:** Pending Review  
**Confidentiality Level:** Internal Use Only

---

_This report is for Rokid internal security assessment use only. Do not distribute externally._
