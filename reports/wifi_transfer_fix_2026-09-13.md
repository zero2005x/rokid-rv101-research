# WiFi 媒體通道無法傳輸 — log 取證與修正（兩個真 bug）

> English: [wifi_transfer_fix_2026-09-13_EN.md](wifi_transfer_fix_2026-09-13_EN.md)

- **日期**：2026-09-13 22:16–22:25
- **症狀**：手機與眼鏡無法透過 WiFi 傳輸
- **log 起點**：手機 `WifiLink: specifier: network available` 之後**就沒有任何動靜**

---

## 0. 先講結論

WiFi 加入 AP 其實**成功**了（手機拿到 `192.168.43.205`、眼鏡 `hostapd` 也回報 `AP-STA-CONNECTED`），
但**兩個獨立的 bug** 讓後續完全走不下去：

| # | Bug | 後果 |
|---|-----|------|
| **1** | `NEARBY_WIFI_DEVICES` **在 Manifest 宣告了、執行期卻從未要求**（`granted=false`） | Android 13 上 `WifiInfo.getSSID()` 只回 `<unknown ssid>` → `isOnAp()` **永遠 false** → 加入 AP 後仍判定失敗（`manual join required`） |
| **2** | TCP socket **沒有綁定到剛加入的 AP 網路** | `WifiNetworkSpecifier` 加入的網路**不是** App 的預設網路 → socket 走行動網路，實測錯誤：`failed to connect to /192.168.43.1 from /10.225.98.247` |

---

## 1. 取證過程（每一段都有 log）

### 1.1 眼鏡端：AP 與服務其實都正常

```
22:14:37 CaptureService: WiFi media server ready (:8555)
22:14:37 WifiServer: WifiServer listening on :8555
22:16:23 wificond: New station 9a:42:2c:7d:f1:57 connected to hotspot using interface wlan2
22:16:23 hostapd : wlan2: STA 9a:42:2c:7d:f1:57 WPA: pairwise key handshake completed (RSN)
$ ip addr show wlan2   → inet 192.168.43.1/24  state UP
```
→ 熱點、密碼交握、伺服器監聽**全部正常**；問題不在眼鏡。

### 1.2 手機端：加入成功，但之後斷掉

```
22:16:23 wpa_supplicant: wlan0: CTRL-EVENT-CONNECTED - Connection to c2:7a:40:cb:3d:cd completed
         TransportInfo: SSID "RokidCam", IP /192.168.43.205, Supplicant state COMPLETED, RSSI -34
22:16:23 WifiLink: specifier: network available          ← 之後就再也沒有 WifiLink/TcpClient 訊息
```
眼鏡端也**沒有** `wifi connection accepted` → 手機從未真正建立 TCP。

### 1.3 修正 bug 1 之後，暴露出 bug 2

```
22:21:55 WifiLink: specifier: network available (localIp=fe80::...)
22:22:03 WIFI_CONNECT result=Failure(tcp connect failed:
         failed to connect to /192.168.43.1 (port 8555) from /10.225.98.247 (port 48082) after 6000ms)
```
`from /10.225.98.247` 是**行動網路**位址 → 證實 socket 沒綁到眼鏡 AP。

---

## 2. 修正

### 2.1 `NEARBY_WIFI_DEVICES` 要在執行期要求（`MainActivity.requestBlePermissions`）

```kotlin
if (Build.VERSION.SDK_INT >= 33) needed += Manifest.permission.NEARBY_WIFI_DEVICES
```
安裝後實測：`android.permission.NEARBY_WIFI_DEVICES: granted=true` ✅

### 2.2 不再只靠 SSID 判斷「是否已在 AP 上」（`WifiLink`）

- 新增 `joinedNetwork`：在 `onAvailable(network)` 記下該 `Network`。
- `isOnAp()` 改為**優先**判斷「這個 Network 的本機 IPv4 是否落在眼鏡 AP 子網（`192.168.43.x`）」
  —— 這個判斷**不需要任何權限**；SSID 比對退為備援。

### 2.3 TCP socket 綁定到該網路（`TcpClient` + `Remote`）

```kotlin
// TcpClient.connect(..., network: Network?)
val s = Socket()
network?.let { runCatching { it.bindSocket(s) } }    // ← 關鍵
s.connect(InetSocketAddress(host, port), timeoutMs)
```
`Remote.ensureWifiMedia()` 傳入 `wifiLink.joinedApNetwork()`。

---

## 3. 修正後實測（端到端成功）

```
22:24:08 WifiLink: specifier: network available (localIp=fe80::...)
22:24:09 Remote  : wifi media: apNet=116 localIp=fe80::...
22:24:09 DebugReceiver: WIFI_CONNECT result=Success(WifiApInfo(up=true, ssid=RokidCam, ip=192.168.43.1, port=8555))

# 眼鏡端
22:24:09 WifiServer: wifi connection accepted from /192.168.43.205:60284
$ netstat -tn | grep 8555 → ::ffff:192.168.43.:8555 … ESTABLISHED

# 實際傳輸
22:24:40 CommandRouter: recv: LIST_ALBUMS id=4 → LIST_MEDIA id=6 → DOWNLOAD_MEDIA id=7
22:24:40 DebugReceiver: TEST_CHAIN albums=[Camera(7)] / media count=7
22:24:40 DebugReceiver: TEST_CHAIN 縮圖=Success(.../thumbs/med-a3fbaa79.jpg)
```

---

## 4. 信心

| 陳述 | 標記 | 信心 | 依據 |
|------|------|------|------|
| 眼鏡端 AP／伺服器正常 | FACT | **Very High** | hostapd／wificond／netstat |
| `NEARBY_WIFI_DEVICES` 未授予導致 SSID 判斷失效 | FACT | **Very High** | `dumpsys package` 顯示 `granted=false`；授權後行為改變 |
| socket 原本走行動網路 | FACT | **Very High** | 錯誤訊息內的來源位址 `10.225.98.247` |
| 修正後 WiFi 傳輸可用 | FACT | **Very High** | `wifi connection accepted` ＋ TCP ESTABLISHED ＋ 縮圖／清單實際傳輸成功 |
| 「按鈕實際下載照片到相簿」的 UI 流程 | 未逐一驗證 | — | 底層通道已通；建議使用者用 UI 再確認一次 |

## 5. 未執行
未變更眼鏡端 AP 設定、未動任何既有配對；只改手機端權限要求與 socket 綁定。

---

# Rev.2（22:50–23:00）— 第三個、也是真正讓「傳輸一直在藍牙」的 bug

- **使用者回報**：「目前照片正在傳輸藍芽中，不是預期的 wifi，傳送一步 10 秒影片非常慢」
- **結論**：WiFi 通道本身沒問題（Rev.1 已修好），問題是**通道選擇與生命週期**：
  眼鏡端兩條通道**互斥**，而手機端**每一道一般指令都會把 WiFi 踢掉**，且**沒有任何媒體操作會自動建立 WiFi**。

## 6. 根因鏈（每一環都有原始碼依據）

| 環節 | 證據 |
|------|------|
| 眼鏡端 BT／WiFi server **互斥**，accept 時都關掉前一條 | `BluetoothServer.kt:43`、`WifiServer.kt:39` 皆為 `graph.connection?.close()` |
| 手機端**所有**一般指令硬走藍牙 | `Remote.request()` 原本直接 `client.request(...)`（`LIST_ALBUMS`／`LIST_MEDIA`／`GET_STATUS` 全在內） |
| 藍牙一斷就自動重連藍牙 | `Remote.ensureConnected()` 只要 `client.connected == false` 就重撥 |
| 媒體通道只在「藍牙也斷」時才會回退 | `requestFile()` 原本 `mediaClient()` → `tcp.connected ? tcp : client` |
| WiFi **不會自動建立** | `ensureWifiMedia()` 的呼叫點只有兩個：`DebugReceiver(WIFI_CONNECT)` 與 `SettingsFragment` 的開關（預設關閉） |

**翻轉迴圈**：WiFi 連上 → 眼鏡關掉藍牙 → 手機端下次 `Remote.status()`（分頁刷新即觸發）→
`ensureConnected()` 重連藍牙 → 眼鏡 `BluetoothServer.accept()` 關掉 WiFi socket →
`mediaClient()` 回退藍牙。實測這個迴圈在數秒內完成，使用者看到的「傳輸：藍牙」就是這個結果。

## 7. 修正內容

| # | 修正 | 檔案 |
|---|------|------|
| 1 | 新增 `Remote.link()`（WiFi 優先、藍牙回退），`request()` 改走它；WiFi 送一般指令失敗時退回藍牙（長時指令不重送，避免重複觸發 LPE） | `Remote.kt` |
| 2 | `ensureConnected()` 在 WiFi 已連線時**不再重撥藍牙** | `Remote.kt` |
| 3 | 新增 `ensureMediaLink()`：媒體操作前若無通道，**自動**請眼鏡開熱點 → 加入 AP → 連 TCP；失敗才回退藍牙（`Mutex` 去重、失敗 60 秒冷卻、自動模式熱點等待上限 30 秒） | `Remote.kt` |
| 4 | `isConnected` 改為 `client.connected \|\| tcp.connected`（WiFi 連上時藍牙本來就會被對端關閉，否則 UI 誤顯示「未連線」） | `Remote.kt` |
| 5 | 設定頁 WiFi 開關改為**持久化偏好** `auto_wifi_media`（**預設開啟**）；關閉時把藍牙控制鏈路接回 | `SettingsFragment.kt`、`Remote.kt` |
| 6 | `WifiLink`：specifier callback **刻意保留註冊**（反註冊會讓系統拆掉該 WiFi 連線），只在發出新加入請求前釋放舊的 | `WifiLink.kt` |
| 7 | `LogStore.onChanged` 一律 post 到主執行緒（自動連線紀錄來自背景執行緒，原本會觸發 `CalledFromWrongThreadException`） | `RemoteFragment.kt` |
| 8 | 設定頁說明文字改為「媒體與控制都優先走 WiFi，藍牙僅在 WiFi 不可用時作為備援」（14 語系同步） | `strings.xml`、`scripts/i18n/` |

## 8. 實測（同一支 10.6 秒影片、同一台裝置、同一晚）

樣本：`VID_1789308941304.mp4`，**16,736,320 bytes（16.0 MiB）、時長 10,562 ms**（`mediaId=med-f593c229`）。
量測方式：`TEST_TRANSFER` DebugReceiver 指令（新增，會印出實際傳輸通道、耗時與吞吐）。

| 通道 | 耗時 | 吞吐 | 來源 |
|------|------|------|------|
| **WiFi（修正後自動建立）** | **3,786 ms** | **4,420.6 KB/s（≈4.3 MB/s）** | `TEST_TRANSFER ... elapsed=3786ms throughput=4420.6KB/s link=wifi` |
| 藍牙（強制 `SET_AUTO_WIFI false` + `WIFI_DISCONNECT`） | **323,956 ms（5 分 24 秒）→ 逾時失敗** | **51.7 KB/s** | `TEST_TRANSFER ... result=Failure(TimeoutCancellationException: Timed out waiting for 323938 ms) elapsed=323956ms throughput=51.7KB/s link=bt`；`.part` 取樣亦吻合（22:54:04→5,505,024；22:54:24→6,553,600；22:54:45→7,340,032） |

→ **同一支影片 WiFi 約快 85 倍**（3.8 秒 vs 324 秒），與使用者「非常慢」的感受一致。

**附帶發現（藍牙回退路徑本身的 bug）**：`transferTimeout()` 原本以 **60 KB/s** 估算，
但實測藍牙只有 **51.7 KB/s** → 這個 16.7 MB 檔**剛好逾時失敗**（`.part` 顯示位元組其實已全部送達，
是 `TRANSFER_END` 晚了 18 ms 才到）。已把估算速率下修為 **45 KB/s**，讓回退路徑至少真的能完成。

### 8.1 自動建立 WiFi 的實測序列（未手動開開關、未手動加入 AP）

```
22:50:26 DebugReceiver: CONNECT <DEVICE_ALIAS> result=Success
22:50:29 Remote  : 媒體通道：建立 WiFi 中…            ← 媒體操作自動觸發（完全沒按設定頁開關）
22:50:31 WifiLink: specifier: network available
22:50:33 Remote  : wifi media: apNet=120
22:50:33 Remote  : 媒體通道：WiFi 192.168.43.1:8555    ← 4 秒完成，且未跳系統加入視窗（手機已在 AP 上）
22:51:11 TEST_TRANSFER ... link=wifi → elapsed=3786ms
```

### 8.2 最終驗證（**出貨 APK `1.2`**，23:00–23:02，同一支影片）

| 情況 | 耗時 | 吞吐 | 備註 |
|------|------|------|------|
| 冷啟動後第一次傳輸 | 16,532 ms | 1,012 KB/s | 其中約 **11 秒是重新加入 AP**（前一個測試才剛 `WIFI_DISCONNECT`） |
| **同一 session 第二次傳輸** | **2,990 ms** | **5,597 KB/s（5.5 MB/s）** | log 起點即 `link=wifi`，且 `DebugReceiver: connected=false`（**藍牙是斷的**） |

```
23:01:25 DebugReceiver: TEST_TRANSFER ... link=wifi          ← 起點就在 WiFi
23:01:28 DebugReceiver: TEST_TRANSFER result=Success elapsed=2990ms throughput=5597.4KB/s link=wifi
```

第二筆同時證明了 **§7 第 1 項修正有效**：藍牙完全沒連線（`connected=false`）時，
`LIST_ALBUMS`／`LIST_MEDIA` 等一般指令**照樣送得出去**（走 WiFi），且傳輸 3 秒完成。

> 冷啟動 11 秒的「加入 AP」成本只在每次重新協商時付一次；同一個 session 之後都是 3 秒級。

## 9. 信心

| 陳述 | 標記 | 信心 | 依據 |
|------|------|------|------|
| 眼鏡端兩通道互斥（後連上者勝） | FACT | **Very High** | 兩處原始碼 `graph.connection?.close()` |
| 一般指令走藍牙會殺掉 WiFi socket | FACT | **Very High** | 上述原始碼 ＋ 修正前手機 log 的翻轉行為 |
| 媒體操作原本不會自動建立 WiFi | FACT | **Very High** | `ensureWifiMedia()` 全repo 僅 2 個呼叫點 |
| 修正後媒體自動走 WiFi | FACT | **Very High** | §8.1 序列 ＋ `link=wifi` |
| WiFi 比藍牙快約 85 倍（3.8s vs 324s） | FACT | **Very High** | 兩者皆為 `TEST_TRANSFER` 自報 `elapsed`／`throughput`，並與 `.part` 取樣一致 |
| UI（相簿手勢下載）走同一條路徑 | 推論 | High | 兩者都經 `Remote.downloadMedia()` → `requestFile()` |

## 10. 未執行／未驗證
- 未改動眼鏡端任何程式（本次全部為手機端）。
- 未實測「使用者拒絕加入 AP」時的回退體驗（冷卻 60 秒後才會再試）。
- 舊的 `.part` 殘檔（先前中斷的傳輸，含本次藍牙逾時那個）仍在裝置上，未清理。
- 未以相簿 UI 手勢重跑一次（UI 與 `TEST_TRANSFER` 共用 `Remote.downloadMedia()` → `requestFile()` 同一條路徑）。

---

# Rev.3（23:03–23:10）— 「下載很快、文字卻寫藍牙」＝UI 標示過期（已修）＋ 附帶抓到索引資料遺失

## 11. 症狀與真因

使用者：「下載影片時雖然很快，但文字顯示是用藍牙傳輸」。

**傳輸本身沒問題（確實是 WiFi），是「傳輸方式」那行字沒有跟著更新。** 機制是一個**競態**：

1. 眼鏡端在 `WifiServer.accept()` 時會關閉藍牙連線 → 手機端 `client.onDisconnected` 觸發。
2. `RemoteFragment` **只**註冊了 `onDisconnected`（沒有 `onConnected`）→ 就在這一刻呼叫 `refresh()`。
3. 此時 WiFi 的 TCP 才正要連上（`tcp.connected` 仍為 false）⇒ 畫面把「傳輸：**藍牙**」寫進去。
4. WiFi 連上後**沒有任何回呼**會再刷新 ⇒ 這行字就一直是「藍牙」，而傳輸早已跑在 WiFi 上（所以很快）。

## 12. 修正

| # | 修正 | 檔案 |
|---|------|------|
| 1 | `LinkClient` 新增 `onConnected`（於 `attachStreams()` 觸發） | `net/LinkClient.kt` |
| 2 | `Remote` 新增 `onTransportChanged` 單一通知點，並在 `ensureInit()` **集中**註冊四條回呼（client/tcp × 連上/斷線）；`RemoteFragment` 不再自行覆寫 `onDisconnected` | `Remote.kt`、`ui/RemoteFragment.kt` |
| 3 | 新增 `Remote.wifiNegotiating`：協商中的幾秒顯示 **「傳輸：WiFi 連線中…」**（沿用既有字串 `transport_wifi_connecting`），不再誤標藍牙 | `Remote.kt`、`ui/RemoteFragment.kt`、`ui/SettingsFragment.kt` |
| 4 | `MainActivity` 接收 `Remote.onTransportChanged` → 主執行緒 `refreshAllFragments()`，並把**設定分頁**也納入刷新（原本只刷新遙控／相簿／本機） | `MainActivity.kt` |

### 12.1 實機驗證（三張截圖，皆未手動切換分頁）

| 時點 | 畫面文字 | 截圖 |
|------|----------|------|
| 下載前（剛 `WIFI_DISCONNECT`） | 傳輸：藍牙 | `reports/ui_transport_2026-09-13/01_before_bt.png` |
| 下載觸發、協商中（t+2s） | **傳輸：WiFi 連線中…** | `reports/ui_transport_2026-09-13/02_negotiating_wifi.png` |
| 連上後（t+11s） | **傳輸：WiFi 192.168.43.1:8555** | `reports/ui_transport_2026-09-13/03_wifi_connected.png` |

log 對應：`23:04:42 媒體通道：建立 WiFi 中…` → `23:04:48 wifi media: apNet=126` → `23:04:48 媒體通道：WiFi 192.168.43.1:8555` → 下載 `link=wifi`。

> 截圖已歸位至 `reports/ui_transport_2026-09-13/`（含 README 說明）；A/B 索引樣本在 `reports/media_index_repair_2026-09-13/`。

## 13. 附帶發現：眼鏡端媒體索引遺失 6 筆（已修，並已自我修復回 6 筆）

追查「相簿內只剩 1 筆」時發現 **眼鏡端 `media/index.json` 在 23:04 被重寫成只剩 1 筆**，
但相簿目錄裡的檔案都還在：

```
23:04 index.json: {"albums":[{"id":"alb-3b6dfe09",...}],"media":[僅 IMG_1789311878598.jpg 一筆]}
手機 LIST_MEDIA_INFO: total=7（22:51） → total=1（23:05）
磁碟: 7 個檔案仍在
```

**為什麼會這樣**：`index.json` 是**單一 JSON 全量覆寫**，且有兩個脆弱點——
①`load()` 的解析迴圈**共用一個 try/catch**，任何一筆 `MediaItem.fromJson` 丟例外，
   外層 catch 一吞，**後面全部不載入**（此例是 albums 有載到、media 全空）；
②`persist()` 用 `File.writeText()`（先 truncate 再寫），程序若在中間被殺（LMK／force-stop／安裝 APK）
   會留下空檔或半截檔。

**修正**（`MediaRepository.kt`）：
| # | 修正 |
|---|------|
| 1 | 解析改為**逐筆 try/catch**：單一壞項目只跳過自己，不再拖垮全部 |
| 2 | 新增 **`reconcileWithDisk()`**：載入後掃描每個相簿目錄，把「在磁碟上但不在索引裡」的檔案重新登記（索引不再是唯一真相） |
| 3 | `persist()` 改為**原子寫入**：先寫 `index.json.tmp` 再 `renameTo()` |

**實機驗證**（安裝新版後重啟眼鏡 App）：
```
23:07:20 W MediaRepository: index reconciled: 6 file(s) re-registered from disk
手機 LIST_MEDIA_INFO: total=8（含 636MB／664MB 兩支長影片）   ← 清單已恢復
```

## 14. 尚未解釋：3 個檔案在 22:58–23:05 之間從磁碟消失

`VID_1789308941304.mp4`（16.7 MB，本次基準測試用的那支）、`IMG_1789308934884.jpg`、
`IMG_1789308959814.jpg` 在 22:58 還在（藍牙基準測試正在寫它的 `.part`），23:05 已不在磁碟上。

- **可排除 App 的刪除路徑**：`DELETE_MEDIA` → `MediaRepository.deleteMedia()` 會**刪檔＋同步 persist 索引**，
  若走這條路，索引不會變成「只剩 1 筆」。
- **可排除眼鏡端的自動清理**：全 repo 只有 `deleteMedia()`、`deleteAlbum()` 與 `CameraController` 的
  「錄影中止丟棄當次殘檔」會刪檔，沒有掃描刪除未知檔案的邏輯。
- logcat 緩衝已輪替，無 `DELETE_MEDIA` 紀錄可查 ⇒ **成因未定**（可能是使用者在其他 UI／工具刪除）。

⚠ 兩支超長影片 `VID_1789290250579.mp4`（**636 MB**）與 `VID_1789304656692.mp4`（**664 MB**）仍在眼鏡上
（合計約 1.3 GB；後者索引中的 `durationMs=0`，研判是先前中斷的錄影）。若要清理請從 App 內刪除，才會同步索引。

---

# Rev.4（23:14–23:20）— 回答「還有什麼異常嗎」：索引問題比原先認定的更早、更嚴重

## 15. 使用者確認：消失的檔案是自己刪的 ⇒ 排除外力

但重新對帳後發現**另一個、更早的異常**：

| 時點 | 磁碟上的 App 拍攝檔 | App 索引筆數 | 差異 |
|------|--------------------|-------------|------|
| 22:45（Rev.2 測試期間） | **13** | 7（22:51 `LIST_MEDIA_INFO total=7`） | **6 筆在磁碟上、但 App 完全看不到**（無法瀏覽也無法下載） |
| 23:04 | 7（使用者刪掉 7 個） | 1 | 索引整批遺失 |
| 23:18（修正後） | 9 | 9 | ✅ 一致 |

也就是說：**索引與磁碟不一致並不是 23:04 才發生，至少 22:45 就已經有 6 筆「幽靈檔案」**
（`VID_1788822607406`、`VID_1788822710658`、`VID_1789152641290`、`VID_1789290250579`、
`VID_1789304656692`、`CameraXeb9697ea-…`），而它們正好就是我修正後 `reconcileWithDisk()`
救回來的那 6 筆。

## 16. A/B 實驗：證明「一筆壞掉 → 其餘整批無聲消失」

把索引中**第 4 筆**的必填欄位 `fileName` 移除（`MediaItem.fromJson` 用 `getString`，會丟 `JSONException`），
其餘 8 筆保持完好，然後分別用兩版 APK 啟動：

| 版本 | 啟動後 log | 手機看到的清單 |
|------|-----------|---------------|
| **舊版 `1.0-safe`（code 3）** | `E MediaRepository: index load failed` ＋ 完整 stack trace（被外層 catch 吞掉） | **total=4** ← 第 4 筆之後的 **5 筆全部消失** |
| **新版 `1.0-safe.2`（code 4）** | `E media entry #4 skipped: No value for fileName`<br>`W index reconciled: 1 file(s) re-registered from disk` | **total=9** ← 一筆都沒少 |

**這就是幽靈檔案的成因**：`load()` 的解析迴圈共用一個 try/catch，任何一筆丟例外，
**其後所有項目都不載入**；而任何一次後續的 `persist()`（拍照、錄影、產生縮圖都會觸發）
就把這個「已經變短」的清單**永久寫回**，磁碟上的檔案從此在 App 裡不存在。

## 17. 其他異常（已確認，非程式錯誤）

1. **3 支無法播放的影片，合計約 1.4 GB**（錄影被中斷 → MP4 沒有寫入 `moov` atom）：

   | 檔案 | 大小 | 索引 durationMs | 尾端 moov |
   |------|------|----------------|-----------|
   | `VID_1789290250579.mp4` | 636.9 MB | **0** | **無** |
   | `VID_1789304656692.mp4` | 664.7 MB | **0** | **無** |
   | `VID_1789152641290.mp4` | 111.9 MB | **0** | **無** |
   | `VID_1788822710658.mp4`（對照） | 25.0 MB | 15139 | 有 |

   `durationMs=0` 與「尾端無 `moov`」一致 ⇒ 這三段錄影在收尾前就被中斷
   （裝置重開機／App 被殺），**檔案不完整、無法播放**，建議直接刪除。

2. **手機端 5 個 `.part` 殘骸共 63 MB**：傳輸是「先寫 `.part`、完成才 rename」，
   任何失敗（含藍牙逾時那次）都會留下完整大小的殘骸。已手動清除，並在 App 加入
   `LocalStore.cleanupStaleParts()`（App 啟動時清掉 >24 小時的 `.part`；
   單筆傳輸逾時上限約 4 小時，故不會誤刪進行中的檔案）。

3. 眼鏡上 636 MB ＋ 664 MB 兩支無用檔案佔 1.3 GB，建議一併刪除。

---

# Rev.5（23:29–23:30）— 依指示刪除 3 支未收尾影片（走 App 路徑，索引同步）

- **授權**：使用者明確指示刪除下列 3 支；**對照組 `VID_1788822710658.mp4` 保留**。
- **方式**：**不使用 `rm`**，改走 App 的刪除路徑（`DELETE_MEDIA` → `MediaRepository.deleteMedia()`），
  因為該路徑會**同時刪檔案、刪縮圖、並 persist 索引**；若用 `rm` 會留下 3 筆指向不存在檔案的懸空索引。

| 檔案 | 大小 | mediaId | 結果 |
|------|------|---------|------|
| `VID_1789290250579.mp4` | 636,873,536 B | `med-eba0d12e` | ✅ 已刪 |
| `VID_1789304656692.mp4` | 664,663,857 B | `med-0e50b012` | ✅ 已刪 |
| `VID_1789152641290.mp4` | 111,906,896 B | `med-a918b5ef` | ✅ 已刪 |
| `VID_1788822710658.mp4`（對照） | 25,043,832 B | `med-86d1521c` | ⬜ **保留** |

**釋放空間：1,348.0 MB（約 1.3 GB）**

## 18. 刪除後驗證

```
眼鏡磁碟：6 個檔案（原 9）
索引    ：6 筆（與磁碟一致）
手機清單：LIST_MEDIA_INFO total=6
DELETE_MEDIA 回報：Success（3/3）
對照組 VID_1788822710658.mp4 仍在
重啟眼鏡 App：無任何 reconcile 訊息 ⇒ 磁碟與索引一致（reconcile 只在真的不一致時才會動作）
儲存空間：/storage/emulated 19G，已用 7.3G，可用 11G
```

剩餘 6 筆：`CameraXeb9697ea-….jpg`、`IMG_1789311878598.jpg`、`IMG_1789312579881.jpg`、
`VID_1788822607406.mp4`、`VID_1788822710658.mp4`、`VID_1789311924352.mp4`。

---

# Rev.6（2026-09-13 23:4x）— 對抗式驗證後補強（3 個真缺口，已修並重建）

以獨立 agent 對本報告 §6–§18 與 FACTS P27–P30 **逐條試圖否證**，結果：8 項主張中 5 項成立、
2 項「部分成立」、1 項成立但有附帶條件。以下是**被否證或不足之處**與補強。

## 19. 被驗證抓到的真缺口（皆已修）

| # | 缺口 | 為什麼是真問題 | 修正 |
|---|------|---------------|------|
| **1** | `Remote.link()` 是**死碼**：`request()` 自行 inline 判斷 `tcp.connected`，唯一呼叫者 `mediaClient()` 又零呼叫 ⇒ 報告/FACTS 寫的「`request()` 改走 `link()`」**與程式不符** | 文件與程式不一致（本專案最在意的那類問題） | `request()` 改為 `if (link() === tcp)`；**刪除死碼 `mediaClient()`** |
| **2** | **`tcp.onPush` 從未註冊**：`ensureInit()` 只掛了四條連線回呼，`onPush` 只設在 `Remote.client`；而眼鏡 `AppGraph.push()` 是走**當前連線**推送 ⇒ **WiFi 成為當前通道時，`PROGRESS`（LPE 進度）等推播全部被丟棄** | 這是把控制改走 WiFi 之後**新引入的功能回歸**（不是舊 bug） | 新增 `Remote.onPush` 集中屬性，setter 內**同時**掛到 `client` 與 `tcp`；`RemoteFragment` 改設 `Remote.onPush` |
| **3** | **索引解析只保護了 `media` 陣列**：`albums` 迴圈仍共用外層 try，`Album.fromJson` 用 `getString("id"/"name")` 同樣會丟例外 ⇒ **一筆壞 album 會讓所有 album 與整個 media 陣列都不載入** | §16 的 A/B 只測了 media 陣列，修正範圍不足即宣稱「不再拖垮全部」 | `albums` 迴圈也改逐筆 try/catch（`album entry #i skipped`）；並補測試 |

## 20. 另外三項一併補強（驗證報告的附帶發現）

| # | 問題 | 修正 |
|---|------|------|
| 4 | **顯式斷線不通知 UI**：`LinkClient.disconnect()` 不呼叫 `onDisconnected`（只有 readLoop 的 finally 會）⇒ 按「斷線」或關閉 WiFi 通道時，其他分頁可能停在舊的傳輸方式 | `disconnect()` 在確實由連線狀態轉為斷線時呼叫 `onDisconnected`（先記 `wasConnected`，避免重複通知） |
| 5 | **幽靈項目永遠不會消失**：`reconcileWithDisk()` 只增不減 ⇒ 被 App 以外方式刪掉的檔案，清單仍顯示、每次 persist 又寫回；且 `getThumbnail()` 會**直接回舊快取縮圖**（不檢查來源檔）⇒ 刪檔後仍看得到圖 | `reconcileWithDisk()` 加入**反向校正**（索引有、磁碟沒有 → 移除該筆並刪縮圖，log `-$dropped`）；`getThumbnail()` 先檢查來源檔存在才回快取 |
| 6 | `lastAutoWifiFailAt` 非 `@Volatile`，卻在 `closeWifiMedia()`（鎖外）寫、鎖內讀 | 加 `@Volatile` |

## 21. 驗證報告中「不需修改、但要如實記錄」的附帶條件

1. **兩條通道的互斥不是原子操作**：`graph.connection` 雖是 `@Volatile`，但 accept 端是
   check-then-act（讀舊 handler → close → 裝新 handler），兩條 coroutine 同時進入時理論上可能兩條都活著
   （`connection` 只指向最後一個）。窗口極小，**未實測到**；本報告與 FACTS 的「互斥」應理解為
   「正常序列下互斥」，非執行緒安全保證。
2. **`WifiServer.stop()` 不會關 `graph.connection`**，而 `BluetoothServer.stop()` 會（不對稱）。
   未觀察到實際後果，記錄待查。
3. **`ensureWifiMedia()` 的手動呼叫路徑**（`SettingsFragment`、`DebugReceiver`）**不取 `wifiEnsureLock`、不受冷卻限制**
   ⇒ 手動與自動協商可並行。自動路徑本身的去重是有效的。
4. **「失敗才回退藍牙」的精確說法**是「沒拿到 TCP 就回藍牙」——冷卻中或 `autoWifi=false` 時也會直接走藍牙。
5. **`.part` 清理的界線**：只看 mtime，且 `.part` 每個 chunk 都會更新 mtime ⇒ 進行中的傳輸不會被誤刪；
   要誤刪需「連續 24 小時零寫入且 withTimeout 未到期」，由公式反推需 `size > 3.62 GiB`
   （目前最大檔 664 MB，且其 timeout 為 4.115 h）。因此 §17-2「單筆逾時上限約 4 小時」應更正為
   **「以目前最大檔計約 4.1 小時；公式本身無上限」**。
6. **「三態顯示」只適用於遙控分頁**；設定頁是**四態**（多一個「自動模式待命：媒體與控制都優先走 WiFi…」提示）。
   本報告與 FACTS 的「三態」字樣以此為準。

## 22. 重建與驗證

- 眼鏡 `1.0-safe.3`（versionCode 5）、手機 `1.3`（versionCode 4）。
- 驗證重點：①壞 album 不再拖垮 media；②WiFi 為當前通道時 `PROGRESS` 仍送達手機；
  ③按「斷線」後畫面立即更新；④外部刪檔後清單自動移除該筆（`-$dropped`）。

## 23. Rev.6 的安裝與驗證狀態（如實記錄）

| 項目 | 狀態 |
|------|------|
| 眼鏡 `1.0-safe.3`（code 5） | ✅ 已建置並複製到 `dist/`；當晚未能安裝（眼鏡離線）。**2026-09-16 07:51 已補安裝並完成 A–E 全部驗證**，見 `reports/index_resilience_verification_2026-09-16.md` |
| 手機 `1.3`（code 4） | ✅ 已建置、已安裝、啟動無崩潰（`versionName=1.3`、process 存活） |
| 執行期驗證（壞 album 容錯／WiFi 下推播／斷線通知／外部刪檔自動移除） | ⏳ **待眼鏡連線恢復後補做**（見 §24 待辦） |

> 本節存在的目的：避免把「已建置」誤記為「已驗證」。上表未列為 ✅ 的項目即尚未在裝置上證實。
>
> ⚠ **離線原因更正（2026-09-16）**：當時推測為 adb／USB 連線問題，實際是**眼鏡自行關機**——
> `ro.boot.bootreason=shutdown,leg_fold_timeout`（折疊／未配戴逾時的自動關機）、`uptime` 僅 9 分鐘（重新插電後才開機）。
> 關機導致 root 消失（SELinux 回復 `Enforcing`），因此補跑時 **SoftAP 無法啟動**（`WifiHotspot: root daemon 未就緒`），
> D 項改以 **adb tunnel** 造出真實 TCP 通道完成驗證（見補跑報告 §2.2）。

## 24. 待辦（眼鏡恢復連線後）— **已於 2026-09-16 全部完成**

> **已備妥自動化**：`scripts/verify_index_resilience.sh`（涵蓋下列 1–5 項，只動自建測試檔 `TEST_reconcile.jpg`
> 並在每次改動前備份索引，跑完還原）。另為此新增兩個測試 hook：
> 眼鏡端 `PUSH_TEST` 除錯指令（不必真的跑 LPE 就能驗證 WiFi 下的推播），
> 手機端收到 push 時寫 logcat `RemotePush: progress code=… arg=…`（可自動判定）。

1. 安裝 `dist/app-glasses-v1.0-safe.3.apk`，確認 `versionName=1.0-safe.3`。
2. **壞 album 容錯**：把索引 `albums[0]` 的必填 `name` 移除 → 應見 `album entry #0 skipped`，
   且 **media 仍全部載入**（舊版會 album 與 media 全滅）。
3. **反向校正**：在相簿目錄放一個測試檔（如 `TEST_reconcile.jpg`）→ 啟動應見 `+1 re-registered`；
   再從 App 外刪除該檔 → 下次啟動應見 `-1 removed (file missing on disk)`，且手機清單同步少一筆。
4. **WiFi 下的推播**：WiFi 為當前通道時觸發一次 LPE 進度（`ACQUIRE_ROOT`），手機操作紀錄應出現進度訊息
   （修正前會完全沒有）。
5. **斷線通知**：按「斷線」後，遙控分頁的傳輸方式應立即更新，不需手動切換分頁。
