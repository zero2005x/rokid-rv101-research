# Rokid Glasses Privacy Audit Report (Data Exfiltration Analysis)

> 中文版：[privacy-audit-report.md](privacy-audit-report.md)

- Device: Rokid RG-glasses, serial number `<DEVICE_SERIAL>`, Android 12 (SDK 32), YodaOS-Sprite `1.22.009-20260710-150201`
- Paired phone: Redmi 21091116UG (`<PHONE_SERIAL>`), with the official Rokid App `com.rokid.sprite.global.aiapp` installed
- Audit method: after gaining root (uid=0 + SELinux Permissive), statically decompiled the glasses-side system Apps and the phone-side Rokid App (jadx 1.5.1), extracted native library strings, and dumped on-device databases/SharedPreferences
- Date: 2026-09-05

---

## 1. Executive summary (TL;DR)

1. **The glasses are currently offline**: `wlan0` is DOWN, there is no route, no DNS, and no outbound TCP/UDP connection of any kind (the only listener is the local `0.0.0.0:8341`). So "at this moment" no data is being exfiltrated.
2. **Rokid does send user data to Rokid's own and third-party servers** (when there is a network), in four main categories:
   - Usage-behaviour telemetry → `datagather.rokid.com` (Rokid)
   - Voice-assistant ASR/TTS audio → **Microsoft Azure Speech** (third party)
   - AI large-language-model conversation text → `rcs.rokid.com/metis` (Rokid)
   - Live-streaming audio/video → **Agora RTC** (third party, only when the user initiates it)
3. **No cloud endpoint for "automatic photo/video upload" was found**: photos/videos are stored locally and transferred to the phone over Bluetooth; live streaming/recording/screen streaming are all either "user-initiated" or local/Bluetooth transfers.
4. **Sensitive permissions are granted automatically without consent**: the camera/microphone/location/contacts/phone-state permissions of Rokid's preloaded system Apps are granted on first boot with `SYSTEM_FIXED|GRANTED_BY_DEFAULT`, **without any consent dialog appearing**; no privacy policy/EULA/consent flow could be **found inside the system Apps either**. In other words, data collection does not take place under a mechanism of "explicit informed user consent" (by contrast, only Apps the user installs themselves under `/data/app` get runtime permission prompts).

---

## 2. Root mode confirmation

```
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
getenforce → Permissive
root_daemon.sock → alive (/proc/net/unix)
```

(Reusing the existing `ghostlock_rv101` LPE → a persistent `root_daemon` UNIX socket executes root commands in sub-second time.)

---

## 3. Current network state (measured)

| Check | Result |
|------|------|
| `ip addr` | Only `lo`; `wlan0` `NO-CARRIER state DOWN`; no bt-pan/rmnet |
| `ip route` | Empty (no default route) |
| DNS | No `/etc/resolv.conf`, no DHCP DNS |
| `/proc/net/tcp` | Only `0.0.0.0:8341` LISTEN (uid 0, local vendor listener) |
| `/proc/net/udp` | Empty |
| Accounts | `dumpsys account` → Accounts: 0 (not signed in to any Rokid account) |

---

## 4. Permission and "consent" analysis (core)

### 4.1 Rokid preloaded system Apps — sensitive permissions granted automatically without consent

The following permissions show `granted=true, flags=[SYSTEM_FIXED|GRANTED_BY_DEFAULT|RESTRICTION_SYSTEM_EXEMPT...]` in `dumpsys package`, i.e. **granted at boot, not revocable by the user, and never asked about in a dialog**:

| App (all preloaded in /system or /product) | INTERNET | CAMERA | RECORD_AUDIO | Location | Contacts | Phone state |
|---|---|---|---|---|---|---|
| com.rokid.glass.ota | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.os.sprite.assistserver | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.os.sprite.live | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.os.sprite.record | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.os.master.screenstream | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.cxrservice | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| com.rokid.sysconfig | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |

> For comparison: the permissions of the user-installed (`/data/app`) `com.rokid.hud.glasses` and `com.example.rokidglasses` carry `USER_SENSITIVE_WHEN_GRANTED` (which is what triggers the runtime consent dialog).

### 4.2 No privacy consent flow inside the system Apps

- Decompiled the `com.rokid.*` code of launcher / assistserver and grepped for `privacy / 隱私 / EULA / 同意 / agreement / privacy policy`:
  - Only the `privacyPolicy` field of third-party Apps in the App Store, and the AI assistant's `privacy_level` (a runtime switch controlling "whether AI is enabled / whether the scenario is recorded"), were found; there is **no first-launch privacy-policy consent page**.

### 4.3 Existing switches in the device settings (showing that users can in fact turn some things off)

`RKMobileSettings.xml`: `settings_voice_control=close` (voice control off), `settings_wifi_enable=false` (WiFi off), `settings_app_is_overseas=true`, `country_code=tw`.
There is also "word tips (live captions) `onLineAsr=true`" — that feature uses online speech recognition.

---

## 5. Cloud endpoints and data flows

### 5.1 Hard-coded endpoints in the glasses-side (YodaOS-Sprite system App)

| Endpoint | Purpose | Location |
|------|------|------|
| `https://datagather.rokid.com` (+dev/test) | Data collection (telemetry) | launcher `com.rokid.sprite.basic.ITrackConstants` |
| `https://rcs.rokid.com/metis/api/chat/completions` (+dev/test/internal) | AI large language model (OpenAI-compatible chat) | assistserver `InkLanguageModelCapability` |
| `https://rcs.rokid.com` (Open Service) | RCS cloud service | assistserver `InkOpenServiceCapability` |
| `https://account-center.rokid.com` / `member-server.rokid.com` | Rokid account/membership | launcher `AccountManager` |
| `https://ota.rokid.com/v1/extended/ota/check` + `ota-g.rokidcdn.com` | OTA version check/download | com.rokid.glass.ota (see device-debug-notes.md) |
| `https://rokid-content-pro.rokid.com` | Content service | assistserver |
| `oss-cn-hangzhou.aliyuncs.com` (Aliyun OSS) | Object-storage SDK (bundled; no upload call by launcher itself was observed) | launcher (Aliyun SDK) |

### 5.2 Actual telemetry content (`datagather.rokid.com`)

`ITrackConstants` defines the uploaded fields and a **hard-coded signature secret**:

```
fields: app_name, app_type, business_type, custom_data, device_id,
      environment, event_name, event_type, request_id, user_id
event_name = "Open_Application"
station_secret      = af0d60727ab94deb941b6b31f9893fd9
eye_station_secret  = 66c4c2ca73ef4a50a3ee13e84a2cbd3c
```

`AIUIDataGatherReporter` on the assistserver side reports events:

```
event_name = "AIUI_Report_Duration"   event_type = 5
{ event_name, event_type, code, duration_uuid, custom_data:{bsId(agentId)} }
→ sent to the phone over the Bluetooth CXR channel (CMD_BLUETOOTH_SYSTEM + KEY_BLUETOOTH_SYS_DATAGATHER)
→ the phone then uploads it to datagather.rokid.com
```

That is: **it uploads `device_id`, `user_id`, app-launch records, and the agentId and duration of voice-assistant use**. This is "usage behaviour/volume" data, **not** photo/video/audio content.

### 5.3 Voice assistant ASR/TTS → third party (Microsoft Azure)

The native libraries of the phone-side official Rokid App `com.rokid.sprite.global.aiapp` (arm64 split, 228MB) contain:

- `libMicrosoft.CognitiveServices.Speech.*.so`, whose strings include
  `stt.speech.microsoft.com` (speech to text), `tts.speech.microsoft.com` (text to speech), `cts/convai.speech.microsoft.com`, `dev.microsofttranslator.com`
- `assets/cei.json`: ASR engine configuration, `cei_param_audio_format_str = "16k16bitmono"` (16kHz 16-bit mono audio upload)

That is: **the voice assistant (speech recognition/speech synthesis) sends microphone audio to the Microsoft Azure Speech cloud** (a third party).

### 5.4 Live-streaming audio/video → third party (Agora)

- Glasses side `com.rokid.os.sprite.live`: an RTMP/FLV/H264 live-streaming engine; `live.url` is supplied by the caller (over the Bluetooth framework) and it only starts when WiFi is available.
- The phone-side App includes the **Agora RTC/RTM SDK** (`libagora-rtc-sdk.so`, `libagora-ffmpeg.so`, `libalivcffmpeg.so`, etc.) → live audio/video goes through the **Agora** cloud (a third party). This only happens when the **user actively starts the "live stream" feature**.

### 5.5 Other third-party SDKs (phone-side App)

- **Firebase Analytics / Auth** (Google): `firebase-analytics.properties`, `firebase-auth.properties`, firestore proto → analytics data goes to Google.
- **Aliyun SDK** (`endpoints.json` is Alibaba Cloud's general endpoint table).
- **Alipay / WeChat login**: `account_wechat_login_icon`, `mp.alipay` (payments/mini-programs).
- **TextModeration** (content-moderation native library).

---

## 6. Are photos/videos uploaded? (key conclusion)

| Component | Behaviour | Cloud upload |
|------|------|----------|
| Photo/video capture (glasses camera) | Stored locally (12MP 4032×3024 / 3072×1728), transferred to the phone over Bluetooth | **No automatic-upload endpoint found** |
| com.rokid.os.sprite.record (recording service) | Records locally; delivered to the phone over Bluetooth/binder | No cloud URL |
| com.rokid.os.master.screenstream (screen streaming) | Local binder/Bluetooth to the phone (`com.rokid.yodaos.action.SCREENSTREAM_SERVICE_OFF`) | No cloud URL |
| com.rokid.cxrservice (CXR Bluetooth service) | Sends audio to the phone over an RFCOMM/MFI socket | No cloud URL |
| com.rokid.os.sprite.live (live streaming) | RTMP (`live.url` specified by the caller), requires WiFi | Goes through the phone to **Agora** (user-initiated) |
| Voice assistant ASR/TTS | Bluetooth→phone→Azure Speech | **Microsoft Azure** (when the user uses voice) |
| AI conversation text | → `rcs.rokid.com/metis` | Rokid (requires a Rokid token; when AI is used) |
| Usage telemetry | → `datagather.rokid.com` | Rokid (background, not user-initiated) |

**Conclusion: photos/videos are not "automatically uploaded in the background" to Rokid or third parties**. The only cases where "audio/video content" leaves the device are when the user actively starts a "live stream" (Agora) or uses the "voice assistant" (Azure Speech). By contrast, "usage telemetry" and "OTA checks" happen automatically in the background and carry identifying data such as device_id/user_id.

---

## 7. Assessment of "is it sent with the user's informed consent"

1. **Sensitive permissions**: the camera/microphone/location/contacts/phone-state permissions of Rokid's preloaded system Apps are **granted without consent** under `SYSTEM_FIXED|GRANTED_BY_DEFAULT`, and there is **no first-launch privacy-policy consent page** → they are obtained **not under a mechanism of "explicit informed consent"**.
2. **Background telemetry** (`datagather.rokid.com`) and **OTA checks**: not triggered by the user and with no visible switch → this tends towards "**without the user's explicit consent**".
3. **Voice assistant (Azure Speech) / AI conversation (metis) / live streaming (Agora)**: these are **features the user actively uses**, and the device settings provide switches such as voice control/WiFi, but **no explicit notice or consent regarding "audio being sent to Microsoft Azure" was found**.

> Note: this audit is based on **device-side static evidence**. Rokid's official privacy policy (in the App or on the website) may state the above collection; but "a policy document exists" and "the device actually obtains user consent via a consent dialog" are two different things. As far as on-device measurement goes, **no consent dialog or privacy-policy consent flow was found**.

---

## 8. Evidence files (this workspace)

- APK images: `apks/*.apk` (glasses-side system Apps + phone-side `phone_aiapp_*.apk`)
- Decompiled sources: `analysis/jadx_out/{launcher,live,assistserver}/`
- String extraction: `analysis/strings/`, `analysis/strings_phone/`, `analysis/phone/native_strings.txt`
- Device data: `analysis/device_data/usage-app.db` (local table of app launch counts), `ota_sp.xml`, `RKMobileSettings.xml`, `er_pub_config.xml` (⚠ 2026-09-13 re-check: these device XMLs are **not in this workspace**; `analysis/device_data/` currently contains only `all_pkgs.txt`, `usage-app.db`, `usage-app-journal`)
- Full package list: `all_pkgs.txt`

## 9. Recommendations

1. If background telemetry is unwanted: block `datagather.rokid.com` as root (hosts/firewall), or disable INTERNET for `com.rokid.os.sprite.launcher`.
2. The voice assistant/online captions send audio to Azure Speech → turn off voice control and the "word tips" online ASR (`onLineAsr=false`).
3. To fully verify the "actual exfiltration packets", connect the glasses to a controlled WiFi (or a phone hotspot) and run `tcpdump` as root to capture and compare endpoints.
