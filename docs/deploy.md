# 建置與佈署指南

## 0. 前置

- 開發機：Android Studio（Windows/WSL 均可），JDK 17（本機路徑見 §1）。
- Rokid 眼鏡已開啟 ADB（`adb devices` 可看到，序列號範例 `<DEVICE_SERIAL>`）。
- `ghostlock_rv101` 與 `root_daemon` 已放上眼鏡 `/data/local/tmp/`（若尚未，見 §4）。
- **Shizuku manager APK 已安裝**（預設觸發路徑，見 §9.1）＋眼鏡 App 已預授權。
- 手機與眼鏡已在系統藍牙設定中**配對**（控制通道）。
- （選用）WiFi 媒體傳輸：手機需加入眼鏡 SoftAP `RokidCam`（§9.4）。

## 1. 建置

在 Android Studio 開啟 `android/` 目錄（含 `settings.gradle.kts`），同步後：

- 眼鏡 App：模組 `:app-glasses` → `assembleDebug`
- 手機 App：模組 `:app-phone` → `assembleDebug`

命令列：

```bash
cd android
# 若無 wrapper，先讓 Android Studio 產生，或用本機 gradle
./gradlew :app-glasses:assembleDebug :app-phone:assembleDebug
```

> 本機（WSL/Kali）建置注意：系統無 JDK，請用工作區內建的 Temurin 17：
> ```bash
> export JAVA_HOME=/home/kali/rokid_root/tools/jdk/jdk-17.0.20.1+1
> export GRADLE_USER_HOME=/home/kali/rokid_root/gradle_home   # 可選（共用依賴快取）
> ```

產出：
- `android/app-glasses/build/outputs/apk/debug/app-glasses-debug.apk`
- `android/app-phone/build/outputs/apk/debug/app-phone-debug.apk`

## 2. 佈署眼鏡端

```bash
cd deploy
# 依你的 adb 路徑/序列號調整 deploy.sh 頂部變數
./deploy.sh
```

腳本做的事：
1. 確認 `ghostlock_rv101` 存在於 `/data/local/tmp/`（不存在則提示手動 push）。
2. `adb install -r` 安裝眼鏡 APK。
3. 啟動眼鏡 App（`am start -n <pkg>/.MainActivity`）。
   ⚠ **不要**用 `am start-foreground-service`——Android 12 背景啟動限制會回
   `app is in background`；由 Activity 在前景啟動 FGS 才是可用路徑。
4. 提示先完成藍牙配對（控制走 RFCOMM；媒體可另開 WiFi 通道 §9.4）。

## 3. 佈署手機端

- 直接安裝手機 APK（`adb install` 或傳檔安裝）。
- 先在系統藍牙設定中配對手機與眼鏡，再開啟 App → 自動連線或選取已配對的眼鏡。

## 4. 佈署 ghostlock_rv101（一次性）

```bash
adb push /path/to/ghostlock_rv101 /data/local/tmp/ghostlock_rv101
adb shell chmod 755 /data/local/tmp/ghostlock_rv101
```

> App 端觸發層已實作（見 `architecture.md §5`）：預設走 **Shizuku 引擎**（§9，shell 身份直跑
> exploit），Shizuku 不可用時自動回退 root 橋接（§7）。實機驗證前，可先以既有的
> `scripts/run_led_orchestrator.sh` 確認 exploit 可正常掉 root。

## 5. 連線與測試

1. 眼鏡端啟動 App（`am start -n com.rokid.rc.glasses/.MainActivity`，會同時拉起 CaptureService）。
2. 手機端開啟 App → 藍牙連線 → 選取已配對的眼鏡（顯示「已連線」＋傳輸模式）。
3. 基本：切換 ghostlock → 拍照/錄影 → 檢視相簿 → 下載原檔。
4. 眼鏡端本機：底部儀表板五區狀態；點按橫幅可收合（全透明不遮視線）。
5. 眼鏡顯示配置：手機「進階」卡 →「眼鏡顯示」5 chip／全顯示／全隱藏（眼鏡端持久化）。
6. WiFi 媒體：手機按「啟用 WiFi 傳輸」→ 系統彈窗加入 `RokidCam` → 傳輸模式顯示 WiFi。
7. 操作紀錄：兩行預覽；長按開啟完整視窗（可捲動／選取複製／清除）。

## 6. 疑難排解

| 症狀 | 可能原因 | 處理 |
|------|---------|------|
| 連不上 | 藍牙未開啟/未配對 | 確認兩端藍牙開啟、已在系統配對 |
| 拍照無反應 | 相機 lens facing 設定 | 調整 `CameraController` 的 `LENS_FACING` / camera id |
| ghostlock 無效 | Shizuku 未就緒且 helper 未常駐 | 確認每 boot 跑過 §9.2 的 `start_shizuku.sh` 或 §7 的 `root_bridge.sh`（引擎會**執行期自動探測**，不需改程式） |
| 相簿空 | 索引未刷新 | 眼鏡端重啟服務會重掃資料夾 |

## 7. root 橋接 daemon（備援路徑）

當 Shizuku 不可用（server 未啟動/未授權）時，`ShizukuGhostlockEngine` 會**自動回退**到此橋接模式：

```bash
cd deploy
./root_bridge.sh
```

它會：建立 `chmod 777` 的 `/data/local/tmp/ghostlock_bridge/`、寫入並常駐 `ghostlock_bridge_helper.sh`（shell 迴圈），並在 `[2.5/4]` 推送 `root_daemon`（見 §8）。

（已移除）過去可用 `AppGraph.USE_ENGINE` 強制指定引擎；2026-09-13 起改為執行期自動探測（見 §9.3），
`Engine.DIRECT` 僅剩研究用途且會 kernel panic，不在自動選擇清單內。

通訊（皆在 `/data/local/tmp/ghostlock_bridge/`）：
- `last.cmd`：App 寫入 payload → helper 讀走並置換進 `.ghostlock_root.sh`
- `last.done`：payload（root）寫入 DONE 標記 → helper 據此判定完成
- `last.out`：helper 回寫結果（`OK` / `ERR:...`）

helper log：`/data/local/tmp/ghostlock_bridge_helper.log`

## 8. 常駐 root daemon（單次提權後，LED 秒級控制）

提權成功後，payload（`LedPayloads.startRootDaemon`）不再「每次指令重跑 LPE」，
而是拉起一個常駐 root daemon 監聽 UNIX domain socket。App 之後的 LED 指令直接走
socket（毫秒級），只有 socket 消失時才回退到 §7 的 bridge/LPE 路徑。

### 8.1 建置

```bash
cd deploy
./build_root_daemon.sh
```

- 用 NDK r27d 的 `aarch64-linux-android24-clang` 交叉編譯 `root_daemon.c` → `root_daemon`。
- 腳本會先把原始碼複製到 Windows 可讀路徑（`/mnt/c/Users/<user>/`）再編譯，因為
  Windows clang.exe 讀不到 WSL 的 `/home` 路徑。

### 8.2 佈署與啟動

`root_bridge.sh` 的 `[2.5/4]` 已把 `root_daemon` push 到 `/data/local/tmp/root_daemon`（0755）。
它**不是**直接啟動，而是等提權 payload 以 root 執行：

```sh
echo 0 > /sys/fs/selinux/enforce 2>/dev/null
if ! grep -q 'root_daemon.sock' /proc/net/unix 2>/dev/null; then
    rm -f /data/local/tmp/root_daemon.sock
    setsid /data/local/tmp/root_daemon >/data/local/tmp/root_daemon.log 2>&1 &
fi
```

> 為何用 `/proc/net/unix` 判斷存活：**reboot 後 `/data/local/tmp` 會殘留 stale 的
> `root_daemon.sock`「檔案」但沒有 listener**。若只檢查 `[ -S ]` 會誤判而跳過啟動，
> 導致 socket 永遠連不上。`/proc/net/unix` 只列出有 listener 的 socket，可正確分辨。

### 8.3 socket 協定

- 路徑：`/data/local/tmp/root_daemon.sock`（`chmod 0777`，讓 untrusted_app 可連）。
- 一條連線 = 一個指令：client 連上 → 寫入指令（純文字，可多行/結尾換行）→ `shutdownOutput`
  → daemon 讀到 EOF → `sh -c` 執行 → 把 stdout 寫回 → 關閉連線。
- App 端 client：`RootDaemonClient`（`android.net.LocalSocket`）。

### 8.4 為何 daemon 要 fork-per-connection（重要）

原版 daemon 是「單執行緒 + `popen` 管線」，有兩個會把它永久卡死的 bug：

1. 指令背景化子行程（白燈 hold-loop）繼承 `popen` 管線寫端 → `fgets` 永不 EOF。
2. 背景化子行程繼承 client socket `c` → client 端 `readText()` 永遠收不到 EOF。

修法（已寫入 `root_daemon.c`）：
- **每條連線 fork 一個 handler**：單一指令卡住不影響 accept 迴圈。
- **stdout/stderr 導向暫存檔**（非 pipe）：`sh -c` 在 `... &` 後即返回，`waitpid` 立即回收。
- **sh -c child 關閉 `fd 3..127`**（含 client socket）：背景子行程不持有連線。
- `main()` 開頭也關 `fd 3..127`：防禦 `setsid ... &` 從 shell 啟動時帶入舊連線。

## 9. Shizuku 模式（新預設：App 以 shell 身份直跑 exploit）

原理（詳見 `docs/shizuku-feasibility-report.md`）：Shizuku server 以 adb shell 啟動後，
App 經 UserService（uid 2000 + `u:r:shell:s0`，等同 adb shell）執行 `ghostlock_rv101`，
取代 helper 迴圈。**每 boot 需重啟一次 Shizuku server**（含 exploit kernel-panic 重啟後）。

### 9.1 一次性前置

```bash
# 1) 安裝 Shizuku manager APK（v13.6.0+，從 https://shizuku.rikka.app/download/ 下載）
adb install -r shizuku-v13.x.x-rxxx.apk
# 2) 預授權眼鏡 App（跳過授權 UI；dangerous 權限，server 啟動時寫回授權 config）
adb shell pm grant com.rokid.rc.glasses moe.shizuku.manager.permission.API_V23
```

### 9.2 每 boot bootstrap（一鍵）

```bash
cd deploy
./start_shizuku.sh    # = 確認 manager + pm grant + adb shell <lib>/libshizuku.so
```

驗證：`adb shell ps -A | grep shizuku_server` 應常駐。之後眼鏡 App 開機（BootReceiver
自動啟動 CaptureService）→ 觸發 ACQUIRE_ROOT（或 rootMode=AUTO 自動）→ Shizuku 引擎
以 shell 身份跑 exploit → 提權後 payload 啟動 root_daemon（§8）。

### 9.3 模式與回退

> **2026-09-13 更新（P1）**：引擎改為**執行期自動探測**，`AppGraph.USE_ENGINE` 編譯期開關已移除。
> 每次提權時由 `AutoGhostlockEngine` 判斷：Shizuku binder 可用 → 走 Shizuku（內部仍會回退 bridge）；
> 否則 bridge helper 心跳（`ghostlock_bridge/helper.alive`）新鮮 → 走 bridge；兩者皆無 → 選 Shizuku 引擎
> 並回報「需先啟動 server/helper」。**換環境不需重新建置 APK。**
- `Engine.SHIZUKU`（原預設，現為自動選擇的一支）：Shizuku 不可用（server 未啟動/未授權/
  綁定失敗）時自動回退 §7 bridge helper。
- `Engine.BRIDGE`：helper 迴圈（`deploy/root_bridge.sh` 常駐）。
- `Engine.DIRECT`：App 直連（symlink trick；untrusted_app 跑 exploit 會 kernel panic，**已不使用**，
  `ShellGhostlockEngine` 僅保留供研究，也不在自動選擇清單內）。
- 注意：若 Shizuku server 是 **root** 啟動的（子程序變 uid 0），引擎會嘗試用
  `/data/local/tmp/run_shell` 降回 shell 再跑 exploit；無 run_shell 時會明確報錯。
  因此 bootstrap 一律走 §9.2 的 adb/shell 啟動。

### 9.4 WiFi 傳輸（控制與媒體都優先走 WiFi，藍牙為回退）

- 拓撲：眼鏡**提權後自動開 SoftAP**（SSID `RokidCam`、密碼 `rokid1234`、
  固定 IP `192.168.43.1`），眼鏡端 `WifiServer` 監聽 **TCP 8555**（與藍牙共用同一套
  `ConnectionHandler`/`CommandRouter`/frame 協定）⇒ **WiFi 連上後，一般指令也走 WiFi**。
- **不需要手動開啟**：媒體操作（縮圖／下載）前，`Remote.ensureMediaLink()` 會自動
  請眼鏡開熱點（`ENSURE_WIFI_AP`，輪詢到 AP 就緒）→ 加入 AP → 建立 TCP；
  由持久化偏好 `auto_wifi_media`（**預設開啟**，設定頁「WiFi 傳輸」開關）控制。
  首次加入 AP 會跳系統「加入 RokidCam」確認框，**點允許**（手機已在該 AP 上時不會跳）。
- ⚠ **兩條通道互斥（最重要）**：眼鏡端 `BluetoothServer.accept()` 與 `WifiServer.accept()`
  都會先 `graph.connection?.close()`（後連上者勝）。因此**任何走藍牙的指令都會踢掉 WiFi socket**，
  媒體便靜默退回藍牙——這是「明明很快、卻顯示藍牙」與「10 秒影片極慢」的根因
  （已修：`Remote.link()` WiFi 優先 ＋ `ensureConnected()` 在 WiFi 連線時不重撥藍牙）。
- 顯示：遙控頁顯示三段狀態（WiFi 已連線／**WiFi 連線中…**／藍牙），由
  `Remote.onTransportChanged`（`LinkClient.onConnected`/`onDisconnected`）即時更新。
- 回退：WiFi 未連線或中斷時自動回退藍牙。**速率實測**：同一支 10.6 秒 / 16,736,320 bytes 影片——
  **WiFi 3.0–3.8 秒（4.4–5.6 MB/s）**；**藍牙 324 秒且逾時失敗（51.7 KB/s）**，相差約 85 倍。
- 眼鏡端 debug 指令：`am broadcast -n com.rokid.rc.glasses/.DebugReceiver --es cmd PUSH_TEST [--es code <碼> --es arg <參數>]`
  —— 主動推送一個結構化進度事件，用來驗證「眼鏡端 push 走當前連線 → 手機兩條通道都收得到」
  （修正前只註冊了藍牙的 `onPush`，WiFi 為當前通道時推播會被丟棄）。手機端收到時會在 logcat 印 `RemotePush: progress code=… arg=…`。
- 手機端 debug 指令：`am broadcast -n com.rokid.rc.phone/.DebugReceiver --es cmd WIFI_CONNECT`
  （可加 `--es host <ip>` 直連測試）、`WIFI_DISCONNECT` 關閉。
- 注意：本裝置韌體 **STA 與 SoftAP 不可同時維持**（開熱點會把既有 WiFi STA 踢掉）；
  關閉熱點後 STA 自動恢復（`cmd wifi stop-softap`，root）。

### 9.5 眼鏡儀表板與遠端顯示配置

- 眼鏡端 MainActivity 為**底部橫幅資訊儀表板**（連線/Root/相機/儲存/指示燈 五區），
  **全透明（無面板底色、無背景色）**——文字直接浮在視線上，不遮蔽視野；
  顯示為主、操作由手機；點按橫幅（觸控板 tap 或 DPAD 中心鍵/Enter）切換展開/摺疊。
- 手機端「進階」卡內有「**眼鏡顯示**」區塊：5 個 chip 即時推送 `SET_DISPLAY_CONFIG`，
  另有「全顯示/全隱藏」快鍵；配置在眼鏡端持久化（reboot 保留），手機連線時自動讀回同步。
  五類全隱藏時眼鏡自動收成摺疊條。
- 除錯廣播（避免對眼鏡做 input 注入——實測有整機重啟風險）：
  `am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd <TOGGLE_PANEL|SET_DISPLAY|SET_LOCALE>`（SET_DISPLAY 帶 `--ez conn/root/camera/storage/led true|false`）。

### 9.6 多國語言（14 語系）與 App 內語言選擇

- 兩 App 支援：繁中（預設）、英、簡中、日、法、西、泰、越、韓、阿（RTL）、葡、俄、烏、德。
- **手機端 App 內切換**：遙控分頁 →「語言：<目前語言>」按鈕 → 單選對話框（跟隨系統 + 14 個母語名稱），
  選取後以 `AppCompatDelegate.setApplicationLocales` + `recreate()` 立即生效，寫入 `app_locale`
  SharedPreferences，App 重啟後保留（`LocaleHelper.apply()` 於啟動時套用）。
- 預設跟隨系統語言；per-app 覆寫亦可用廣播：手機 `DebugReceiver SET_LOCALE --es lang <tag>`、
  眼鏡 `Glasses DebugReceiver SET_LOCALE`（`lang` 不帶 = 還原跟隨系統；不影響系統語言）。
- **在地化覆蓋率**：手機端介面、Toast、對話框、**操作紀錄**與狀態摘要皆走字串資源
  （**203 key × 14 語系**；68 為初版數字）；僅 `DebugReceiver` 的 logcat／`TEST_LOG` 測試訊息保留中文（開發者工具）。
  翻譯來源：`scripts/i18n/phone_keys.tsv`（zh-TW／en）＋ `scripts/i18n/out/*.xml`（其餘 12 語系）；
  流程：`normalize_locale_terms.py`（術語對齊各語系既有慣用詞，如 ja `Wi-Fi`／`メガネ`、de `WLAN`、
  ar `واي فاي`）→ `validate_translations.py`（覆蓋率／佔位符／轉義檢查）→
  `apply_phone_strings.py`（併入 `values*/strings.xml`；以上三支皆可重複執行）。
- 實作與驗證細節：`reports/phone_i18n_localization_report.md`（含 en/ja/ar/th 實機證據與 RTL 截圖）。

### 9.7 操作紀錄捲動與品牌圖示

- 手機端「操作紀錄」為**兩行預覽**（固定顯示最新兩行）；**長按**彈出「操作紀錄（完整）」
  視窗：可捲動、可長按選取複製、內建清除，與預覽即時同步；「清除」按鈕一鍵清空。
- 除錯灌入：`am broadcast -n com.rokid.rc.phone/.DebugReceiver -a com.rokid.rc.phone.DEBUG --es cmd TEST_LOG --ei n 25`
- 品牌圖示（SVG）：`android/branding/logo.svg`（全彩）、`logo_monochrome.svg`（單色）；
  兩 App launcher 為 adaptive icon（`mipmap-anydpi-v26` + 前景/單色 vector + 深藍背景
  `#0B1220`，Android 13+ 支援 themed icon）；眼鏡前台服務通知使用 `ic_notification`。
  設計概念：AR 眼鏡 × 相機光圈（青）× 隱私遮斷（琥珀）。

### 9.8 無線 adb（adb-over-TCP）— ✅ **可行，2026-09-20 端到端實測成功**

> ⚠ **本節原結論（「本 build 不支援 adb-over-TCP」）已推翻**。完整證據：
> [`reports/wireless_adb_feasibility_2026-09-20.md`](../reports/wireless_adb_feasibility_2026-09-20.md)。

- **真相**：眼鏡 adbd **本來就綁 TCP**，位置是 **IPv6 wildcard `[::]:5555`**
  （`/proc/net/tcp6`，uid 2000；`netstat` 顯示 `tcp6 [::]:5555 LISTEN`）。
  AOSP 邏輯：`service.adb.listen_addrs` → `service.adb.tcp.port` → **`persist.adb.tcp.port`**
  依序取用，故 `persist.adb.tcp.port=5555` 讓 adbd **每次啟動自動開 TCP**（跨開機、免插線）。
- **原誤判根因**：偵測碼只 grep IPv4 的 `/proc/net/tcp`（`:15B3`）→ 看不到 IPv6-only listener
  → 誤回報 `adb-over-tcp not supported on this build`（**已修正**：`CommandRouter.enableWifiAdb()`
  同時檢查 `/proc/net/tcp` 與 `/proc/net/tcp6`，並回報 `boundOn`）。
- **實測（PC 加入眼鏡 SoftAP `RokidCam`）**：
  ```
  adb connect 192.168.43.1:5555     → connected
  adb devices -l                    → 192.168.43.1:5555   device
  adb -s 192.168.43.1:5555 shell id → uid=2000(shell) context=u:r:shell:s0
  ```
  `adb devices -l                    → 192.168.43.1:5555   device`
  同一次 `adb devices` 可見 **USB 眼鏡 ＋ USB 手機 ＋ WiFi 眼鏡** 三條 transport 並存；
  同一次無線 run 連續 4 條 session（`SESSION_1..4`、`uid=2000`）全部成功 → 多連線並行成立。
  一鍵驗證：`SERIAL=<序號> bash deploy/adb_wifi_verify.sh`（**不需切換 PC 網路**，
  只在 PC 已在眼鏡網段時才嘗試連線）。
- **⚠ 不要重啟 adbd**：文件記載「重啟 adbd 會掉 USB 列舉」是**重啟本身**的副作用
  （vendor 啟動方式 + USB FunctionFS），**與 TCP 綁定無關**；要開 TCP 只需設
  `persist.adb.tcp.port 5555` 後**正常重開機一次**。
- **安全**：等於把偵錯埠開到網路上（`ro.adb.secure=1` 仍要求金鑰，但本機
  `/data/misc/adb/adb_keys` 預載 6 把、且 SoftAP 密碼 `rokid1234` 寫死於 APK）。
  不需要時：`setprop persist.adb.tcp.port ""` ＋重開機，或關閉 SoftAP。
- **備援路線**：若未來 OTA 改壞 adbd，可用 `deploy/adb_tcp_relay.c`
  （TCP → `/dev/socket/adbd`；**需 root**，因 shell 連該 socket 是 `Permission denied`）。
- **「不靠線 root」現況**：無線 adb 已通 → **免 USB 即可重新武裝提權**
  （`deploy/root_bridge.sh` 可改走 `adb connect 192.168.43.1:5555`），
  但**首次連線仍需要把該主機公鑰寫進 `adb_keys`**（眼鏡無螢幕可彈授權視窗，需 root 寫入）。
  Magisk 刷入仍是「開機即持久 root」的唯一路線，但已非「免線」的必要條件。

### 9.9 靜音拍攝（強制壓制拍照/錄影提示音）

- 機制：`libcameraservice` 播放 `/product/media/audio/ui/{camera_click,camera_focus,VideoRecord,VideoStop}.ogg`
  與 Rokid 自訂 `rkd_record_{start,end}.ogg`；`/product` 唯讀，但 root 可 **bind-mount 合法靜音 OGG**
  覆蓋這 6 檔 → 播放端無聲；`umount` 還原；**reboot 自動清除**。
- 使用：提權後 App **自動套用**（預設開啟）；手機「進階」卡可即時開關；`SET_SILENT` 廣播可自動化。
- 驗證：壓制後 md5 == 靜音檔、掛載數 6；拍照/錄影/停止皆正常且無 MediaPlayer 錯誤；
  還原後 md5 復原、無殘留掛載。詳見 `reports/silent_capture_mechanism.md`。
- 限制：需 root；每 boot 需重新套用（與 root 同生命週期）；僅影響相機/錄影提示音。

### 9.11 戒指控制（拍照／錄影）

戒指（BLE HID）可操控眼鏡拍照／錄影，兩條路徑都已實作並實機驗證；**路徑 B 於提權成功後
自動啟動**（App 重啟亦會恢復；裝置 pattern 存 `ring_ctrl.pattern`，預設 `RGR`，
可用 `RING_START --es name` 指定，例如眼鏡觸控板 `TP`）：

| 路徑 | 位置 | 說明 |
|------|------|------|
| **A：手機端攔截** | `app-phone` `ring/RingController`+`RingGestureDetector` | 戒指配手機；`MainActivity.dispatchKeyEvent`/`dispatchGenericMotionEvent` 攔 HID 事件 → 手勢 → 透過既有 BT/WiFi 通道命令眼鏡拍攝（眼鏡端 root 功能照常） |
| **B：眼鏡端 root 直讀** | `app-glasses` `ring/RingEvdevReader`+`RingControlManager` | root daemon 背景跑 `getevent -lt /dev/input/eventN` 寫入 `/data/local/tmp/ring_events.log`，App 每 400ms 以 `tail -c +offset` 取增量 → 手勢 → **本機**拍攝（背景可用、不經手機） |

- 手勢預設：**單擊=拍照、雙擊=錄影開始/停止、長按=靜音切換、滾輪=切換眼鏡顯示資訊**。
- 手機端 UI：遙控分頁「戒指控制」卡 — 路徑 A 啟用開關、手勢說明、狀態、**手勢學習**與**配對戒指**；
  另有**眼鏡端（路徑 B）開關與狀態**（`SET_RING` + `STATUS.ring*`）。
- 攔截位置：鍵盤/媒體鍵走 `dispatchKeyEvent`；**滑鼠式戒指的點擊走 `dispatchTouchEvent`**
  （依 `event.source` 只攔 MOUSE/TOUCHPAD，手指觸控不受影響）；滾輪走 `dispatchGenericMotionEvent`。
- 只攔「戒指可能送的鍵」（ENTER/DPAD_CENTER/媒體鍵/音量鍵/首頁鍵/F1–F12）＋自訂對應，**其餘放行**
  （否則 BACK 等系統鍵會被吃掉）。
- **手機端背景限制**：Android 媒體鍵會派送給 media button session；實測有音樂 App 時
  （`Media button session is yt.morphe.noname.exe`）我們的 `RingMediaSession` 收不到，
  本專案不搶佔。**背景控制的可靠解＝眼鏡端路徑 B**。
  **可學習的鍵碼對應**：學習到的鍵碼可綁定動作（拍照／錄影／靜音／顯示上/下一組／不動作），
  存 prefs 且優先於手勢預設；長按「手勢學習」清除全部自訂對應。
- 戒指探索／配對（共用 `shared/ring/`）：`RingScanner`（BLE 掃描含 raw AD／manufacturer ID）、
  `RingGatt`（連線列舉服務並讀 HID Report Map）、`RingBonder`（掃描後以即時 `BluetoothDevice`
  配對；**不可用字串 MAC 重建，random address 會得到 GATT 133**）、`RingWatcher`（連續監看＋自動配對）。
- ⚠️ **BLE 掃描會影響既有 BLE 連線**：連續掃描（`WATCH_RING`）實測會讓 AIVELA 健康戒指的
  GATT/HID 連線中斷；平日戒指控制不需掃描，配對時才用 App 內「配對戒指」（單次 15 秒）。
- 除錯指令：手機 `SCAN_RING`／`PAIR_RING`／`PROBE_RING`／`WATCH_RING`／`CONNECT`；
  眼鏡 `RING_DEVICES`／`RING_START`／`RING_STOP`／`RING_STATUS`（詳見
  `reports/ring_control_report.md` §6）。
- 注意（實作要點，全部踩過）：
  1. 顯示器休眠時 FGS 無法背景啟動 → `RING_START` 偵測相機未就緒時**改用 root 啟動** CaptureService。
  2. `getevent` 的**停止與啟動不可寫在同一行**（`pkill -f` 會比對到自己而自殺），且用
     `[g]etevent` 括號技巧 + PID 檔。
  3. **讀取位移要防越界**：log 檔被重建後，舊位移會讓 `tail` 永遠讀不到 → 每輪比對檔案大小並歸零。
  4. **手勢判定用事件時間戳**（`getevent -lt` 的 `[sec.usec]`），否則批次補讀會把時序壓縮成 0ms。
  5. 程式被回收後的救援：log 檔 10 秒內有活動 → 從上次位移續讀（不漏手勢）；另備 root 端
     keep-alive（每 15 秒檢查並拉回 App；`force-stop` 後無法喚起屬 Android 限制）。

### 9.10 除錯指令與腳本索引

**廣播指令（自動化驗證用；指令格式：`am broadcast -n <元件> -a <action> --es cmd <CMD> [extras]`）**

| 目標 | 元件 / action | 指令 | 用途 |
|------|---------------|------|------|
| 手機 | `.DebugReceiver` / `com.rokid.rc.phone.DEBUG` | `SET_LED`（`--ez on`、`--es color`） | 開關 LED |
| 手機 | 同上 | `CAPTURE_PHOTO` / `START_VIDEO` / `STOP_VIDEO` | 拍照錄影 |
| 手機 | 同上 | `SET_GHOSTLOCK --ez enabled` | ghostlock 開關 |
| 手機 | 同上 | `ACQUIRE_ROOT` | 觸發提權 |
| 手機 | 同上 | `GET_DEVICE_INFO` / `LIST_MEDIA_INFO` / `TEST_CHAIN` | 查狀態／媒體鏈測試 |
| 手機 | 同上 | `SET_DISPLAY --ez conn/root/camera/storage/led` | 設定眼鏡顯示內容 |
| 手機 | 同上 | `WIFI_CONNECT`（可加 `--es host <ip>`）／`WIFI_DISCONNECT` | WiFi 媒體通道 |
| 手機 | 同上 | `WIFI_ADB` | 要求眼鏡開無線 adb（**2026-09-20 已實測可行**；眼鏡回 `ip`/`port`/`boundOn`，PC 以 `adb connect <ip>:5555` 連入） |
| 手機 | 同上 | `SET_SILENT --ez enabled` | 靜音拍攝（壓制快門/錄影提示音，需 root） |
| 手機 | 同上 | `SET_LOCALE --es lang <tag>` | per-app 語言 |
| 手機 | 同上 | `TEST_LOG --ei n 25` | 灌入 log 行（驗證兩行預覽/長按視窗） |
| 眼鏡 | `.DebugReceiver` / `com.rokid.rc.glasses.DEBUG` | `TOGGLE_PANEL` | 儀表板收合/展開 |
| 眼鏡 | 同上 | `SET_DISPLAY --ez …` | 本機設定顯示配置 |
| 眼鏡 | 同上 | `SET_LOCALE --es lang <tag>` | per-app 語言 |

> ⚠ 眼鏡端**不要**用 `input tap/keyevent` 自動化（實測曾導致整機重啟）。

**腳本索引（`deploy/`）**

| 腳本 | 用途 |
|------|------|
| `deploy.sh` | 安裝眼鏡 APK 並啟動 App |
| `start_shizuku.sh` | 每 boot 一次：安裝/確認 manager、預授權、啟動 Shizuku server |
| `test_shizuku_e2e.sh` | Shizuku 路徑端到端提權測試（含 panic 自動重試） |
| `test_bridge_fallback.sh` | Shizuku 不可用時的回退路徑測試 |
| `recover_and_verify.sh` | 眼鏡 adb 恢復後的一鍵回歸（安裝＋提權＋WIFI_ADB 非破壞性驗證） |
| `root_bridge.sh` / `run_root.sh` / `get_root_now.sh` | bridge helper 佈署／以 root 執行指令／全自動提權 |
| `lock_ota.sh` | 鎖定 OTA（`pm suspend`，跨 reboot 持久） |
| `check_ota.sh` / `ota_api.py` / `ghostlock_patch_check.py` | OTA 版本查詢與 GhostLock 修補判定 |
| `env.sh` | **所有腳本共用的 `ADB`／`SERIAL` 唯一來源**（可用環境變數覆寫） |
| `install_root_keepalive.sh` | 取得 root 後補裝 keep-alive＋OOM 保護（任何已 root 狀態都能執行） |
| `lease.sh` / `test_lease.sh` | 跨工具**共用租約** primitive 與其測試套件（搭配 App 看守、桌面工具；測試不需 root、不觸發 exploit） |
| `proc_util.sh` | 安全殺程序（避免 `pkill -f` 自我匹配／誤殺 `*_desktop` 變體） |
| `test_keeper_lease_guard.sh` | keeper 租約守衛端到端測試（製造 root 失效；判準用日誌，並以 `root_restore_delayed.sh` 免重跑 exploit 復原）|
| `root_restore_delayed.sh` | 延遲復原行程：經 root socket 啟動，時間到把 daemon＋keepalive 帶回來 |
| `buddy_watch.sh` | `/proc/buddyinfo`、`/proc/meminfo` 取樣（mm leak 失敗診斷） |
| `keep_alive.sh` / `watch_root.sh` / `build_root_daemon.sh` / `build_ring_input_daemon.sh` / `setup_direct.sh` / `trigger_root_daemon.cmd` | 輔助／研究用（`setup_direct.sh` 的 DIRECT 路徑會 kernel panic，勿用於正式流程） |

## 提權維運（P0–P3，2026-09-13）

> 背景與投資判斷見 `reports/su_escalation_roi_assessment.md`（實測 84–112 秒取得 root、
> 歷史 143 次統計、失敗分類）。結論是**不改寫 exploit**，改強化維運層。

### P0：root daemon 存活強化

| 機制 | 位置 | 行為 |
|------|------|------|
| root 側 keep-alive | payload `LedPayloads.startRootDaemon()`／`deploy/install_root_keepalive.sh` 安裝 `/data/local/tmp/root_daemon_keepalive.sh` | 20 秒一輪檢查 listener，daemon 掛掉就 `setsid` 重建（**不需 adb、不需重跑 exploit**）；實測 **9 秒**救回 |
| OOM 保護 | 同上 | daemon 與 keep-alive 皆設 `oom_score_adj=-1000`（避免被 lmkd 回收） |
| pid 檔 | `/data/local/tmp/root_daemon.pid`、`root_daemon_keepalive.pid` | 精準鎖定、方便清理 |
| App 側看守 | `RootDaemonKeeper`（30 秒一輪，`App.kt`／`CaptureService` 啟動） | 失效時清 stale socket → 判斷「還有人能觸發提權嗎」→ 自動重建（冷卻 5 分鐘）或回報需介入 |
| **共用租約（避免兩套工具互踩）** | `RootDaemonKeeper`（App 端）＋ `deploy/lease.sh`（deploy 端）＋桌面工具 | 固定契約檔 `/data/local/tmp/ghostlock_op.lease`（內容 `owner=… pid=… ts=…`）。App 看守在**清 stale socket 之前**先檢查租約，取得租約者才動用 exploit，避免桌面板與 App 同時改 `.ghostlock_root.sh`；重新武裝前另有 **TOCTOU 預檢** |
| **helper 變體** | `root_bridge.sh`＝`ghostlock_bridge_helper.sh`；桌面工具＝`ghostlock_bridge_helper_desktop.sh` | ⚠ App 的心跳探針只認前者與 `ghostlock_bridge/helper.alive`；若**只有桌面 helper 在跑**，看守會回報「需重新武裝」（可接受：桌面工具通常同時持有租約） |
| **安全殺程序** | `deploy/proc_util.sh` | 取代 `pkill -f`（會自我匹配、也會誤殺名稱相似的 `*_desktop` 變體） |
| **記憶體取樣** | `deploy/buddy_watch.sh` | 取樣 `/proc/buddyinfo`、`/proc/meminfo`，用於判斷 KernelSnitch mm leak 失敗是否與高階頁枯竭相關 |
| 探針修正 | `AutoGhostlockEngine` | ⚠ **不可用 `ps` 判斷**：Android 的 App 看不到其他 process（實測 helper 在跑卻回 false）。改讀 helper 每秒寫入的 `ghostlock_bridge/helper.alive` 心跳檔；Shizuku 用官方 `Shizuku.pingBinder()` |

### P0-b：失敗可視化

手機「設定 → 拍攝與權限 → root 狀態」顯示狀態列＋提示（點擊看操作指引）：

| 狀態代碼 | 手機文字（zh-TW） |
|----------|------------------|
| `root_ok` | root：可用 |
| `root_rearming` | root：失效，自動重建中… |
| `root_rearmed` | root：已自動重建 |
| `root_lost_need_arm` | root：已失效，需重新武裝 ＋「請執行：deploy/root_bridge.sh 或 deploy/start_shizuku.sh」 |
| `root_lost_cooldown` | root：已失效（冷卻中，稍後自動重試） |

實測（真實重開機後）：`root：已失效，需重新武裝` / `請執行：deploy/root_bridge.sh  或  deploy/start_shizuku.sh` / `看守 2 次 · 自動重建 0 次`。

### P1：執行期引擎探測

`AppGraph` 不再有 `USE_ENGINE` 編譯期常數，改由 `AutoGhostlockEngine` 每次呼叫時探測：
Shizuku 在跑 → Shizuku 引擎（內含 bridge 回退）；否則 bridge helper 心跳新鮮 → bridge 引擎；
都沒有 → 選 Shizuku 引擎並回報 `gl_shizuku_not_running`（提示要跑哪支腳本）。
首選引擎失敗時，若另一條管線可用會自動再試一次。

### P2：提權度量

`AppGraph.acquireRootMetered()` 是所有提權呼叫的唯一入口，記錄
`lastEngine`／`lastOk`／`lastMs`／`attempts`／`successes`／`firstTryOk`（**存 prefs，App 被殺不歸零**；
快速路徑不計入「首發成功」以免美化數字），連同看守計數隨 `STATUS` 回報手機。

### P3：免插線武裝（`persist.adb.tcp.port`）— ✅ **可行（2026-09-20 推翻原結論）**

`WIFI_ADB` 指令設定 `persist.adb.tcp.port=5555`（Android 11+ 官方無線偵錯屬性；
只設 `service.*` 只在本次開機有效）。程式面已實作，ack 會回報 `autoAfterReboot`／`boundOn`。

**原「不可行」結論的錯誤處（2026-09-13 的測法）**：判定「有沒有綁」時只 grep 了
**IPv4** 的 `/proc/net/tcp`（`:15B3`）；本機 adbd 綁的是 **IPv6 wildcard**，
只出現在 `/proc/net/tcp6` → 一律數到 0，於是誤判為「adbd 不理會此屬性」。

**2026-09-20 實測（穩態，未經任何修改）**：

| 檢查 | 結果 |
|------|------|
| `getprop persist.adb.tcp.port` | ✅ `5555`（跨開機保留） |
| `grep -c ':15B3 ' /proc/net/tcp`（IPv4） | ❌ 0（**舊判準只看到這裡**） |
| `grep -c ':15B3 ' /proc/net/tcp6`（IPv6） | ✅ **2**（LISTEN ＋ 連線） |
| `netstat -tlnp` | ✅ `tcp6 [::]:5555 LISTEN` |
| 裝置內 `nc 127.0.0.1 5555` | ✅ `TCP connect OK` |
| **PC 加入 SoftAP → `adb connect 192.168.43.1:5555`** | ✅ `device` |
| **`adb -s 192.168.43.1:5555 shell id`** | ✅ `uid=2000(shell) context=u:r:shell:s0` |

→ **結論：本 build 支援 adb-over-TCP**；免插線重新武裝**已可行**（`adb connect` 後跑
`deploy/root_bridge.sh` 即可，不必再插 USB）。唯一仍有限制的是**新主機的首次授權**：
眼鏡無螢幕可彈窗，需先以 root 把該主機公鑰寫入 `/data/misc/adb/adb_keys`。

⚠ 別再重啟 adbd（會掉 USB 列舉）——`persist.adb.tcp.port` 只需**正常重開機一次**即生效，
本機早已生效，現在就通。

詳見 [`reports/wireless_adb_feasibility_2026-09-20.md`](../reports/wireless_adb_feasibility_2026-09-20.md)。

## 附錄：裝置端探針的交叉編譯（2026-09-13 新增）

原本只有 Windows 版 NDK clang 一條路（`deploy/build_root_daemon.sh`），但沙箱中 `/mnt/c` **唯讀**
→ 無法在那裡放暫存檔，導致「本機無法編譯新探針」。改用 **ziglang**（pip 套件，自帶交叉編譯器）：

```bash
.venv/bin/pip install ziglang          # 一次即可
PUSH=1 SERIAL=$SERIAL bash deploy/build_probe_zig.sh deploy/kgsl_micronode_probe.c kgsl_micronode_probe
```

- 目標：`aarch64-linux-musl` **靜態** ELF（可直接在 Android 執行，實測 `/dev/kgsl-3d0` 可開）。
- ⚠ zig 的快取目錄必須在工作區內（`ZIG_GLOBAL_CACHE_DIR`／`ZIG_LOCAL_CACHE_DIR` → `build-cache/`），
  預設 `~/.cache/zig` 在沙箱中唯讀會失敗（`CacheCheckFailed`）。
- 產物放 `build-cache/`（已列入 `.gitignore` 類的暫存區），原始碼留在 `deploy/*.c`。
