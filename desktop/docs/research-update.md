# v2.0 對應的最新研究整理

> English: [research-update_EN.md](research-update_EN.md)

> 来源：`/home/kali/rokid_root`（分析日期 2026-09-11）
> 本文只記錄**與桌面板工具有關**的結論，以及每一項在 v2.0 中如何落實。

## 1. 漏洞與韌體狀態

| 項目 | 內容 |
|---|---|
| CVE | **CVE-2026-43499**（GhostLock），CWE-416 核心堆疊 use-after-free |
| 位置 | futex-PI / rt-mutex 的 `remove_waiter()` —— 清錯 `pi_blocked_on`，留下懸空的 `rt_mutex_waiter` |
| 上游修補 | mainline `3bfdc63936dd`（rtmutex: Use waiter::task instead of current in remove_waiter()） |
| 影響範圍 | 2.6.39 – 7.1 |
| **漏洞版韌體** | `1.22.009-20260710-150201`（Android 12 / SDK 32，kernel 5.10.209） |
| **已修補韌體** | `1.25.012-20260901-150201`（2026-09-01 推送） |
| 修補證據 | 反組譯確認 `remove_waiter` 出現 `ldr x20,[waiter,#0x30]`（= `waiter->task`）且**無** `mrs sp_el0` |
| 成功率 | **每次開機約 50%**；失敗 = kernel panic → 2–5 分鐘後自動重開機，KASLR slide 改變 |
| root 存續 | **僅存在於記憶體**，重開機即消失（`/data/local/tmp` 的檔案會保留） |

### ⚠ 兩個關鍵陷阱

1. **安全修補層級（SPL）不可信。** 1.25.012 的 SPL 仍是 `2024-07-05`，與漏洞版相同。
   **不能用 SPL 判斷是否已修補。**
2. **升級不可逆。** A/B 防回滾、沒有公開降級包、EDL 沒有 firehose。
   一旦升到 1.25.012 就永久失去這條漏洞。

> **v2.0 落實**：`fw_classify()` 以 `ro.build.version.incremental` 的版號
> （門檻 `1.25.012`）判定，不用 SPL；判定為已修補時「一鍵取得 root」直接拒跑。
> `LOG_OTACHECK_SPL` 也會在 UI 明講「此欄位不可靠」。

---

## 2. Root 取得途徑

| 途徑 | 前置條件 | 可靠性 | 跨重開機 | 需要 PC |
|---|---|---|---|---|
| **A. Bridge helper**（經典） | adb + exploit 已推送 | 典型 1–2 次開機 | ❌ | ✅ |
| **B. Shizuku**（App 內新預設引擎） | manager APK v13.6.0+，`pm grant` | 實測 2 分 6 秒、第 1 次嘗試成功 | ❌（每次開機需重跑） | ✅（或手機 OTG） |
| **C. Magisk**（可用但未啟用） | 解鎖 bootloader | 100% 決定性 | ✅ | 只需刷一次 |
| **D. 手機當 adb host** | termux-adb + OTG | 可用 | ❌ | ❌ |

已排除：adb-over-TCP（此 adbd 忽略 `persist.adb.tcp.port`，重啟 adbd 會**失去 USB 列舉**）、
`install-recovery.sh`（dm-verity）、由 App 直接跑 exploit（必 panic）。

> **v2.0 落實**：實作 A（主要流程）與 **B（「啟動 Shizuku」按鈕）**。
> 依 `deploy/start_shizuku.sh`：確認／安裝 manager → `pm grant
> com.rokid.rc.glasses moe.shizuku.manager.permission.API_V23` →
> 由 `pm path` 推導 `lib/arm64/libshizuku.so` → `pkill` 舊 server → 執行 →
> 以 `ps -A -o ARGS | grep -c shizuku_serve[r]` 驗證。

---

## 3. Bridge / helper 機制（與 v2.0 實作完全一致）

**裝置端路徑**

```
/data/local/tmp/ghostlock_rv101          0755  exploit
/data/local/tmp/root_daemon              0755  root 常駐服務
/data/local/tmp/root_daemon.sock         0777  UNIX socket
/data/local/tmp/gl_run.log                     exploit 輸出
/data/local/tmp/.ghostlock_root.sh             exploit 預設 payload（ready 旗標）
/data/local/tmp/ghostlock_bridge_helper.sh     helper 常駐迴圈
/data/local/tmp/ghostlock_bridge/        0777  信箱目錄
    last.cmd  last.out  last.done  last.progress  payload.staging
```

**helper 迴圈**：偵測到 `last.cmd` → 清 out/done/staging →
`echo started > last.progress` → `cp last.cmd payload.staging` → 最多 3 次嘗試：
- 跑 exploit（環境變數 `GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1`）
- 最多等 **120 秒** 出現 ready 旗標 `dropping to root shell`（`FOPS install failed` 提前放棄）
- 把 payload 覆蓋到 `.ghostlock_root.sh` → 最多等 **200 秒** 出現 `__GHOSTLOCK_PAYLOAD_DONE__`
- 成功寫 `OK` 到 `last.out`，失敗寫 `ERR:<原因>`
- 單次開機最壞情況約 **640 秒**

**payload**：`echo 0 > /sys/fs/selinux/enforce`；若 `/proc/net/unix` 沒有
`root_daemon.sock` 就 `setsid root_daemon &`；等 socket 出現後把 `id` 寫進 `last.done`。

> ⚠ **必須用 `/proc/net/unix` 判斷 daemon 是否存活**，不能用 socket *檔案* 是否存在：
> 該檔案在重開機後仍會留著，但已經沒有監聽者。

**socket 協定**（`deploy/root_daemon.c`）：AF_UNIX SOCK_STREAM，
`CMD_MAX 8192`，**一次連線 = 一條命令，讀到 EOF 為止**，交給 `/system/bin/sh -c`
執行，stdout 回寫；每條連線 fork 一個 handler，子行程關閉 fd 3..127
（避免背景化命令卡住 daemon）。

> **v2.0 落實**：`HELPER_SCRIPT` 與 `TRIGGER_SCRIPT` 常數與上述腳本逐字對應。
> `nc -U /data/local/tmp/root_daemon.sock` + stdin 餵命令，與 `deploy/run_root.sh` 相同。
> 信箱目錄 0777、檔案 0666；`last.cmd` 改為**先寫 `.tmp` 再 `mv`（原子）**。
> `pkill` 全面改用字元類別（`ghostlock_rv10[1]`、`ghostlock_bridge_helpe[r]`、`shizuku_serve[r]`）
> 以避免 `pkill -f` 自我匹配。

---

## 4. 驗證 root

```bash
printf 'id; getenforce\n' | adb shell "nc -U /data/local/tmp/root_daemon.sock"
```

預期輸出**精確**為：

```
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

次要證據：`grep -c root_daemon.sock /proc/net/unix` = `1`；
`last.done` 含 `uid=0(root)` 與 `__GHOSTLOCK_PAYLOAD_DONE__`。

> **v2.0 落實**：`do_verify()` 先確認 daemon 存活（`/proc/net/unix`）再送命令，
> 以輸出是否含 `uid=0` 判定成敗。

---

## 5. 遙測欄位

手機 App 的 `GET_DEVICE_INFO` 只有 `selinux / id / uid / rootAcquired /
ghostlockEnabled / rootMode / wifiAp`，**不含序號／型號／韌體**。PC 端的等價指令為：

```
adb devices -l
getprop ro.serialno | ro.product.model | ro.build.version.incremental
        | ro.build.version.security_patch | ro.build.version.release
        | sys.boot_completed | ro.boot.glassesWithPanel
id; getenforce
grep -c root_daemon.sock /proc/net/unix
ps -A -o ARGS | grep -c shizuku_serve[r]
cut -d' ' -f1 /proc/uptime
```

> **v2.0 落實**：合併成**一次** `adb shell`，以 `KEY=value` 標記回傳再解析，
> 避免多次往返、也避免解析歧義。

---

## 6. OTA 修補檢查邏輯

**API**：`POST https://ota.rokid.com/v1/extended/ota/check`

```
Body:         {"version":"<ro.build.version.incremental>","osType":"","cpuType":""}
Content-Type: application/json;charset=utf-8
Authorization: version=1.0;time=<unix>;sign=<md5>;key=<K>;device_type_id=<T>;device_id=<D>;service=ota

sign = MD5("key=<K>&device_type_id=<T>&device_id=<D>&service=ota&version=1.0&time=<ts>&secret=<S>")
```

`K` / `S` / `T` 來自裝置屬性 `ro.boot.key` / `ro.boot.secret` / `ro.boot.devicetypeid`，
`D` 來自 `ro.serialno`（`deploy/ota_api.py` 內有硬編預設值）。
回應欄位：`code`（`"OK"`）、`version`、`imageUrl`、`checksum`、`changelog`、`isForceUpdate`。

兩條判定路徑：

| 路徑 | 做法 | 可靠性 |
|---|---|---|
| 便宜 | 下載 OTA zip → 讀 `META-INF/com/android/metadata` 的 `post-security-patch-level` | **低**（SPL 已知會說謊） |
| 權威 | 解 `payload.bin` → dump `boot` → 解 gzip → `vmlinux-to-elf` → `nm` 找 `remove_waiter` → capstone ARM64 反組譯，看有無 `mrs sp_el0` / `ldr [x,#0x30]` | 高 |

> **v2.0 落實**：實作便宜路徑的**上層**（直接查 API 取得最新版號，再與門檻
> `1.25.012` 比對），並在 UI 明示 SPL 不可靠。**刻意不做**反組譯路徑 ——
> 那需要 DEFLATE、ELF 符號還原與 ARM64 解碼器，不適合塞進單一 Win32 程式。
> 需要權威判定時請跑 `rokid_root/deploy/ghostlock_patch_check.py`。

---

## 7. Windows 自動化的坑（v2.0 已處理）

| 坑 | 處理方式 |
|---|---|
| exploit 只能以 uid 2000 跑 | 全程走 `adb shell`（= uid 2000），不從 App 觸發 |
| 約 50% panic → 重開機（2–5 分） | 重試前等 `sys.boot_completed=1` **且** uptime > 150 秒 |
| 重開機判定 | **uptime 回退**（`/proc/uptime` 變小 > 5 秒）比「裝置離線」可靠 |
| 開機後系統忙碌 | 執行緒數（`/proc/loadavg` 第 4 欄）> 2500 時多等一輪 |
| USB 重新列舉 | `adb devices` 輪詢 + 退避；**絕不重啟 adbd**（此 build 會失去 USB 列舉） |
| 多裝置 | 所有 adb 呼叫皆帶 `-s <serial>` |
| `pkill -f` 自我匹配 | 字元類別：`ghostlock_rv10[1]` 等 |
| 讀到半截 payload | `last.cmd` 以 `.tmp` + `mv` 原子寫入 |
| App 寫不進 `/data/local/tmp`（0771） | 信箱目錄 0777、檔案 0666 |
| 殘留 socket 檔 | 只用 `/proc/net/unix` 判斷存活 |
| 每次觸發前需清理 | 清 `last.*` / `payload.staging` / `.ghostlock_root.sh` / `gl_run.log` 並殺掉殘留 exploit |
| Android 12 背景啟動限制 | `am start-foreground-service` 會失敗，需改用 `MainActivity` |
| lmkd 在 1.8 GB 裝置上殺 helper | 若 `last.progress` 卡住就重跑「② 安裝 helper」 |
| `pm disable-user` 不支援 | 改用 `pm suspend`（`pm disable-user` 回 `Failure [not support for 0]`） |

---

## 8. 其他可從 PC 做、但 v2.0 刻意未做的事

| 能力 | 為什麼沒做 |
|---|---|
| WiFi 媒體通道（SoftAP `RokidCam` / `192.168.43.1:8555`） | 與 root 流程無關；且啟用 AP 需 root，STA/SoftAP 互斥 |
| 相簿／媒體拉取（app-private 目錄） | 需要 root 且優先度低 |
| 眼鏡顯示／語系廣播（`DebugReceiver`） | 只有 `TOGGLE_PANEL` / `SET_DISPLAY` / `SET_LOCALE`，與 root 無關 |
| OTA 映像反組譯判定 | 見 §6，工程量不適合單一 Win32 程式 |
| `input tap/keyevent` 自動化 | ⚠ 在眼鏡上實測會造成整機重開機，**絕對不要用** |

---

## 9. 與 `rokid_root` 的對應關係

| 本工具 | 對應的專案腳本 |
|---|---|
| ① 推送 exploit | `adb push ghostlock_rv101` + `root_daemon` |
| ② 安裝 helper | `deploy/root_bridge.sh` 的 helper 安裝／啟動部分 |
| ③ 觸發提權 | `deploy/trigger_root_daemon.cmd` → `last.cmd` |
| 🚀 一鍵取得 root | `deploy/get_root_now.sh`（`MAX_BOOTS=6`、`TRIGGERS_PER_BOOT=2`、`ROUND_TIMEOUT_S=950`） |
| ✓ 驗證 root | `deploy/run_root.sh 'id; getenforce'` |
| 🔒 鎖定 OTA | `deploy/lock_ota.sh` |
| OTA 修補檢查 | `deploy/ota_api.py`（上層）／`deploy/ghostlock_patch_check.py`（權威版） |
| 啟動 Shizuku | `deploy/start_shizuku.sh` |
| 靜音拍攝 | `reports/media/silent_camera.ogg` + bind mount |
| 抑制隱私燈 | `reports/RV101_CAMERA_LED_FLASH_MECHANISM.md` 的 Binder + sysfs 做法 |
