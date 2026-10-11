# Rokid 眼鏡隱私稽核報告（資料外傳分析）

> English: [privacy-audit-report_EN.md](privacy-audit-report_EN.md)

- 設備：Rokid RG-glasses，序號 `<DEVICE_SERIAL>`，Android 12（SDK 32），YodaOS-Sprite `1.22.009-20260710-150201`
- 對應手機：Redmi 21091116UG（`<PHONE_SERIAL>`），裝有 Rokid 官方 App `com.rokid.sprite.global.aiapp`
- 稽核方式：root（uid=0 + SELinux Permissive）後，靜態反編譯（jadx 1.5.1）眼鏡端系統 App 與手機端 Rokid App、擷取 native 庫字串、dump 裝置內資料庫/SharedPreferences
- 時間：2026-09-05

---

## 1. 執行摘要（TL;DR）

1. **眼鏡目前離線**：`wlan0` 為 DOWN、無路由、無 DNS、無任何對外 TCP/UDP 連線（唯一 listener 是本機 `0.0.0.0:8341`）。因此「此刻」沒有資料正在外傳。
2. **Rokid 確實會傳送用戶資料到 Rokid 官方與第三方伺服器**（在有網路時），主要有四類：
   - 使用行為埋點（telemetry）→ `datagather.rokid.com`（Rokid 官方）
   - 語音助理的 ASR/TTS 音訊 → **Microsoft Azure Speech**（第三方）
   - AI 大語言模型對話文字 → `rcs.rokid.com/metis`（Rokid 官方）
   - 直播串流影音 → **Agora RTC**（第三方，僅使用者主動發起時）
3. **未發現「自動上傳照片/影片」的雲端端點**：拍照/錄影檔案存本機、經藍牙傳到手機；直播/錄影/螢幕串流皆為「使用者主動觸發」或「本機/藍牙」傳輸。
4. **敏感權限是「免同意」自動授予的**：Rokid 預載系統 App 的相機/麥克風/定位/聯絡人/電話狀態，以 `SYSTEM_FIXED|GRANTED_BY_DEFAULT` 在首次開機就授予，**沒有跳出任何同意對話框**；系統 App 內也**找不到隱私權政策/EULA/同意流程**。也就是說，資料收集並非在「使用者明確知情同意」的機制下進行（相較之下，user 自行安裝的 `/data/app` App 才有 runtime 權限彈窗）。

---

## 2. Root 模式確認

```
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
getenforce → Permissive
root_daemon.sock → 存活（/proc/net/unix）
```

（沿用既有 `ghostlock_rv101` LPE → 常駐 `root_daemon` UNIX socket 秒級執行 root 指令。）

---

## 3. 網路現況（實測）

| 檢查 | 結果 |
|------|------|
| `ip addr` | 僅 `lo`；`wlan0` `NO-CARRIER state DOWN`；無 bt-pan/rmnet |
| `ip route` | 空（無預設路由） |
| DNS | 無 `/etc/resolv.conf`、無 DHCP DNS |
| `/proc/net/tcp` | 僅 `0.0.0.0:8341` LISTEN（uid 0，本機 vendor listener） |
| `/proc/net/udp` | 空 |
| 帳號 | `dumpsys account` → Accounts: 0（未登入任何 Rokid 帳號） |

---

## 4. 權限與「同意」分析（核心）

### 4.1 Rokid 預載系統 App —— 敏感權限「免同意」自動授予

以下權限在 `dumpsys package` 中顯示 `granted=true, flags=[SYSTEM_FIXED|GRANTED_BY_DEFAULT|RESTRICTION_SYSTEM_EXEMPT...]`，即**開機即授予、使用者不可撤銷、從未彈窗詢問**：

| App（皆為 /system 或 /product 預載） | INTERNET | CAMERA | RECORD_AUDIO | 定位 | 聯絡人 | 電話狀態 |
|---|---|---|---|---|---|---|
| com.rokid.glass.ota | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.os.sprite.assistserver | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.os.sprite.live | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.os.sprite.record | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.os.master.screenstream | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.cxrservice | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.sysconfig | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |

> 對照：user 自行安裝（`/data/app`）的 `com.rokid.hud.glasses`、`com.example.rokidglasses` 權限帶 `USER_SENSITIVE_WHEN_GRANTED`（才會走 runtime 彈窗同意）。

### 4.2 系統 App 內無隱私同意流程

- 反編譯 launcher / assistserver 的 `com.rokid.*` 程式碼，grep `privacy / 隱私 / EULA / 同意 / agreement / privacy policy`：
  - 僅有 App Store 第三方 App 的 `privacyPolicy` 欄位、以及 AI 助理的 `privacy_level`（控制「是否開啟 AI / 是否錄音場景」的執行期開關），**無首啟隱私政策同意頁**。

### 4.3 裝置設定中的既有開關（顯示使用者其實可部分關閉）

`RKMobileSettings.xml`：`settings_voice_control=close`（語音控制已關）、`settings_wifi_enable=false`（WiFi 已關）、`settings_app_is_overseas=true`、`country_code=tw`。
另有「word tips（即時字幕）`onLineAsr=true`」——該功能會走線上語音辨識。

---

## 5. 雲端端點與資料流

### 5.1 眼鏡端（YodaOS-Sprite 系統 App）硬編碼端點

| 端點 | 用途 | 位置 |
|------|------|------|
| `https://datagather.rokid.com`（+dev/test） | 資料蒐集（埋點） | launcher `com.rokid.sprite.basic.ITrackConstants` |
| `https://rcs.rokid.com/metis/api/chat/completions`（+dev/test/internal） | AI 大語言模型（OpenAI 相容 chat） | assistserver `InkLanguageModelCapability` |
| `https://rcs.rokid.com`（Open Service） | RCS 雲服務 | assistserver `InkOpenServiceCapability` |
| `https://account-center.rokid.com` / `member-server.rokid.com` | Rokid 帳號/會員 | launcher `AccountManager` |
| `https://ota.rokid.com/v1/extended/ota/check` + `ota-g.rokidcdn.com` | OTA 版本檢查/下載 | com.rokid.glass.ota（見 device-debug-notes.md） |
| `https://rokid-content-pro.rokid.com` | 內容服務 | assistserver |
| `oss-cn-hangzhou.aliyuncs.com`（Aliyun OSS） | 物件儲存 SDK（bundled，未見 launcher 自體呼叫上傳） | launcher（Aliyun SDK） |

### 5.2 埋點實際內容（`datagather.rokid.com`）

`ITrackConstants` 定義上傳欄位與**硬編碼簽章 secret**：

```
欄位: app_name, app_type, business_type, custom_data, device_id,
      environment, event_name, event_type, request_id, user_id
event_name = "Open_Application"
station_secret      = af0d60727ab94deb941b6b31f9893fd9
eye_station_secret  = 66c4c2ca73ef4a50a3ee13e84a2cbd3c
```

assistserver 端 `AIUIDataGatherReporter` 回報事件：

```
event_name = "AIUI_Report_Duration"   event_type = 5
{ event_name, event_type, code, duration_uuid, custom_data:{bsId(agentId)} }
→ 透過藍牙 CXR 通道 (CMD_BLUETOOTH_SYSTEM + KEY_BLUETOOTH_SYS_DATAGATHER) 送給手機
→ 手機再上傳 datagather.rokid.com
```

即：**會上傳 `device_id`、`user_id`、app 開啟紀錄、語音助理使用的 agentId 與使用時長**。這是「使用行為/用量」資料，**不是**照片/影片/音訊內容。

### 5.3 語音助理 ASR/TTS → 第三方（Microsoft Azure）

手機端 Rokid 官方 App `com.rokid.sprite.global.aiapp` 的 native 庫（arm64 split，228MB）內含：

- `libMicrosoft.CognitiveServices.Speech.*.so`，字串含
  `stt.speech.microsoft.com`（語音轉文字）、`tts.speech.microsoft.com`（文字轉語音）、`cts/convai.speech.microsoft.com`、`dev.microsofttranslator.com`
- `assets/cei.json`：ASR 引擎組態，`cei_param_audio_format_str = "16k16bitmono"`（16kHz 16bit 單聲道音訊上傳）

即：**語音助理（語音辨識/語音合成）會把麥克風音訊送到 Microsoft Azure Speech 雲端**（第三方）。

### 5.4 直播串流影音 → 第三方（Agora）

- 眼鏡端 `com.rokid.os.sprite.live`：RTMP/FLV/H264 直播引擎，`live.url` 由呼叫端（經藍牙的框架）提供，僅在有 WiFi 時啟動。
- 手機端 App 含 **Agora RTC/RTM SDK**（`libagora-rtc-sdk.so`、`libagora-ffmpeg.so`、`libalivcffmpeg.so` 等）→ 直播影音走 **Agora** 雲端（第三方）。此為**使用者主動啟動「直播」功能**時才發生。

### 5.5 其他第三方 SDK（手機端 App）

- **Firebase Analytics / Auth**（Google）：`firebase-analytics.properties`、`firebase-auth.properties`、firestore proto → 分析資料送 Google。
- **Aliyun SDK**（`endpoints.json` 為 Alibaba Cloud 通用端點表）。
- **Alipay / WeChat 登入**：`account_wechat_login_icon`、`mp.alipay`（支付/小程序）。
- **TextModeration**（內容審核 native 庫）。

---

## 6. 照片/影片是否上傳？（關鍵結論）

| 元件 | 行為 | 雲端上傳 |
|------|------|----------|
| 拍照/錄影（眼鏡相機） | 存本機（12MP 4032×3024 / 3072×1728），藍牙傳手機 | **未發現自動上傳端點** |
| com.rokid.os.sprite.record（錄影服務） | 本機錄影，經藍牙/binder 給手機 | 無雲端 URL |
| com.rokid.os.master.screenstream（螢幕串流） | 本機 binder/藍牙給手機（`com.rokid.yodaos.action.SCREENSTREAM_SERVICE_OFF`） | 無雲端 URL |
| com.rokid.cxrservice（CXR 藍牙服務） | RFCOMM/MFI socket 傳音訊給手機 | 無雲端 URL |
| com.rokid.os.sprite.live（直播） | RTMP（`live.url` 由呼叫端指定），需 WiFi | 經手機走 **Agora**（使用者主動） |
| 語音助理 ASR/TTS | 藍牙→手機→Azure Speech | **Microsoft Azure**（使用者使用語音時） |
| AI 對話文字 | → `rcs.rokid.com/metis` | Rokid 官方（需 Rokid token、使用 AI 時） |
| 使用埋點 | → `datagather.rokid.com` | Rokid 官方（背景，非使用者主動） |

**結論：照片/影片沒有「背景自動上傳」到 Rokid/第三方**。唯一會把「影音內容」送出裝置的情況是使用者主動發起「直播」（Agora）或使用「語音助理」（Azure Speech）。相較之下，「使用埋點」與「OTA 檢查」是背景自動發生，會帶 device_id/user_id 等識別資料。

---

## 7. 對「是否在使用者知情同意下傳送」的判定

1. **敏感權限**：Rokid 預載系統 App 的相機/麥克風/定位/聯絡人/電話狀態以 `SYSTEM_FIXED|GRANTED_BY_DEFAULT` **免同意授予**，且**無首啟隱私政策同意頁** → **並非在「明確知情同意」機制下**取得。
2. **背景埋點**（`datagather.rokid.com`）與 **OTA 檢查**：非使用者主動觸發、無可見開關 → 傾向「**未經使用者明確同意**」。
3. **語音助理（Azure Speech）／AI 對話（metis）／直播（Agora）**：屬**使用者主動使用功能**，且裝置設定提供語音控制/WiFi 等開關，但**未見針對「音訊將送往 Microsoft Azure」的明確提示或同意**。

> 注意：本稽核為**裝置端靜態證據**。Rokid 官方隱私政策（App 內或官網）可能載明上述收集；但「政策文件存在」與「裝置實際以同意彈窗取得用戶同意」是兩件事。就裝置端實測而言，**未發現同意彈窗或隱私政策同意流程**。

---

## 8. 證據檔案（本工作區）

- APK 鏡像：`apks/*.apk`（眼鏡端系統 App + 手機端 `phone_aiapp_*.apk`）
- 反編譯源碼：`analysis/jadx_out/{launcher,live,assistserver}/`
- 字串抽取：`analysis/strings/`、`analysis/strings_phone/`、`analysis/phone/native_strings.txt`
- 裝置資料：`analysis/device_data/usage-app.db`（本機 app 開啟次數表）、`ota_sp.xml`、`RKMobileSettings.xml`、`er_pub_config.xml`（⚠ 2026-09-13 複查：這些裝置 XML **不在本工作區**，`analysis/device_data/` 目前僅有 `all_pkgs.txt`、`usage-app.db`、`usage-app-journal`）
- 完整套件清單：`all_pkgs.txt`

## 9. 建議

1. 若不希望背景埋點：以 root 阻斷 `datagather.rokid.com`（hosts/firewall），或停用 `com.rokid.os.sprite.launcher` 的 INTERNET。
2. 語音助理/線上字幕會把音訊送 Azure Speech → 關閉語音控制與「word tips」線上 ASR（`onLineAsr=false`）。
3. 若要完整驗證「實際外傳封包」，可在眼鏡接上受控 WiFi（或手機熱點）後以 root 跑 `tcpdump` 抓包比對端點。
