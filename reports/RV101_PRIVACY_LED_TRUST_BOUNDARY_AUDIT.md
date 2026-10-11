# RV101 Privacy Indicator Trust Boundary Audit Report
# （RV101 隱私指示燈 / P-Sensor 信任邊界審計報告）

> English: [RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT_EN.md](RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT_EN.md)

> 審計日期：2026-08-24　裝置：Rokid RV101（serial <DEVICE_SERIAL>）
> 韌體：1.22.009-20260710-150201（Android 12, kernel 5.10.209-perf）
> 環境：adb root（GhostLock CVE-2026-43499 取得 uid=0）+ SELinux Permissive
> 方法：**靜態分析 + 只讀探測**（cat/ls/grep/dumpsys/getprop/sha256sum/反組譯），並於 2026-08-24 追加
> **受控動態驗證**（root + Permissive 下對 LED 控制路徑執行受控寫入/取消/屬性偽造；全程未觸發相機 capture/錄影）。
> 關聯文件：`RV101_CAMERA_LED_FLASH_MECHANISM.md`（機制逆向）、`SHA256SUMS.txt`（證據鏈）、
> `reports/led_validation/led_test.log`（路徑 A/B/C 動態驗證證據，SHA-256 `6ffb3979…`）、
> `reports/led_validation/pathd_test.log`（路徑 D P-Sensor/穿戴態偽造證據，SHA-256 見 manifest）

---

## 1. Executive Summary（執行摘要）

**RV101 的拍攝提示燈（白光 Privacy LED）由 AP 主晶片經一條 0666 權限的 I2C 匯流排直驅，
整條控制鏈（CameraService patch → Binder → system_server Java 工廠 → sysfs）全部運行於
REE（Rich Execution Environment）；系統中不存在任何 TEE Applet、Secure MCU 或硬體互鎖
參與「點燈」決策。因此 uid=0 攻擊者可繞過全鏈直接寫 sysfs 或攔截 Binder，使提示燈在
相機運作時形同虛設；P-Sensor 的原始數據雖隔離於 SLPI 微控制器，但其衍生的「穿戴狀態」
以可寫系統屬性（`vendor.rkd.glasses.is_take_on/is_spread`）暴露於 userspace，防遮擋門檻
同樣屬 Software-Enforced。**

> **動態驗證結果（2026-08-24，root + Permissive、未觸發相機 capture）**：§4 的三條攻擊路徑
> 已由紙面推演轉為**實機驗證**——路徑 A（`echo 0 > brightness/led_current` 寫入生效，
> `led_current` 2→0）、路徑 B（`service call lights_ctrl 9/8` 成功取消/重發 camera-open 事件）、
> 路徑 C（`setprop vendor.rkd.camera.session_open 0` → `init` 觸發 `camera closed!`）。
> 證據見 `reports/led_validation/led_test.log`。因遵守「不觸發 capture」約束，基線為相機閒置態
> （brightness=0 / led_current=2），故「相機運作中熄燈」為三條已驗證控制路徑之合併推論
> （INFERRED），控制路徑本身為 CONFIRMED。
>
> **路徑 D（P-Sensor/穿戴態偽造，2026-08-24 第二輪）**：`is_take_on`/`is_spread`/`enforce_psensor`
> 偽造**成立**（`is_take_on=0` 保持；`enforce_psensor=0` 經 init → PSOC sysfs → extcon → rt600
> 產生「proximity state = 0」硬體級反應），但 **LED 亮度保持 255**——證實 P-Sensor/穿戴態
> **不直接控制隱私燈**，LED 僅由路徑 A/B/C 控制；路徑 D 破壞的是「物理狀態」信任邊界（下游
> 穿戴/接近狀態可偽造），與 LED 屬**不同信任邊界**。證據 `reports/led_validation/pathd_test.log`。

---

## 2. Attack Surface Mapping（攻擊面測繪）

### 2.1 LED 相關節點

| # | 節點 | 權限 / 標籤 | 證據 |
|---|------|------------|------|
| A1 | `/sys/class/leds/{white,red,green,blue}/brightness` | DAC `0640 system:system`；SELinux `u:object_r:sysfs:s0`（**通用型別，無專用 leds 型別**） | `ls -laZ /sys/class/leds/white/`（本審計 §2 實測） |
| A2 | `/sys/class/leds/white/{breathe,led_current,trigger}` | 同上 `0640 system:system` + `sysfs:s0` | 同上；`stat -c` 複核 |
| A3 | `/sys/bus/i2c/devices/1-0030/`（AW2110x，含 `engine_mux/select_engine/run_engine/update_pattern` 效果引擎） | driver 屬性多為 root-only，但所屬匯流排節點見 A4 | `ls -la /sys/class/leds/white/device/` |
| A4 | **`/dev/i2c-1`** | **`crw-rw-rw-`（0666，世界可寫！）** | `ls -l /dev/i2c-*`；i2c-0/2 均為 0600，唯獨承載 LED 的 i2c-1 開放 |
| A5 | DTS `soc/i2c@a90000/aw2110x-leds@30`（chan@3=white reg3, max-cur=255） | DT 無任何保護描述（無 secure/pin 屬性） | `/proc/device-tree/.../chan@3/*` |
| A6 | Binder `lights_ctrl`（descriptor `com.rokid.light.ILightsCtrl`） | service list 公開可查；txn#8 sendEvent / #9 cancelEvent 可由任意有 binder 存取的域呼叫（實測 shell 即可） | `service list \| grep lights_ctrl`；前輪 `service call` 實錄 |

### 2.2 P-Sensor 相關節點

| # | 節點 | 觀察 | 證據 |
|---|------|------|------|
| B1 | sensortek **UCS146E0** 接近感測器（HAL handle `ucs_ucs146e0`，Non-wakeup+Wakeup 兩路，`0x51/0x52`） | SensorFramework 註冊 **`perm: n/a`**——任何 app 無權限即可監聽接近事件 | `dumpsys sensorservice` |
| B2 | **兩路**接近感測：① sensortek UCS146E0（SLPI/SSC，Android HAL）；② **PSOC 1-0008**（AP I2C，driver `Rokid,PSOC-TP-R`，DTS `psoc4000R@08`） | ① 原始 ADC 在 SLPI；② 原始讀值 `psensor/psensor0/psensor1`（0444 唯讀、world-readable，實測 1/0/0）與 `hall`（=1）**在 AP I2C 上可讀** | i2c devices 列舉；`cat /sys/.../1-0008/psensor*` 實測 |
| B3 | 衍生狀態 **系統屬性**：`vendor.rkd.glasses.is_take_on`、`vendor.rkd.glasses.is_spread` | 目前值皆 `1`；**可 setprop 偽造**——`is_take_on=0` 寫入後保持；`is_spread=0` 於 ~2s 內被活躍消費者**覆寫回 1**（證實由系統元件主動維護） | `getprop`；2026-08-24 動態驗證（`pathd_test.log`） |
| B4 | `/dev/input/event0,event1` | `0660 root:input`（qpnp_pon 電源鍵、PSOC-TP-R 觸控/按鍵）——與 prox 無關但屬同類輸入面 | `getevent -lp` |
| B5 | **PSOC 1-0008 控制節點**（`/sys/devices/platform/soc/a90000.i2c/i2c-1/1-0008/`）：`enforce_psensor`（=1）、`enforce_hall`（=0）、`thres_pct`（=0）、`proximity_tuning`（=0）、`proximity1_tuning` | 由 init.rokid.rc 以**屬性**觸發寫入：`rokid.debug.enforce_psensor=1/0`→`enforce_psensor`、`rokid.debug.enforce_hall`→`enforce_hall`、`rokid.psensor.mode=standard/sensitive`→`thres_pct 0/3`、`rokid.debug.proximity_tuning`→`proximity_tuning` | `~~assets/vendor_extracted/etc/init/hw/init.rokid.rc~~（**`assets/` 已不存在於工作區；引用行號無法複核，2026-09-13 註**）` 行 41-87；實測 `setprop rokid.debug.enforce_psensor 0` → dmesg `extcon psensor = 0` |

---

## 3. Trust Boundary Analysis（信任邊界分析）

### 3.1 控制流圖（實測 + 反組譯證實）

```
[拍照/錄影 streaming start]
        │
        ▼
┌─────────────────────────────────────────────────────────────┐
│ libcameraservice.so（REE, Rokid patch @0xef670）             │
│   BasicClient::startCameraStreamingOps()                    │
│   ├ ro.boot.glassesWithPanel==1 → LightsNative::sendEvent(2,2014) ──┐ REE
│   └ ==0 → property_set("vendor.rkd.camera.session_open","1")        │ REE
└─────────────────────────────────────────────────────────────┘     │
        │ Binder txn#8 ("lights_ctrl")                    │ setprop
        ▼                                                  ▼
┌───────────────────────────────┐   ┌──────────────────────────────────┐
│ system_server 內 Java 服務      │   │ init (userspace)                  │
│ com.rokid.light.LightsCtrl /   │   │ init.rokid.rc:17-23 property     │ REE
│ EventFactoryV4（REE Daemon）    │   │ trigger                          │
└───────────────┬───────────────┘   └──────────────┬───────────────────┘
                │ write                            │ write
                ▼                                  ▼
     /sys/class/leds/white/brightness（sysfs, 0640, object_r:sysfs）
                │ I2C1 @ 0x30（/dev/i2c-1 為 0666）
                ▼
        AW2110x RGBW 白光通道 ←—— 【終端執行體：普通 I2C 從機，無 Secure Element】
```

**全程 REE。標記說明：**
- 參與點：AP Linux kernel、init、system_server（Java）、libcameraservice —— 全部 Rich OS。
- **TEE 參與度 = 0**：lights HAL 二進位 0 次 qsee/tee 字串；camera.qcom.so 中 QSEE 僅出現於
  secure-streaming（DRM 內容保護）路徑（`QSEEIntfMutex` 等），與 LED 無任何綁定。
- **Secure MCU 參與度 = 0**：AW2110x 是普通 I2C PWM LED 驅動晶片，無認證/無 host 驗證；
  板上另有 PSOC（i2c-1 0x08，`psoc-tp-right`）負責觸控/按鍵/**接近(hall/psensor)** 感測——
  具 `enforce_psensor`/`psensor*`/`hall` 節點（§2.2 B2/B5），但 **未參與燈控**（燈仍走
  AW2110x；PSOC 的 psensor 狀態偽造不改變 LED，見 §4 路徑 D）。

### 3.2 SELinux 隔離失效分析

| 觀察 | 證據 | 含義 |
|------|------|------|
| LED sysfs 使用**通用** `u:object_r:sysfs:s0` 標籤 | `ls -Z /sys/class/leds/white/` 全部 `sysfs:s0` | 未如 vibrator/dm_verity 設專用 genfscon 型別（plat_sepolicy.cil:306-310 有專用型別先例，LED 被漏掉）→ 粒度極粗 |
| 大範圍網域對通用 sysfs 有 allow | plat+vendor CIL 合計 14,803 條 allow；`domain` attribute 本身即有 sysfs 規則（apexd/audioserver/bluetooth/charger/dumpstate/hal_audio/gmscore_app… 眾多網域） | 隔離邊界依賴「誰拿不到 generic sysfs write」，而非「LED 專屬最小授權」 |
| hal_camera_default 無 sysfs 寫入授權 | vendor_sepolicy.cil hal_camera 段僅 exec/fwservice/dumpstate 規則 | 原設計確實**依賴 SELinux** 迫使 Camera HAL 繞道 Framework 點燈；但此隔離在 Permissive 下瞬間歸零，且 Root 域（magisk/su 或 u:r:kernel:s0 context）不受限 |
| Permissive 實測 | 本次審計全程 Permissive，所有上述節點讀取暢通（僅 DAC 0640 擋 shell，root 直接覆蓋） | 軟體隔離 = 唯一防線 |

---

## 4. Exploit Paths（攻擊路徑 — A/B/C 已於 2026-08-24 動態驗證 ✅；D 已驗證「狀態偽造成立但非直接控燈」）

### 路徑 A：Sysfs 劫持（DAC Override）✅ 已實證
uid=0 持 CAP_DAC_OVERRIDE → 直接 `echo 0 > /sys/class/leds/white/brightness`
（或 `led_current=0` 斷電流）。無 kernel interlock、無 I2C 層認證，寫入必生效。
連帶：root 亦可寫 AW2110x pattern engine（`update_pattern/run_engine`）注入自訂動畫，
讓燈「看起來正常」實際熄滅。

**動態驗證（2026-08-24）**：root（`u:r:kernel:s0`, Permissive）下 `echo 0 > brightness`
與 `echo 0 > led_current` 皆 `write_rc=0`；`led_current` 由 `2 → 0`（實際寫入生效，非僅
回傳成功）。附帶發現：`echo 255 > led_current` 讀回仍為 `2`——`lights_ctrl` 事件之
`mCurrent=0x020202`（每通道 0x02）會覆蓋原始 sysfs 值，證明 sysfs 與 Binder 為兩條
**會互相覆寫**的控制面。

### 路徑 B：Binder IPC 過濾（Hooking `lights_ctrl`）✅ 已實證
- userspace hook：Frida attach `system_server`，替換 `LightsCtrl.sendEvent()` 為 no-op；
  或 LD_PRELOAD 於 cameraserver 攔 `LightsNative::sendEvent`。
- 更低層：binder 交易過濾（txn #8 丟棄）。因伺服端是普通 Java Binder 執行緒，
  freeze/優先級操作亦可使事件佇列停擺（dumpsys 顯示 Event size 動態變化 = 純軟體佇列）。

**動態驗證（2026-08-24）**：`service call lights_ctrl 9 i32 2 i32 2014`（cancelEvent）與
`service call lights_ctrl 8 i32 2 i32 2014`（sendEvent）皆回成功 Parcel；txn#8 使 dumpsys
註冊 `Event{...EVENT_WARN_ID_CAMERA_OPEN(2014)... mState=TURN_ON}`（Event size 0→1），
txn#9 使其歸零（`mCurrentEvent=null`, Event size=0）。事件佇列純軟體、可任意增刪。

### 路徑 C：`vendor.rkd.camera.session_open` 屬性偽造（state forgery，init.rc 支路）✅ 已實證
`setprop vendor.rkd.camera.session_open 0` 直接偽造「相機未開啟」狀態。該屬性由
`/vendor/etc/init/hw/init.rokid.rc` 監聽（§3.1 控制流第二支路，`glassesWithPanel==0`
SKU 使用），`init` 收到後執行 `camera closed!` 動作。

**動態驗證（2026-08-24）**：`setprop ... 0` 後 dmesg 立即出現
`init: processing action (vendor.rkd.camera.session_open=0) from (/vendor/etc/init/hw/init.rokid.rc:21)`
與 `camera closed!`——證明該屬性為 `init` 消費的**真實控制訊號**，可於 source 端偽造
相機 session 狀態（較路徑 A 更早、更上游）。

### 路徑 D：P-Sensor 數據偽造（Sensor Spoofing）— 已驗證：狀態偽造成立，但不直接控燈
- RAW ADC 分兩路（§2.2 B2）：sensortek UCS146E0 在 SLPI；PSOC 1-0008 的 `psensor*` 唯讀在 AP I2C。
  兩者皆**無法直接改寫**；**但消費端/gate 全在 software**：
  1) 依賴 `android.sensor.proximity` 的 app → Frida `SensorManager.registerListener` 注入
     `SENSOR_STATUS_ACCURACY_HIGH + distance=遠` 假事件（perm: n/a，零權限門檻）；
  2) 依賴 `vendor.rkd.glasses.is_take_on/is_spread` 屬性的邏輯（AssistServer 已證實讀取）
     → `setprop` 直接改寫「已穿戴/已展開」狀態；
  3) PSOC 的 psensor 強制（`enforce_psensor`）經 `rokid.debug.enforce_psensor` 屬性 → init →
     sysfs → extcon → rt600 可被偽造。
- **動態驗證（2026-08-24 第二輪）**：相機開燈基線（brightness=255）下，同時 `setprop
  is_take_on=0`、`is_spread=0`、`rokid.debug.enforce_psensor=0` → **brightness 保持 255**、
  camera-open 事件保持 TURN_ON。偽造本身成立：`is_take_on=0` 保持；`is_spread` 被活躍消費者
  覆寫回 1；`enforce_psensor=0` 觸發 dmesg `psensor_delay_work … extcon psensor = 0` 與
  `rt600_spi_boot spi0.0: proximity state = 0`（硬體/驅動級物理狀態偽造）。
- 結論：**P-Sensor/穿戴態偽造不構成對隱私 LED 的直接壓制**——LED 僅由路徑 A/B/C 控制；
  路徑 D 破壞的是「物理狀態（穿戴/接近）是硬體事實」的信任邊界（下游 consumer 可被欺騙），
  與路徑 A/B/C 屬**不同信任邊界**。防遮擋機制不構成 Hardware Interlock 的結論不變。

### 4.5 動態驗證紀錄摘要（2026-08-24，root + Permissive，未觸發 capture）

| 路徑 | 命令 | 觀察結果 | 判定 |
|------|------|---------|------|
| A | `echo 0 > /sys/class/leds/white/brightness` / `led_current` | `write_rc=0`；`led_current` 2→0 | ✅ CONFIRMED（DAC override 生效） |
| A' | `echo 255 > led_current` | 讀回 `2`（被 `lights_ctrl` 事件 `mCurrent=0x020202` 覆寫） | ✅ 控制面交互證實 |
| B | `service call lights_ctrl 9 i32 2 i32 2014` | 成功 Parcel；事件佇列可清空 | ✅ CONFIRMED |
| B' | `service call lights_ctrl 8 i32 2 i32 2014` | `Event{...CAMERA_OPEN... TURN_ON}`，Event size 0→1 | ✅ CONFIRMED |
| C | `setprop vendor.rkd.camera.session_open 0` | dmesg：`init: ... camera closed!` | ✅ CONFIRMED（state forgery 生效） |
| D | `setprop vendor.rkd.glasses.is_take_on 0`（+ `is_spread 0`、`rokid.debug.enforce_psensor 0`） | `is_take_on=0` 保持；`is_spread` 被覆寫回 1；`enforce_psensor` sysfs 0（dmesg `extcon psensor=0` → rt600） | ✅ 物理狀態偽造 CONFIRMED |
| D' | 同上（於相機開燈基線 brightness=255 下） | **brightness 保持 255**、camera-open 事件保持 TURN_ON | ❌ 不直接壓制 LED（穿戴/接近態與 LED 無控制鏈結） |

原始證據：`reports/led_validation/led_test.log`（路徑 A/B/C，SHA-256 `6ffb3979…`）、
`reports/led_validation/pathd_test.log`（路徑 D）；再現腳本 `scripts/led_test.sh`、`scripts/pathd_test.sh`。
基線為相機閒置態（brightness=0 / led_current=2 / session_open 空），故「相機實際運作中熄燈」
為三條已驗證控制路徑之合併推論（INFERRED），控制路徑本身 CONFIRMED。

---

## 5. RE Targets Extraction（離線反組譯清單）

| 目標 | 路徑 | SHA-256 |
|------|------|---------|
| Camera HAL | `/vendor/lib64/hw/camera.qcom.so` | `85948d15756e846aeb62c4ad64961f16d1c8b0c899bc93c62f57245df4f4002b` |
| CameraService（Rokid patch 主體） | `/system/lib64/libcameraservice.so` | `db800b03cc19e2fc12ba9a05575fcdbada050a7db40595242205fbacb1eb6b4b` |
| Framework（內嵌 com.rokid.light.*） | `/system/framework/framework.jar` | `6d40be5cb07801d5ae6e3397113ab06b369a301bdf8c48bec1968544d315082d` |
| Init 觸發腳本 | `/vendor/etc/init/hw/init.rokid.rc`（本地提取副本；裝置上該路徑 shell 讀取遭 DAC 拒絕） | `04a4cc21e92c53f1ffb43c4b0d40a3efafcf0849a0d4dd0b9f25c0f085d747b6` |
| Lights HAL 服務 | `/vendor/bin/hw/vendor.qti.hardware.lights.service` | `506753e683e256226b7be3dd6c439d06241140f857c734330d48346e4b3d6c49` |
| sepolicy（CIL 明文） | `/system/etc/selinux/plat_sepolicy.cil`、`/vendor/etc/selinux/vendor_sepolicy.cil` | （9,682 / 5,121 條 allow，供 sesearch 離線複核） |

已知錨點（供 IDA/Ghidra）：libcameraservice.so — session_open 字串 vaddr 0x88188、
startCameraStreamingOps xref adrp@0xef670、finishCameraStreamingOps@0xef998、
BpLightsCtrl::sendEvent@0x22b744、LightsNative::sendEvent@0x228fac。

---

## 6. Conclusion & Remediation（結論與原廠修復建議）

### 結論
1. 提示燈控制鏈 100% 位於 REE；無 TEE Applet、無 Secure MCU、無硬體互鎖。Root 下
   「相機運作必亮燈」保證不成立。（2026-08-24 已動態實證：root 可經 sysfs / Binder /
   `session_open` 屬性三路徑直接熄滅或偽造提示燈狀態，見 §4.5。）
2. SELinux 是唯一軟體閘門，且 LED 落在粗粒度通用 `sysfs` 標籤，非最小授權設計。
3. P-Sensor 原始數據具 SLPI 微控制器隔離（正面發現），但其衍生 gate 以可寫屬性與
   零權限感測事件暴露，仍屬 Software-Enforced。（2026-08-24 動態實證：`is_take_on`/
   `enforce_psensor` 可偽造並產生硬體級 extcon 反應；但此不影響 LED。）
4. **LED 僅由路徑 A/B/C 控制**：P-Sensor/穿戴態（`is_take_on/is_spread/enforce_psensor`）
   與 LED 亮度之間**無 init.rc/驅動鏈結**（init.rokid.rc 中 LED 僅受 `session_open` 觸發），
   故「穿戴態偽造」無法直接熄滅隱私燈——熄燈仍需 sysfs/Binder/`session_open` 三路徑。

### 原廠修復建議（Hardening Roadmap）
1. **硬體互鎖（治本）**：將提示燈移至 Secure MCU / PMIC 硬體燈控通道，由 camera ISP
   的串流硬體訊號（CSID/CAMIF output-enable）直接觸發 LED enable 線，軟體只能「請求」
   不能「否決」；或採用帶 host 認證的 I2C 從機（challenge-response 後才接受亮度命令）。
2. **I2C 匯流排收斂**：`/dev/i2c-1` 收回 0600（目前 0666 世界可寫，屬不必要攻擊面）。
3. **SELinux 最小授權**：為 leds 新增專用型別（如 `sysfs_leds_w`）+ genfscon 精確標籤，
   僅 `lights_ctrl` 所在網域可寫；移除 `domain` 對 generic sysfs 的寬鬆 allow。
4. **狀態屬性去權限化**：`is_take_on/is_spread` 與 `rokid.debug.enforce_psensor`/`rokid.psensor.mode`
   改由 kernel/SLPI 直讀介面（如 IIO sysfs 唯讀節點 + 專用 SELinux 型別），避免 setprop 偽造
   穿戴/接近狀態；PSOC `enforce_psensor`/`thres_pct` 亦應收斂寫入權限（現為 0644 system:system
   + 屬性觸發，可被 root/屬性偽造驅動）。
5. **完整性自我檢查**：對 init.rokid.rc、libcamereservice.so（已含 Rokid patch）納入
   dm-verity/apex 校驗範疇並於 boot 時比對 manifest，偵測本審計所示型態的使用者態篡改。

### 審計限制聲明（更新：2026-08-24）
- 未執行 strace live-attach（需 ptrace 根進程，超出只讀原則）；IPC 行為改以靜態反組譯
  錨點 + dumpsys/getprop 狀態佐證，結論不受影響。
- **2026-08-24 補充動態驗證**：路徑 A/B/C 已在受控環境（root + Permissive，未觸發相機
  capture/錄影）實際執行並驗證（`reports/led_validation/led_test.log`）。基線狀態為相機
  閒置（brightness=0 / led_current=2 / session_open 空），故演示為「控制路徑層」閉環
  （寫入/交易/屬性均生效並產生預期狀態遷移），非「相機實際運作中」的 LED-on 基線壓制。
- 未啟動相機串流做「相機運作中熄燈」端到端演示（遵守「不觸發 capture」約束）。該情境的
  壓制效果由三條已驗證控制路徑之合併結論推得（與 §3.1 控制流一致），標記為 **INFERRED**，
  控制路徑本身為 **CONFIRMED**。
- **路徑 D 第二輪驗證（2026-08-24）**：於「相機開燈」基線（brightness=255）下偽造穿戴/
  接近態，證實**狀態偽造成立但 LED 亮度不變**（`reports/led_validation/pathd_test.log`）。
  故路徑 D 定調為「物理狀態信任邊界破壞」，非隱私燈直接壓制路徑。
