# GateServiced Attack Analysis Summary

> 中文版：[ATTACK_SUMMARY.md](ATTACK_SUMMARY.md)

> ⚠ **2026-09-13 status correction (especially important for external documents)**: at the time this document was written **root had not yet been obtained**; **since 2026-08-22 runtime root has been obtained via the GhostLock LPE (CVE-2026-43499)** (`uid=0(root)`, `context=u:r:kernel:s0`, **SELinux Permissive**). The "SELinux Enforcing / Root Access: None" in the text represents only the state at that time; for the current flow see `docs/deploy.md` (armed once per boot via adb).


## Device: Rokid AR Glasses

## Date: 2026-02-02 (updated)

---

## 🔴 Latest finding: KGSL GPU driver vulnerability (2026-02-02)

### Vulnerability Summary

| Attribute   | Value                            |
| ------ | ----------------------------- |
| Type   | Cross-FD GPU Memory Access    |
| Severity | **Critical** (CVSS ~7.8)      |
| Status   | ✅ **confirmed**                 |
| Driver   | Qualcomm KGSL (msm_kgsl.ko)   |
| Device   | /dev/kgsl-3d0 (accessible with O_RDWR) |

### PoC Result

```
fd1 allocated: gpuaddr=0x4000000000
fd1 wrote secret: 0xaabbccdd11223344
fd2 mmap offset=0x1000 → reads: 0xaabbccdd11223344 ✅
fd2 modified → fd1 sees change ✅ CONFIRMED!
```

### Mapping Formula

```
offset = (page_index + 1) * 0x1000
gpuaddr = 0x4000000000 + (page_index * 0x1000)
```

### Security Impact

- **Sandbox escape**: the security isolation between different fds is bypassed
- **Information disclosure**: read sensitive GPU data of other fds
- **Memory corruption**: critical GPU structures can be modified

### Related Files

- `kgsl_cross_fd_exploit.c` - cross-FD PoC
- `KGSL_FINAL_REPORT.md` - complete report

---

## 🔓 Confirmed attack surface

### 0. KGSL GPU Driver (highest priority) ⭐ NEW

| Attribute     | Value                       |
| -------- | ------------------------ |
| Device     | /dev/kgsl-3d0            |
| Access permission | O_RDWR ✅                |
| GPU      | Adreno 621v1, SoC ID 579 |
| Vulnerability type | Cross-FD Memory Access   |
| Status     | **confirmed exploitable**         |

---

### 1. GateServiced (highest priority)

| Attribute         | Value                      |
| ------------ | ----------------------- |
| PID          | dynamic (currently: 5323)       |
| UID/GID      | 0/0 (root)              |
| Listening         | TCP 0.0.0.0:8341        |
| SELinux      | u:r:tee:s0              |
| Capabilities | 000001ffffffffff (all) |

**Protocol format:**

```
| cmd_id (4B) | rsv_len (4B) | payload |
```

**Current problem:** "TA is not already loaded" - a TrustZone TA must be loaded first

---

### 2. Writable partition findings

| Path                 | Permission            | Use              |
| -------------------- | --------------- | ----------------- |
| `/mnt/vendor/qmcs/`  | 777 (rwxrwxrwx) | ✅ can write arbitrary files |
| `/mnt/vendor/logfs/` | 755             | log storage          |
| `/data/local/tmp/`   | 755             | standard temporary directory      |

---

### 3. System property control

**Confirmed usable:**

```bash
# can modify persist properties
setprop persist.sys.usb.config adb,diag

# can control service start/stop
setprop ctl.restart GateServiced
```

---

### 4. TrustZone services

| Service               | Status                     | Note                            |
| ------------------ | ------------------------ | ------------------------------- |
| TZAccessService    | ✅ exported=true, no permission | binding can be attempted                      |
| SecCamService      | ✅ exported=true         | invocation can be attempted                      |
| qseecomd (PID 553) | running                   | QSEE daemon                   |
| IQSEECom HIDL      | usable                     | vendor.qti.hardware.qseecom@1.0 |

---

## 🎯 Attack vector priorities

### 🔴 Highest priority (confirmed exploitable)

1. **KGSL Cross-FD GPU Memory Access** ⭐ NEW
   - PoC verified: fd2 can read and write fd1's GPU memory
   - may lead to sandbox escape
   - should be reported as a CVE

### High priority

2. **TZAccessService binding**
   - exported=true
   - no permission restriction
   - may gain TrustZone access

3. **GateServiced TA loading research**
   - find the correct TA load command
   - study the QSEE HIDL interface

4. **BPF Heap Spray + TIPC UAF**
   - unprivileged_bpf_disabled=0
   - 224k+ TIPC operations succeeded
   - can be combined with the KGSL vulnerability

5. **Service control + environment injection**
   - use `ctl.restart` to restart the service
   - environment variables may be injected in some way

### Medium priority

6. **qmcs partition exploitation**
   - 777 permission, writable
   - a service that reads this directory needs to be found

7. **Frida Hook (requires bypassing SELinux)**
   - currently fails because of SELinux Enforcing
   - a permissive state or a bypass method needs to be found

### Low priority

8. **Other kernel vulnerabilities**
   - most have already been patched
   - a 0-day is needed

---

## 📝 Test scripts on the device

```bash
# location: /data/local/tmp/

# 1. basic protocol test
/data/local/tmp/gate_exploit.sh -all

# 2. advanced protocol analysis
/data/local/tmp/gate_adv.sh -monitor

# 3. RCE test
/data/local/tmp/gate_rce.sh -all

# 4. TZ service invocation
/data/local/tmp/tz_call.sh
```

---

## 🔬 Next research steps

1. **Study the QSEE HIDL interface**

   ```bash
   lshal debug vendor.qti.hardware.qseecom@1.0::IQSEECom/default
   ```

2. **Create an APK to bind TZAccessService**
   - the code is already in the `TZAccessApp/` directory
   - it needs to be compiled and installed

3. **Frida alternatives**
   - try a Zygisk module
   - or use app_process + dex

4. **Monitor GateServiced startup**
   ```bash
   logcat -c && setprop ctl.restart GateServiced && logcat -d | grep Gate
   ```

---

## 🔐 Security configuration summary

```
SELinux: Enforcing (**boot default**; Permissive after escalation, corrected 2026-09-13)
ro.secure: 1
ro.debuggable: 0
ro.build.type: user
kernel.kptr_restrict: 2
kernel.perf_event_paranoid: 3
kernel.unprivileged_bpf_disabled: 0  # BPF available!
```

---

## 🆕 Latest finding (2026-02-02 10:10)

### 🔴 Root cause found: GateServiced lacks the drmrpc group permission

**This is the root cause of the TA failing to load!**

| Service              | User   | Group                       | Can access /dev/qseecom |
| ----------------- | ------ | ------------------------ | ------------------- |
| qseecomd          | root   | root, **drmrpc**, system | ✅                  |
| qseecom-service   | system | system, **drmrpc**       | ✅                  |
| **GateServiced**  | root   | root, system             | ❌ **missing drmrpc**  |
| graphics_composer | system | graphics, **drmrpc**     | ✅                  |

**GateServiced init.rc configuration:**

```bash
# /vendor/etc/init/init.gateserviced.rc
service GateServiced /vendor/bin/GateServiced
    user root
    group root system    # missing drmrpc!
    disabled
on property:sys.boot_completed=1
    start GateServiced
```

**Conclusion:** GateServiced cannot access `/dev/qseecom` directly and must load the TA through the QSEE HIDL service.

---

### QSEE HIDL API findings

Methods extracted from `vendor.qti.hardware.qseecom@1.0-impl.so`:

```c
QSEECom_start_app          // start the TA
QSEECom_shutdown_app       // shut down the TA
QSEECom_send_cmd           // send a command
QSEECom_app_load_query     // query the TA load state
QSEECom_register_listener  // register a listener
QSEECom_send_modified_cmd  // send a modified command
```

**HIDL service path:** `vendor.qti.hardware.qseecom@1.0::IQSEECom/default`

---

### graphics_composer exploitation potential

**Key finding:** graphics_composer has the drmrpc group permission and can access qmcs!

```bash
# /vendor/etc/init/vendor.qti.hardware.display.composer-service.rc
service vendor.qti.hardware.display.composer ...
    user system
    group graphics drmrpc   # has drmrpc permission!
    capabilities SYS_NICE
```

**SELinux policy (vendor_sepolicy.cil):**

```lisp
(allow hal_graphics_composer_default vendor_qmcs_file
  (dir (ioctl read write create getattr setattr lock rename open ...)))
(allow hal_graphics_composer_default vendor_qmcs_file
  (file (ioctl read write create getattr setattr lock append map unlink rename open ...)))
```

**Attack idea:**

1. Place a malicious configuration file in `/mnt/vendor/qmcs/`
2. If graphics_composer reads and processes that configuration
3. Code execution or privilege escalation may be triggered

---

### 1. QSEE kernel module already loaded

```
QSEECOM: qseecom_init_control: qseecom.qsee_version = 0x1402000
DMA-BUF Heap: Created qcom,qseecom
DMA-BUF Heap: Created qcom,qseecom-ta
```

### 2. qseecomd Socket

- Path: `/dev/socket/notify-topology`
- Permission: `srw-rw---- system drmrpc`
- Problem: the shell user is not in the drmrpc group

### 3. Magisk App installed (but not rooted)

- Package: `com.topjohnwu.magisk`
- Version: 30.6
- Status: only the app, no root binary

### 4. SELinux context

- `/data/local/tmp/`: `u:object_r:shell_data_file:s0`
- `/mnt/vendor/qmcs/`: `u:object_r:vendor_qmcs_file:s0`
- GateServiced: `u:r:tee:s0`

### 5. graphics_composer has qmcs access permission

- Process: `vendor.qti.hardware.display.composer-service` (PID 801)
- Can read and write: `/mnt/vendor/qmcs/`

---

## 🎯 Recommended attack paths (updated priorities)

### 🔴 Highest priority ⭐ UPDATED

0. **KGSL Cross-FD GPU Memory Exploit** 🆕
   - Status: **confirmed exploitable**
   - Goal: use cross-FD memory access to achieve information disclosure or memory corruption
   - PoC: `kgsl_cross_fd_exploit.c` verified
   - Next steps:
     - combine with BPF spray to build a complete exploit chain
     - study the possibility of cross-process exploitation (currently confirmed not feasible)
     - report a CVE to Qualcomm

1. **DRM service escalation to obtain the drmrpc group**
   - Goal: obtain the drmrpc group permission to access `/dev/qseecom`
   - Method: study the `drm.drmManager` service
   - Rationale: this is the root cause of GateServiced being unable to load the TA

2. **Direct QSEE HIDL invocation**
   - Goal: bypass GateServiced and call QSEE HIDL directly
   - Method: create an APK that uses `vendor.qti.hardware.qseecom@1.0::IQSEECom`
   - API: `QSEECom_start_app(path, app_name, size, callback)`

### 🟡 Medium priority

3. **TZAccessService APK binding**
   - Service: `com.qualcomm.qti.qms.service.trustzoneaccess/.TZAccessService`
   - Action: `com.qualcomm.qti.qms.service.trustzoneaccess.TZAccessService`
   - Note: the Foreground Service notification must be handled correctly

4. **graphics_composer configuration injection**
   - Path: `/mnt/vendor/qmcs/`
   - Goal: find the format of the configuration file the composer reads
   - Potential: code execution or IPC hijacking

### 🟢 Low priority

5. **Modify GateServiced init.rc**
   - Requires: modifying the vendor partition (needs an unlocked bootloader + re-signing)
   - Operation: add `group root system drmrpc`
6. **Frida + Zygisk**
   - Obstacle: SELinux Enforcing
   - Requires: a complete Magisk installation + Zygisk

---

## 📋 Session record (2026-02-02 10:15)

### Summary of this test session

#### 1. Complete GateServiced protocol analysis

- **Protocol format confirmed**: `cmd_id(4B LE ASCII) + rsv_len(4B) + payload`
- **cmd_id values tested**:
  - `676e6970` = "ping"
  - `74617473` = "stat"
  - `6f666e69` = "info"
  - `73726576` = "vers"
  - `706c6568` = "help"
  - `45544147` = "GATE"
- **Result**: all commands returned `ret:300c` ("TA is not already loaded")

#### 2. QSEE subsystem analysis

```
QSEECOM version: 0x1402000
qseecomd PID: 553/554
HIDL: vendor.qti.hardware.qseecom@1.0::IQSEECom/default
Socket: /dev/socket/notify-topology (srw-rw---- system drmrpc)
```

- the shell user is not in the drmrpc group (GID 1019)
- the QSEE socket cannot be accessed directly

#### 3. TZAccessService test

- Package: `com.qualcomm.qti.qms.service.trustzoneaccess`
- exported=true, no permission required
- **Problem**: it crashes after using `am start-foreground-service`
- Error: `ForegroundServiceDidNotStartInTimeException`
- a self-built APK is needed to bind the service correctly

#### 4. Magisk status

- App installed: `com.topjohnwu.magisk` v30.6
- **Not active**: no root binary, no Magisk daemon
- possibly left over from a previous installation attempt

#### 5. Permission enumeration

- No SUID/SGID binaries found
- `/mnt/vendor/qmcs/` confirmed to have 777 permission
- files are created with owner root:root
- `/data/vendor/`, `/cache/`, `/persist/` cannot be accessed

### Key obstacles

| Problem              | Impact                      | Possible solution            |
| ----------------- | ------------------------- | ----------------------- |
| TA not loaded         | GateServiced cannot execute commands | find the TA load trigger      |
| drmrpc group restriction     | cannot access the QSEE socket      | DRM service escalation            |
| SELinux Enforcing | Frida cannot attach         | use Zygisk or a kernel bypass  |
| user build        | no adb root               | needs a vulnerability or a modified boot.img |

### Scripts already deployed on the device

```
/data/local/tmp/gate_exploit.sh        - basic protocol test
/data/local/tmp/gate_adv.sh            - advanced TA load test
/data/local/tmp/gate_rce.sh            - RCE vector test
/data/local/tmp/tz_call.sh             - TZ service invocation
/data/local/tmp/priv_enum.sh           - permission enumeration
/data/local/tmp/ta_monitor.sh          - TA load monitoring
/data/local/tmp/kgsl_cross_fd_exploit  - 🔴 KGSL cross-FD PoC
/data/local/tmp/kgsl_offset_exploit    - KGSL offset mapping
/data/local/tmp/kgsl_fd_confusion_exploit - FD confusion test
/data/local/tmp/kgsl_gpu_reset_exploit - GPU reset research
```

---

## 📋 Session record (2026-02-02 Latest) ⭐ NEW

### KGSL GPU driver vulnerability research

#### 1. Cross-FD GPU memory access (Critical)

- **Vulnerability type**: Cross-FD Memory Access
- **PoC result**: fd2 successfully read fd1's secret 0xaabbccdd11223344
- **Modification confirmed**: after fd2 modified it, fd1 observed the change
- **Mapping formula**: `offset = (page_index + 1) * 0x1000`

#### 2. The 6 working ioctls found

| ioctl         | Function                       |
| ------------- | -------------------------- |
| 0xc018092f    | GPUMEM_ALLOC               |
| 0xc0040938-3a | GPU Info                   |
| 0xc0040940    | Resource Alloc             |
| 0xc0040956    | SYNCSOURCE_CREATE_FENCE    |
| 0xc0200958    | GPU_AUX_COMMAND            |
| 0xc0300948    | Wait (returns ERESTARTSYS) |

#### 3. Additional findings

- Double mmap points at the same physical page
- 100/100 mappings remain valid after the fd is closed
- the GPU reset count is stable (34, not increasing)

### Suggested attack chain

```
KGSL Cross-FD (info leak) → BPF Spray (object placement) → Root
```

---

**Author**: <USER>  
**Status**: research in progress - the KGSL vulnerability is confirmed  
**Last updated**: 2026-02-02 (KGSL update)
