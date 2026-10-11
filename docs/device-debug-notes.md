> ⚠ 本檔為除錯筆記（歷史記錄）。其中 `USE_BRIDGE` 等編譯期開關已於 2026-09-13 由
> 執行期引擎探測取代（見 `docs/deploy.md` §9.3 與 §P1）；文內相關敘述僅代表當時狀態。

# 實機除錯紀錄（Rokid RG-glasses）

> English: [device-debug-notes_EN.md](device-debug-notes_EN.md)

> 本文為歷史除錯紀錄（記載撰寫當下的狀態）。**最新架構**：Shizuku 模式（預設）+
> bridge 自動回退，見 `docs/deploy.md` §9 與 `reports/shizuku_integration_test_report.md`。

2026-08-31 實機（序列號 `<DEVICE_SERIAL>`）除錯結果。

## 設備

| 項目 | 值 |
|------|-----|
| 型號 | Rokid RG-glasses |
| Android | 12（SDK 32） |
| SELinux | **Enforcing**（開機）→ **Permissive**（exploit 成功後，payload 寫 `/sys/fs/selinux/enforce`） |
| 相機 | Qualcomm CHI，camera id 0，Back，`maxNumOutputStreams=3` |
| 手機 | Redmi 21091116UG（Android 13，SDK 33） |

## exploit（ghostlock_rv101）契約

- `/data/local/tmp` = `drwxrwx--x shell shell`（0771）；`ghostlock_rv101` = `0755`。
- 目錄鏈 `/data`(0771)→`/data/local`(0751)→`/data/local/tmp`(0771) 對 others 皆只有 `x`。
- **結論**：App（`u:r:untrusted_app`）可 traverse + read/exec 二進位，但**寫不了 `.ghostlock_root.sh`**（目錄對 others 無 `w`）。
- 預設腳本 `.ghostlock_root.sh`（exploit 寫出）內含 ready 標記 `echo '[+] dropping to root shell'`，並 `cp ghostlock_rv101 su_gl`、`exec /system/bin/sh`。
- **時序**（實測）：exploit 啟動後 **~2s** 就寫出預設腳本（ready 標記）；之後跑 ~100–142s 的核心利用；最後以 root spawn `.ghostlock_root.sh`。→ 置換窗口約 100s，充裕。
- **flaky**：FOPS 安裝常失敗（`FOPS install failed after 3 attempts`），且約半數機率會讓 kernel reboot。需同 boot 重試（orchestrator 用 3 次）+ reboot 重試。

## 觸發層結論

- **直連版不可用**：App 寫不了 `/data/local/tmp`。
- **橋接版可用且已實證**：`BridgeGhostlockEngine` + `deploy/root_bridge.sh` 的 shell helper 可完成「跑 exploit → ready 偵測 → 置換 payload → 等 DONE」。實測 payload 以 `uid=0(root) context=u:r:kernel:s0` 執行成功。

## 橋接 helper 修正

1. 置換改用 `cp`（保留 `last.cmd` 供重試），非 `mv`。
2. 每次跑 exploit 前 `rm -f .ghostlock_root.sh`（避免誤抓上次殘留 ready 標記）。
3. 加入 **3 次重試** + `FOPS install failed` 快速偵測。
4. done 輪詢拉長到 200s（exploit 完成時間會飄）。
5. 佈署冪等：`pkill` 舊 helper 再 `setsid` 啟動單一實例。
6. App `BridgeConfig.timeoutMs` → 500s（涵蓋重試）。

## 相機修正

- Rokid CHI HAL 拒絕「多 use case 同綁」（`No supported surface combination`，含 CameraX 的 StreamSharing）。
- 解法：**單一 use case 綁定**——拍照只綁 ImageCapture，錄影 rebind 為 VideoCapture，錄完切回。
- `bindToLifecycle`/`unbindAll` 必須在主執行緒 → rebind 用 `withContext(Dispatchers.Main)`。

## 已驗證（歷史：早期 WiFi/TCP 版，現已改藍牙）

- （歷史）TCP server 監聽 8555、HELLO/STATUS/LIST_ALBUMS/LIST_MEDIA 正常。
- 拍照：`IMG_*.jpg` 4032×3024（12MP）、5.4MB。
- 錄影：`VID_*.mp4` 1280×720、2.7s、2.3MB。
- 相簿索引：Camera 相簿正確累計 mediaCount。
- 橋接 root payload：`id` 回傳 `uid=0(root)`。

## 操作模型

- 橋接 helper 是 `setsid` 常駐，但 **device reboot 後會消失**，需重跑 `deploy/root_bridge.sh`。
- （歷史）手機↔眼鏡原本走 WiFi；現已改為藍牙 RFCOMM（見下方「通訊改為藍牙」）。
- ~~App 目前 `AppGraph.USE_BRIDGE = true`~~（**已過時**：現由 `AutoGhostlockEngine` 執行期探測，無此開關）。

## 待驗證

- ✅ **LED 壓制已實證**：聚焦測試中 suppress payload 以 root 執行後，回讀 `suppressed brightness=0` + `__GHOSTLOCK_PAYLOAD_DONE__`——LED 亮度確定被寫為 0（熄滅）。
- ✅ **ghostlock=ON 拍照已實證**：完整 `SET_GHOSTLOCK(true)` + `CAPTURE_PHOTO` 收到 `PHOTO_TAKEN`（12MP JPEG）。
- ⚠️ **restore 仍 flaky**：第二次 exploit（restore）期間 helper 常被 OOM/lmkd 殺掉，LED 會停留在壓制狀態；reboot 可復原。
- ✅ 已解決：改走藍牙 RFCOMM，手機 App 完整遙控已實證（見下方「通訊改為藍牙」與「root daemon 常駐化驗證」）。
- ⚠️ exploit 本身 ~50% 機率 kernel reboot，需重試（orchestrator 式）。

## 權限修復（重要）

- App 寫 `last.cmd` 預設為 `0600`，helper(shell) 讀不到 → **app→helper 橋接原本是斷的**。
- 修正：`BridgeGhostlockEngine` 寫 `last.cmd` 設 0666；helper 寫 `last.out` 後 `chmod 666`。

## 通訊改為藍牙（2026-09）

- 指令與照片/影片**皆走 Bluetooth Classic RFCOMM**（SPP UUID `00001101-...`），WiFi/TCP 已移除。
- 眼鏡端 `BluetoothServer`（RKFCOMM server）；手機端 `BluetoothClient`（client + request/requestFile）。
- MIUI 手機：shell 的 `input` 注入被擋、隱式廣播被擋 → 自動化改用**顯式廣播**
  `am broadcast -n com.rokid.rc.phone/.DebugReceiver --es cmd SET_LED ...`。

## root daemon 常駐化驗證（2026-09-02，單次提權 → socket 秒級 LED）

### 目標與結論

把「每次指令觸發 LPE」改成「**單次提權 → 常駐 root daemon → UNIX socket 即時執行**」，
socket 消失才回退 bridge/LPE。**已實機驗證通過**。

### 驗證結果（冷啟動全流程）

| 項目 | 結果 |
|------|------|
| 提權 | 單次，`uid=0(root) context=u:r:kernel:s0`，`getenforce Permissive` |
| daemon | `root_daemon` PID root、PPid 1、`srwxrwxrwx` socket 且列於 `/proc/net/unix` |
| socket 單指令延遲 | **39–48ms**（含 nc + daemon `sh -c` spawn） |
| hold-loop（白燈壓制背景迴圈） | 往返 **306ms**，之後 daemon 仍正常回應 |
| LED | 綠燈亮=255、白燈熄=0，readback 正確 |
| LPE 重跑 | **0**：LED 指令全程走 socket，無 `ghostlock_rv101` 重新觸發、無 OOM/reboot |
| 手機端連動 | `recv: SET_LED` → `LedSuppressor` → `RootDaemonClient` → socket → LED，即時生效 |

### 修復的 bug（root_daemon.c 與 LedPayloads.kt）

1. **單執行緒 + `popen` 管線卡死**：指令背景化子行程（hold-loop）繼承 popen pipe 寫端 →
   `fgets` 永不 EOF，整個 daemon 停止 accept。
   → 修法：每條連線 fork handler；stdout/stderr 導**暫存檔**（非 pipe）；`waitpid` 只等 `sh -c`。
2. **client socket 被背景子行程繼承**：hold-loop 持有連線 `c` → App `readText()` 永不返回。
   → 修法：sh -c child `close(fd 3..127)`；`main()` 開頭也 `close(fd 3..127)` 防禦。
3. **冷啟動 stale socket 誤判**：reboot 後 `/data/local/tmp/root_daemon.sock`「檔案」殘留但無
   listener，`startRootDaemon` 的 `[ ! -S ... ]` 會誤判而跳過啟動。
   → 修法：改用 `grep -q root_daemon.sock /proc/net/unix` 判斷「活著」的 socket（`/proc/net/unix`
   只列有 listener 者），無存活才 `rm -f` + 啟動。

### Q5：persist.adb.tcp.port 5555（持久化 adb-over-tcp 可行性）

- 以 root `setprop persist.adb.tcp.port 5555` → **重啟後 `getprop` 仍為 5555**（persist 屬性可持久化）。
- 但 **adbd 重啟後並未綁定 TCP 5555**（`/proc/net/tcp` 只有 0.0.0.0:8341 的 vendor listener）。
- 結論：此 Rokid build 的 adbd 不採納 `persist.adb.tcp.port`，該「持久 adb-over-tcp」技巧**不可行**
  （需 `service.adb.tcp.port` + adbd restart，而 service.* 重啟即失）。已於驗證後清除殘留屬性。

### 操作模型（更新）

- 提權後的 root daemon、helper 都是記憶體常駐，**device reboot 後消失**，需重跑：
  `deploy/root_bridge.sh`（重啟 helper）→ 觸發一次 `ACQUIRE_ROOT`（或手動 push
  `deploy/trigger_root_daemon.cmd` 到 `ghostlock_bridge/last.cmd`）。
- 每次提權成功會殘留一個 idle 的 `ghostlock_rv101` root 行程（exploit 自身的 root child），
  可用 socket `kill -9 <pid>` 清除。

## OTA 版本檢查 API：簽名破解與「查新版本 → 下載 → 比對 SPL」一條龍（2026-09）

### 緣起

要判斷 Rokid 新韌體是否修補 GhostLock，需拿到最新 OTA 的 `post-security-patch-level`。
OTA app（`com.rokid.glass.ota`）的版本檢查 API `ota.rokid.com/v1/extended/ota/check`
原本需要「裝置認證簽名」，直接呼叫只會回 `C100401 认证不通过`。

### Frida hook 過程（真機）

- frida-server 17.17.0 部署到眼鏡（root），經 WSL2→Windows relay + adb forward 連線。
- 掛 `com.rokid.glass.ota`，直接呼叫 `CheckService.checkNewOtaVersion()` 觸發檢查。
- 抓到請求 URL `https://ota.rokid.com/v1/extended/ota/check` 與 logcat 中的請求參數：
  `version=1.0;time=<ts>;sign=<md5>;key=...;device_type_id=...;device_id=...;service=ota`。
- 發現正確的 broadcast extra key 是 `normal_cmd`（不是 `cmd`/`NORMAL_CMD_PARAM`）。
- **重要**：眼鏡端**無網路**（`UnknownHostException`），所以 hook 拿不到回應；回應需從**主機**打。

### 簽名破解（jadx 反編譯 CheckService）

`CheckService.generateSignature()`：
```java
sign = MD5("key="+key+"&device_type_id="+deviceTypeId+"&device_id="+deviceId
         +"&service="+service+"&version="+version+"&time="+time+"&secret="+secret)
Authorization = "version="+version+";time="+time+";sign="+sign
              +";key="+key+";device_type_id="+deviceTypeId+";device_id="+deviceId+";service="+service
Body = {"version": <systemVersion>, "osType": "", "cpuType": ""}
```
參數來源（`DeviceProp.init()` 讀 system prop）：
| 欄位 | 來源 | 值 |
|------|------|-----|
| key | `ro.boot.key` | `<REDACTED_OTA_KEY>` |
| secret | `ro.boot.secret` | `<REDACTED_OTA_SECRET>` |
| deviceTypeId | `ro.boot.devicetypeid` | `<REDACTED_OTA_DEVICETYPEID>` |
| deviceId | `ro.serialno` | `<DEVICE_SERIAL>` |
| version | 硬編碼 | `1.0` |
| service | 硬編碼 | `ota` |

已用已知 `time=1788364983` 驗證：算出 MD5 = `99d904b1e9e94d1a85d2a3d3fda51611`，與 logcat 一致 ✅。

### 查詢結果（2026-09-03）

- 裝置目前：`1.22.009-20260710-150201`（SPL `2024-07-05`）。
- **最新版本：`1.25.012-20260901-150201`**（完整包 ~1GB，MD5 `bec406d9e28ee779d8e876e57ba69f51`）。
- 最新包 metadata：`post-security-patch-level = 2024-07-05` —— **與舊版完全相同，沒變**。

### 結論（先看 SPL 的誤判）

- **SPL 不可靠**：1.25.012 的 `post-security-patch-level` 仍是 `2024-07-05`（與 1.22.009 相同），
  看似「沒修補」。但 SPL 只反映 Android 資安通報等級，**不代表 kernel 沒 backport 個別 CVE 修補**。
- 實測（見下節）：**1.25.012 的 kernel 已把 CVE-2026-43499（GhostLock）修補 backport 進去**。

### kernel 層驗證：反組譯 remove_waiter（重要）

GhostLock 本體是 `kernel/locking/rtmutex.c` 的 `remove_waiter()` UAF，官方修補
（commit `3bfdc63`「rtmutex: Use waiter::task instead of current in remove_waiter()」）
把 `current` 改成 `waiter->task`。

- 用 `payload-dumper-go` 解 `payload.bin` → `boot.img` → `Image`，`vmlinux-to-elf` 還原符號，
  `capstone` 反組譯 `remove_waiter`。
- 判斷基準（ARM64）：
  - `mrs x?, sp_el0`（存取 `current`）→ **漏洞版**。
  - `ldr x?, [waiter, #0x30]`（載入 `waiter->task`）→ **修補版**。
  - ⚠ **2026-09-16 更正**：不可只搜尋 `#0x30`。兩版在 `remove_waiter()+0x160` 都有一處
    `ldr x14, [x9, #0x30]`（基底 `x9`，與 `waiter->task` 無關）→ 只看它會誤判「已修補」。
    正確判準是看 `+0x040` 載入的 `x20` 餵給誰（`+0x044` pi_lock／`+0x088` pi_blocked_on／
    `+0x210` task 參數）。
- **1.25.012 實測**：`remove_waiter` 內 `x20 = ldr [waiter,#0x30]`（= `waiter->task`），
  `raw_spin_lock(&x20->pi_lock)`、`str xzr,[x20,#0x888]`（`pi_blocked_on=NULL`）、
  `rt_mutex_adjust_prio_chain(..., x5=x20)` —— 全程 `waiter->task`、無 `mrs sp_el0` → **已修補**。
- 對照：裝置目前 1.22.009 是漏洞版（`ghostlock_rv101` 多次成功提權）。升級 1.25.012 後
  `ghostlock_rv101`（CVE-2026-43499 exploit）應會失效。

**修正後結論：Rokid 1.25.012（2026-09-01）已修補 GhostLock（CVE-2026-43499）；SPL 不可靠，
需反組譯 `remove_waiter` 才能確認。**

**2026-09-16 獨立複核（兩版同套工具鏈、逐位元組）**：`remove_waiter()` 在 1.22.009 與 1.25.012
皆為 `0x28c`（163 條指令），機器碼**只有 `+0x040` 一條 4-byte 指令不同**
（`mrs x20, sp_el0` → `ldr x20, [x23, #0x30]`），其餘 162 條（含所有呼叫目標）完全相同。
工具：`scripts/ota_kernel_diff.py`；兩版 OTA 雜湊：1.22.009 SHA-256 `163ab205…c94e5`（與留存清單一致）、
1.25.012 MD5 `bec406d9…f51`（與原廠 OTA metadata 一致）。

### 工具

- `deploy/check_ota.sh`（預設）＝一條龍：讀裝置 + 已破解簽名查最新 → 下載 → 比對 SPL。
- `deploy/ota_api.py`：獨立 Python 版查詢工具（環境變數 `KEY/SECRET/DTID/DID/SYSTEM_VERSION` 可覆寫）。
- `deploy/ghostlock_patch_check.py`：**查最新 → 解 payload.bin → boot.img → Image → 反組譯
  `remove_waiter` → 比對官方 patch → 固定格式報告**（`ghostlock_patch_report.txt`）。
- 反編譯源碼：`/tmp/rokid_ota/jadx_out/`（`CheckService.java`、`DeviceProp.java`、`MD5Utils.java`）。

## 手機串接眼鏡提權的穩定性修正 + 「免 adb」可行性結論（2026-09）

### 提權「當機」根因與修正

「手機 App → 眼鏡 App → 提權」會當機的主因是：**每次按取得 root 都重跑一次 exploit
（ghostlock_rv101 有 ~50% 機率 kernel reboot）**，加上 Rokid 螢幕 5 秒休眠把 app/服務殺掉。

修正（已建置安裝；**現為執行期自動探測引擎**，不需此開關）：

1. **已 root 就跳過重跑 exploit**：`RootDaemonClient.isRoot()`（連 socket 跑 `id` 確認 uid=0），
   `BridgeGhostlockEngine.acquireRoot()` / `ShellGhostlockEngine.acquireRoot()` 先查，已 root 直接回成功。
2. **提權期間螢幕常亮**：`CaptureService` 加 `SCREEN_BRIGHT_WAKE_LOCK`，`MainActivity` 加
   `FLAG_KEEP_SCREEN_ON`。
3. **root 模式持久化**：`AppGraph.rootMode` 存 SharedPreferences（`setRootMode()`），手機設 AUTO
   重啟後仍保留。
4. **開機自啟 app**：新增 `BootReceiver`（`BOOT_COMPLETED`）自動啟動 `CaptureService`。

### 操作模型（維持現況）

- **同一 boot 內免 adb**：helper 已由 `deploy/root_bridge.sh` 常駐 → 手機連眼鏡 → 觸發
  `ACQUIRE_ROOT`（或 AUTO）→ 眼鏡 App 寫 `last.cmd` → helper 跑 exploit → root。全程不需桌機 adb。
- **跨 reboot 仍要一次 adb**：helper 是記憶體常駐，reboot 後消失，需 `deploy/root_bridge.sh`
  重跑一次（外加眼鏡 App 已裝好、`ghostlock_rv101`/`root_daemon` 已在 `/data/local/tmp`）。

### 「helper 開機自啟 / 完全免 adb」為什麼做不到（已實測）

1. **傳統 `install-recovery.sh` 掛鉤被 dm-verity 擋住**：唯一掛鉤
   `/vendor/etc/init/vendor_flash_recovery.rc` → `/vendor/bin/install-recovery.sh`，但 `/vendor`
   是 dm-verity 唯讀（`mount -o remount,rw` 回 read-only）。`/system`、`/product`、`/odm` 也都唯讀。
2. **無 Magisk**：`/data/adb/` 是空的，沒有 `service.d/`。`magisk_patched-30600_9C56p.img`
   只是 patch 過的 boot.img，未真正刷入。
3. **symlink trick（App 直連提權，不靠 helper）失敗**：把 `.ghostlock_root.sh` 設 symlink →
   777 目錄後，App 確實能寫 payload（`setup_direct.sh` 實測 target 變 `u0_a53` 830B），
   但 **exploit 以 `untrusted_app`(uid 10053) 執行會 kernel panic 重開機**——它需要
   `shell`(uid 2000) 的權限（`perf_event_open` 等）。

### 唯一能達成「完全免 adb」的路徑（未採用）

刷入 Magisk（用既有的 `magisk_patched-30600_9C56p.img` `dd` 到 boot 分區 `mmcblk0p17/18`），
之後用 `/data/adb/service.d/` 放開機腳本啟動 helper。有磚機風險，需使用者確認才做。

---

## 2026-09-10 / 09-11 後續實測附錄

> 本節之後的內容為 App 產品化階段的實測補充；上方 §1–§末為歷史紀錄，保留原貌。

### A. Shizuku 路徑成為預設（已實機驗證）

- Shizuku server（adb 啟動）→ UserService 身份 = `uid 2000` + `u:r:shell:s0`（等同 adb shell），
  可直接跑 `ghostlock_rv101`。實測 `auto acquireRoot: success=true`（最快一次 9 秒）。
- 詳見 `docs/shizuku-feasibility-report.md`、`reports/shizuku_integration_test_report.md`。

### B. WiFi 媒體通道（SoftAP + TCP 8555）

- 眼鏡提權後 `cmd wifi start-softap RokidCam wpa2 rokid1234` → `wlan2` = `192.168.43.1`；
  `WifiServer` 監聽 TCP 8555，與藍牙**共用同一套 `ConnectionHandler`/`CommandRouter`**。
- 手機端 `TcpClient`（WiFi）＋ `BluetoothClient`（回退）：實測縮圖約 1.2s、5.4MB 照片下載成功；
  藍牙回退約 63KB/s。詳見 `reports/wifi_transfer_test_report.md`。
- 本裝置韌體 **STA 與 SoftAP 互斥**：開熱點踢掉 STA，`cmd wifi stop-softap` 後 STA 自動恢復。

### C. 隱私燈（錄影閃爍）根治

- 點亮源是 `lights_ctrl` 的 camera-open 事件（Binder `sendEvent(2,2014)`），純寫 sysfs 會互搶 →
  錄影閃爍。壓制改為 `cancelEvent(2,2014)`（Binder 層）＋ suppress 移到 `bindVideo()` 之前；
  實測錄影 20 秒 × 100ms 採樣 **200/200 全為 0**。詳見 `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` §9。

### D. 無線 adb（adb-over-TCP）**可行** —— 本節原結論已於 2026-09-20 推翻

> ⚠ **更正**：以下原本的「不可行」判斷是**偵測方法的 bug**，不是平台限制。
> 眼鏡 adbd **確實綁 TCP 5555，位置是 IPv6 wildcard `[::]`**（只在 `/proc/net/tcp6`），
> 舊偵測碼只 grep IPv4 的 `/proc/net/tcp` → 一律數到 0。
> 2026-09-20 端到端實測：PC 加入 SoftAP 後 `adb connect 192.168.43.1:5555` → `device`，
> `adb shell id` → `uid=2000(shell) context=u:r:shell:s0`。
> 完整證據：[`reports/wireless_adb_feasibility_2026-09-20.md`](../reports/wireless_adb_feasibility_2026-09-20.md)；
> 文件修正：`docs/deploy.md` §9.8／§P3、`reports/FACTS.md` R7。

- 原紀錄：root 後 `service.adb.tcp.port 5555` + **重啟 adbd** → TCP 5555 拒絕連線，
  且 **USB 列舉丟失**。**其中「USB 列舉丟失」是重啟 adbd 本身的副作用**（本機 adbd 由
  vendor 方式啟動並自拉 USB FunctionFS），與 TCP 綁定無關；**正確做法是不要重啟 adbd**。
- `ENABLE_WIFI_ADB` 已改為**非破壞性**（只設屬性＋偵測、不重啟 adbd）——這個決定是對的，
  但**偵測邏輯**需同時查 IPv4 與 IPv6（已於 `1.0-safe.4` 修正，ACK 另回 `boundOn`）。
- **免線 root 不再只剩 Magisk**：無線 adb 已通，取得 root 後設
  `persist.adb.tcp.port 5555`（本機早已是 5555）即可在**每次開機**自動監聽 TCP，
  重新武裝提權（`deploy/root_bridge.sh`）不必再插 USB；唯一限制是**新主機首次授權**
  需以 root 把公鑰寫入 `/data/misc/adb/adb_keys`（眼鏡無螢幕可彈窗）。

### F. 拍照／錄影提示音壓制（靜音拍攝，2026-09-11）

- **聲音來源**：`libcameraservice.so` 自行播放 `/product/media/audio/ui/` 下的
  `camera_click.ogg`（拍照）、`VideoRecord.ogg`／`VideoStop.ogg`（錄影）、`camera_focus.ogg`（對焦），
  另有 Rokid 自訂 `rkd_record_start.ogg`／`rkd_record_end.ogg`（共 6 檔）。
  `strings /system/lib64/libcameraservice.so | grep -E 'camera_click|VideoRecord'` 可重現；
  本機 `getprop` 未設 `ro.camera.sound.forced`（即使其他生產號設 1，檔案層覆蓋仍有效）。
- **壓制方法**：`/product` 唯讀（dm-verity）→ root 以 `mount --bind` 把合法靜音 OGG
  （ffmpeg 產生，Ogg Vorbis mono 44.1kHz 0.3s / 3584 B）覆蓋該 6 檔；播放端讀到靜音內容即無聲。
  `umount` 還原、**reboot 自動清除**。
- **實測**：壓制後 `camera_click.ogg` md5 `a3fc812e…` → `d7d0ebd6…`（= 靜音檔），掛載數 6；
  靜音狀態下拍照/錄影/停止皆正常（`result=true`）、無 `MediaPlayer` 錯誤；umount 後 md5 復原、殘留 0。
- **App 整合**：眼鏡端 `SilentMode`／`SilentPayloads`（提權後自動套用，預設開啟）、
  協定 `SET_SILENT`、手機端「進階」卡開關。詳見 `reports/silent_capture_mechanism.md`。

### E. 其他實測修正

- `am start-foreground-service` 在 Android 12 會被背景啟動限制擋下（`app is in background`）→
  改由 `am start -n .MainActivity`（前景啟動 FGS）。
- `RootDaemonClient.exec` 遇 reboot 後 stale socket（`Connection refused`）曾導致 App FATAL →
  已改為永不拋出（失敗回空字串）。
- 眼鏡上 `input` 注入（tap/keyevent）風險高：實測曾造成整機重啟；自動化一律改用
  `DebugReceiver` 廣播（`com.rokid.rc.glasses.DEBUG`）。
- 2026-09-11：Shizuku manager 曾從裝置消失（原因未明，`/data` 其他資料完好），重新 `adb install` 後
  提權流程恢復正常。
