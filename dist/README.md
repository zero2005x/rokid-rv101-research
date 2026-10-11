# dist/ — 可安裝的 APK

| 檔案 | 版本 | 說明 |
|------|------|------|
| `app-glasses-v1.0-safe.6.apk` | `1.0-safe.6` (code 8) | **眼鏡端．目前版本**。在 `1.0-safe.5` 之上新增**開發模式**：`net/WifiJoin`（A：`WifiNetworkSuggestion`／B：`cmd wifi connect-network`）＋`net/DevModeController`（狀態機 `OFF→JOINING→ACTIVE→REVERTING→OFF`、idle 逾時自動關、失敗回退並還原 SoftAP）＋`DebugReceiver` 指令 `DEV_MODE_START/STOP/STATUS`、`WIFI_SET_CRED`。敏感指令加**來源守衛**（只允許本 App／adb shell uid 2000／同簽章權限）並在 manifest 宣告 `com.rokid.rc.glasses.permission.DEV_MODE`（signature）。**實測**：路徑 B 成功讓眼鏡加入 `LouisaCoffee`（`wlan0=10.0.0.120/24`）→ PC `adb connect` 得 `uid=2000(shell)`；路徑 A 在 headless 眼鏡失敗（系統通知無法點擊）。詳見 [`docs/wireless-adb-solution-design.md`](../docs/wireless-adb-solution-design.md) §4.1 |
| `app-glasses-v1.0-safe.5.apk` | `1.0-safe.5` (code 7) | 新增 **`AdbWifiService`**（TCP:5555 → `/dev/socket/adbd` 的 relay）與 `DebugReceiver` 指令 `ADB_WIFI_START/STOP/STATUS`。⚠ **實測結論：無 root 時不可用**——`/dev/socket/adbd` 是 `0660 system:system`，第三方 App（uid 10064）`connect()` 得 **EACCES(errno 13)**，擋住的是 **DAC 而非 SELinux**（無任何 `avc: denied`）；實機 log 為 `E AdbWifi: adbd socket 不可連：IOException: Permission denied`。因此定位為**研究版（有 root 時可用）**。 |
| `app-glasses-v1.0-safe.5.apk` | `1.0-safe.5` (code 7) | **眼鏡端．目前版本**。在 `1.0-safe.4` 之上新增 **`AdbWifiService`**（TCP:5555 → `/dev/socket/adbd` 的 relay）與 `DebugReceiver` 指令 `ADB_WIFI_START/STOP/STATUS`。⚠ **實測結論：無 root 時不可用**——`/dev/socket/adbd` 是 `0660 system:system`，第三方 App（uid 10064）`connect()` 得 **EACCES(errno 13)**，擋住的是 **DAC 而非 SELinux**（無任何 `avc: denied`）；實機 log 為 `E AdbWifi: adbd socket 不可連：IOException: Permission denied`。因此定位為**研究版（有 root 時可用）**；通用無線 adb 走「一次插線 bootstrap ＋ adbd 原生 TCP」路線，見 [`docs/wireless-adb-solution-design.md`](../docs/wireless-adb-solution-design.md) |
| `app-glasses-v1.0-safe.4.apk` | `1.0-safe.4` (code 6) | 修正無線 adb 偵測 bug：`enableWifiAdb()` 原本只 grep IPv4 的 `/proc/net/tcp`（`:15B3`），但本機 adbd 綁在 **IPv6 `[::]:5555`**（`/proc/net/tcp6`），因此把「其實有綁」誤判成 `adb-over-tcp not supported on this build`。現在兩張表都查、ACK 回報 `boundOn`（`ipv4`/`ipv6`）。 |
| `app-glasses-v1.0-safe.3.apk` | `1.0-safe.3` (code 5) | 在 `1.0-safe.2` 之上：`albums` 陣列也逐筆容錯（原本一筆壞 album 會讓 album 與 media 全滅）、索引**反向校正**（磁碟上已不存在的檔案會自動移除該筆，不再留幽靈項目）、`getThumbnail()` 不再對已消失的來源檔回舊快取縮圖。 |
| `app-glasses-v1.0-safe.2.apk` | `1.0-safe.2` (code 4) | 修正 **媒體索引遺失**：`media` 陣列逐筆容錯、載入後用磁碟檔案自我修復（實測救回 6 筆消失項目）、`persist()` 原子寫入。 |
| `app-glasses-v1.0-safe.apk` | `1.0-safe` (code 3) | 安全武裝版（第一版）：開機迴圈斷路器／每次開機重試上限＋指數退避／僅充電時自動武裝／開機後延遲，以及「戒指裝置路徑失效自動回退」。 |
| `app-glasses-v1.0-original.apk` | `1.0` (code 1) | **眼鏡端．原始版**，套用安全政策前的行為，供 A/B 對照。 |
| `app-phone-v1.3.apk` | `1.3` (code 4) | **手機端．目前版本**。在 `1.2` 之上：`Remote.onPush` 集中註冊（**修掉 WiFi 為當前通道時 `PROGRESS`／LPE 進度推播被丟棄**的回歸）、`LinkClient.disconnect()` 會通知 UI（按「斷線」畫面立即更新）、`lastAutoWifiFailAt` 加 `@Volatile`、刪除死碼 `mediaClient()`、`request()` 確實走 `Remote.link()`。 |
| `app-phone-v1.2.apk` | `1.2` (code 3) | 修正**媒體一直走藍牙**：一般指令與媒體都優先走 WiFi、媒體操作**自動**建立 WiFi 通道、設定頁開關改為持久化偏好（預設開）、傳輸標示即時更新、`.part` 殘骸清理。實測同一支 10.6 秒影片由「藍牙 324 秒逾時失敗」變「WiFi 3.0 秒」。 |
| `app-phone-v1.1.apk` | `1.1` (code 2) | 手機端舊版（保留對照）。含 `DUMP_STATUS`、拍照／錄影失敗會記錄真正原因、`camera_not_ready` 改為可行動指示。 |
| `usbdebug-hider-v1.1.apk` | `1.1` (code 2) | **USB 偵錯防偵測模組（LSPosed/Vector，自建）**。對 scoped 的第三方 App 遮蔽 `adb_enabled`／`adb_wifi_enabled`／`development_settings_enabled` 與 `sys.usb.*`／`init.svc.adbd`；Java 層＋native PLT/GOT 兩層，adb 本身不受影響。**v1.1 修掉 `dlopen` 攔截破壞 App linker namespace 的 bug**（改用 bionic `__loader_*` + caller 位址，詳見報告 §3.6）。⚠ 需先裝 **NeoZygisk v2.4-289 + Vector v2.2-3080**（`tools/framework/`）。安裝與驗證見 [`reports/usbdebug_antidetect_module_2026-09-30.md`](../reports/usbdebug_antidetect_module_2026-09-30.md) |
| `debugdetectprobe-v1.0.apk` | `1.0` (code 1) | **對照組偵測器**：把 4 類 USB 偵錯偵測途徑（Settings getter／cursor 直讀／Java 屬性／native `__system_property_get`）實際讀一遍，輸出固定格式 `CHECK <向量> = <值>`，用來做遮蔽前後 diff。 |

> **目前實際安裝**：眼鏡 `1.0-safe.6`（2026-09-20，含無線 adb 偵測修正＋AdbWifiService relay＋開發模式）、手機 `1.3`。
> 舊版一律保留供 A/B 對照。

## 安裝

```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
# 眼鏡
"$ADB" -s <DEVICE_SERIAL> install -r dist/app-glasses-v1.0-safe.6.apk
# 手機
"$ADB" -s <PHONE_SERIAL> install -r dist/app-phone-v1.3.apk
```

## USB 偵錯防偵測模組（手機／任何 APatch·KernelSU 裝置）

`usbdebug-hider` 是 **LSPosed/Vector 模組**（不是普通 App），必須先有框架才有效：

```bash
# 1) 框架（各重開機一次）：NeoZygisk → Vector
"$ADB" -s <serial> push tools/framework/NeoZygisk-v2.4-289-release.zip /data/local/tmp/
"$ADB" -s <serial> shell "su -c '/data/adb/apd module install /data/local/tmp/NeoZygisk-v2.4-289-release.zip'"
"$ADB" -s <serial> reboot
"$ADB" -s <serial> push tools/framework/Vector-v2.2-3080-Release.zip /data/local/tmp/
"$ADB" -s <serial> shell "su -c '/data/adb/apd module install /data/local/tmp/Vector-v2.2-3080-Release.zip'"
"$ADB" -s <serial> reboot

# 2) 模組本體 + scope（這一段不必重開機）
./deploy/usbdebug_hider_ctl.sh install
./deploy/usbdebug_hider_ctl.sh scope-set com.example.bank   # 或 scope-all
./deploy/usbdebug_hider_ctl.sh verify
```

兩版眼鏡 APK **同 applicationId**，可直接互相覆蓋；用
`adb -s <DEVICE_SERIAL> shell dumpsys package com.rokid.rc.glasses | grep versionName`
確認目前是哪一版。

## 重新建置

```bash
cd android
./gradlew :app-glasses:assembleDebug                      # safe（預設）
./gradlew :app-glasses:assembleDebug -PsafeArming=false   # 原行為版
./gradlew :app-phone:assembleDebug
```
