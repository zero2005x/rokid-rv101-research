# 實際抓包驗證報告（Rokid 眼鏡 tcpdump 實測）

- 設備：Rokid RG-glasses `<DEVICE_SERIAL>`，root（uid=0 / SELinux Permissive）
- 網路：眼鏡接上公開 WiFi「iTaiwan」（無密碼、經 captive portal 點「我同意&免費上網」後上線）
- 工具：裝置內建 `/system/bin/tcpdump 4.9.2`（root 執行）+ 主機 tshark 分析
- 時間：2026-09-05 12:29–12:49

---

## 1. 上線流程（iTaiwan captive portal）

眼鏡原本 `wlan0 DOWN`。`svc wifi enable` 後自動連上已記憶的 `iTaiwan`（BSSID `ec:ad:e0:bf:76:c0`，`Security type 0` 開放網路）。

連上後 `NetworkCapabilities` 帶 `CAPTIVE_PORTAL`，HTTP 一律被 302 重導：

```
GET http://www.example.com  →  302 Location: http://10.10.10.10
GET http://10.10.10.10      →  302 Location: https://wlangw.hinet.net/v2_0/itaiwan_vsr_wlanac/auth_portal.php?...
   → JS redirect → https://auth.itaiwan.gov.tw/auth_page.php?...
   → 302 → /noauth_page.php   （iTaiwan 認證網頁）
   form action = POST https://wlangw.hinet.net/v2_0/itaiwan_vsr_auth/auth_page.php
     hidden: username = ac86d155d6f8@itw   password = myegov
   button: 「我同意&免費上網」
POST 後 → "AUTH SUCCESS" → 連線測試 http://connectivitycheck.gstatic.com/generate_204 回 204 → 上線完成
```

> 備註：iTaiwan 條款載明「登入即視為同意蒐集設備 IP/MAC、AP 地點、使用時間、瀏覽器」——這是 **iTaiwan/Hinet 場域** 的蒐集，非 Rokid。

---

## 2. 抓包結果（三次 tcpdump，視窗合計 800 秒；**封包實際跨度約 12 分鐘**（166s＋300s＋251s））

### Capture 1（200s，觸發 OTA 檢查廣播 `JUDGE_BEFORE_CHECK`）

DNS 查詢與 TLS SNI：

| 時間 | 動作 | 目的端點 | IP |
|------|------|----------|-----|
| 12:31:08–10 | 我方 curl 驗證 | ota.rokid.com / datagather.rokid.com / rcs.rokid.com | 見下 |
| 12:32:32.5 | **OTA 版本檢查**（裝置） | `ota.rokid.com` | 116.62.24.158 |
| 12:32:32.9–33.3 | **韌體下載**（裝置） | `ota-g.rokidcdn.com` | 192.169.122.148 |

TCP 流量統計：

| 連線 | 位元組 | 說明 |
|------|--------|------|
| 眼鏡 ↔ `192.169.122.148:443`（ota-g.rokidcdn.com） | **69 MB 下載** | 韌體 `RVE01-ota-1.25.012-20260901-150201.zip`（~1GB，我中途停止） |
| 眼鏡 ↔ `116.62.24.158:443`（ota.rokid.com） | ~9 KB | 版本檢查 API |
| 眼鏡 ↔ `121.40.237.216:443`（rcs.rokid.com） | ~8 KB | 我方 curl |

### Capture 2（300s，OTA 下載持續）

- `ota-g.rokidcdn.com` 下載 **264 MB**（占滿整段 300s 視窗）。
- 另僅一條 `ota.rokid.com` 檢查連線（156 bytes）。

### Capture 3（300s，已停止 OTA，開 3 個 App 各 3 次）

- **0 個自主封包**。整個視窗內裝置本身 **沒有任何對外連線**（開 App 也一樣）。
- 僅在我方手動 `curl https://ota.rokid.com` 時才出現 1 條連線（證明抓包正常、且裝置閒置即無流量）。

---

## 3. 端點 IP 歸屬（whois）

| 端點 | IP | 歸屬 |
|------|-----|------|
| ota.rokid.com | 116.62.24.158 | **Aliyun（阿里巴巴雲），杭州，中國** |
| datagather.rokid.com | 116.62.24.158 | Aliyun，杭州（與 ota 同 IP） |
| rcs.rokid.com | 121.40.237.216 | **Aliyun，杭州，中國** |
| ota-g.rokidcdn.com | 192.169.122.148 | **Zenlayer**（全球 CDN，Rokid 韌體 CDN） |

即：Rokid 官方後端（OTA 檢查、埋點 datagather、AI rcs）都架在 **阿里雲杭州**；韌體下載走 **Zenlayer CDN**。

---

## 4. 驗證結論

1. **眼鏡「直接走 WiFi」對外傳輸極少**：實測約 12 分鐘（封包跨度；視窗 800 秒），眼鏡自主連外的**只有 OTA 版本檢查（ota.rokid.com）與韌體下載（ota-g.rokidcdn.com）**，且這是「觸發檢查」時才發生（`otaAutoUpdateEnabled=false`，閒置時完全不連）。

2. **未觀察到眼鏡直接上傳「埋點/照片/影片/音訊」**：三個抓包視窗內，眼鏡本身 **沒有** 直連 `datagather.rokid.com`、`rcs.rokid.com`、`account-center`、`oss` 或任何第三方（Azure/Agora/Google）。

3. **埋點與 AI/ASR 是經手機中繼**（非眼鏡直連）：靜態分析已證明——
   - 眼鏡 assistserver `AIUIDataGatherReporter` 把 `AIUI_Report_Duration`（agentId/時長）經 **藍牙 CXR** 送給手機，`CMD_BLUETOOTH_SYSTEM + KEY_BLUETOOTH_SYS_DATAGATHER`；
   - launcher 的 `Open_Application` 埋點（device_id/user_id/app_name）同樣屬 `com.rokid.sprite.basic` 追蹤 SDK；
   - 手機端 Rokid App 才是真正把資料送 `datagather.rokid.com`、以及用 **Microsoft Azure Speech（ASR/TTS）**、**Agora RTC（直播）**、**Firebase Analytics** 的實體。
   → 因此這些流量**不會出現在眼鏡的 wlan0 抓包**，而會出現在**手機端**。

4. **「照片/影片自動上傳」未發現**：眼鏡端拍照/錄影/螢幕串流/直播皆為本機或藍牙（或使用者主動啟動的 RTMP→Agora）；抓包中也未見任何照片/影片上傳連線。

---

## 5. 抓包檔案（本工作區）

- `analysis/cap1.pcap`（71 MB，OTA 檢查 + 69MB 下載 + curl 驗證）
- `analysis/cap2.pcap`（272 MB，OTA 韌體下載 264MB）
- `analysis/cap3.pcap`（9.6 KB，僅 curl，證明閒置 0 流量）

## 6. 待補（若要 100% 閉環）

- 手機端（`<PHONE_SERIAL>`）尚未 root，故未能直接抓「手機→datagather/Azure/Agora」的封包。若要完整閉環，需在手機端以 VPN/代理或 root tcpdump 抓包，驗證手機 App 實際上傳的內容。
