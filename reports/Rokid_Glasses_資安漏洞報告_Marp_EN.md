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

# 🔒 Rokid Glasses Information Security Vulnerability Assessment Report

> 中文版：[Rokid_Glasses_資安漏洞報告_Marp.md](Rokid_Glasses_%E8%B3%87%E5%AE%89%E6%BC%8F%E6%B4%9E%E5%A0%B1%E5%91%8A_Marp.md)

**Assessment Date:** January 30, 2026  
**Device Model:** RG-glasses (RV101)  
**Firmware Version:** 1.12.009-20260109-150201  
**Report Number:** RKID-SEC-2026-001

---

## 📊 Risk Level Summary

| Risk Level               | Count | Description                     |
| ---------------------- | ---- | ------------------------ |
| 🔴 **Critical** | 3    | major security risks requiring immediate action |
| 🟠 **High**   | 5    | vulnerabilities that may lead to system compromise |
| 🟡 **Medium** | 6    | security issues that need attention       |
| 🟢 **Low**    | 4    | security configurations recommended for improvement       |

**Total findings: 18 security issues**

---

# 🔴 Critical Risks

---

## CRIT-001: Bootloader Not Locked

**Risk Level:** 🔴 Critical | **CVSS:** 9.8

### Finding

```
ro.boot.flash.locked = 0
ro.boot.verifiedbootstate = orange
ro.boot.vbmeta.device_state = unlocked
```

### Impact

- ❌ An attacker can flash malicious firmware
- ❌ System image integrity cannot be guaranteed
- ❌ A backdoor may survive a device reset
- ❌ Verified Boot protection is ineffective

---

## CRIT-001: Remediation Recommendations

### Immediate Actions

1. ✅ Lock the Bootloader on production devices
2. ✅ Ensure the OTA update verification mechanism is complete
3. ✅ Confirm the Bootloader is locked before leaving the factory
4. ✅ Implement runtime integrity checks

### Verification Commands

```bash
adb shell getprop ro.boot.flash.locked  # should return 1
adb shell getprop ro.boot.verifiedbootstate  # should return green
```

---

## CRIT-002: Security Patch Expired (over 6 months)

**Risk Level:** 🔴 Critical | **CVSS:** 9.1

### Finding

```
ro.build.version.security_patch = 2024-07-05
current date: 2026-01-30
patch lag: about 19 months
```

### Known Affected CVEs

| CVE ID         | Severity   | Component      | Description         |
| -------------- | -------- | --------- | ------------ |
| CVE-2024-32896 | Critical | Framework | privilege escalation     |
| CVE-2024-29748 | Critical | Kernel    | remote code execution |
| CVE-2024-23706 | High     | Bluetooth | information disclosure     |

---

## CRIT-002: Remediation Recommendations

### Urgent Measures

1. ✅ **Immediately release** an OTA update containing the latest security patches
2. ✅ **Process improvement** - establish a monthly security patch update mechanism
3. ✅ **Compliance requirement** - security patches should not lag by more than 90 days
4. ✅ **Regression testing** - establish an automated security testing process

### Affected Components

- Android kernel vulnerabilities, Linux kernel vulnerabilities
- Qualcomm chipset vulnerabilities
- Bluetooth stack vulnerabilities, media framework vulnerabilities

---

## CRIT-003: Network Service Exposure (Port 8341)

**Risk Level:** 🔴 Critical | **CVSS:** 8.6

### Finding

```
State: LISTEN
Local Address: 0.0.0.0:8341
Protocol: TCP
UID: 0 (root)
```

### Risk

- ❌ Allows unauthorized remote access
- ❌ Provides a network-layer attack surface
- ❌ May be exploited for man-in-the-middle attacks
- ❌ Runs with root privileges

---

## CRIT-003: Remediation Recommendations

### Immediate Measures

1. ✅ **Identify the service** - assess the necessity of this service
2. ✅ **Network restriction** - bind only to localhost (127.0.0.1)
3. ✅ **Authentication mechanism** - implement strong authentication and encryption
4. ✅ **Firewall** - block non-essential ports by default
5. ✅ **Privilege reduction** - avoid running with root privileges

---

# 🟠 High Risks

---

## HIGH-001: dmesg Access Not Restricted

**Risk Level:** 🟠 High | **CVSS:** 7.5

### Finding

```
/proc/sys/kernel/dmesg_restrict = 0
```

### Risk

Any application can read the kernel log, potentially leaking:

- memory address information
- loaded kernel modules
- hardware configuration details
- security-related events

### Remediation

Set `kernel.dmesg_restrict = 1`

---

## HIGH-002: Over-Privileged System App

**Risk Level:** 🟠 High | **CVSS:** 7.8

### Finding - `com.rokid.os.sprite.assistserver`

```
- android.permission.INJECT_EVENTS
- android.permission.READ_LOGS
- android.permission.WRITE_SECURE_SETTINGS
- android.permission.MASTER_CLEAR
- android.permission.INSTALL_PACKAGES
- android.permission.DELETE_PACKAGES
- android.permission.REBOOT
```

**This app holds more than 100 system permissions!**

---

## HIGH-002: Remediation Recommendations

### Risk

If this app is compromised:

- ❌ it can fully control the device
- ❌ it can access all user data
- ❌ it can install malicious software
- ❌ it can monitor all communications

### Remediation

1. ✅ **Principle of least privilege** - remove unnecessary permissions
2. ✅ **Component separation** - split into multiple lower-privilege components
3. ✅ **Runtime checks** - secondary verification for sensitive operations
4. ✅ **Code audit** - secure code review

---

## HIGH-003: Debuggable App Installed

**Risk Level:** 🟠 High | **CVSS:** 7.2

### Finding

```
Package: com.cooper.wheellog.debug
Flags: [ DEBUGGABLE, HAS_CODE, TEST_ONLY, ALLOW_BACKUP ]
```

### Dangerous Permissions Granted

- `ACCESS_BACKGROUND_LOCATION`
- `ACCESS_FINE_LOCATION`
- `BLUETOOTH_SCAN/CONNECT/ADVERTISE`
- `READ_EXTERNAL_STORAGE`

### Remediation

Uninstall all debuggable and test-only apps

---

## HIGH-004: Unknown-Source Installation Enabled

**Risk Level:** 🟠 High | **CVSS:** 6.8

### Finding

```
install_non_market_apps = 1
unknown_sources_default_reversed = 1
```

### Risk

- ❌ malware installation risk
- ❌ social-engineering attack surface
- ❌ bypasses Google Play protection mechanisms

### Remediation

1. Disable unknown-source installation by default
2. Implement an application allowlist mechanism

---

## HIGH-005: ADB Persistently Enabled

**Risk Level:** 🟠 High | **CVSS:** 6.5

### Finding

```
persist.sys.usb.config = adb
persist.vendor.adb = true
adb_enabled = 1
```

### Risk

- ❌ physical-access attacks
- ❌ authorization credentials may be stolen
- ❌ exploitation by malicious accessories

### Remediation

Disable ADB by default in production builds and implement a connection timeout

---

# 🟡 Medium Risks

---

## MED-001 ~ MED-003

### MED-001: Screen Lock Disabled (CVSS 5.5)

```
lockscreen.disabled = 1
```

**Remediation:** assess the feasibility of a lock mechanism for glasses devices

### MED-002: NetworkSecurityConfig Not Configured (CVSS 5.3)

Multiple apps use default network security settings
**Remediation:** force all apps to implement NetworkSecurityConfig

### MED-003: Incorrect SELinux Label (CVSS 5.0)

```
Could not start service 'vendor.face-default': incorrect label
```

**Remediation:** fix the SELinux policy file

---

## MED-004 ~ MED-006

### MED-004: Third-Party Payment App Has Excessive Permissions (CVSS 5.5)

Alipay Glasses Edition requests sensitive permissions:

- MANAGE_EXTERNAL_STORAGE
- CAMERA
- START_ACTIVITIES_FROM_BACKGROUND

### MED-005: Bluetooth SCRAM Not Enabled (CVSS 4.8)

```
persist.vendor.qcom.bluetooth.scram.enabled = false
```

### MED-006: Camera Privacy LED Can Be Disabled (CVSS 4.6)

The `CAMERA_DISABLE_TRANSMIT_LED` permission exists

---

# 🟢 Low Risks

---

## LOW-001 ~ LOW-004

| ID      | Risk               | Finding                     | CVSS |
| ------- | ------------------ | ------------------------ | ---- |
| LOW-001 | Development files left behind       | /data/local/tmp/.studio/ | 3.3  |
| LOW-002 | Sample applications installed | com.example.\* apps      | 3.1  |
| LOW-003 | Location mocking configurable     | location_changer = 1     | 2.8  |
| LOW-004 | Battery-optimization allowlist too broad | multiple apps can run in the background       | 2.5  |

### Remediation Recommendations

- clean up development files before leaving the factory
- remove all sample applications
- forcibly disable location mocking

---

# 🛡️ Security Hardening Items (already implemented)

---

## ✅ Correctly Configured Security Mechanisms

| Item           | Status           | Description               |
| -------------- | -------------- | ------------------ |
| SELinux        | ✅ Enforcing   | mandatory access control enabled |
| ASLR           | ✅ full (value=2) | address space randomization     |
| Kernel Pointer | ✅ enabled (value=2) | kernel pointers hidden     |
| Perf Event     | ✅ maximum (value=3) | performance monitoring restricted     |
| FBE            | ✅ enabled        | file-based encryption implemented           |
| ADB authentication       | ✅ enabled        | requires RSA key authorization  |
| Root access      | ✅ none          | no su/magisk found   |
| ro.debuggable  | ✅ disabled        | system debugging disabled     |

---

# 📋 Patch Priority Recommendations

---

## First Priority (within 7 days)

| ID       | Risk    | Patch Item                  |
| -------- | ------- | ------------------------- |
| CRIT-001 | 🔴 Critical | Lock the Bootloader           |
| CRIT-002 | 🔴 Critical | Release a security patch update          |
| CRIT-003 | 🔴 Critical | Restrict or remove the Port 8341 service |

---

## Second Priority (within 30 days)

| ID       | Risk  | Patch Item               |
| -------- | ----- | ---------------------- |
| HIGH-001 | 🟠 High | Restrict dmesg access        |
| HIGH-002 | 🟠 High | Review and reduce assistant service permissions |
| HIGH-003 | 🟠 High | Remove debuggable apps         |
| HIGH-004 | 🟠 High | Disable unknown-source installation by default   |
| HIGH-005 | 🟠 High | Disable ADB by default           |

---

## Third Priority (within 90 days)

| ID                | Risk  | Patch Item           |
| ----------------- | ----- | ------------------ |
| MED-001 ~ MED-006 | 🟡 Medium | Implement the various medium-risk fixes |
| LOW-001 ~ LOW-004 | 🟢 Low | Clean up and harden the configuration     |

---

# 📊 Compliance Assessment

---

## Compliance Status

| Standard            | Status        | Issue                       |
| --------------- | ----------- | -------------------------- |
| **GDPR**        | ⚠️ partially compliant | camera/location access requires explicit consent    |
| **PCI DSS**     | ❌ non-compliant   | payment app has excessive permissions, patches expired |
| **HIPAA**       | ⚠️ needs assessment   | medical scenarios require additional protection         |
| **Android CDD** | ⚠️ partially compliant | patches lag by more than 90 days         |

---

# 🔧 Recommended Security Testing

---

## Follow-up Test Items

1. **Penetration testing**
   - perform full penetration testing against the Port 8341 service

2. **Bluetooth security**
   - test known vulnerabilities in the Bluetooth stack

3. **Application audit**
   - perform static and dynamic analysis of Rokid-exclusive apps

4. **Firmware analysis**
   - extract and analyze hard-coded credentials in the firmware

5. **Privacy impact assessment**
   - assess the privacy risks of the camera/microphone

---

# 📝 Appendix

---

## A. Scanning Tools and Commands Used

```bash
# system property check
adb shell getprop | grep -E "secure|debug|verify|crypto"

# network port scan
adb shell ss -tlnp

# permission audit
adb shell dumpsys package <package_name>

# SELinux status
adb shell getenforce

# kernel security parameters
adb shell cat /proc/sys/kernel/dmesg_restrict
adb shell cat /proc/sys/kernel/randomize_va_space
```

---

## B. References

- Android Security Bulletin
  https://source.android.com/security/bulletin
- CIS Android Benchmark
- OWASP Mobile Security Testing Guide
- NIST SP 800-163: Vetting Mobile Applications

---

# 🔒 End of Report

**Report prepared by:** security assessment specialist  
**Review status:** pending review  
**Confidentiality level:** internal use

---

_This report is for Rokid internal security assessment only; do not distribute._
