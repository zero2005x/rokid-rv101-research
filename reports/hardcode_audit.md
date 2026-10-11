# 硬編碼稽核報告（全專案）

> English: [hardcode_audit_EN.md](hardcode_audit_EN.md)

- 日期：2026-09-12
- 工具：`python3 scripts/audit_hardcode.py`（可重跑；輸出五個章節：中文字面值／路徑／版面／重複字面值／腳本）
- 範圍：`android/app-phone`、`android/app-glasses`、`android/shared`、`deploy/`、`scripts/`、版面 XML

## 1. 結論摘要

| 類別 | 修正前 | 修正後 |
|------|--------|--------|
| 版面硬編碼**顏色** | 3 筆（`#CCFFFFFF`×2、`#591A73E8`） | **0 筆**（改為 `@color/on_scrim`、`@color/accent_soft`） |
| 版面硬編碼**文字** | 3 筆 | 3 筆（皆為裝飾性圖片 `contentDescription="@null"`，**刻意保留**） |
| 眼鏡端**裝置路徑**散落 | 15+ 處硬寫死於 8 個檔案 | **全部集中**到 `GlassesPaths.kt`（`/data/local/tmp`、`/sys/class/leds` 各只剩 1 處定義） |
| 手機端**使用者可見中文字串** | 31 筆（`RingController` 狀態／Toast）＋`Remote.kt` 例外訊息 | **已全部資源化**（22 個新字串 × 14 語系），僅剩 logcat 用字面值 |
| **腳本**硬編碼序號 | 11 支腳本各自寫死 `<DEVICE_SERIAL>` | 集中到 `deploy/env.sh`（可用 `SERIAL` 覆寫；無值時自動偵測 adb 第一台） |
| **腳本**硬編碼 adb／NDK 路徑 | 12 支腳本寫死 `/mnt/c/Users/<USER>/...` | 全部改為 `${VAR:-預設}` 可覆寫；預設值只留在 `deploy/env.sh` 與兩支 NDK 建置腳本 |
| 其他機器路徑（`Documents/codebase/rokid_root`） | 3 支腳本 | 改為 repo 相對路徑（`REPO=$(cd "$(dirname "$0")/.." && pwd)`） |

## 2. 修正明細

### 2.1 手機端：戒指控制字串資源化（影響最大）
`RingController` 的狀態列與 Toast 原本是硬編碼中文（例如 `"已拍照 ${m.fileName}"`、`"拍照失敗：${e.message}"`），
在 14 語系下不會跟著翻譯。新增 22 個 key（`ring_state_enabled`／`ring_photo_ok`／`ring_video_saved`／
`ring_silent_on`／`ring_display_switched`／`ring_err_prefix`…），並以 `s(resId, …)`  helper 取字串。

- `Remote.kt`：`requireOk()` 的 `"眼鏡端回報失敗"` → `R.string.err_unknown`
- `SettingsFragment`：戒指列舉／開關訊息中的 `"ON"/"OFF"` → `R.string.ring_on/ring_off`
- 實機驗證：`TAP → 已拍照 IMG_1789176096288.jpg`、`SCROLL_UP → 顯示設定失敗：not connected`
  （字串分別來自 `ring_photo_ok`／`ring_display_fail`）

### 2.2 眼鏡端：裝置路徑集中化
新增 `app-glasses/.../GlassesPaths.kt`，把原本散落的路徑集中：

| 常數 | 值 |
|------|-----|
| `TMP` / `ROOT_DAEMON_SOCK` / `ROOT_DAEMON_BIN` | `/data/local/tmp`、`…/root_daemon.sock`、`…/root_daemon` |
| `GHOSTLOCK_BIN` / `GHOSTLOCK_ROOT_SCRIPT` / `BRIDGE_DIR` / `RUN_SHELL` | ghostlock LPE 相關 |
| `SILENT_OGG` | 靜音拍攝用的 ogg |
| `RING_EVENTS_LOG` / `RING_READER_PID` / `RING_KEEPALIVE_SH` / `RING_KEEPALIVE_PID` | 戒指 root 直讀 |
| `LED_BASE` / `ledBrightness(color)` / `LED_WHITE_CURRENT` | 隱私指示燈 sysfs |

同步更新：`GhostlockEngine`、`RootDaemonClient`、`LedPayloads`、`LedSuppressor`、`ShizukuGhostlockEngine`、
`RingEvdevReader`、`RingControlManager`、`SilentPayloads`（產生檔，並同步 `scripts/gen_silent_payload.py` 模板）。

### 2.3 版面顏色
`activity_viewer.xml`、`item_album_card.xml` 的 `#CCFFFFFF` → `@color/on_scrim`；
`item_media_grid.xml` 的 `#591A73E8` → `@color/accent_soft`（新增於 `colors.xml`）。

### 2.4 腳本
- 新增 `deploy/env.sh`：唯一定義 `WIN_ADB_DIR`／`ADB`／`SERIAL`（`SERIAL` 未給時自動取 `adb devices` 第一台），
  並說明 `source "$(dirname "$0")/env.sh"` 用法。
- 11 支 `deploy/*.sh` 與 3 支 `scripts/*.sh` 改為 source 它、移除寫死的序號與 adb 路徑；
  NDK 建置腳本的 `NDK_BIN`／`WIN_DIR` 改為 `${VAR:-預設}`。
- `scripts/Collect-RV101-Validation.ps1`：`$Serial` 改由參數或環境變數 `SERIAL` 提供。
- 全部腳本以 `bash -n` 驗證語法。

## 3. 刻意保留（非不良硬編碼）

| 項目 | 原因 |
|------|------|
| `LocaleHelper.LANGUAGES` 的母語名稱（`繁體中文`／`日本語`／`العربية`…） | 語言選單必須以該語言自身顯示 |
| `DebugReceiver` 的 logcat 與 `TEST_LOG` 測試訊息 | 開發者工具訊息，不進使用者介面 |
| 眼鏡端 ghostlock／戒指／靜音的 **logcat** 訊息 | 僅供 adb 除錯 |
| `Protocol.kt` 的 `AP_IP`／`WIFI_PORT`／`AP_SSID`／`AP_PASS` | **協定常數**，集中於共用模組即為正確位置 |
| `/proc/uptime`、`127.0.0.1` | 平台固定值 |
| 版面 3 處 `contentDescription="@null"` | 裝飾性圖片，無障礙不需朗讀 |
| `deploy/env.sh` 與兩支 NDK 腳本中的 WSL 預設路徑 | 已是「可用環境變數覆寫」的預設值，集中一處 |

## 4. 待處理（**已於第二、三輪完成，見 §7、§10–12**）

**眼鏡端推到手機顯示的文字仍是硬編碼中文**（約 72 條）：

| 來源 | 內容 | 為何算不良 |
|------|------|-----------|
| ~~`app-glasses` `RingControlManager.lastEvent`~~（已改 `lastKey`/`lastArg`） | 例：`已啟動（/dev/input/event1）`、`TAP → 已拍照 IMG_…jpg` | 經 `STATUS.ringLast` 顯示在**手機**的戒指卡上（實機可見） |
| `app-glasses` 三個 Ghostlock 引擎的 `onProgress` | 例：`已偵測到常駐 root daemon，跳過 LPE`、`等待 ready 標記` | 經 `PROGRESS` 推到**手機**的操作紀錄面板 |

兩種修法：

- **A（較快）**：把眼鏡端這些字串放進眼鏡 App 既有的 14 語系資源。
  缺點：手機顯示的是**眼鏡的語系**（若手機設英文、眼鏡設繁中，會混語系）。
- **B（較正確）**：改成**結構化事件**（例如 `ringLast = {"g":"TAP","a":"PHOTO","ok":true,"file":"IMG_…jpg"}`），
  由手機用自己的資源組字。缺點：需動協定 + 兩端，工作量較大。

> 目前未動，等決定後再做。稽核工具已可隨時重跑確認。

## 5. 如何重跑

```bash
python3 scripts/audit_hardcode.py            # 全部
python3 scripts/audit_hardcode.py --only 2   # 只看路徑/IP/MAC
python3 scripts/audit_hardcode.py --only 5   # 只看腳本
```

---

# 第二輪：**App 內部**硬編碼複查（2026-09-12 下午）

範圍擴大：除中文字串外，另掃「直接寫進 UI 的字面值、單位／格式字串、dp 數學、套件名、
prefs 名稱、逾時魔術數字、副檔名／MIME、鍵碼魔術值」。

## 6. 本輪修正（皆為**跨檔案一致性風險**）

| 項目 | 問題 | 修正 |
|------|------|------|
| **藍牙 SPP UUID** | 手機 `BluetoothClient` 與眼鏡 `BluetoothServer` **各寫死一份** `00001101-…`；一邊改動就靜默連不上 | 集中為 `Protocol.SPP_UUID`，兩端改為引用 |
| **MIME／副檔名** | `"image/jpeg"`／`"video/mp4"`／`".mp4"`／`".mov"`／`".part"` 散落兩個 App 共 **7 處**，且「是否為影片」「是否為暫存檔」邏輯重複實作 | 新增 `shared/protocol/MediaTypes`（常數＋`isVideo()`／`isPartial()`／`mimeOf()`），全部改呼叫 |
| **WiFi 埠號** | 手機 `DebugReceiver` 寫死 `8555` | 改用 `Protocol.WIFI_PORT` |
| **prefs 名稱** | `"ring_ctrl"` 在眼鏡端兩個檔案各寫一次 | 集中為 `RingPrefs.NAME`（另附 `KEY_READER_OFFSET`／`KEY_PATTERN`） |
| **滑鼠鍵碼位移** | `0x1000 + button` 以魔術數字出現 2 處 | 改為 `RingController.MOUSE_BUTTON_BASE` |

## 7. 結構化事件：眼鏡端不再送「中文句子」給手機（本輪最大修正）

**問題**：眼鏡端 `RingControlManager` 把結果組成中文句子（`已拍照 IMG_…jpg`、`顯示切換為 連線+指示燈`）
經 `STATUS.ringLast` 送到手機顯示 → 手機設英文時會**混語系**，且眼鏡端無法在地化（只有 zh-TW 資源）。

**修正（結構化事件）**：

| 層 | 變更 |
|----|------|
| 眼鏡 `RingControlManager` | 動作回傳 `RingOutcome(key, arg)`（如 `photo_ok` + 檔名）；顯示配置參數改為機器可讀旗標 `conn,led`；中文結果字串**全部移除**（logcat 改英文） |
| `shared` `Status` | `ringLast` → `ringLastKey` + `ringLastArg` |
| 手機 | 新增 `ui/RingText.kt` 對照表：代碼 → 手機語系字串（沿用既有 `ring_*` 資源＋5 個新 key） |
| 除錯 | 手機 `DebugReceiver` 新增 `TEST_RING_TEXT`，可列出全部代碼在目前語系下的文字 |

**實機驗證**（同一批代碼、手機切換語言）：

| 代碼 | zh-TW | en |
|------|-------|-----|
| `photo_ok` | 已拍照 IMG_1.jpg | Photo captured: IMG_1.jpg |
| `silent_on_pending` | 靜音開啟（待 root） | Silent capture on (waiting for root) |
| `display_switched`（arg `conn,led`） | 顯示切換為 連線+指示燈 | Display switched to Connection+Indicator |
| `running` | （隱藏；狀態列已呈現執行中／裝置） | (hidden) |

眼鏡端 `RING_STATUS` 亦確認只送結構化欄位：`enabled=true running=true device=/dev/input/event1 last=running//dev/input/event1`。

## 8. 本輪未修改（附理由）

| 項目 | 現況 | 理由 |
|------|------|------|
| ~~Ghostlock 三個引擎的 `onProgress` 中文（**49 條**）~~ | **已於第三輪完成結構化**（`ProgressEvent`／`GhostlockCodes`／手機 `GhostlockText`），見 §10 | — |
| 手機 `DebugReceiver`（10）、`RingController` logcat（10）、`RingMediaSession`（3） | 中文字面值 | **logcat 除錯訊息**，不進使用者介面 |
| 手機 `LocaleHelper` 語言母語名稱（3） | 中文字面值 | 語言選單必須以該語言自身顯示（`繁體中文`／`日本語`／`العربية`） |
| `shared/ring` 掃描器（RingBonder 14／RingGatt 14／RingWatcher 8） | 中文字面值 | 全部為 `Log.*`／例外訊息（已人工確認無 UI 出口） |
| 逾時與尺寸常數（6000ms、320px、512px…） | 行內數值 | 多為單點使用且語意明確；已將真正重複者（埠號、UUID、MIME、prefs）集中 |

## 9. 掃描結果（App 內部）

| 檢查項 | 結果 |
|--------|------|
| `setText/Toast/contentDescription` 等直接寫入 UI 的字面值 | **0 筆** |
| 單位／格式字串（B/KB/MB、時間） | **0 筆**（皆走資源／`Format`） |
| 程式內 dp/sp 數學與 `applyDimension` | **0 筆** |
| 硬編碼套件／元件名 | 1 筆（眼鏡 `DebugReceiver` 的 action 字串，開發工具） |
| 副檔名／MIME 散落 | 修正前 7 處 → **0 處**（集中 `MediaTypes`） |
| 兩端重複的協定常數（UUID） | 修正前 2 份 → **1 份**（`Protocol.SPP_UUID`） |

---

# 第三輪：Ghostlock 進度訊息結構化（2026-09-12 晚）

第二輪盤點時列為「未修改」的 **49 條 ghostlock 進度／失敗中文訊息**，本輪全部結構化。

## 10. 作法

| 層 | 變更 |
|----|------|
| `shared` | 新增 `Progress.kt`：`ProgressEvent(code, arg)`、`GhostlockCodes`（37 個代碼）、`ProgressCodec`（`code\|arg` 編解碼，用於 ack 失敗訊息） |
| 眼鏡 `GhostlockEngine` | `onProgress` 型別改為 `(ProgressEvent) -> Unit`；新增 `progress(code, arg)`；**所有**進度與失敗訊息改送代碼（`ExecResult.fail(stage, ProgressCodec.encode(code, arg))`）；引擎內中文結果字串全部移除 |
| 眼鏡 `AppGraph.pushProgress` | 送 `{code, arg, message}`（`message` 保留為 `code\|arg`，方便 logcat 與向後相容） |
| 手機 | 新增 `ui/GhostlockText.kt`（37 個代碼 → 手機語系字串）；`RemoteFragment` 進度以代碼渲染、`Remote.requireOk()` 的錯誤訊息經 `ProgressCodec` 解碼後在地化 |
| 字串 | 新增 37 個 `ghost_*` key × 14 語系（累計 **180 keys**；後續 P0-b/P2 再增 13 條，**現為 193 keys**） |
| 除錯 | 手機 `DebugReceiver` 新增 `TEST_PROGRESS`，列出全部代碼在當前語系的文字 |

## 11. 驗證

- **對照表**：`TEST_PROGRESS` 於 zh-TW 與 en 各跑一輪，37 個代碼全部有對應（無 `(unmapped)`）：
  `gl_skip_lpe → 已偵測到常駐 root daemon，跳過 LPE` / `Persistent root daemon detected; skipping LPE`；
  `gl_kernel_panic → kernel panic：裝置已重啟（正常失敗，需重試）` / `Kernel panic: device rebooted (expected failure, retry needed)`；
  `gl_display…`（戒指）等亦同。
- **端到端**：手機按「立即取得 root」觸發真實提權流程，手機操作紀錄面板出現
  `[進度] 已偵測到常駐 root daemon，跳過 LPE` — 確認
  眼鏡 `progress(gl_skip_lpe)` → `ProgressEvent` → `pushProgress` → 手機 `GhostlockText` → 日誌面板 的完整鏈路。
- 兩端 `assembleDebug`＋`lintDebug` 綠燈；i18n 驗證通過（當時 180 keys；**現為 193 keys**）。
- 代碼集精確數字：`GhostlockCodes` 定義 **42** 個代碼，手機 `GhostlockText` 映射 **38** 個；
  未映射的 4 個為 `ENGINE_SHIZUKU_RUNNING`／`ENGINE_BRIDGE_RESIDENT`／`ENGINE_NONE`／`ENGINE_RETRY_OTHER`
  （**診斷用途**：只寫入 logcat 與 `lastReasonCode`，不進使用者介面；僅 `ENGINE_SELECTED` 有映射）。

## 12. 現況總結（三輪後）

| 類別 | 狀態 |
|------|------|
| 眼鏡 → 手機的**所有**顯示文字（戒指結果、ghostlock 進度／失敗） | **結構化**（只傳代碼＋參數，手機以自己語系組字） |
| 眼鏡端 `ghost/` 中文殘留 | 僅 logcat 訊息（`SilentMode` 3 條；其餘引擎已無中文結果字串） |
| 手機端中文殘留 | logcat／開發工具（`DebugReceiver`）、語言母語名稱（刻意） |
| 跨檔重複常數 | SPP UUID／MIME／prefs／埠號／鍵碼位移全部集中 |
| 腳本 ADB/SERIAL | **18/18 支 `deploy/*.sh` 皆 `source deploy/env.sh`**（本文 §2.4 寫的「11 支＋3 支」是當時數字） |

---

## 13. 追加（2026-09-13 晚）：稽核工具的盲點與補強

本文 §5 把 `DebugReceiver` 的行動字串歸類為「開發工具」而**未評估其可達性**，
§12 也把「手機端中文殘留＝開發工具」列為可接受。戒指稽核
（`reports/ring_capture_security_audit_2026-09-13.md`）發現這個分類漏掉了真正的問題：
**元件可達性不是「文字」問題，而是「誰能觸發」問題**。

### 13.1 `scripts/audit_hardcode.py` 新增三節

| 節 | 檢查 | 為什麼先前會漏 |
|----|------|---------------|
| 6 | `android:exported="true"` 而**無** `android:permission` 的元件（launcher activity 與僅受系統保護廣播的 receiver 會標為「預期」以降噪） | 舊版只掃字串／路徑／版面 |
| 7 | 憑證型字面值（`pass`／`password`／`token`／`secret`／`pin`；`key` 結尾者需值像金鑰才算） | 舊版沒有憑證視角 |
| 8 | world-accessible `chmod`（mode 含 other 位元） | 舊版**只掃 Kotlin**，完全沒看 native |

### 13.2 補完立刻抓到的兩項（先前三輪都沒看到）

1. **`deploy/root_daemon.c:132` `chmod(..., 0777)`** → 眼鏡的 `/data/local/tmp/root_daemon.sock`
   實機為 `srwxrwxrwx root root`，且 `getenforce` ＝ **Permissive** →
   **任何已安裝 App 都能連上取得 root shell**（本專案自己引入的介面）。
2. **`scripts/rokid_ota_check.py:25-28` 寫死 OTA API 憑證**（`KEY`／`SECRET`／`DEVICE_TYPE_ID`；
   其中兩項來自 bootloader 屬性 `ro.boot.secret`／`ro.boot.devicetypeid`）。

兩者都已記入 `reports/FACTS.md` §7（P12／P13）並列入待決修復清單；**尚未修改程式**。

### 13.3 已套用的零風險清理

| 項目 | 檔案 | 驗證 |
|------|------|------|
| `"RGR"` 集中為單一常數（11 處引用改寫） | 新增 `shared/ring/RingIdentity.kt`；`RingBonder`／`RingWatcher`／`RingEvdevReader`／`RingControlManager`／`CommandRouter`／兩端 `DebugReceiver`／`SettingsFragment` | `./gradlew :app-glasses:compileDebugKotlin :app-phone:compileDebugKotlin --offline` → **BUILD SUCCESSFUL** |
| `chmod 0777 → 0600`（未採用的備援 daemon） | `deploy/ring_input_daemon.c` | zig 交叉編譯通過（aarch64 ELF） |
| 稽核工具第 6–8 節 | `scripts/audit_hardcode.py` | 實跑即抓到上列兩項 |

> §12 的結論因此需修正一句：**「手機端中文殘留＝開發工具」沒錯，但「開發工具」不等於「低風險」**——
> `DebugReceiver` 是 exported 且無 permission 的元件，已實測可由一般 App（uid 10346／10064）觸發。
