# Shizuku × Rokid RV101：眼鏡端 App 整合 GhostLock 提權之可行性報告

> English: [shizuku-feasibility-report_EN.md](shizuku-feasibility-report_EN.md)

> 日期：2026-09-09（定稿）
> 適用設備：Rokid RG-glasses（RV101，serial <DEVICE_SERIAL>），Android 12，kernel 5.10.209-perf，韌體 1.22.009（GhostLock 漏洞版）
> 用途：自有/授權設備之安全研究。
> ✅ **已實機驗證**：本報告的方案已全數開發並通過真機測試（E2E 提權、fallback、手機連動），
> 見 `reports/shizuku_integration_test_report.md`。

## 0. TL;DR（結論）

**適合，但定位是「App 側執行通道」，不是「自舉/持久化方案」。**

1. **核心鏈實證可行**：Shizuku server 以 adb shell 啟動後，App 經 Shizuku（UserService，shell uid 2000 + `u:r:shell:s0`）執行 `ghostlock_rv101`，身份**完全等同 adb shell** → 滿足 exploit 必須 shell 起跑、不可用 App uid（會 kernel panic）的硬需求。
2. **不能免 adb**：Shizuku server 本身每 reboot 要一次 adb/shell 啟動（Android 12 的 adb 模式無開機自啟）；完全免 adb 只有 Magisk（§5 順位 1）。
3. **整合成本低**：`GhostlockEngine` 介面現成、加一個 Shizuku 引擎即可；api 13.1.5 走 `bindUserService` + 自訂 AIDL（aak 模式）；預授權可用 `pm grant moe.shizuku.manager.permission.API_V23` 或直寫 `shizuku.json` 跳過 UI。
4. **OOM 風險可控**：server 是 shell/root 類別行程，lmkd 風險低於 App；加 oom_score_adj 保護 + binder-dead 監聽。
5. **當務之急不是 Shizuku 而是鎖 OTA**：升級 1.25.012 會不可逆修補 GhostLock（`deploy/lock_ota.sh` 已備好）。
6. 建議路徑：**鎖 OTA → 手機 adb host（先）→ Shizuku 引擎整合 → 必要時 Magisk（終極）**。

## 1. 現行架構（基線，已實證）

| 環節 | 現況 |
|------|------|
| exploit 執行身份 | 只能 **shell (uid 2000)**；App（untrusted_app uid 10053）直跑會 kernel panic（需 `perf_event_open` 等 shell 權限）|
| 觸發方式 | host adb 啟動 shell helper `/data/local/tmp/ghostlock_bridge_helper.sh`（`deploy/root_bridge.sh` 佈署，setsid 常駐）|
| App↔helper 通訊 | `/data/local/tmp/ghostlock_bridge/`（777）：`last.cmd`（App 寫 payload）/ `last.out` / `last.progress` |
| exploit 行為 | `ghostlock_rv101`（帶 env `GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1`）啟動 ~2s 後寫出 `.ghostlock_root.sh`（含 ready 標記 `dropping to root shell`），100–142s 後以 root 執行該腳本 |
| payload | helper 以 `cp` 原子置換 `.ghostlock_root.sh`；payload（`LedPayloads.startRootDaemon`）關 SELinux（`echo 0 > /sys/fs/selinux/enforce`）→ 啟動 `root_daemon`（UNIX socket 0777）→ App 走 socket 以 root 跑指令 |
| 提權後身份 | `uid=0(root) gid=0(root) context=u:r:kernel:s0` + Permissive |
| 持久性 | root/helper/daemon 皆記憶體常駐，**reboot 全失**；跨 reboot 需一次 host adb 重跑 `root_bridge.sh` |
| 已知痛點 | exploit ~50% 機率 kernel panic→reboot；helper 偶被 lmkd/OOM 殺；App 寫不了 `/data/local/tmp`（0771）|
| 已否決路徑 | symlink trick（`setup_direct.sh`）解決 payload 寫入，但 exploit 啟動身份無解；install-recovery.sh 掛鉤被 dm-verity 擋；`persist.adb.tcp.port` 對本 build 無效 |

## 2. Shizuku 在此設備上的角色

Shizuku = 「讓一般 App 透過 binder 使用 **adb/root 等級身份**」的框架。對本專案的價值是：
以 shell 身份執行 exploit 的**通道**——把 helper 迴圈（shell 腳本 + 檔案信箱）換成 App 內直呼 Shizuku API。
它不是 exploit 本身，也**不會自己產生 root**；Shizuku server 仍需 adb 或 root 啟動。

## 3. 可行性分析

### 3.1 平台相容性（已確認）
- Android 12 / SDK 32 ✓（Shizuku 支援 API 23+）；aarch64 ✓；無 Google Play → sideload manager APK。
- Maven Central 最新穩定版：`dev.rikka.shizuku:api:13.1.5`、`dev.rikka.shizuku:provider:13.1.5`。
- app-glasses：minSdk 26 / targetSdk 34 / Kotlin+coroutines，加依賴即可。

### 3.2 執行身份（決定性，已從源碼證實，詳見 `research/shizuku/shizuku_internals.md` §2）
- Server 身份 = 啟動者身份（`OsUtils.getuid()` + `SELinux.getContext()` 快取）。
- `newProcess`（server 端 `Service.java:306-323` = `Runtime.exec`）與 rish（native fork+exec）的子程序**繼承 server 的 uid + SELinux context，無 setuid/setcon**。
- UserService 同樣是 server 進程內 `Runtime.exec("sh")` 啟動、無 setuid（`--uid` 只算 userId）——**同為 server 身份**。

| 啟動路徑 | server 身份 | 子程序（跑 exploit）身份 |
|---------|------------|------------------------|
| **adb shell 啟動** | uid 2000, `u:r:shell:s0` | **uid 2000, `u:r:shell:s0` = 完全等同 adb shell ✅** |
| root 啟動（本裝置） | uid 0, `u:r:kernel:s0` | uid 0, kernel context ❌（exploit 要 unprivileged 起跑） |

- **結論**：要跑 `ghostlock_rv101`，Shizuku server **必須走 adb/shell 啟動**；root 啟動的 server 適合跑 root 指令，不適合跑 exploit。

### 3.3 Server 啟動路徑（v13.6.0，已確認）
- **v13.6.0 起沒有 start.sh**（舊文件過時）：啟動器是 manager APK 內的原生 `libshizuku.so`。
  指令 = `adb shell <nativeLibraryDir>/libshizuku.so`（starter.cpp 檢查 uid 必須是 0 或 2000；root 版加 `--apk=`）。
  nativeLibraryDir 可由 `pm path moe.shizuku.privileged.api` 推導。
- Server 是裸 `app_process --nice-name=shizuku_server`，**不跨 reboot**；被殺無 watchdog、無 adb/root 不能自癒。
- 開機自動重啟：root 模式可在 BOOT_COMPLETED 自動 `su -c`；adb 模式自動重啟僅 Android 13+ 且需 WRITE_SECURE_SETTINGS → **本裝置（Android 12）adb 模式每次 reboot 要手動一次**。
- manager APK 必須安裝（server class 打包在內，`moe.shizuku.privileged.api`，minSdk 24，無 GMS 依賴，sideload 即可）。

### 3.4 授權 UX 與預授權（已確認，可行性高）
- v13 授權不是 SQLite、**不是 binder token**：持久化在 **`/data/user_de/0/com.android.shell/shizuku.json`**（`ShizukuConfigManager.java:42`）：
  `{"version":2,"packages":[{"uid":10053,"flags":2,"packages":["com.rokid.rc.glasses"]}]}`（FLAG_ALLOWED=2）。
  server 重啟不失效；client 授權由 config 重建。
- **免 UI 預授權兩條路（皆可用 root_daemon 做）**：
  1. `pm grant com.rokid.rc.glasses moe.shizuku.manager.permission.API_V23`（該權限 protectionLevel=dangerous，`pm grant` 可用；server 啟動時會依 runtime permission 狀態寫回 config）；
  2. 直接寫 shizuku.json（**先寫檔再啟動 server**，避免 server 延遲 10s 的寫入覆蓋）。
- 正常 UI 流程（manager 彈確認框點允許）也可用，眼鏡 UI 可操作時只痛一次。

### 3.5 OOM/lmkd 風險（已確認）
- Server 以 shell/root 身份跑、不屬 app 的 oom_adj 類別 → **lmkd 獵殺風險低於一般 App，但非零**；root 啟動時 starter 會切 cgroup（`switch_cgroup()`），adb 啟動不會。
- 無前台服務機制（裸 app_process）。緩解：root 下 `echo -1000 > /proc/<pid>/oom_score_adj`；App 掛 binder-dead 監聽 + 重拉；長駐 shell ELF 不要綁死 App binder 生命週期（owner binder 死時 `RemoteProcessHolder` 會 destroy 子程序）。
- 官方已知 reboot 後消失（issue #363）、部分 OEM 殺 UserService（issue #2048）——與我們 helper 被 lmkd 殺是同類問題，無新增風險。

### 3.6 ghostlock-apk 參考實作複用評估（已完成，詳見 `research/shizuku/ghostlock_apk_analysis.md`）
- **`oopnv70-lab/ghostlock-apk` 本身不可用**：AI 生成、STRUCTURAL_NOTICE 自承 12 次 CI 全敗、已歸檔；無 exploit 二進位、無 offsets、無 LICENSE（默認版權保留，不可複製）；Shizuku 層缺 `provider` 依賴 + manifest 未宣告 `ShizukuProvider` → 拿不到 binder。
- **核心鏈確認可行**：Shizuku 以 adb 啟動時，UserService 跑在 server 進程內、以 **shell uid 2000** 執行 `sh -c` —— 正是 ghostlock_rv101 需要的身份。
- **最佳參考 = 同作者 `ghostlock-aak-apk`（GPL-3.0）**：正確的 ShizukuProvider 設定、`writeFile(byte[])`（shell 身份寫檔 + chmod）、`runCommandStream`（oneway 流式回呼、雙線程並行讀 stdout/stderr 防死鎖）。本地複本在 `research/shizuku/`。
- **與 RV101 差異（要改 8 處）**：換 ghostlock_rv101 ELF、補 provider 依賴、加 env 傳遞（`GL_SETUP_ATTEMPTS`）、流式輸出 + 長超時（100–150s）、並行讀 stdout/stderr、**維持「等 ready → 置換 `.ghostlock_root.sh`」順序**（⚠ aak 的「先寫 payload」不適用：我們的 exploit 啟動 ~2s 會寫出自己的預設腳本，先寫會被覆蓋）、加 kernel-panic/重啟偵測（比對 boot 時間戳）、錯誤重試。
- **授權**：aak 是 GPL-3.0 → 若合併並分發需整包 GPL；建議以 aak 為骨架**自行重寫**執行層（本來就要大改），並標注 oopnv70-lab 出處。僅內部研究、不分發時 copyleft 義務通常不觸發。

## 4. 整合設計（定稿）

### 4.1 Gradle / Manifest（已核對版本事實）
- `implementation("dev.rikka.shizuku:api:13.1.5")` + `implementation("dev.rikka.shizuku:provider:13.1.5")`（Maven Central；api=MIT、Shizuku 主體=Apache-2.0，可嵌入）。
- Manifest：`<provider android:name="rikka.shizuku.ShizukuProvider" android:authorities="${applicationId}.shizuku" android:exported="true" android:multiprocess="false" android:permission="android.permission.INTERACT_ACROSS_USERS_FULL"/>` + `<uses-permission android:name="moe.shizuku.manager.permission.API_V23"/>`；`ShizukuProvider.enableMultiProcessSupport(true)`。
- ⚠ **api 13.1.5 的 `Shizuku.newProcess` 已是 private+deprecated**（Maven sources jar 實證），公開通道是 `Shizuku.bindUserService(UserServiceArgs, ServiceConnection)` → **走 UserService + 自訂 AIDL**。

### 4.2 執行鏈設計（aak 模式，自行重寫）
- App 內宣告 UserService（自訂 AIDL：`runCommandStream(cmd, env, oneway callback)` + `writeFile(path, byte[])`），`bindUserService(UserServiceArgs(component).daemon(true).version(1))`。
- UserService 在 **shell uid 2000（adb-started server）** 進程內 `Runtime.exec(["sh","-c",cmd], env)`：雙線程並行讀 stdout/stderr、oneway 流式回呼（防死鎖）、不設會砍行程的硬 timeout。
- 狀態機沿用現有 Bridge/Shell 引擎（PREFLIGHT/LAUNCH/READY/INSTALL/DONE）：
  LAUNCH（UserService 跑 `GL_SETUP_ATTEMPTS=1 … ghostlock_rv101`）→ READY（輪詢 ready 標記）→ INSTALL（經 UserService `cat > .ghostlock_root.sh` 置換，或 symlink 直寫）→ DONE。
  ⚠ 順序不可顛倒（exploit 啟動 ~2s 會寫出自己的預設腳本）。
- 新增：執行前記錄 boot 時間戳（`/proc/uptime` 或 sys.boot_completed 對照）→ 事後比對偵測 kernel-panic 重啟；失敗重試；全程**永不從 App uid 直接 exec ELF**。
- AppGraph：~~`USE_ENGINE`（Engine 列舉：SHIZUKU / BRIDGE / DIRECT）三態~~ **（2026-09-13 更新：已改為執行期自動探測 `AutoGhostlockEngine`，Shizuku 用 `pingBinder()`、bridge 用 helper 心跳檔判斷；不再有編譯期開關）**。`acquireRoot()` 先查 `RootDaemonClient.isRoot()`。
- 保活：既有 CaptureService 前景服務 + WakeLock 已足夠；加 `Shizuku.addBinderDeadListener` 偵測 server 死亡。
- **已實作並實機驗證**：`android/app-glasses/src/main/kotlin/com/rokid/rc/glasses/ghost/ShizukuGhostlockEngine.kt` + `CommandService.kt` + `aidl/`（歷史設計草稿：`research/shizuku/ShizukuGhostlockEngine.draft.kt`，已被正式版取代）。
- 實測修正 4 處（processNameSuffix 必填、OOM fast-fail+內部重試×3、Android 12 需前台啟動 FGS、doneTimeout 240s）——詳見測試報告 §5.1。

### 4.3 Server 啟動與 bootstrap 策略（與 §5 對應）
- **路徑 1（adb/手機 adb）**：每 reboot 由 host 或手機（termux-adb）跑 `adb shell <lib>/libshizuku.so` 一次 → App 全程自主。
- **路徑 2（root 啟動）**：第一次 LPE 成功後由 root_daemon 啟動——但 server 變 root 身份，**跑 exploit 前須經 run_shell 降回 shell**（uid 2000；SELinux context 仍是 kernel:s0 + Permissive，推論可行但未實測，優先採路徑 1）。
- **路徑 3（Magisk）**：跨 reboot 全自動（見 §5B）；Shizuku 可走 su root 模式開機自啟。

## 5. 替代方案比較矩陣（已完成，詳見 `research/shizuku/alternatives_and_ota.md`）

| 方案 | 免 adb 程度 | 跨 reboot | 磚機風險 | 實作難度 | 推薦順位 |
|------|------------|-----------|---------|---------|---------|
| **B. Magisk dd 刷入**（service.d 自啟 root_daemon） | ✅ 完全免（開機全自動） | ✅ 完全持久 | 中（只動 boot，可 fastboot 回滾；EDL 缺 firehose） | 中高 | **1（若接受刷機風險）** |
| **A. 手機當 adb host**（termux-adb + adb_keys 注入） | ⚠️ 免 PC；每 reboot 手機一鍵 | ❌ 每 boot 重啟 helper | 低（不碰分區） | 中（MIUI OTG 坑 + 一次性 key 注入） | **2（低風險首選）** |
| 基線：PC adb + shell helper（現行） | ❌ 需 PC | ❌ | 低 | 已完成 | 3（兜底） |
| Shizuku 引擎 | 與 server 啟動方式綁定（見 §3.3） | 需 Magisk 才免 adb | 無 | 低-中 | 作為 App 側執行通道，可與 A/B 疊加 |
| ~~install-recovery.sh 掛鉤~~ | — | — | — | — | ❌ dm-verity 擋死（已實測） |
| ~~symlink trick App 直連~~ | — | — | — | — | ❌ 需 shell 權限，App 直跑 kernel panic（已實測） |

**關鍵事實（子代理 C）**：
- **A 方案授權雞生蛋已解**：手機 termux-adb 產生 `adbkey.pub`（adb 專用 mincrypt 格式，非 OpenSSH）→ 經既有 root_daemon socket 以 root `printf >> /data/misc/adb/adb_keys` + `chown system:shell` + `chmod 640` + `restorecon` → adbd 收到 AUTH 請求時重讀，通常當下生效、免指紋 UI。
- **B 方案**：root `dd` 寫 `magisk_patched-30600_9C56p.img` 到 `mmcblk0p17`（active slot A）。已解鎖 bootloader → AVB 放行，不需動 vbmeta/dm-verity（Magisk systemless，不碰唯讀分區）。成功後 `/data/adb/service.d/` 開機腳本直接啟動 root_daemon → ghostlock flaky 鏈可退休、Shizuku 可 root 啟動。**刷前必 dd 備份原廠 boot_a 並存裝置外 + 驗證 MD5**（EDL 腳本只能喚醒、缺 firehose 無法線刷）。
- 建議路徑：**先 A 後 B**；B 完成後把 OTA 鎖寫進 service.d。

## 6. OTA 防護（已完成，詳見 `research/shizuku/alternatives_and_ota.md` §C）
- 現況：韌體 1.22.009（漏洞版）；1.25.012 已 backport 修補 CVE-2026-43499（SPL 相同會騙人、需反組譯 remove_waiter 辨識）；`isForceUpdate=false`、無 silent 強制升級。
- 眼鏡本身無網路 → 「自己檢查+下載」先天斷路；真正風險 = **官方手機 Rokid App 代理 / `com.rokid.os.sprite.assistserver` 中繼**（未能證實有本地 sideload，但應以「有」設防）。
- **首選（立即做）**：`adb shell pm disable-user --user 0 com.rokid.glass.ota`（reboot 持久，存 package-restrictions.xml）；次要可評估 `com.rokid.os.sprite.assistserver`（可能影響手機連動，先盤點）。
- **不要** disable `com.android.providers.downloads`（通用下載器，會打壞相簿）。
- 研究期間：眼鏡與官方 Rokid App 藍牙斷連/不登入。
- **升級 1.25.012 視為不可逆**（A/B anti-rollback + 無公開降級/線刷包）→ 阻擋 OTA 是保住 ghostlock_rv101 的前提。

## 7. 結論與建議

**回答原始問題**：「Shizuku 是否適合安裝在眼鏡端 APP，然後執行 adb shell + ghostlock exploit？」
→ **適合，且是現有方案中最乾淨的 App 內 shell 執行通道**；但「執行 adb shell」要理解為「取得與 adb shell 相同的 shell 身份執行 exploit」，而非真的在裝置內起 adbd。Shizuku 不解決「reboot 後零外部介入」——那屬於 Magisk 的範疇。

**建議執行順序**：
1. **立即（不接設備也能先準備）**：跑 `deploy/lock_ota.sh`（停用 `com.rokid.glass.ota`）；研究期間眼鏡不連官方 Rokid App。
2. **低風險改造（建議先做）**：手機 termux-adb + `adb_keys` 注入（§5A）→ 移除 PC 依賴，每 reboot 手機一鍵重啟 helper/Shizuku server。
3. **App 整合（本報告主體）**：app-glasses 加 Shizuku 依賴 + `ShizukuGhostlockEngine`（UserService/AIDL 模式），fallback 回 Bridge；預授權走 `pm grant`。
4. **終極（可選，需確認風險接受度）**：dd 備份原廠 boot_a → 刷 `magisk_patched-30600_9C56p.img` → `service.d` 開機腳本自動啟動 root_daemon（ghostlock 鏈可退休）+ 鎖 OTA + Shizuku root 自啟。

**主要風險提醒**：exploit ~50% panic 本質不變（Shizuku 只是通道）；kernel panic 會帶走 Shizuku server → App 需偵測重啟並配合 §5 的 bootstrap 重來；升級 1.25.012 不可逆。

## 附錄
- 研究細節：`research/shizuku/shizuku_internals.md`（含勘誤）、`research/shizuku/ghostlock_apk_analysis.md`、`research/shizuku/alternatives_and_ota.md`
- 設計工件：`research/shizuku/ShizukuGhostlockEngine.draft.kt`、`research/shizuku/aar/`（13.1.5 的 aar/sources 實證）、`research/shizuku/shizuku-src/`（Shizuku 源碼）
- 落地件：`deploy/lock_ota.sh`（一鍵鎖 OTA）
