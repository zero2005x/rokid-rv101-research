# 無線 adb（adb-over-TCP）可行性實測報告 — RV101 眼鏡

> English: [wireless_adb_feasibility_2026-09-20_EN.md](wireless_adb_feasibility_2026-09-20_EN.md)


> **一句話結論**：**可行，而且不必自製任何東西。** 眼鏡的 adbd **本來就會綁 TCP 5555**
> （IPv6 wildcard），PC 加入眼鏡 SoftAP `RokidCam` 後，標準 `adb connect 192.168.43.1:5555`
> 直接得到 `device` 與 shell。
>
> ⚠ **本報告推翻** `reports/FACTS.md` R7 與 `docs/deploy.md` §9.8／§P3 的「本 build 不支援
> adb-over-TCP」結論。該結論是**偵測方法的 bug**（只 grep IPv4 的 `/proc/net/tcp`），
> 不是平台限制。詳見 §3。

- **日期**：2026-09-20
- **裝置**：RG-glasses（RV101）序號 `<DEVICE_SERIAL>`，Android 12 / SDK 32，
  build `1.22.009-20260710-150201`，kernel `5.10.209`，`ro.debuggable=0`、`ro.adb.secure=1`
- **PC**：Windows 11 + Intel Wi-Fi 6E AX211（WSL2 內操作）
- **原始輸出**：`reports/media/` 以外之文字證據見本檔 §2；PC 端原始檔
  `C:\Users\<USER>\wifitest_result.txt`（腳本 `C:\Users\<USER>\wifitest.ps1`）

---

## 1. 測試方法（為何可信）

1. **先唯讀探測**：`deploy/adb_wifi_probe.sh`（不改任何狀態）→ 發現 adbd 聽在
   `/dev/socket/adbd`（UNIX）**且** `[::]:5555`（TCP）。
2. **從裝置內部確認 listener 真的可連**：`nc 127.0.0.1 5555` → `TCP connect OK`。
3. **端到端實測**：眼鏡開 SoftAP（`cmd wifi start-softap RokidCam wpa2 rokid1234`）→
   PC 切到該 AP（`192.168.43.x`）→ `adb connect 192.168.43.1:5555` → `adb shell`。
4. **自動還原**：測試腳本在 `finally` 內把 PC 切回原 AP，因此**PC 斷網期間測試仍完成**、
   結果落地成檔案（避免「切網路就看不到結果」的取證斷點）。

---

## 2. 實測證據（節錄自 `wifitest_result.txt`）

```
[4] adb kill-server + connect 192.168.43.1:5555
[5] adb devices -l:
<DEVICE_SERIAL>        device product:glasses model:RG_glasses device:glasses
<PHONE_SERIAL>        device product:pissarropro_global ...
192.168.43.1:5555       device          ← 無線 transport 建立成功

[6] adb -s 192.168.43.1:5555 shell 'echo WIRELESS_OK; id; getprop ro.serialno; getprop ro.product.model'
WIRELESS_OK
uid=2000(shell) gid=2000(shell) groups=2000(shell),1004(input),1007(log),1011(adb),1015(sdcard_rw),
  1028(sdcard_r),1078(ext_data_rw),1079(ext_obb_rw),3001(net_bt_admin),3002(net_bt),3003(inet),
  3006(net_bw_stats),3009(readproc),3011(uhid),3012(readtracefs) context=u:r:shell:s0
<DEVICE_SERIAL>
RG-glasses
```

**同時存在三條 transport**（`adb devices` 同一次輸出）：USB 眼鏡、USB 手機、**WiFi 眼鏡**。

**多連線並行測試（同一次無線 run，`wifitest3_results.txt`）**：

```
PC IP = 192.168.43.86
connected to 192.168.43.1:5555
--- concurrent call 1..4 ---
SESSION_1 / 2000 / <DEVICE_SERIAL>      ← uid=2000(shell)
SESSION_2 / 2000 / <DEVICE_SERIAL>
SESSION_3 / 2000 / <DEVICE_SERIAL>
SESSION_4 / 2000 / <DEVICE_SERIAL>
--- devices ---  192.168.43.1:5555  device   （與 USB 眼鏡、USB 手機並存）
```

→ **「無限 adb 連線」在架構與實測兩層都成立**：同一 client 可連續開多條 session
（adbd 每條 TCP 連線各給一個獨立 transport／session），不同 PC 只是多開幾條 TCP 連線，
唯一的閘門是 **RSA 金鑰授權**（`ro.adb.secure=1`）與 SoftAP 的連線數上限。

---

## 3. 根因：專案原本的偵測碼只看 IPv4（真 bug）

眼鏡 stead-state 屬性（**未經本次任何修改**）：

| 屬性 | 值 |
|------|-----|
| `persist.adb.tcp.port` | **`5555`**（跨開機保留） |
| `service.adb.tcp.port` | *(空)* |
| `ro.debuggable` | `0` |

| 檢查 | 結果 | 意義 |
|------|------|------|
| `grep -c ':15B3 ' /proc/net/tcp`（**IPv4**） | **0** | ← 專案舊偵測碼看的就是這裡 |
| `grep -c ':15B3 ' /proc/net/tcp6`（**IPv6**） | **2** | ← adbd 實際綁在這裡 |
| `netstat -tlnp` | `tcp6 [::]:5555 LISTEN` | 對外確實 LISTEN |
| `/proc/net/tcp6` 該行 uid 欄 | `2000`（shell） | 與 adbd 降權後身分一致 |
| 裝置內 `nc 127.0.0.1 5555` | `TCP connect OK` | listener 真的活著 |

`[::]` 是 **dual-stack wildcard**：同時接受 IPv6 與 **IPv4-mapped** 連線，
所以 IPv4 的 `192.168.43.1:5555` 連得到（§2 已證）。

**舊結論怎麼來的**：`deploy/start_shizuku.sh`／`CommandRouter.enableWifiAdb()` 都以
`grep -c ':15B3 ' /proc/net/tcp` 當「有沒有綁」的判準 → IPv6-only 的 listener 一律看不到
→ 回報 `adb-over-tcp not supported on this build`。
（`netstat` 那行在有 `-p` 時會去查 uid→行程，權限不足時欄位是 `-`，也容易誤讀成「不是 adbd」。）

**⚠ 一個已知的高風險動作要澄清**：文件記載「重啟 adbd 會掉 USB 列舉」——那是**重啟 adbd
本身**的副作用（本機 adbd 由 vendor 方式啟動、USB FunctionFS 由它自己拉起），
**與 TCP 綁定無關**。因此正確做法是**不要重啟 adbd**（見 §6）。

---

## 4. 機制：adbd 為什麼會綁 TCP

AOSP `adbd`（`daemon/main.cpp`）的邏輯：

```cpp
if (access(USB_FFS_ADB_EP0, F_OK) == 0) { usb_init(); is_usb = true; }   // USB transport
std::string prop_addr = GetProperty("service.adb.listen_addrs", "");
if (prop_addr.empty()) {
    std::string prop_port = GetProperty("service.adb.tcp.port", "");
    if (prop_port.empty()) prop_port = GetProperty("persist.adb.tcp.port", "");  // ← 本機走這條
    ...
}
setup_adb(addrs);   // init_transport_socket_server() → TCP listener
```

兩點關鍵：

1. **`persist.adb.tcp.port` 會讓 adbd「每次啟動」都自己開 TCP listener**，
   不需要 `adb tcpip`、不需要重啟 adbd，**開機即生效**。本機此值已是 `5555`。
2. 綁的是 `[::]`（dual-stack），因此 `/proc/net/tcp6` 才看得到。

**認證**：`ro.adb.secure=1` + `ro.boot.verifiedbootstate=orange` →
`auth_required = true`，走正常 RSA 金鑰握手。本次能直接連上是因為
`/data/misc/adb/adb_keys`（mode `0640`、6 把金鑰）**已預載**本機公鑰（原廠預授權，
見 `reports/ADB_Media_Exfiltration_Report.md`）。

---

## 5. 可行性結論（依主機類型）

| 主機 | 可行性 | 條件 |
|------|--------|------|
| **Windows / macOS / Linux 筆電、桌機** | ✅ **已實證** | 加入眼鏡 SoftAP（或同網段）→ `adb connect 192.168.43.1:5555`；金鑰需在 `adb_keys` |
| **Android 手機**（本專案手機端 App） | ✅ 可行（同一條路） | 手機已具備加入 `RokidCam` 與 TCP 的實作（`WifiLink`）；把 `Protocol.AP_IP:5555` 當 adb 目標即可。**未實測**（手機 WiFi 當時為關閉） |
| **多主機同時連線** | ✅ **已實測** | 同一次無線 run 連續 4 條 session 全成功（`SESSION_1..4`、uid 2000），且與 USB 眼鏡／USB 手機 transport 並存。adbd 對每條 TCP 連線各開一個 session；不同 PC 只是多開 TCP 連線，實務上限是**金鑰授權**與 AP 連線數 |
| **來源白名單** | ❌ 無 | 只要在同網段、且金鑰在 `adb_keys`，任何主機都能連；SoftAP 密碼 `rokid1234` 寫死在 APK（`Protocol.kt`） |

**「拿到 root 之後」的角色**：無線 adb **不需要 root 就能被連**（adbd 是 shell 身分），
root 的價值在於**設定 `persist.adb.tcp.port`**（一次性，跨開機）與其餘 root 功能；
本機該屬性**已經設好**，故目前**連線本身完全不依賴再提權**。

---

## 6. 操作建議（避免踩坑）

1. **確認 listener**（唯一正確判準，兩張表都要看）：
   ```sh
   grep -q ':15B3 ' /proc/net/tcp && echo ipv4 || true
   grep -q ':15B3 ' /proc/net/tcp6 && echo ipv6 || true   # ← 本機在這裡
   ```
2. **不要重啟 adbd**（會掉 USB 列舉）。要開 TCP 只需要
   `setprop persist.adb.tcp.port 5555`（root，跨開機）**然後正常重開機一次**。
3. **入口 IP**：SoftAP `wlan2 = 192.168.43.1`（固定）；若眼鏡改連一般 WiFi（STA，`wlan0`），
   則用該網段 IP，但**本機 STA 與 SoftAP 互斥**，且 STA 下 IP 不固定。
4. **PC 端**：`adb connect 192.168.43.1:5555`；新主機首次連線需眼鏡端授權，
   而眼鏡**沒有螢幕可彈窗** → 需先把該主機公鑰寫入 `/data/misc/adb/adb_keys`（root）。
5. **省電**：眼鏡待機可能讓 AP 或 WiFi 掉；`svc wifi enable` / `cmd wifi start-softap` 可重拉。

---

## 7. 自製 relay（`deploy/adb_tcp_relay.c`）：現在是**備援**，不是必需

本次仍完成並實作了一條「不依賴 adbd TCP」的路線，作為未來 OTA 改壞 adbd 時的備援：

- **原理**：TCP listener → 轉 `/dev/socket/adbd`（adbd 的 UNIX socket），
  認證／shell／sync 全由原廠 adbd 處理。已確認眼鏡 **確實有** `/dev/socket/adbd`：
  `srw-rw---- system system u:object_r:adbd_socket:s0`
- **限制（實測）**：`shell`(2000) 連 `/dev/socket/adbd` → **`Permission denied`**（SELinux），
  **必須 root** 才能跑 relay；手機（Magisk）上已驗證 root 可連。
- **何時才需要它**：若某版韌體把 `persist.adb.tcp.port` 拿掉、且不方便重開機時，
  可在已 root 的 session 內即時開一個入口：
  ```sh
  adb push build-cache/adb_tcp_relay /data/local/tmp/
  adb shell 'su -c "setsid /data/local/tmp/adb_tcp_relay -p 5555 -l /data/local/tmp/relay.log -d"'
  ```
- 檔案：`deploy/adb_tcp_relay.c`（原始碼）、`deploy/build_adb_relay.sh`（NDK 建置）、
  `deploy/adb_wifi_relay.sh`（start/stop/status/probe）、`deploy/adb_wifi_probe.sh`（唯讀探測）

---

## 8. 安全性意涵（必須揭露）

- 這等於**把 USB 偵錯埠開到網路上**：同一網段內、金鑰已授權的主機可直接取得 shell
  （uid 2000，含 `sdcard_rw`、`log`、`input` 等群組）→ 可讀全部照片／影片、下 `input` 事件、
  讀 logcat。`reports/ADB_Media_Exfiltration_Report.md` 的 CVSS 4.6（Physical）
  **在無線 adb 開啟後應重評為 AV:N（約 7.5 High）**——該報告早已預告此條件，現已成立。
- **SoftAP 密碼 `rokid1234` 硬編碼於 APK**（`Protocol.kt:15`）→ 並非有效的存取控制。
- 建議：不需要時 `setprop persist.adb.tcp.port ""`（root）＋重開機，或關閉 SoftAP。

---

## 9. 可重跑指令

```bash
source deploy/env.sh                      # ADB/SERIAL
bash deploy/adb_wifi_probe.sh             # 唯讀探測（屬性/ socket / listener / 工具 / 金鑰）
bash deploy/adb_wifi_relay.sh status      # relay 狀態（備援路線）

# 眼鏡開 AP（root 或 shell 皆可）：
"$ADB" -s "$SERIAL" shell 'cmd wifi start-softap RokidCam wpa2 rokid1234'
# PC 加入 RokidCam 後：
adb connect 192.168.43.1:5555 && adb devices -l && adb -s 192.168.43.1:5555 shell id
```

---

## 10. 待辦（尚未實測，標明不確定）

| 項目 | 狀態 |
|------|------|
| 手機端 App 直接以 `192.168.43.1:5555` 當 adb 目標 | ⏳ 未實測（需手機 WiFi 開啟；路徑與 PC 完全相同） |
| 多條 session 並行 | ✅ **已實測**：同一次無線 run 連續 4 條全成功（§2） |
| 冷開機後 adbd 自動綁 TCP（`persist` 已為 5555） | ⏳ 未實測（未重開機）；AOSP 邏輯＋屬性值推論為「會」，且**不需 USB** |
| OTA 後是否仍保留 `persist.adb.tcp.port` | ⏳ 未知（`persist.*` 通常保留） |
| 待機／省電後 AP 或 listener 是否掉線 | ⏳ 未實測 |

## 11. 本次一併修正的專案資產

| 檔案 | 變更 |
|------|------|
| `android/.../net/CommandRouter.kt` | `enableWifiAdb()` 同時檢查 `/proc/net/tcp` 與 `/proc/net/tcp6`、ACK 新增 `boundOn`、AP 未開時自動 `ensureStarted()` |
| `android/app-glasses/build.gradle.kts` | 版本 → `1.0-safe.4`（versionCode 6） |
| `dist/app-glasses-v1.0-safe.4.apk` | 已建置並**安裝到眼鏡**（`dumpsys` 確認 versionName=1.0-safe.4）；實機驗證偵測指令回 `v6` |
| `reports/FACTS.md` R7 | 由「不支援」改為「**已推翻**：綁在 IPv6 `[::]:5555`」 |
| `docs/deploy.md` §9.8／§P3／指令表 | 由「不可行（放棄）」改為「可行」＋正確判準 |
| `docs/device-debug-notes.md` §D | 加註更正 |
| `deploy/adb_wifi_probe.sh`（新） | 唯讀可行性探測（屬性／socket／listener／工具／金鑰） |
| `deploy/adb_wifi_verify.sh`（新） | 一鍵驗證 listener＋入口 IP＋實際 `adb connect` |
| `deploy/adb_tcp_relay.c`／`build_adb_relay.sh`／`adb_wifi_relay.sh`（新） | 備援 relay（TCP → `/dev/socket/adbd`，需 root） |
