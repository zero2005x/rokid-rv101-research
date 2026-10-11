# WiFi 傳輸功能 + 空白畫面修復 實機測試報告
> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


> 日期：2026-09-09 · 設備：Rokid RG-glasses（RV101，fw 1.22.009，root 已取得）+ Redmi <PHONE_SERIAL>
> 測試方式：WSL2 → Windows adb.exe；眼鏡 root 指令經 root_daemon socket

## 0. 總覽

| # | 測試項 | 結果 |
|---|--------|------|
| 1 | **手機 App 開啟空白畫面修復**（shownTag 初始化 bug） | ✅ PASS（首開即完整渲染，UI dump 實證） |
| 2 | 眼鏡 WiFi TCP server（:8555，複用 ConnectionHandler/CommandRouter） | ✅ PASS |
| 3 | 眼鏡 SoftAP（root 觸發 `cmd wifi start-softap`，RokidCam/192.168.43.1） | ✅ PASS（含 status 偵測修復） |
| 4 | 協定擴充（ENSURE_WIFI_AP / WIFI_AP_STATUS / DEVICE_INFO.wifiAp） | ✅ PASS（BT 實測回傳） |
| 5 | **WiFi 媒體傳輸 E2E**（TcpClient → 縮圖 → 照片本體） | ✅ PASS（LAN 直連實測；縮圖 1.2s、照片 5.4MB 完成） |
| 6 | **藍牙自動回退**（WiFi 斷開後媒體走 RFCOMM） | ✅ PASS（5.4MB 約 85s ≈ 63KB/s，符合預期） |
| 7 | 手機加入 AP（WifiNetworkSpecifier 系統彈窗） | ⚠️ 已到「系統彈窗等待確認」步驟（Android 強制使用者點按，無法自動化；逾時正確回傳手動加入指引） |
| 8 | Lint + 建置（兩模組） | ✅ PASS（全綠） |

## 1. 空白畫面 bug（修復）
- 根因：`MainActivity.shownTag` 初始值 `"remote"` 與 `show()` 的
  `if (tag == shownTag) return` 短路 → `onCreate` 首次 `show("remote")` 未加入任何 Fragment。
- 修復：初始值改 `""`（附註解）。
- 驗證：`uiautomator dump` 首開即見 連線卡/「傳輸：藍牙」/「啟用 WiFi 傳輸」/拍攝卡 全部渲染。

## 2. 實作架構
- 眼鏡：`net/WifiServer.kt`（TCP 8555 → ConnectionHandler，與藍牙同協定）、
  `net/WifiHotspot.kt`（root 經 root_daemon 執行 `cmd wifi start-softap`，背景化送出；
  `NetworkInterface.getByName("wlan2")` 偵測 AP 狀態）；CaptureService 啟動 WifiServer、
  AUTO 提權成功後自動開熱點；CommandRouter 新增 `ENSURE_WIFI_AP` 處理與
  `DEVICE_INFO.wifiAp` 欄位。
- 手機：`net/LinkClient.kt`（自 BluetoothClient 抽出的傳輸無關基類：request/response +
  二進位檔傳輸）、`BluetoothClient`（RFCOMM 子類）、`net/TcpClient.kt`（TCP 子類）、
  `net/WifiLink.kt`（WifiNetworkSpecifier 加入 AP + SSID 偵測）、`Remote.mediaClient()`
  （WiFi 優先、藍牙回退）、RemoteFragment 新增傳輸狀態列 + 「啟用 WiFi 傳輸」按鈕、
  DebugReceiver 新增 `WIFI_CONNECT`/`WIFI_DISCONNECT`。
- shared：`Protocol.kt`（WIFI_PORT=8555 / AP_SSID=RokidCam / AP_PASS / AP_IP）、
  `WifiApInfo.kt`、MsgType 兩個新值。

## 3. 實測發現並修復的問題（5 個）
1. `WifiHotspot.status()` 的 shell 管線 `awk '{print \\$2}'` 轉義錯誤（awk 收到 `\$2`
   語法錯誤）→ 改用純 Java `NetworkInterface`，零 shell 依賴。
2. `cmd wifi start-softap` 在 wifi 服務轉換期可能長時間阻塞 root socket → 改**背景化送出** +
   `RootDaemonClient.exec` 加 30s 逾時。
3. AP 啟動時間不定（5~90 秒）→ 眼鏡端「觸發即回報」、手機端**輪詢 ENSURE_WIFI_AP 直到
   `wifiAp.up`**。
4. WifiNetworkSpecifier 需要 `CHANGE_NETWORK_STATE` 權限（Android 12+）→ Manifest 補上
   （另有 COARSE_LOCATION 以過 lint）。
5. `suspendCancellableCoroutine` 重複註冊取消 handler 拋例外 → 合併為單一 handler。

## 4. 已知限制
- 本裝置韌體 **STA 與 SoftAP 互斥**：開熱點會踢掉既有 WiFi 連線（關熱點後 STA 自動恢復）。
- 加入 AP 需**一次系統彈窗確認**（Android 安全設計，任何 App 無法跳過）；未確認時 App
  會給出「手動加入 WiFi RokidCam 密碼 rokid1234」的指引，加入後重按按鈕即完成。
- 熱點與 root 同生命週期（reboot 後需重新提權 + 自動重開熱點）。

## 5. 交付物
- 程式碼：app-glasses（WifiServer/WifiHotspot/CaptureService/CommandRouter/AppGraph/
  RootDaemonClient）、app-phone（LinkClient/TcpClient/WifiLink/Remote/RemoteFragment/
  DebugReceiver/MainActivity bug 修復/Manifest/layout/strings）、shared（Protocol/WifiApInfo）
- 文件：`docs/protocol.md`（雙通道說明 + 新訊息）、`docs/deploy.md` §9.4（WiFi 媒體通道使用）
- APK：`app-glasses-debug.apk`、`app-phone-debug.apk`（均已安裝於裝置）
