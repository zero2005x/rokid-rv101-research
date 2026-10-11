# Rokid AR Glasses - Research Findings Summary

> 中文版：[RESEARCH_FINDINGS_20260202.md](RESEARCH_FINDINGS_20260202.md)

> ⚠ **2026-09-13 correction**: the **Fastboot/Magisk/boot.img flashing route mentioned in this document is confirmed infeasible** (boot.img cannot be obtained: `fastboot fetch` is unsupported, `dd` requires root, and there is no compatible recovery/OTA source), and **root was acquired on 2026-08-22 via the GhostLock LPE** (at runtime, armed once per boot over adb). Treat the following sections as a historical assessment and do not follow the flashing steps.


## 2026-02-02 Session

---

## 🔴 Root-Cause Finding

### Why GateServiced Cannot Load a TA

**GateServiced is missing the `drmrpc` group permission!**

```bash
# GateServiced configuration (/vendor/etc/init/init.gateserviced.rc)
service GateServiced /vendor/bin/GateServiced
    user root
    group root system    # missing drmrpc!
    disabled
```

And the `/dev/qseecom` device permissions are:

```
crw-rw---- system drmrpc /dev/qseecom
```

This means GateServiced cannot access the QSEE device directly; it must load the TA through the QSEE HIDL service.

---

## 📊 drmrpc Group Permission Analysis

| Service           | User         | Group                          | Can access /dev/qseecom |
| ----------------- | ------------ | ------------------------------ | ------------------- |
| qseecomd          | root         | root, **drmrpc**, system       | ✅                  |
| qseecom-service   | system       | system, **drmrpc**             | ✅                  |
| graphics_composer | system       | graphics, **drmrpc**           | ✅                  |
| display.allocator | system       | graphics, **drmrpc**           | ✅                  |
| keymaster/keymint | system       | system, **drmrpc**             | ✅                  |
| media.c2          | mediadrm     | camera, **drmrpc**, system     | ✅                  |
| audio.service     | audio        | audio, camera, **drmrpc**, ... | ✅                  |
| camera.provider   | cameraserver | audio, camera, **drmrpc**, ... | ✅                  |
| **GateServiced**  | **root**     | **root, system**               | ❌                  |

---

## 🔬 QSEE HIDL API

### Methods Found (from vendor.qti.hardware.qseecom@1.0-impl.so)

```c
// TA lifecycle
QSEECom_start_app(path, app_name, size, callback)
QSEECom_shutdown_app(handle)
QSEECom_app_load_query(handle, app_name)

// Command communication
QSEECom_send_cmd(handle, req_buf, rsp_buf)
QSEECom_send_modified_cmd(handle, req_buf, rsp_buf, mod_info, is_64bit)
QSEECom_send_modified_cmd_64(...)

// Listener
QSEECom_register_listener(fd, size, flags, callback)
QSEECom_unregister_listener(handle)

// Request/response
QSEECom_receive_req(handle, req_buf)
QSEECom_send_resp(handle, rsp_buf)
QSEECom_send_modified_resp(...)

// Other
QSEECom_set_bandwidth(handle, high_bandwidth)
```

### HIDL Service Path

```
vendor.qti.hardware.qseecom@1.0::IQSEECom/default (PID 554)
```

---

## 🎯 Attack Vector Update

### Priority 1: Obtain drmrpc Group Permissions

**Method A: Via the DRM service**

- Service: `drm.drmManager` (PID 883, user: drm)
- Test: `service call drm.drmManager 1` returned success
- Potential: permissions might be obtained through Binder IPC

**Method B: Via graphics_composer**

- Process: PID 801, group: graphics, **drmrpc**
- qmcs write: `/mnt/vendor/qmcs/` (777)
- Potential: configuration injection leading to code execution

**Method C: Via audio.service**

- Has the drmrpc group
- Possibly through AudioFlinger IPC

### Priority 2: Direct QSEE HIDL Invocation

Create an APK that uses the HIDL interface:

```java
IQSEECom qseecom = IQSEECom.getService("default");
qseecom.startApp("path", "ta_name", size, callback);
```

### Priority 3: TZAccessService

- Package: `com.qualcomm.qti.qms.service.trustzoneaccess`
- Action: `com.qualcomm.qti.qms.service.trustzoneaccess.TZAccessService`
- Issue: needs to handle the Foreground Service notification

---

## 📁 Deployed Scripts

```
/data/local/tmp/
├── gate_exploit.sh       # basic protocol test
├── gate_adv.sh          # advanced TA load test
├── gate_rce.sh          # RCE vector test
├── tz_call.sh           # TZ service call
├── priv_enum.sh         # privilege enumeration
├── ta_monitor.sh        # TA load monitoring
├── tz_bind_test.sh      # TZAccessService bind test
├── qsee_research.sh     # QSEE HIDL research
├── gate_ta_load.sh      # GateServiced TA load test
└── drm_analysis.sh      # DRM service analysis
```

---

## 📝 Next Actions

1. **Create an APK that calls QSEE HIDL**
   - Use `vendor.qti.hardware.qseecom@1.0::IQSEECom`
   - Try `startApp` to load GateServiced's TA

2. **Study graphics_composer configuration parsing**
   - Analyze how the composer uses `/mnt/vendor/qmcs/`
   - Test various configuration file formats

3. **In-depth DRM service analysis**
   - Study the `drm.drmManager` Binder interface
   - Try to trigger a drmrpc group context switch

4. **Modify boot.img (last resort)**
   - Extract boot.img
   - Modify init.gateserviced.rc to add the drmrpc group
   - Repack and flash

---

**Last updated**: 2026-02-02 10:20
