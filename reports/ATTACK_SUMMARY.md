# GateServiced 攻擊分析總結

> English: [ATTACK_SUMMARY_EN.md](ATTACK_SUMMARY_EN.md)

> ⚠ **2026-09-13 狀態更正（對外文件尤須注意）**：本檔寫作時 **root 尚未取得**；**2026-08-22 起已由 GhostLock LPE（CVE-2026-43499）取得執行期 root**（`uid=0(root)`、`context=u:r:kernel:s0`、**SELinux Permissive**）。文中「SELinux Enforcing／Root Access: None」僅代表當時狀態；現行流程見 `docs/deploy.md`（每 boot 以 adb 武裝一次）。


## 設備: Rokid AR Glasses

## 日期: 2026-02-02 (更新)

---

## 🔴 最新發現: KGSL GPU 驅動漏洞 (2026-02-02)

### 漏洞摘要

| 屬性   | 值                            |
| ------ | ----------------------------- |
| 類型   | Cross-FD GPU Memory Access    |
| 嚴重性 | **Critical** (CVSS ~7.8)      |
| 狀態   | ✅ **已確認**                 |
| 驅動   | Qualcomm KGSL (msm_kgsl.ko)   |
| 設備   | /dev/kgsl-3d0 (O_RDWR 可訪問) |

### PoC 結果

```
fd1 allocated: gpuaddr=0x4000000000
fd1 wrote secret: 0xaabbccdd11223344
fd2 mmap offset=0x1000 → reads: 0xaabbccdd11223344 ✅
fd2 modified → fd1 sees change ✅ CONFIRMED!
```

### 映射公式

```
offset = (page_index + 1) * 0x1000
gpuaddr = 0x4000000000 + (page_index * 0x1000)
```

### 安全影響

- **沙箱逃逸**: 不同 fd 的安全隔離被繞過
- **資訊洩露**: 讀取其他 fd 的 GPU 敏感資料
- **記憶體損壞**: 可修改關鍵 GPU 結構

### 相關文件

- `kgsl_cross_fd_exploit.c` - 跨 FD PoC
- `KGSL_FINAL_REPORT.md` - 完整報告

---

## 🔓 已確認的攻擊表面

### 0. KGSL GPU Driver (最高優先級) ⭐ NEW

| 屬性     | 值                       |
| -------- | ------------------------ |
| 設備     | /dev/kgsl-3d0            |
| 訪問權限 | O_RDWR ✅                |
| GPU      | Adreno 621v1, SoC ID 579 |
| 漏洞類型 | Cross-FD Memory Access   |
| 狀態     | **已確認可利用**         |

---

### 1. GateServiced (最高優先級)

| 屬性         | 值                      |
| ------------ | ----------------------- |
| PID          | 動態 (當前: 5323)       |
| UID/GID      | 0/0 (root)              |
| 監聽         | TCP 0.0.0.0:8341        |
| SELinux      | u:r:tee:s0              |
| Capabilities | 000001ffffffffff (全部) |

**協議格式:**

```
| cmd_id (4B) | rsv_len (4B) | payload |
```

**當前問題:** "TA is not already loaded" - 需要先載入 TrustZone TA

---

### 2. 可寫分區發現

| 路徑                 | 權限            | 用途              |
| -------------------- | --------------- | ----------------- |
| `/mnt/vendor/qmcs/`  | 777 (rwxrwxrwx) | ✅ 可寫入任意文件 |
| `/mnt/vendor/logfs/` | 755             | 日誌存儲          |
| `/data/local/tmp/`   | 755             | 標準臨時目錄      |

---

### 3. 系統屬性控制

**已確認可用:**

```bash
# 可修改 persist 屬性
setprop persist.sys.usb.config adb,diag

# 可控制服務啟停
setprop ctl.restart GateServiced
```

---

### 4. TrustZone 服務

| 服務               | 狀態                     | 說明                            |
| ------------------ | ------------------------ | ------------------------------- |
| TZAccessService    | ✅ exported=true, 無權限 | 可嘗試綁定                      |
| SecCamService      | ✅ exported=true         | 可嘗試調用                      |
| qseecomd (PID 553) | 運行中                   | QSEE 守護進程                   |
| IQSEECom HIDL      | 可用                     | vendor.qti.hardware.qseecom@1.0 |

---

## 🎯 攻擊向量優先級

### 🔴 最高優先級 (已確認可利用)

1. **KGSL Cross-FD GPU Memory Access** ⭐ NEW
   - PoC 已驗證: fd2 可讀寫 fd1 的 GPU 記憶體
   - 可能導致沙箱逃逸
   - 應報告為 CVE

### 高優先級

2. **TZAccessService 綁定**
   - exported=true
   - 無權限限制
   - 可能獲取 TrustZone 訪問

3. **GateServiced TA 載入研究**
   - 找到正確的 TA 載入命令
   - 研究 QSEE HIDL 接口

4. **BPF Heap Spray + TIPC UAF**
   - unprivileged_bpf_disabled=0
   - 224k+ TIPC 操作成功
   - 可與 KGSL 漏洞組合

5. **服務控制 + 環境注入**
   - 使用 `ctl.restart` 重啟服務
   - 可能通過某種方式注入環境變數

### 中優先級

6. **qmcs 分區利用**
   - 777 權限可寫
   - 需要找到讀取此目錄的服務

7. **Frida Hook (需要繞過 SELinux)**
   - 目前因 SELinux Enforcing 失敗
   - 需要找到 permissive 或繞過方法

### 低優先級

8. **其他內核漏洞**
   - 大多數已被修補
   - 需要 0-day

---

## 📝 設備上的測試腳本

```bash
# 位置: /data/local/tmp/

# 1. 基礎協議測試
/data/local/tmp/gate_exploit.sh -all

# 2. 進階協議分析
/data/local/tmp/gate_adv.sh -monitor

# 3. RCE 測試
/data/local/tmp/gate_rce.sh -all

# 4. TZ 服務調用
/data/local/tmp/tz_call.sh
```

---

## 🔬 下一步研究

1. **研究 QSEE HIDL 接口**

   ```bash
   lshal debug vendor.qti.hardware.qseecom@1.0::IQSEECom/default
   ```

2. **創建 APK 綁定 TZAccessService**
   - 代碼已在 `TZAccessApp/` 目錄
   - 需要編譯並安裝

3. **Frida 替代方案**
   - 嘗試 Zygisk 模組
   - 或使用 app_process + dex

4. **監控 GateServiced 啟動**
   ```bash
   logcat -c && setprop ctl.restart GateServiced && logcat -d | grep Gate
   ```

---

## 🔐 安全配置總結

```
SELinux: Enforcing（**開機預設**；提權後 Permissive，2026-09-13 更正）
ro.secure: 1
ro.debuggable: 0
ro.build.type: user
kernel.kptr_restrict: 2
kernel.perf_event_paranoid: 3
kernel.unprivileged_bpf_disabled: 0  # BPF 可用!
```

---

## 🆕 最新發現 (2026-02-02 10:10)

### 🔴 根本原因發現：GateServiced 缺少 drmrpc 組權限

**這是 TA 無法載入的根本原因！**

| 服務              | 用戶   | 組                       | 可訪問 /dev/qseecom |
| ----------------- | ------ | ------------------------ | ------------------- |
| qseecomd          | root   | root, **drmrpc**, system | ✅                  |
| qseecom-service   | system | system, **drmrpc**       | ✅                  |
| **GateServiced**  | root   | root, system             | ❌ **缺少 drmrpc**  |
| graphics_composer | system | graphics, **drmrpc**     | ✅                  |

**GateServiced init.rc 配置:**

```bash
# /vendor/etc/init/init.gateserviced.rc
service GateServiced /vendor/bin/GateServiced
    user root
    group root system    # 缺少 drmrpc!
    disabled
on property:sys.boot_completed=1
    start GateServiced
```

**結論:** GateServiced 無法直接訪問 `/dev/qseecom`，必須通過 QSEE HIDL 服務載入 TA。

---

### QSEE HIDL API 發現

從 `vendor.qti.hardware.qseecom@1.0-impl.so` 提取的方法：

```c
QSEECom_start_app          // 啟動 TA
QSEECom_shutdown_app       // 關閉 TA
QSEECom_send_cmd           // 發送命令
QSEECom_app_load_query     // 查詢 TA 載入狀態
QSEECom_register_listener  // 註冊監聽器
QSEECom_send_modified_cmd  // 發送修改過的命令
```

**HIDL 服務路徑:** `vendor.qti.hardware.qseecom@1.0::IQSEECom/default`

---

### graphics_composer 利用潛力

**關鍵發現:** graphics_composer 有 drmrpc 組權限且可訪問 qmcs！

```bash
# /vendor/etc/init/vendor.qti.hardware.display.composer-service.rc
service vendor.qti.hardware.display.composer ...
    user system
    group graphics drmrpc   # 有 drmrpc 權限!
    capabilities SYS_NICE
```

**SELinux 策略 (vendor_sepolicy.cil):**

```lisp
(allow hal_graphics_composer_default vendor_qmcs_file
  (dir (ioctl read write create getattr setattr lock rename open ...)))
(allow hal_graphics_composer_default vendor_qmcs_file
  (file (ioctl read write create getattr setattr lock append map unlink rename open ...)))
```

**攻擊思路:**

1. 在 `/mnt/vendor/qmcs/` 放置惡意配置文件
2. 如果 graphics_composer 讀取並處理該配置
3. 可能觸發代碼執行或權限提升

---

### 1. QSEE 內核模組已載入

```
QSEECOM: qseecom_init_control: qseecom.qsee_version = 0x1402000
DMA-BUF Heap: Created qcom,qseecom
DMA-BUF Heap: Created qcom,qseecom-ta
```

### 2. qseecomd Socket

- 路徑: `/dev/socket/notify-topology`
- 權限: `srw-rw---- system drmrpc`
- 問題: shell 用戶不在 drmrpc 組

### 3. Magisk App 已安裝 (但未 root)

- Package: `com.topjohnwu.magisk`
- Version: 30.6
- 狀態: 只有 app，無 root 二進制

### 4. SELinux 上下文

- `/data/local/tmp/`: `u:object_r:shell_data_file:s0`
- `/mnt/vendor/qmcs/`: `u:object_r:vendor_qmcs_file:s0`
- GateServiced: `u:r:tee:s0`

### 5. graphics_composer 有 qmcs 訪問權限

- 進程: `vendor.qti.hardware.display.composer-service` (PID 801)
- 可讀寫: `/mnt/vendor/qmcs/`

---

## 🎯 推薦攻擊路徑 (優先級更新)

### 🔴 最高優先級 ⭐ UPDATED

0. **KGSL Cross-FD GPU Memory Exploit** 🆕
   - 狀態: **已確認可利用**
   - 目標: 利用跨 FD 記憶體訪問實現資訊洩露或記憶體損壞
   - PoC: `kgsl_cross_fd_exploit.c` 已驗證
   - 下一步:
     - 組合 BPF spray 構建完整利用鏈
     - 研究跨進程利用可能性 (目前確認不可行)
     - 報告 CVE 給 Qualcomm

1. **DRM 服務提權獲取 drmrpc 組**
   - 目標: 獲取 drmrpc 組權限以訪問 `/dev/qseecom`
   - 方法: 研究 `drm.drmManager` 服務
   - 理由: 這是 GateServiced 無法載入 TA 的根本原因

2. **QSEE HIDL 直接調用**
   - 目標: 繞過 GateServiced 直接調用 QSEE HIDL
   - 方法: 創建 APK 使用 `vendor.qti.hardware.qseecom@1.0::IQSEECom`
   - API: `QSEECom_start_app(path, app_name, size, callback)`

### 🟡 中等優先級

3. **TZAccessService APK 綁定**
   - 服務: `com.qualcomm.qti.qms.service.trustzoneaccess/.TZAccessService`
   - Action: `com.qualcomm.qti.qms.service.trustzoneaccess.TZAccessService`
   - 注意: 需要正確處理 Foreground Service 通知

4. **graphics_composer 配置注入**
   - 路徑: `/mnt/vendor/qmcs/`
   - 目標: 找到 composer 讀取的配置文件格式
   - 潛力: 代碼執行或 IPC 劫持

### 🟢 低優先級

5. **修改 GateServiced init.rc**
   - 需要: 修改 vendor 分區 (需要解鎖 bootloader + 重新簽名)
   - 操作: 添加 `group root system drmrpc`
6. **Frida + Zygisk**
   - 障礙: SELinux Enforcing
   - 需要: Magisk 完整安裝 + Zygisk

---

## 📋 Session 記錄 (2026-02-02 10:15)

### 本次測試總結

#### 1. GateServiced 完整協議分析

- **協議格式確認**: `cmd_id(4B LE ASCII) + rsv_len(4B) + payload`
- **測試的 cmd_id**:
  - `676e6970` = "ping"
  - `74617473` = "stat"
  - `6f666e69` = "info"
  - `73726576` = "vers"
  - `706c6568` = "help"
  - `45544147` = "GATE"
- **結果**: 所有命令均返回 `ret:300c` ("TA is not already loaded")

#### 2. QSEE 子系統分析

```
QSEECOM version: 0x1402000
qseecomd PID: 553/554
HIDL: vendor.qti.hardware.qseecom@1.0::IQSEECom/default
Socket: /dev/socket/notify-topology (srw-rw---- system drmrpc)
```

- shell 用戶不在 drmrpc 組 (GID 1019)
- 無法直接訪問 QSEE socket

#### 3. TZAccessService 測試

- Package: `com.qualcomm.qti.qms.service.trustzoneaccess`
- exported=true, 無權限要求
- **問題**: 使用 `am start-foreground-service` 後崩潰
- 錯誤: `ForegroundServiceDidNotStartInTimeException`
- 需要自建 APK 來正確綁定服務

#### 4. Magisk 狀態

- App 已安裝: `com.topjohnwu.magisk` v30.6
- **未激活**: 無 root 二進制，無 Magisk daemon
- 可能是之前的安裝嘗試遺留

#### 5. 權限枚舉

- 未發現 SUID/SGID 二進制
- `/mnt/vendor/qmcs/` 確認 777 權限
- 文件創建時 owner 為 root:root
- 無法訪問 `/data/vendor/`, `/cache/`, `/persist/`

### 關鍵阻礙

| 問題              | 影響                      | 可能解決方案            |
| ----------------- | ------------------------- | ----------------------- |
| TA 未載入         | GateServiced 無法執行命令 | 找到 TA 載入觸發器      |
| drmrpc 組限制     | 無法訪問 QSEE socket      | DRM 服務提權            |
| SELinux Enforcing | Frida 無法 attach         | 使用 Zygisk 或內核繞過  |
| user build        | 無 adb root               | 需要漏洞或修改 boot.img |

### 設備上已部署的腳本

```
/data/local/tmp/gate_exploit.sh        - 基礎協議測試
/data/local/tmp/gate_adv.sh            - 進階 TA 載入測試
/data/local/tmp/gate_rce.sh            - RCE 向量測試
/data/local/tmp/tz_call.sh             - TZ 服務調用
/data/local/tmp/priv_enum.sh           - 權限枚舉
/data/local/tmp/ta_monitor.sh          - TA 載入監控
/data/local/tmp/kgsl_cross_fd_exploit  - 🔴 KGSL 跨 FD PoC
/data/local/tmp/kgsl_offset_exploit    - KGSL offset 映射
/data/local/tmp/kgsl_fd_confusion_exploit - FD 混淆測試
/data/local/tmp/kgsl_gpu_reset_exploit - GPU 重置研究
```

---

## 📋 Session 記錄 (2026-02-02 Latest) ⭐ NEW

### KGSL GPU 驅動漏洞研究

#### 1. 跨 FD GPU 記憶體訪問 (Critical)

- **漏洞類型**: Cross-FD Memory Access
- **PoC 結果**: fd2 成功讀取 fd1 的 secret 0xaabbccdd11223344
- **修改確認**: fd2 修改後 fd1 觀察到變化
- **映射公式**: `offset = (page_index + 1) * 0x1000`

#### 2. 發現的 6 個工作 ioctl

| ioctl         | 功能                       |
| ------------- | -------------------------- |
| 0xc018092f    | GPUMEM_ALLOC               |
| 0xc0040938-3a | GPU Info                   |
| 0xc0040940    | Resource Alloc             |
| 0xc0040956    | SYNCSOURCE_CREATE_FENCE    |
| 0xc0200958    | GPU_AUX_COMMAND            |
| 0xc0300948    | Wait (returns ERESTARTSYS) |

#### 3. 額外發現

- Double mmap 指向相同物理頁
- fd 關閉後 100/100 映射仍有效
- GPU 重置計數穩定 (34 次，未增加)

### 建議的攻擊鏈

```
KGSL Cross-FD (info leak) → BPF Spray (object placement) → Root
```

---

**作者**: <USER>  
**狀態**: 研究進行中 - KGSL 漏洞已確認  
**最後更新**: 2026-02-02 (KGSL update)
