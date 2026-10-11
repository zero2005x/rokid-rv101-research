# RV101 拍照/錄影「閃光燈」開啟機制研究

> 日期：2026-08-23　裝置：<DEVICE_SERIAL>（fw 1.22.009，Android 12，kernel 5.10.209-perf）
> 方法：韌體映像靜態分析（system/product/vendor.img）+ 實機 sysfs/DTS/logcat/dmesg 驗證。
> 所有路徑與行號皆可重現。

---

## 0. 一句話結論

RV101 **沒有傳統相機閃光燈**（Camera HAL 回報 flash unit=false、kernel flash 子系統全關、
DTS 無 strobe 節點）。所謂「閃光燈」是 **AW2110x RGBW 指示燈的白色通道**，由
`init.rokid.rc` 監聽系統屬性 `vendor.rkd.camera.session_open`，在**相機 session 開啟時
點亮白光 255、關閉時熄滅 0**，總開關為 `vendor.rkd.camera.led.enable`（預設 1）。
本質是 **camera-in-use 補光/隱私指示燈**，不是拍照瞬間的 strobe。

---

## 1. 完整機制鏈（四層）

```
[App 層]   AssistServer(com.rokid.os.sprite.assistserver) / 相機 App
              │  開啟相機 capture session
              ▼
[HAL 層]   libcameraservice.so（Rokid 修改版，字串 vendor.rkd.camera.session_open @offset 557448）
              │  SystemProperties.set("vendor.rkd.camera.session_open","1"/"0")
              ▼
[Init 層]  /vendor/etc/init/hw/init.rokid.rc（第 17–23 行）
              on property:vendor.rkd.camera.session_open=1
                  write /dev/kmsg "camera opened!"
                  write /sys/class/leds/white/brightness 255
              on property:vendor.rkd.camera.session_open=0
                  write /dev/kmsg "camera closed!"
                  write /sys/class/leds/white/brightness 0
              ▼
[Kernel 層] leds-aw2110x 驅動（I2C1 位址 0x30，Awinic AW21104 RGBW）
            DTS：soc/i2c@a90000/aw2110x-leds@30，chan@0=blue reg0、chan@1=green reg1、
                 chan@2=red reg2、chan@3=white reg3（led-cur=4, max-cur=255）
```

**總開關**：`vendor.rkd.camera.led.enable`（實測 getprop = `1`）。

## 2. 「沒有閃光燈硬體」的證據

| # | 證據 | 來源 |
|---|------|------|
| 1 | `dumpsys media.camera`：`Has a flash unit: false`、`android.flash.info.available = 0` | 實機 2026-08-23 |
| 2 | `# CONFIG_LEDS_QTI_FLASH is not set`、`# CONFIG_LEDS_QPNP_FLASH_V2 is not set`、`# CONFIG_V4L2_FLASH_LED_CLASS is not set`（`CONFIG_LEDS_CLASS_FLASH=y` 但無任何實際驅動） | `research/kernel_configs/device_config_1.22.009_live.txt` |
| 3 | Device tree 全域搜尋 `*flash*/*strobe*` 僅命中 `mmc-hs400-enhanced-strobe`（SD 卡，無關）；LED 節點只有 `aw2110x-leds@30` | `/sys/firmware/devicetree/base/` |
| 4 | 全部 APK dex 無 `setTorchMode`、無 torch 引用；僅 AOSP Camera2 帶一個無作用的 legacy `FLASH_MODE` 字串 | pulled APKs（record/assist/live/screenrecord/camera2/seccamservice） |
| 5 | vendor.img 中 `sys/class/leds/white` 9 次引用全部來自 lights HAL/init script（狀態燈用途），非相機路徑 | `grep -aoc` 映像掃描 |

## 3. LED 控制權限模型（誰能點燈）

`/vendor/etc/ueventd.rc` 第 384–415 行：

```
/sys/class/leds/{red,green,blue,white}  brightness/delay_on/delay_off/breath/trigger/led_current
    → 0640  system:system
```

- 一般 app（untrusted_app）**不可**直接寫 sysfs → 只能靠 init 的 property 觸發路徑。
- `system` UID 服務（如 AssistServer，UID 1000）可直接寫 brightness（備援路徑）。
- QTI lights HAL（`vendor.qti.hardware.lights.service`）支援 trigger/breath（呼吸燈），
  對應通知燈功能；與相機無關。

## 4. 動態驗證紀錄（2026-08-23，閉環完成 ✅）

| 測試 | 結果 |
|------|------|
| `getprop vendor.rkd.camera.led.enable` | `1`（機制預設啟用） |
| Camera2 app 啟動（API v1 connect→68ms 後 close） | property **不翻轉**、無 kmsg → 單純 open 不觸發 |
| AssistServer 以 API v2 開 `CameraDeviceClient 0`（logcat 22:28:10） | 同樣未見 kmsg → 觸發點在更後面的 streaming 開始 |
| 對相機 app 送快門事件（input tap） | **裝置整機當機自動重啟**（uptime 歸零） |
| **root 後 `setprop vendor.rkd.camera.session_open 1/0`** | ✅ kmsg 出現 `camera opened!` / `camera closed!`（init T193 執行 write） |
| **`service call lights_ctrl 8 i32 2 i32 2014`** | ✅ dumpsys 註冊 `Event{mType=EVENT_TYPE_WARN(2), mId=EVENT_WARN_ID_CAMERA_OPEN(2014), mLightId=8, mState=TURN_ON, mPriority=8000}` |
| **`service call lights_ctrl 9 i32 2 i32 2014`** | ✅ 事件取消、`mCurrentEvent=null` |

### 4.1 壓制驗證（2026-08-24，root + Permissive，未觸發 capture）

在 root（`u:r:kernel:s0`, Permissive）下對三條控制路徑做**受控壓制閉環測試**，原始證據
`reports/led_validation/led_test.log`（SHA-256 `6ffb3979…`），再現腳本 `scripts/led_test.sh`：

| 路徑 | 命令 | 觀察 | 判定 |
|------|------|------|------|
| A — sysfs DAC override | `echo 0 > /sys/class/leds/white/brightness`；`echo 0 > led_current` | `write_rc=0`；`led_current` **2→0** | ✅ 寫入生效（CAP_DAC_OVERRIDE 繞過 `0640 system:system`） |
| A' — 控制面交互 | `echo 255 > led_current` | 讀回 **2**（被 `lights_ctrl` 事件 `mCurrent=0x020202` 覆寫） | ✅ sysfs 與 Binder 兩控制面互相覆寫 |
| B — Binder 取消 | `service call lights_ctrl 9 i32 2 i32 2014` | 成功 Parcel；`mCurrentEvent=null`、Event size=0 | ✅ 事件可清空 |
| B' — Binder 重發 | `service call lights_ctrl 8 i32 2 i32 2014` | `Event{...CAMERA_OPEN(2014)... TURN_ON}`，size 0→1 | ✅ 事件可注入 |
| C — 屬性偽造 | `setprop vendor.rkd.camera.session_open 0` | dmesg：`init: processing action (...session_open=0)...` + `camera closed!` | ✅ `init` 消費偽造狀態 |

**基線（相機閒置）**：`brightness=0`、`led_current=2`、`session_open` 空——白光隱私燈的
**預設（閒置）態為熄滅**，僅在相機 streaming 開始時才由上述路徑點亮（本報告 §1）。
三路徑皆可在軟體層熄滅/偽造，且路徑 C 為最上游（直接偽造「相機未開」狀態）。

### 4.2 邊界確認：P-Sensor / 穿戴態「不」控燈（2026-08-24 第二輪）

為確認白光隱私燈是否另有第三條「穿戴/接近態」控制路徑，於相機開燈基線（brightness=255）
下偽造穿戴/接近態（`setprop vendor.rkd.glasses.is_take_on 0`、`is_spread 0`、
`rokid.debug.enforce_psensor 0`），原始證據 `reports/led_validation/pathd_test.log`：

| 觀察 | 結果 |
|------|------|
| `is_take_on=0` | 偽造保持（未被覆寫） |
| `is_spread=0` | ~2s 內被活躍消費者覆寫回 1（系統元件主動維護） |
| `rokid.debug.enforce_psensor=0` | init → PSOC sysfs `enforce_psensor`=0；dmesg `psensor_delay_work … extcon psensor = 0`、`rt600_spi_boot spi0.0: proximity state = 0`（硬體級物理狀態偽造） |
| **LED brightness** | **保持 255**、camera-open 事件保持 TURN_ON |

**結論**：白光 LED 控制鏈**只有** §1 所述雙路徑（`lights_ctrl` Binder 與 `session_open`
init.rc）。穿戴/接近態（PSOC 1-0008 `enforce_psensor`/`psensor*`、`is_take_on`/`is_spread`）
與 LED 之間**無 init.rc/驅動鏈結**——穿戴態偽造無法直接熄滅隱私燈。

## 5. 精確呼叫點（libcameraservice.so 反組譯，2026-08-23）

字串 `vendor.rkd.camera.session_open` @ .rodata file-offset 557448 (vaddr 0x88188)，
全 binary 僅兩個精確 xref：

| 函式 | 位址 | 行為 |
|------|------|------|
| `CameraService::BasicClient::startCameraStreamingOps` | adrp@0xef670 | 見下方分支邏輯 |
| `CameraService::BasicClient::finishCameraStreamingOps` | adrp@0xef998 | 對稱：cancelEvent 或 setprop "0" |

```c
// startCameraStreamingOps（Rokid patch，偽代碼）
int panel = atoi(property_get("ro.boot.glassesWithPanel", "0"));
if (panel == 1)
    LightsNative::sendEvent(2, 2014);      // 本機實測 ro.boot.glassesWithPanel=1 → 走這條
else if (panel == 0)
    property_set("vendor.rkd.camera.session_open", "1");   // 其他 SKU 的 init.rc 路徑
```

`LightsNative::sendEvent/cancelEvent` 定義於 libcameraservice.so 內部（T symbol 0x228fac /
0x229308），實作 = `defaultServiceManager()->getService(String16("lights_ctrl"))` 後
vtable+0x58/+0x60 呼叫。

**BpLightsCtrl 完整 AIDL 方法表**（位址序＝transaction code 序）：

| code | 方法 |
|------|------|
| 1–7 | setBrightness(int,float) / setFlashing(i,i,i) / pulse(i,i) / turnOn(i) / turnOnAll() / turnOff(i) / turnOffAll() |
| **8** | **sendEvent(int type, int id)** ← 相機使用 |
| **9** | **cancelEvent(int type, int id)** |

**伺服端**：system_server 內 Rokid 框架類 `com.rokid.light.*`
（ILightsCtrl/LightsCtrl/LightsCtrlInternal/LedEventId/LedId/LedType，
見 system.img offset ~490532433 區域；工廠類 `com.rokid.server.light.EventFactoryV4`）。

**事件語意（dumpsys lights_ctrl 實測解碼）**：

```
type=2   → EVENT_TYPE_WARN
id=2014  → EVENT_WARN_ID_CAMERA_OPEN（相機開啟警告）
效果     → mLightId=8 TURN_ON, priority=8000, mCurrent=0x020202（白光）
結束     → cancelEvent(2,2014) 或串流停止自動清除
```

## 6. 最終架構圖（雙路徑）

```
拍照/錄影開始（streaming start）
 └ CameraService::BasicClient::startCameraStreamingOps()   [Rokid patch]
    ├─ ro.boot.glassesWithPanel==1（本機現況）
    │    └ LightsNative::sendEvent(EVENT_TYPE_WARN=2, EVENT_WARN_ID_CAMERA_OPEN=2014)
    │         └ Binder "lights_ctrl" txn#8（com.rokid.light.ILightsCtrl）
    │              └ system_server EventFactoryV4 → LightId 8 白光 TURN_ON（priority 8000）
    └─ ro.boot.glassesWithPanel==0（其他設定）
         └ property "vendor.rkd.camera.session_open"="1"
              └ init.rokid.rc trigger → /sys/class/leds/white/brightness = 255

拍照/錄影結束 → finishCameraStreamingOps() 對稱熄滅（cancelEvent / property "0"+write 0）
```

兩條路徑都已實機閉環驗證（§4 表）。

## 7. 已知風險觀察

- 相機強制操作（input tap 快門）後裝置整機重啟一次（2026-08-23 23:00 前後）。
  **注意**：事發當下為 GhostLock root + Permissive 環境（uid=0 root holder 存活、
  kernel 含未修補 UAF 觸發歷史），**無法排除與 kernel UAF 狀態的交互作用**；
  在乾淨 Enforcing 環境重現前，不得宣稱為相機路徑的獨立 DoS 弱點。

## 9. 錄影閃爍根因與根治（2026-09-09 更新）

**現象**：取得 root 後，手機端 App 開 ghostlock 錄影，隱私燈會閃爍。

**根因（兩層）**：
1. **初始閃一下**：`CameraController.startVideo()` 原本先 `bindVideo()`（streaming 啟動 →
   libcameraservice 發 `sendEvent(2,2014)` → 點燈 255）**之後**才 `led.suppress()`
   → 綁定瞬間會先亮、200ms 內才被 hold-loop 壓下。
2. **錄影中脈衝閃爍**：錄影期間 camera session 若重啟/重發事件（實測每次 streaming
   啟動事件 mTime 會更新），`lights_ctrl` 寫回 255；與 0.2s hold-loop 互搶 → 每次事件
   重發即一次可見脈衝。純 sysfs 壓制是「賽跑」而非「關閉」。

**根治（已實作並驗證）**：
- suppress 時在 Binder 層取消事件：`service call lights_ctrl 9 i32 2 i32 2014`
  （`cancelEvent`）→ `mCurrentEvent=null`，system_server 不再有強制寫入源；
  sysfs 壓 0 + hold-loop 保留為保險。
- `startVideo()` 的 `led.suppress()` 移到 `bindVideo()` **之前**（消除初始閃）。
- restore 時重發事件：`service call lights_ctrl 8 i32 2 i32 2014`（相機使用中時由系統
  自行維護點燈；session 結束時系統自行 cancelEvent 熄燈）。

**實測（2026-09-09 23:08）**：
- ghostlock ON 錄影：20 秒 × 100ms 採樣 **200/200 全為 0**，`mCurrentEvent` 事件在
  streaming 啟動時仍會被相機服務重發，但無再閃爍（壓制路徑在事件源頭截斷）。
- ghostlock OFF 錄影：brightness=255（自然隱私指示）✓。
- 停止錄影後：相機 session 仍綁定（`Active Camera Clients State 2`）→ 燈維持 255，
  屬既有隱私指示語意（相機在用即亮），非本次修復引入。

## 附錄：重現命令清單（2026-08-23 原始 §8，全部驗證）

```bash
source deploy/env.sh   # ADB/SERIAL 唯一來源（2026-09-13 起）; S="<DEVICE_SERIAL>"

# 0) 取得 root（boot 健康 gate 後；root shell 內會 setenforce 0）
./scripts/boot_run_loop.sh 4 w2 v960 "gid = 0 (" \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1" /data/local/tmp/ghostlock_rv101

# 1) init.rc 路徑驗證（Permissive 下 shell 即可）
"$ADB" -s $S shell 'setprop vendor.rkd.camera.session_open 1; dmesg | grep camera'
#    → "camera opened!"；設回 0 → "camera closed!"

# 2) lights_ctrl Binder 路徑驗證（相機實際走的路）
"$ADB" -s $S shell 'service call lights_ctrl 8 i32 2 i32 2014; dumpsys lights_ctrl'   # 點亮
"$ADB" -s $S shell 'service call lights_ctrl 9 i32 2 i32 2014; dumpsys lights_ctrl'   # 熄滅

# 3) sysfs DAC override（需真 uid=0；shell 會被 0640 system:system 的 DAC 擋下）
"$ADB" -s $S shell 'echo 0 > /sys/class/leds/white/brightness; cat /sys/class/leds/white/brightness'

# 4) 一鍵三路徑壓制閉環（root 內執行，輸出寫 /data/local/tmp/led_test.log）
#    → scripts/led_test.sh（配合 scripts/run_led_orchestrator.sh 於 FOPS 窗口注入）

# 5) 反組譯重現
#   字串 xref：python3 /tmp/opencode/find_xref.py libcameraservice.so 557448
#   → startCameraStreamingOps@0xef670 / finishCameraStreamingOps@0xef998
```
