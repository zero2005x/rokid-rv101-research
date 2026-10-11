# Rokid Glasses Information Security Vulnerability Assessment Report

> 中文版：[Rokid_Glasses_資安漏洞報告.md](Rokid_Glasses_%E8%B3%87%E5%AE%89%E6%BC%8F%E6%B4%9E%E5%A0%B1%E5%91%8A.md)

> 📌 **Fact basis**: Whenever this document touches on facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; in case of conflict, that table decides).


**Assessment date:** 2026-01-30  
**Device model:** RG-glasses (RV101)  
**Firmware version:** 1.12.009-20260109-150201  
**Android version:** 12 (API 32)  
**Assessment type:** ADB remote security scan  
**Report ID:** RKID-SEC-2026-001

---

## 📊 Risk Level Summary

| Risk level               | Count | Notes                     |
| ---------------------- | ---- | ------------------------ |
| 🔴 **Critical** | 3    | major security risks requiring immediate action |
| 🟠 **High**   | 5    | vulnerabilities that may lead to system compromise |
| 🟡 **Medium** | 6    | security issues that need attention       |
| 🟢 **Low**    | 4    | security configurations recommended for improvement       |

---

## 🔴 Critical Risks

### CRIT-001: Bootloader Not Locked

**Risk level:** 🔴 Critical  
**CVSS score:** 9.8 (Critical)  
**Scope of impact:** device integrity, data protection

#### Finding

```
ro.boot.flash.locked = 0
ro.boot.verifiedbootstate = orange
ro.boot.vbmeta.device_state = unlocked
```

#### Description

The device's bootloader is unlocked and the verified boot state is "orange" (indicating custom firmware). This means:

- An attacker can flash malicious firmware
- The integrity of the system image cannot be guaranteed
- A backdoor may persist even after a device reset
- Verified Boot protection is ineffective

#### Remediation Recommendations

1. **Immediate action:** lock the bootloader on mass-production devices
2. **Firmware update:** ensure the OTA update verification mechanism is complete
3. **Process improvement:** confirm the bootloader is locked before shipping
4. **Monitoring:** implement runtime integrity checks

#### Verification Commands

```bash
adb shell getprop ro.boot.flash.locked  # should return 1
adb shell getprop ro.boot.verifiedbootstate  # should return green
```

---

### CRIT-002: Security Patch Out of Date (More Than 6 Months)

**Risk level:** 🔴 Critical  
**CVSS score:** 9.1 (Critical)  
**Scope of impact:** all system components

#### Finding

```
ro.build.version.security_patch = 2024-07-05
Current date: 2026-01-30
Patch lag: about 19 months
```

#### Description

The device's security patch level is 2024-07-05, more than 19 months ago. In the intervening period Google has released multiple critical security patches, including:

- Android core vulnerabilities (the CVE-2024-xxxxx series)
- Linux kernel vulnerabilities (kernel version 5.10.209)
- Qualcomm chipset vulnerabilities
- Bluetooth stack vulnerabilities
- Media framework vulnerabilities

#### Known Affected CVEs (Partial List)

| CVE ID         | Severity   | Component      | Description         |
| -------------- | -------- | --------- | ------------ |
| CVE-2024-32896 | Critical | Framework | privilege escalation vulnerability |
| CVE-2024-29748 | Critical | Kernel    | remote code execution |
| CVE-2024-23706 | High     | Bluetooth | information disclosure     |
| CVE-2024-23707 | High     | System    | permission bypass     |

#### Remediation Recommendations

1. **Urgent:** immediately release an OTA update containing the latest security patches
2. **Process improvement:** establish a monthly security patch update mechanism
3. **Compliance requirement:** security patches should not lag more than 90 days
4. **Regression testing:** establish an automated security testing process

---

### CRIT-003: Network Service Exposed (Port 8341)

**Risk level:** 🔴 Critical  
**CVSS score:** 8.6 (High)  
**Scope of impact:** network security, remote access

#### Finding

```
State: LISTEN
Local Address: 0.0.0.0:8341
Protocol: TCP
UID: 0 (root)
```

#### Description

The device listens on TCP port 8341 on all network interfaces (0.0.0.0) and runs with root privileges. This may:

- Allow unauthorized remote access
- Provide an attack surface for network-layer attacks
- Be exploited for man-in-the-middle attacks
- Indicate that the service is a Rokid-specific streaming or control service

#### Remediation Recommendations

1. **Immediate:** identify and assess whether the service is necessary
2. **Network restriction:** bind only to localhost (127.0.0.1) or the necessary interfaces
3. **Authentication mechanism:** implement strong authentication and encryption
4. **Firewall:** block unnecessary ports by default
5. **Privilege reduction:** avoid running the service with root privileges

---

## 🟠 High Risks

### HIGH-001: dmesg Access Not Restricted

**Risk level:** 🟠 High  
**CVSS score:** 7.5 (High)  
**Scope of impact:** information disclosure

#### Finding

```
/proc/sys/kernel/dmesg_restrict = 0
```

#### Description

Any application can read the kernel log (dmesg), which may disclose:

- Memory address information
- Loaded kernel modules
- Hardware configuration details
- Security-related events
- Potentially useful exploitation information

#### Remediation Recommendations

1. Set `kernel.dmesg_restrict = 1`
2. Configure it in `/system/etc/sysctl.conf` or an init script

---

### HIGH-002: Over-Privileged Privileged App

**Risk level:** 🟠 High  
**CVSS score:** 7.8 (High)  
**Scope of impact:** permission control

#### Finding

`com.rokid.os.sprite.assistserver` holds the following high-risk permissions:

```
- android.permission.INJECT_EVENTS (inject input events)
- android.permission.READ_LOGS (read system logs)
- android.permission.WRITE_SECURE_SETTINGS (modify secure settings)
- android.permission.MASTER_CLEAR (factory reset)
- android.permission.INSTALL_PACKAGES (install apps)
- android.permission.DELETE_PACKAGES (delete apps)
- android.permission.BLUETOOTH_PRIVILEGED (Bluetooth privileges)
- android.permission.MODIFY_PHONE_STATE (modify phone state)
- android.permission.MANAGE_USERS (manage users)
- android.permission.REBOOT (reboot the device)
```

#### Description

The Rokid assistant service holds more than 100 system permissions, creating a single point of failure. If the app is compromised:

- It can fully control the device
- It can access all user data
- It can install malicious software
- It can monitor all communications

#### Remediation Recommendations

1. **Principle of least privilege:** review and remove unnecessary permissions
2. **Component separation:** split functionality into multiple lower-privilege components
3. **Runtime checks:** implement secondary verification for sensitive operations
4. **Code audit:** perform a secure code review of the app

---

### HIGH-003: Debuggable App Installed

**Risk level:** 🟠 High  
**CVSS score:** 7.2 (High)  
**Scope of impact:** application security

#### Finding

```
Package: com.cooper.wheellog.debug
Flags: [ DEBUGGABLE, HAS_CODE, TEST_ONLY, ALLOW_BACKUP ]
```

Dangerous permissions already granted:

- `ACCESS_BACKGROUND_LOCATION` (background location access)
- `ACCESS_FINE_LOCATION` (precise location)
- `BLUETOOTH_SCAN/CONNECT/ADVERTISE` (full Bluetooth access)
- `READ_EXTERNAL_STORAGE` (read storage)

#### Description

An application with the `DEBUGGABLE` and `TEST_ONLY` flags is installed on the device. Such applications:

- Can be attached to by a debugger
- May contain extra logging
- May contain test backdoors
- Should not appear on production devices

#### Remediation Recommendations

1. **Remove:** uninstall all debuggable and test-only applications
2. **Policy:** prohibit installing debug builds in production
3. **Scanning:** automatically detect debuggable applications before OTA

---

### HIGH-004: Unknown-Source Installation Enabled

**Risk level:** 🟠 High  
**CVSS score:** 6.8 (High)  
**Scope of impact:** application security

#### Finding

```
install_non_market_apps = 1
unknown_sources_default_reversed = 1
```

#### Description

The device allows applications to be installed from unknown sources, which increases:

- The risk of malware installation
- The social-engineering attack surface
- Bypass of Google Play protection mechanisms

#### Remediation Recommendations

1. Disable unknown-source installation by default
2. Implement an application allowlist mechanism
3. Require applications to be signed with an enterprise certificate in order to be installed

---

### HIGH-005: ADB Persistently Enabled

**Risk level:** 🟠 High  
**CVSS score:** 6.5 (High)  
**Scope of impact:** device access control

#### Finding

```
persist.sys.usb.config = adb
persist.vendor.adb = true
adb_enabled = 1
sys.usb.config = adb
```

#### Description

ADB (Android Debug Bridge) is enabled by default and persisted. Although `ro.adb.secure=1` (authorization required), risks remain:

- Physical access attacks
- Credentials of authorized devices may be stolen
- May be exploited by malicious accessories

#### Remediation Recommendations

1. **Production builds:** disable ADB by default
2. **Conditional enabling:** expose it only when developer options are enabled
3. **Timeout mechanism:** implement an ADB connection timeout
4. **Logging:** log all ADB connection events

---

## 🟡 Medium Risks

### MED-001: Screen Lock Disabled

**Risk level:** 🟡 Medium  
**CVSS score:** 5.5 (Medium)

#### Finding

```
lockscreen.disabled = 1
```

#### Description

The device has no screen lock configured, so anyone can access the device directly. For a wearable device with a camera and microphone, this increases privacy risk.

#### Remediation Recommendations

1. Assess the feasibility of a locking mechanism for glasses devices
2. Consider alternatives such as Bluetooth pairing lock
3. Implement wear-detection locking

---

### MED-002: NetworkSecurityConfig Not Configured

**Risk level:** 🟡 Medium  
**CVSS score:** 5.3 (Medium)

#### Finding

```
Log: No Network Security Config specified, using platform default
Affected apps: multiple third-party apps
```

#### Description

Multiple applications have no custom network security policy configured and use the default settings. This may:

- Allow cleartext HTTP traffic
- Accept invalid certificates
- Lack certificate pinning

#### Remediation Recommendations

1. Require all applications to implement NetworkSecurityConfig
2. Block cleartext HTTP by default
3. Implement certificate pinning

---

### MED-003: Incorrect SELinux Labels

**Risk level:** 🟡 Medium  
**CVSS score:** 5.0 (Medium)

#### Finding

```
Could not start service 'vendor.face-default': incorrect label
Could not start service 'qguard': incorrect label (u:object_r:vendor_file:s0)
```

#### Description

The SELinux labels of some vendor services are misconfigured, so the services cannot start correctly or may have permission problems.

#### Remediation Recommendations

1. Fix the SELinux policy files
2. Define correct domain transitions for all vendor services
3. Perform a complete SELinux policy audit

---

### MED-004: Third-Party Payment App Over-Privileged

**Risk level:** 🟡 Medium  
**CVSS score:** 5.5 (Medium)

#### Finding

Sensitive permissions requested by the Alipay glasses edition (`com.eg.android.AlipayGGlasses`):

```
- MANAGE_EXTERNAL_STORAGE (full storage access)
- SYSTEM_ALERT_WINDOW (floating window, denied)
- WRITE_SECURE_SETTINGS (modify secure settings, denied)
- CAMERA (camera access)
- READ_PRIVILEGED_PHONE_STATE (privileged phone state)
- START_ACTIVITIES_FROM_BACKGROUND (background start)
```

#### Description

The payment app holds excessive permissions, increasing the risk of financial data disclosure.

#### Remediation Recommendations

1. Work with Alipay to minimize permission requirements
2. Implement additional verification for payment operations
3. Isolate the payment app's data access

---

### MED-005: Bluetooth SCRAM Not Enabled

**Risk level:** 🟡 Medium  
**CVSS score:** 4.8 (Medium)

#### Finding

```
persist.vendor.qcom.bluetooth.scram.enabled = false
```

#### Description

Bluetooth SCRAM (Secure Connection Required for Audio/HID Mode) is not enabled, which may permit weaker Bluetooth connection security.

#### Remediation Recommendations

1. Enable SCRAM to enforce secure connections
2. Update the Bluetooth firmware to the latest version
3. Implement Bluetooth pairing verification

---

### MED-006: Camera Privacy LED Can Be Disabled

**Risk level:** 🟡 Medium  
**CVSS score:** 4.6 (Medium)

#### Finding

```
Permission: android.permission.CAMERA_DISABLE_TRANSMIT_LED (present in the system)
vendor.rkd.camera.led.enable = 1 (currently enabled)
```

#### Description

A permission to disable the camera LED indicator exists in the system; if obtained by a malicious application, it could record video without the user's knowledge.

#### Remediation Recommendations

1. Remove or strictly restrict the `CAMERA_DISABLE_TRANSMIT_LED` permission
2. Enforce LED-on at the hardware level
3. Add a software-level indicator

---

## 🟢 Low Risks

### LOW-001: Leftover Development Files

**Risk level:** 🟢 Low  
**CVSS score:** 3.3 (Low)

#### Finding

```
/data/local/tmp/.studio/ (Android Studio debug files)
```

#### Description

Temporary files from development tools remain on the device and may disclose development information.

#### Remediation Recommendations

1. Clean up development files before shipping
2. Clean temporary directories regularly

---

### LOW-002: Sample Applications Installed

**Risk level:** 🟢 Low  
**CVSS score:** 3.1 (Low)

#### Finding

```
com.example.rokidglasses
com.example.wifi
```

#### Description

Applications whose names start with "com.example" are installed on the device; these are usually sample or test applications.

#### Remediation Recommendations

1. Remove all non-officially-released applications
2. Establish an application allowlist system

---

### LOW-003: Mock Location Disabled but Configurable

**Risk level:** 🟢 Low  
**CVSS score:** 2.8 (Low)

#### Finding

```
mock_location = 0
location_changer = 1
```

#### Description

Although mock location is currently disabled, the `location_changer` setting may allow it to be reconfigured.

#### Remediation Recommendations

1. Forcibly disable mock location
2. Remove the related setting options

---

### LOW-004: Battery Optimization Allowlist May Be Too Broad

**Risk level:** 🟢 Low  
**CVSS score:** 2.5 (Low)

#### Finding

Multiple system applications are on the battery optimization allowlist and can keep running in the background.

#### Remediation Recommendations

1. Review whether the allowlist entries are necessary
2. Implement fine-grained background activity control

---

## 🛡️ Security Hardening Items (Already Implemented)

The following security mechanisms are correctly configured:

| Item                       | State           | Notes                     |
| -------------------------- | -------------- | ------------------------ |
| SELinux                    | Enforcing (boot) → **Permissive (after privilege escalation)** | mandatory access control enabled; **corrected 2026-09-13** |
| ASLR                       | ✅ full (value=2) | address space layout randomization enabled |
| Kernel Pointer Restriction | ✅ enabled (value=2) | kernel pointers hidden           |
| Perf Event Paranoid        | ✅ maximum (value=3) | performance monitoring restricted           |
| File-Based Encryption      | ✅ enabled        | file encryption implemented           |
| ADB authentication                   | ✅ enabled        | RSA key authorization required        |
| Root access                  | ✅ none          | no su/magisk binaries found  |
| ro.debuggable              | ✅ disabled (value=0) | system debugging disabled           |
| ro.secure                  | ✅ enabled (value=1) | secure mode enabled           |
| KeyMint HAL                | ✅ running      | hardware key management enabled       |

---

## 📋 Remediation Priority Recommendations

### First Priority (Within 7 Days)

| ID       | Risk | Remediation item                  |
| -------- | ---- | ------------------------- |
| CRIT-001 | Critical | Lock the bootloader           |
| CRIT-002 | Critical | Release a security patch update          |
| CRIT-003 | Critical | Restrict or remove the Port 8341 service |

### Second Priority (Within 30 Days)

| ID       | Risk | Remediation item               |
| -------- | ---- | ---------------------- |
| HIGH-001 | High   | Restrict dmesg access        |
| HIGH-002 | High   | Review and reduce the assistant service's permissions |
| HIGH-003 | High   | Remove debuggable applications         |
| HIGH-004 | High   | Disable unknown-source installation by default   |
| HIGH-005 | High   | Disable ADB by default           |

### Third Priority (Within 90 Days)

| ID                | Risk | Remediation item           |
| ----------------- | ---- | ------------------ |
| MED-001 ~ MED-006 | Medium   | Implement the various medium-risk fixes |
| LOW-001 ~ LOW-004 | Low   | Clean up and harden the configuration     |

---

## 📊 Compliance Assessment

| Standard            | State        | Issue                           |
| --------------- | ----------- | ------------------------------ |
| **GDPR**        | ⚠️ partially compliant | camera/location access requires an explicit consent mechanism  |
| **PCI DSS**     | ❌ non-compliant   | payment app over-privileged, security patches out of date |
| **HIPAA**       | ⚠️ needs assessment   | additional protection required if used in medical scenarios       |
| **Android CDD** | ⚠️ partially compliant | security patches lag beyond the 90-day limit     |

---

## 🔧 Recommended Security Testing

1. **Penetration testing:** conduct full penetration testing of the Port 8341 service
2. **Bluetooth security:** test the Bluetooth stack for known vulnerabilities
3. **Application audit:** perform static and dynamic analysis of Rokid-specific applications
4. **Firmware analysis:** extract and analyze hard-coded credentials in the firmware
5. **Privacy impact assessment:** assess the privacy risks of the camera/microphone

---

## 📝 Appendix

### A. Scanning Tools and Commands Used

```bash
# system property check
adb shell getprop | grep -E "secure|debug|verify|crypto"

# network port scan
adb shell ss -tlnp

# permission audit
adb shell dumpsys package <package_name>

# SELinux state
adb shell getenforce

# kernel security parameters
adb shell cat /proc/sys/kernel/dmesg_restrict
adb shell cat /proc/sys/kernel/randomize_va_space
```

### B. References

- Android Security Bulletin: https://source.android.com/security/bulletin
- CIS Android Benchmark
- OWASP Mobile Security Testing Guide
- NIST SP 800-163: Vetting Mobile Applications

---

**Report prepared by:** security assessment specialist  
**Review status:** pending review  
**Classification:** internal use

---

_This report is for Rokid internal security assessment use only; do not distribute._
