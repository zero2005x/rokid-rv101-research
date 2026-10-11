# On-Device Packet Capture Verification Report (tcpdump measured on Rokid glasses)

> 中文版：[capture-verification-report.md](capture-verification-report.md)

- Device: Rokid RG-glasses `<DEVICE_SERIAL>`, root (uid=0 / SELinux Permissive)
- Network: the glasses joined the public WiFi "iTaiwan" (no password; came online after clicking "我同意&免費上網" on the captive portal)
- Tools: the device's built-in `/system/bin/tcpdump 4.9.2` (run as root) + host-side tshark analysis
- Time: 2026-09-05 12:29–12:49

---

## 1. Going Online (iTaiwan captive portal)

The glasses were originally `wlan0 DOWN`. After `svc wifi enable` they automatically
connected to the remembered `iTaiwan` (BSSID `ec:ad:e0:bf:76:c0`, `Security type 0`, an
open network).

Once connected, `NetworkCapabilities` carried `CAPTIVE_PORTAL`, and all HTTP was
302-redirected:

```
GET http://www.example.com  →  302 Location: http://10.10.10.10
GET http://10.10.10.10      →  302 Location: https://wlangw.hinet.net/v2_0/itaiwan_vsr_wlanac/auth_portal.php?...
   → JS redirect → https://auth.itaiwan.gov.tw/auth_page.php?...
   → 302 → /noauth_page.php   (iTaiwan authentication page)
   form action = POST https://wlangw.hinet.net/v2_0/itaiwan_vsr_auth/auth_page.php
     hidden: username = ac86d155d6f8@itw   password = myegov
   button: 「我同意&免費上網」
after POST → "AUTH SUCCESS" → connectivity test http://connectivitycheck.gstatic.com/generate_204 returns 204 → online
```

> Note: the iTaiwan terms state that "logging in is deemed consent to the collection of
> device IP/MAC, AP location, time of use, and browser" — this is collection by the
> **iTaiwan/Hinet venue**, not by Rokid.

---

## 2. Capture Results (three tcpdump runs, 800 s of window in total; **the actual packet span is about 12 minutes** (166s+300s+251s))

### Capture 1 (200 s; triggered the OTA check broadcast `JUDGE_BEFORE_CHECK`)

DNS queries and TLS SNI:

| Time | Action | Destination endpoint | IP |
|------|------|----------|-----|
| 12:31:08–10 | our own curl verification | ota.rokid.com / datagather.rokid.com / rcs.rokid.com | see below |
| 12:32:32.5 | **OTA version check** (device) | `ota.rokid.com` | 116.62.24.158 |
| 12:32:32.9–33.3 | **firmware download** (device) | `ota-g.rokidcdn.com` | 192.169.122.148 |

TCP traffic statistics:

| Connection | Bytes | Description |
|------|--------|------|
| glasses ↔ `192.169.122.148:443` (ota-g.rokidcdn.com) | **69 MB downloaded** | firmware `RVE01-ota-1.25.012-20260901-150201.zip` (~1 GB; I stopped it partway) |
| glasses ↔ `116.62.24.158:443` (ota.rokid.com) | ~9 KB | version check API |
| glasses ↔ `121.40.237.216:443` (rcs.rokid.com) | ~8 KB | our own curl |

### Capture 2 (300 s; OTA download continues)

- Downloaded **264 MB** from `ota-g.rokidcdn.com` (saturating the entire 300 s window).
- Only one other connection, an `ota.rokid.com` check (156 bytes).

### Capture 3 (300 s; OTA stopped, opened 3 apps 3 times each)

- **0 autonomous packets**. Throughout the window the device itself made **no outbound
  connections at all** (the same when opening apps).
- Only 1 connection appeared, and only when we manually ran `curl https://ota.rokid.com`
  (proving the capture works and that an idle device generates no traffic).

---

## 3. Endpoint IP Ownership (whois)

| Endpoint | IP | Ownership |
|------|-----|------|
| ota.rokid.com | 116.62.24.158 | **Aliyun, Hangzhou, China** |
| datagather.rokid.com | 116.62.24.158 | Aliyun, Hangzhou (same IP as ota) |
| rcs.rokid.com | 121.40.237.216 | **Aliyun, Hangzhou, China** |
| ota-g.rokidcdn.com | 192.169.122.148 | **Zenlayer** (global CDN, Rokid's firmware CDN) |

That is: Rokid's official backends (OTA check, datagather telemetry, AI rcs) all run on
**Aliyun Hangzhou**; firmware downloads go through the **Zenlayer CDN**.

---

## 4. Verification Conclusions

1. **The glasses transmit very little outbound traffic "directly over WiFi"**: about 12
   minutes measured (packet span; 800 s of window), and the only autonomous outbound
   connections from the glasses were the **OTA version check (ota.rokid.com) and the
   firmware download (ota-g.rokidcdn.com)**, and these happen only when a check is
   "triggered" (`otaAutoUpdateEnabled=false`; when idle they do not connect at all).

2. **No direct upload of "telemetry/photos/videos/audio" from the glasses was observed**:
   across the three capture windows, the glasses themselves did **not** connect directly
   to `datagather.rokid.com`, `rcs.rokid.com`, `account-center`, `oss` or any third party
   (Azure/Agora/Google).

3. **Telemetry and AI/ASR are relayed through the phone** (not a direct glasses
   connection): static analysis has already shown —
   - The glasses-side assistserver `AIUIDataGatherReporter` sends `AIUI_Report_Duration`
     (agentId/duration) to the phone over **Bluetooth CXR**, via
     `CMD_BLUETOOTH_SYSTEM + KEY_BLUETOOTH_SYS_DATAGATHER`;
   - The launcher's `Open_Application` telemetry (device_id/user_id/app_name) likewise
     belongs to the `com.rokid.sprite.basic` tracking SDK;
   - The phone-side Rokid App is the entity that actually sends data to
     `datagather.rokid.com` and uses **Microsoft Azure Speech (ASR/TTS)**,
     **Agora RTC (live streaming)** and **Firebase Analytics**.
   → Therefore this traffic **will not appear in a wlan0 capture on the glasses**, but
   will appear **on the phone side**.

4. **"Automatic photo/video upload" was not found**: on the glasses, photo
   capture/recording/screen streaming/live streaming are all local or over Bluetooth (or
   an RTMP→Agora session the user actively starts); no photo/video upload connection was
   seen in the captures either.

---

## 5. Capture Files (this workspace)

- `analysis/cap1.pcap` (71 MB; OTA check + 69 MB download + curl verification)
- `analysis/cap2.pcap` (272 MB; OTA firmware download 264 MB)
- `analysis/cap3.pcap` (9.6 KB; curl only, proving 0 traffic when idle)

## 6. Remaining Work (for a 100% closed loop)

- The phone (`<PHONE_SERIAL>`) is not yet rooted, so packets for
  "phone→datagather/Azure/Agora" could not be captured directly. For a complete closed
  loop, capture on the phone side with a VPN/proxy or a root tcpdump to verify what the
  phone App actually uploads.
