# Rokid AR Glasses - 研究發現總結

> English: [RESEARCH_FINDINGS_20260202_EN.md](RESEARCH_FINDINGS_20260202_EN.md)

> ⚠ **2026-09-13 更正**：本文提及的 **Fastboot／Magisk／boot.img 刷機路線已確認不可行**（取不到 boot.img：`fastboot fetch` 不支援、`dd` 需 root、無相容 recovery／OTA 來源），且 **root 已於 2026-08-22 由 GhostLock LPE 取得**（執行期，每 boot 以 adb 武裝一次）。請以下列段落為歷史評估，勿照做刷機步驟。


## 2026-02-02 Session

---

## 🔴 根本原因發現

### GateServiced 無法載入 TA 的原因

**GateServiced 缺少 `drmrpc` 組權限！**

```bash
# GateServiced 配置 (/vendor/etc/init/init.gateserviced.rc)
service GateServiced /vendor/bin/GateServiced
    user root
    group root system    # 缺少 drmrpc!
    disabled
```

而 `/dev/qseecom` 設備權限為：

```
crw-rw---- system drmrpc /dev/qseecom
```

這意味著 GateServiced 無法直接訪問 QSEE 設備，必須通過 QSEE HIDL 服務來載入 TA。

---

## 📊 drmrpc 組權限分析

| 服務              | 用戶         | 組                             | 可訪問 /dev/qseecom |
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

### 發現的方法 (from vendor.qti.hardware.qseecom@1.0-impl.so)

```c
// TA 生命週期
QSEECom_start_app(path, app_name, size, callback)
QSEECom_shutdown_app(handle)
QSEECom_app_load_query(handle, app_name)

// 命令通信
QSEECom_send_cmd(handle, req_buf, rsp_buf)
QSEECom_send_modified_cmd(handle, req_buf, rsp_buf, mod_info, is_64bit)
QSEECom_send_modified_cmd_64(...)

// 監聽器
QSEECom_register_listener(fd, size, flags, callback)
QSEECom_unregister_listener(handle)

// 請求/響應
QSEECom_receive_req(handle, req_buf)
QSEECom_send_resp(handle, rsp_buf)
QSEECom_send_modified_resp(...)

// 其他
QSEECom_set_bandwidth(handle, high_bandwidth)
```

### HIDL 服務路徑

```
vendor.qti.hardware.qseecom@1.0::IQSEECom/default (PID 554)
```

---

## 🎯 攻擊向量更新

### 優先級 1: 獲取 drmrpc 組權限

**方法 A: 通過 DRM 服務**

- 服務: `drm.drmManager` (PID 883, user: drm)
- 測試: `service call drm.drmManager 1` 返回成功
- 潛力: 可能通過 Binder IPC 獲取權限

**方法 B: 通過 graphics_composer**

- 進程: PID 801, group: graphics, **drmrpc**
- qmcs 寫入: `/mnt/vendor/qmcs/` (777)
- 潛力: 配置注入導致代碼執行

**方法 C: 通過 audio.service**

- 有 drmrpc 組
- 可能通過 AudioFlinger IPC

### 優先級 2: QSEE HIDL 直接調用

創建 APK 使用 HIDL 接口：

```java
IQSEECom qseecom = IQSEECom.getService("default");
qseecom.startApp("path", "ta_name", size, callback);
```

### 優先級 3: TZAccessService

- Package: `com.qualcomm.qti.qms.service.trustzoneaccess`
- Action: `com.qualcomm.qti.qms.service.trustzoneaccess.TZAccessService`
- 問題: 需要處理 Foreground Service 通知

---

## 📁 已部署的腳本

```
/data/local/tmp/
├── gate_exploit.sh       # 基礎協議測試
├── gate_adv.sh          # 進階 TA 載入測試
├── gate_rce.sh          # RCE 向量測試
├── tz_call.sh           # TZ 服務調用
├── priv_enum.sh         # 權限枚舉
├── ta_monitor.sh        # TA 載入監控
├── tz_bind_test.sh      # TZAccessService 綁定測試
├── qsee_research.sh     # QSEE HIDL 研究
├── gate_ta_load.sh      # GateServiced TA 載入測試
└── drm_analysis.sh      # DRM 服務分析
```

---

## 📝 下一步行動

1. **創建 APK 調用 QSEE HIDL**
   - 使用 `vendor.qti.hardware.qseecom@1.0::IQSEECom`
   - 嘗試 `startApp` 載入 GateServiced 的 TA

2. **研究 graphics_composer 配置解析**
   - 分析 composer 如何使用 `/mnt/vendor/qmcs/`
   - 測試各種配置文件格式

3. **DRM 服務深入分析**
   - 研究 `drm.drmManager` Binder 接口
   - 嘗試觸發 drmrpc 組上下文切換

4. **修改 boot.img (最後手段)**
   - 提取 boot.img
   - 修改 init.gateserviced.rc 添加 drmrpc 組
   - 重新打包並刷入

---

**最後更新**: 2026-02-02 10:20
