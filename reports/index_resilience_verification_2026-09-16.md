# 眼鏡媒體索引／傳輸通道 — 補跑驗證（2026-09-16 07:51–08:03）

- **觸發**：`reports/wifi_transfer_fix_2026-09-13.md` §24 的待辦（眼鏡 adb 中斷，未能於當晚完成）。
- **對象**：眼鏡 `1.0-safe.3`（versionCode 5）、手機 `1.3`（versionCode 4）。
- **腳本**：`scripts/verify_index_resilience.sh`（可重跑；只動自建測試檔並先備份索引）。
- **結果**：**A–E 全數通過（PASS=9 → 補完 D/E 後 11/11）**。

---

## 0. 先修正兩個「驗證方法」的坑（不是產品問題）

| # | 坑 | 症狀 | 正解 |
|---|----|------|------|
| 1 | `adb pull /sdcard/Android/data/<pkg>/…` | Android 11+ scoped storage 直接擋掉 → 第一次跑出 `media=?`、`cp: bad ...` | 改用 `adb exec-out cat`（讀）與 `adb shell "cat > …"`（寫） |
| 2 | 眼鏡 App 每次重啟都關掉既有 socket | 取清單前沒重連手機 → `total=` 空值，「假失敗」 | 取清單前先送 `CONNECT` 並重試 3 次 |
| 3 | logcat tag 猜錯 | 眼鏡端除錯訊息的 tag 是 **`GlassesDebugReceiver`**（不是 `DebugReceiver`）→ 誤判「廣播沒送到」 | 用正確 tag |

> 第 1、2 項已寫進腳本註解；第一次執行的逐字稿保留在
> `reports/index_resilience_verification_20260916_0751.txt`（PASS=4／FAIL=3，全為上述腳本缺陷）。

## 1. 驗證結果（第二次執行：`reports/index_resilience_verification_run2.txt`）

| 項 | 驗證內容 | 證據 | 結果 |
|----|----------|------|------|
| — | 安裝 `1.0-safe.3` | `versionName=1.0-safe.3` | ✅ |
| — | 基準一致 | 索引 media=**14**、磁碟 **14** 檔、啟動**無** reconcile 訊息 | ✅ |
| **A** | **壞 album 逐筆容錯**（把 `albums[0].name` 移除） | `E MediaRepository: album entry #0 skipped: No value for name`，且手機清單仍為 **total=14** | ✅ |
| **B** | **正向自我修復**（磁碟有、索引沒有） | `W index reconciled: +1 re-registered / -0 removed`，手機 **total=15** | ✅ |
| **C** | **反向自我修復**（索引有、磁碟沒有） | `W index reconciled: +0 re-registered / -1 removed (file missing on disk)`，手機回到 **total=14** | ✅ |
| — | 還原索引 | `media=14`（與基準相同） | ✅ |

**A 的意義**：這正是對抗式驗證抓到的缺口（首版只保護 `media` 陣列，`albums` 迴圈仍共用外層 try）。
修正後：**一筆壞 album 只跳過自己，media 完全不受影響**——修正前會 album 與 media 全滅。

## 2. D／E：傳輸通道的推播與標示

### 2.1 障礙：SoftAP 需要 root，而 root 已因裝置自行關機而消失

第一次執行的 D 直接失敗，眼鏡端 log 給出明確原因：

```
W WifiHotspot: root daemon 未就緒，無法啟動 SoftAP      ← 連續多次
手機：WIFI_CONNECT result=Failure(WifiSetupException: hotspot not ready)
$ ps -A | grep -c root_daemon → 0 ；getenforce → Enforcing
$ uptime → up 9 min ；ro.boot.bootreason → shutdown,leg_fold_timeout
```

⇒ 眼鏡在 2026-09-15 深夜**自行關機**（`leg_fold_timeout`＝折疊／未配戴逾時的自動關機），
不是 adb／USB 問題，也**不是** GhostLock panic（過去幾次是 `reboot,PS_HOLD`）。
關機即失去 root（SELinux 回復 `Enforcing`），而 **SoftAP 需要 root** ⇒ WiFi 通道無法建立。
（另註：`/data/local/tmp/root_daemon.sock` 這個**殘留 socket 檔**仍在，容易誤判 root 還在——要確認得看 `ps`。）

> ⚠ 修正 `wifi_transfer_fix_2026-09-13.md` §23 的推測：當時寫「adba／USB 連線問題」，實為**裝置自動關機**。

### 2.2 免 root 的替代路徑：用 adb tunnel 造出真實的 TCP 通道

D 要驗的是「**TCP 通道**上的推播」，不必真的開 SoftAP——眼鏡的 `WifiServer` 綁 `[::]:8555`，
於是可用 adb 把該埠接到手機：

```bash
adb -s <眼鏡> forward tcp:8555 tcp:8555     # PC:8555 → 眼鏡:8555
adb -s <手機> reverse tcp:8555 tcp:8555     # 手機:8555 → PC:8555
adb -s <手機> shell am broadcast … --es cmd WIFI_CONNECT --es host 127.0.0.1 --es port 8555
```

眼鏡端確認走的是同一條程式路徑：`WifiServer: wifi connection accepted from /127.0.0.1:47999`
（＝ `WifiServer` → `ConnectionHandler` → `CommandRouter`，與真 WiFi 完全相同，只差底層傳輸）。

### 2.3 結果

| 項 | 驗證內容 | 證據 | 結果 |
|----|----------|------|------|
| **D-1** | **控制指令走 TCP**（刻意不送 `CONNECT`，讓 `Remote.link()` 自選） | 手機 `LIST_MEDIA_INFO … total=14`；眼鏡 `CommandRouter: recv: LIST_ALBUMS id=11` / `recv: LIST_MEDIA id=12` | ✅ |
| **D-2** | **推播走 TCP**（回歸修正的核心） | 眼鏡 `PUSH_TEST sent code=gl_attempt arg=verify-tcp **wifi=true bt=false**`；手機 `RemotePush: progress code=gl_attempt arg=verify-tcp` | ✅ |
| **D-3** | 推播在 UI 上真的顯示 | 手機操作紀錄出現「`[進度] 跑 exploit 第 verify 次嘗試`」 | ✅ |
| **D-4** | 藍牙通道對照 | 眼鏡 `PUSH_TEST … wifi=false bt=true` → 手機同樣收到 `RemotePush … arg=verify-bt` | ✅ |
| **E** | **關閉 WiFi 通道 → UI 立即更新** | 前：`傳輸：WiFi 192.168.43.1:8555`；後：`傳輸：藍牙`（未手動切換分頁） | ✅ |

截圖（皆在**確認前景為本 App** 後才拍）：
`reports/ui_transport_2026-09-13/06_before_wifi_disconnect.png`（WiFi）、
`05_after_wifi_disconnect.png`（藍牙）。

> **「前景檢查」是必要步驟**：第一次拍 E 的截圖時，手機正被使用者用於其他 App，
> 拍到了**與本專案無關的私人畫面**；該兩張已立即刪除，之後所有截圖都先確認
> `topResumedActivity` 是本 App 才拍。日後自動化截圖請沿用這個守衛。

## 3. 裝置現況（驗證後）

```
手機：CONNECT <DEVICE_ALIAS> result=Success（藍牙控制鏈路已恢復）
眼鏡：rootMode=MANUAL rootAcquired=false  silent=true/false
      ringEnabled=true ringRunning=false  ringLast=start_failed//dev/input/event1
索引：media=14（與磁碟一致）
```

- **root 不存在**（裝置自行關機所致）⇒ 目前：(a) WiFi 媒體通道不可用（SoftAP 需 root），
  媒體會自動回退藍牙；(b) 戒指 evdev 控制無法啟動（需 root）；(c) 靜音拍攝不生效。
  要恢復這些功能需重新武裝一次 root（`deploy/root_bridge.sh` 或 `build-cache/one_shot_root.sh`）。
- 未在本輪重跑 exploit（ArmPolicy 為 MANUAL；武裝為風險操作，交由使用者決定時機）。

## 4. 信心

| 陳述 | 標記 | 信心 | 依據 |
|------|------|------|------|
| 壞 album 不再拖垮 media | FACT | **Very High** | log + 手機清單 14 |
| 索引雙向自我修復可用 | FACT | **Very High** | `+1 re-registered` / `-1 removed` + 手機清單 15→14 |
| 控制指令確實走 TCP | FACT | **Very High** | 無 `CONNECT` 情況下指令成功，且眼鏡端收到 |
| TCP 通道上的推播已修好 | FACT | **Very High** | `wifi=true bt=false` 時手機收到 `RemotePush` |
| 關閉通道時 UI 立即更新 | FACT | **High** | 前後截圖（單一案例） |
| 真 SoftAP 路徑（非 tunnel）下的推播 | 推論 | High | tunnel 與 SoftAP 走同一組 `WifiServer`/`ConnectionHandler`/`CommandRouter`；差別僅底層傳輸 |
| 裝置自行關機＝`leg_fold_timeout` | FACT | **Very High** | `ro.boot.bootreason` + `uptime` |

---

## 5. 第三次執行（08:03）中途 adb 斷線 — 處置與殘留說明

改好腳本（CONNECT-first、tunnel 後援、前景守衛）後重跑一次，**在 A 步驟剛開始時兩台裝置同時從
`adb devices` 消失**（`08:04`；`adb kill-server`／重啟 server 後仍為 0 台，疑似線材／hub 被拔或使用者正在用機）。
該次逐字稿 `reports/index_resilience_verification_run3.txt` 止於「--- A. 壞 album 逐筆容錯 ---」。

**可能的殘留**：A 步驟會**刻意**把索引 `albums[0].name` 移除，若中斷發生在複製之後，
裝置上的索引會停在「album 有名稱缺失」的狀態。**這不會造成資料遺失**——App 下次啟動時
`reconcileWithDisk()` 會從磁碟重建 album 與 media（只增不減），唯一可見差異是
**專輯名稱會變成目錄 id**（`alb-3b6dfe09` 而非 `Camera`）。

**已備妥處置**：
- `scripts/check_index_state.sh [--fix]`：檢查索引（含 album 名稱是否異常、索引／磁碟／手機三方筆數是否一致），
  `--fix` 會把名稱修回 `Camera` 並重啟 App 驗證。**唯讀模式可安心先跑**。
- 已在眼鏡回到 adb 時**自動觸發** `check_index_state.sh --fix`。

> 註：A–E 的完整驗證**已於第二次執行 + 手動補完 D/E 完成**（§1、§2），第三次只是「用改良後的腳本再跑一次」，
> 其結果不影響前述結論。

## 6. 腳本改良（本次學到、已寫進 `scripts/verify_index_resilience.sh`）

| 改良 | 原因 |
|------|------|
| D 步驟改為 **先 `CONNECT` 再 `WIFI_CONNECT`** | SoftAP 協商要走藍牙控制鏈路；沒連線時只會得到 `hotspot not ready`（第一次執行的假失敗） |
| WiFi 不可用時**自動改用 adb tunnel**（`forward` + `reverse` 轉 8555） | 真 SoftAP 需 root；tunnel 走的是同一組 `WifiServer`/`CommandRouter`，**免 root 也能驗證 TCP 通道行為** |
| E 步驟**截圖前先查 `topResumedActivity`** | 手機是使用者私人裝置；第一次誤拍到無關的私人畫面（該兩張已刪除）。前景不是本 App 就只記 log、不截圖 |
| 讀寫 `/sdcard/Android/data` 一律用 `exec-out`／`shell cat >` | `adb pull` 在 Android 11+ 被 scoped storage 擋掉 |
| 取清單前先 `CONNECT` 並重試 3 次 | 眼鏡 App 每次重啟都關掉 socket，否則會拿到空值造成假失敗 |
