# 傳輸方式標示修正 — 實機截圖證據（2026-09-13 23:03–23:10）

> English: [README_EN.md](README_EN.md)

## 對應報告
`reports/wifi_transfer_fix_2026-09-13.md` Rev.3 §11–§12（FACTS.md §7 P28 的 UI 標示段）。

## 症狀
下載影片時**傳輸很快（確實走 WiFi）**，但畫面上「傳輸：」那行字顯示**藍牙**。

## 根因（競態）
眼鏡端 `WifiServer.accept()` 會關閉藍牙連線 → 手機端只註冊了 `onDisconnected`（沒有 `onConnected`）
→ 刷新恰好落在「WiFi 的 TCP 還沒連上」的瞬間 → 寫入「藍牙」，此後**再也沒有回呼**會更新它。

## 修正
`LinkClient.onConnected` ＋ `Remote.onTransportChanged` 單一通知點（client/tcp × 連上/斷線），
並新增 `Remote.wifiNegotiating` 三態顯示（沿用既有字串 `transport_wifi_connecting`）。

## 截圖（依時間序，全程**未手動切換分頁**）

| 檔案 | 時點 | 畫面文字 | 說明 |
|------|------|----------|------|
| `01_before_bt.png` | 下載前（剛 `WIFI_DISCONNECT`） | 傳輸：藍牙 | 基準狀態 |
| `02_negotiating_wifi.png` | 下載觸發 t+2s | **傳輸：WiFi 連線中…** | 修正後新增的中間態（舊版此時顯示藍牙） |
| `03_wifi_connected.png` | t+11s | **傳輸：WiFi 192.168.43.1:8555** | 自動更新，無需切換分頁 |
| `04_wifi_state_now.png` | 稍後 | 傳輸：WiFi 192.168.43.1:8555 | 現況確認（遙控分頁） |

## 對應 log（節錄）
```
23:04:42 Remote  : 媒體通道：建立 WiFi 中…
23:04:48 Remote  : wifi media: apNet=126 localIp=fe80::9842:2cff:fe7d:f157
23:04:48 Remote  : 媒體通道：WiFi 192.168.43.1:8555
23:04:51 DebugReceiver: TEST_TRANSFER result=Success(...) elapsed=8391ms ... link=wifi
```

## 裝置
- 手機：21091116UG（`<PHONE_SERIAL>`），Android 13 / MIUI，app-phone **1.2**（versionCode 3）
- 眼鏡：RG-glasses（`<DEVICE_SERIAL>`），Android 12，app-glasses **1.0-safe.2**（versionCode 4）
