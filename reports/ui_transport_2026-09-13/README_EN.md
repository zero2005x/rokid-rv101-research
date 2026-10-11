# Transport-Label Fix — On-Device Screenshot Evidence (2026-09-13 23:03–23:10)

> 中文版：[README.md](README.md)

## Related Report
Rev.3 §11–§12 of `reports/wifi_transfer_fix_2026-09-13.md` (the UI-labelling section of FACTS.md §7 P28).

## Symptom
When downloading a video the **transfer is fast (it really does go over WiFi)**, but the on-screen line "傳輸：" showed **藍牙**.

## Root Cause (race condition)
The glasses-side `WifiServer.accept()` closes the Bluetooth connection → the phone side only registered `onDisconnected` (no `onConnected`)
→ the refresh lands exactly at the moment "the WiFi TCP has not connected yet" → it writes "藍牙", and after that **no callback ever again** updates it.

## Fix
`LinkClient.onConnected` + `Remote.onTransportChanged` as a single notification point (client/tcp × connected/disconnected),
plus a new three-state display `Remote.wifiNegotiating` (reusing the existing string `transport_wifi_connecting`).

## Screenshots (in time order; the tab was **never switched manually** throughout)

| File | Time point | On-screen text | Notes |
|------|------|----------|------|
| `01_before_bt.png` | Before download (just after `WIFI_DISCONNECT`) | 傳輸：藍牙 | Baseline state |
| `02_negotiating_wifi.png` | t+2 s after the download is triggered | **傳輸：WiFi 連線中…** | The new intermediate state added by the fix (the old version showed 藍牙 here) |
| `03_wifi_connected.png` | t+11 s | **傳輸：WiFi 192.168.43.1:8555** | Updated automatically, no tab switch needed |
| `04_wifi_state_now.png` | Later | 傳輸：WiFi 192.168.43.1:8555 | Current-state confirmation (remote tab) |

## Corresponding Log (excerpt)
```
23:04:42 Remote  : 媒體通道：建立 WiFi 中…
23:04:48 Remote  : wifi media: apNet=126 localIp=fe80::9842:2cff:fe7d:f157
23:04:48 Remote  : 媒體通道：WiFi 192.168.43.1:8555
23:04:51 DebugReceiver: TEST_TRANSFER result=Success(...) elapsed=8391ms ... link=wifi
```

## Devices
- Phone: 21091116UG (`<PHONE_SERIAL>`), Android 13 / MIUI, app-phone **1.2** (versionCode 3)
- Glasses: RG-glasses (`<DEVICE_SERIAL>`), Android 12, app-glasses **1.0-safe.2** (versionCode 4)
