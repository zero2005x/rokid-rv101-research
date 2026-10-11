# GhostLock APK（oopnv70-lab/ghostlock-apk）靜態分析與 Shizuku 整合層複用評估

> 分析對象：https://github.com/oopnv70-lab/ghostlock-apk
> 本地複本：`/home/kali/rokid_root/research/shizuku/ghostlock-apk`（`git clone --depth 1`，唯一 commit `94656a1`）
> 相關 repo 本地複本：`ghostlock-aak-apk`、`ghostlock-skeleton-v2`、`ghostlock-honor-aak`（同目錄）
> 分析方式：純靜態程式碼閱讀，未在任何設備上執行。
> 分析日期：2026-09（按任務上下文時間）

---

## 0. 執行摘要（TL;DR）

1. **目標 repo `ghostlock-apk` 本身是一份「已自承失敗」的 AI 生成模板**：`STRUCTURAL_NOTICE.md` 明言「連續 12 次 CI 構建全部失敗、未產出一個可安裝 APK、需重寫 80% 以上」，已於 2026-08-11 歸檔（archived）。它**沒有** exploit 二進位、**沒有** offsets 表、**沒有** LICENSE，且 Shizuku 整合層**缺了關鍵的 `provider` 依賴與 `ShizukuProvider` 宣告，無法取得 Shizuku binder**。
2. **其 Shizuku 執行層不可直接複用，只能當「概念示意」**。真正值得參考的是同作者的 **`ghostlock-aak-apk`（GPL-3.0）**：它補齊了 `provider`、實作了 `writeFile` + `runCommandStream`（oneway 回呼、雙線程並行讀 stdout/stderr 防死鎖），是最接近我們需求的骨架。
3. **「以 shell(uid 2000) 身份執行」這條核心鏈是可行的**：Shizuku 以 adb 方式啟動時，`UserService` 在 Shizuku server 進程內以 shell 身份跑，`Runtime.exec("sh -c ...")` 即得 uid 2000。這與我們 `ghostlock_rv101` 必須 shell 執行的硬需求吻合。
4. 要改成跑我們的 `ghostlock_rv101`，**至少需要改 6 處**（見 §5）：換 ELF、加 env 傳遞、加 `.ghostlock_root.sh` 預寫/攔截、加流式輸出與長超時（100–150s）、加 kernel-panic/重啟偵測、補齊 Shizuku 依賴。結論是**「參考骨架 + 大量自寫」**，而非「複用」。

---

## 1. 專案結構樹

### 1.1 目標 repo：`ghostlock-apk`（唯一 commit）

```
ghostlock-apk/
├── README.md                       # 聲稱「獨立 APK + Shizuku shell 身份」，但與實際不符
├── STRUCTURAL_NOTICE.md            # 「結構性失敗聲明」：AI 生成、12 次 CI 全失敗、已歸檔
├── .gitignore                      # 第 16 行排除 app/src/main/assets/ghostlock（二進位不入庫）
├── build.gradle                    # 僅宣告 AGP 8.7.3
├── settings.gradle                 # 含 google()/mavenCentral()/gradlePluginPortal()
├── gradle.properties
├── gradle/wrapper/gradle-wrapper.properties   # 引用 gradle-8.10-bin.zip，但【無 gradle-wrapper.jar】
├── .github/workflows/build.yml     # CI 用 gradle assembleRelease + 現場生成 debug.keystore
└── app/
    ├── build.gradle                # namespace com.ghostlock.app；deps 只有 dev.rikka.shizuku:api:13.1.5
    └── src/main/
        ├── AndroidManifest.xml     # 無 ShizukuProvider 宣告（致命缺失）
        ├── aidl/com/ghostlock/app/IShellService.aidl
        └── java/com/ghostlock/app/
            ├── MainActivity.java   # UI + Shizuku 綁定 + exploit 執行鏈
            └── ShellService.java   # UserService 實作（exec 跑 sh -c）
```

**注意：不存在以下路徑**（README 聲稱有、實際沒有）：`app/src/main/assets/ghostlock`、`app/libs/shizuku-api.aar`、`debug.keystore`、`gradle-wrapper.jar`、`LICENSE`。

### 1.2 同作者相關 repo（本次一併抓取）

| repo | 語言 | 狀態 | 內容重點 |
|---|---|---|---|
| `ghostlock-apk`（目標） | Java | **archived** | 獨立 APK + Shizuku，結構性失敗 |
| `ghostlock-aak-apk` | Java | 活躍(09-05 push) | **GPL-3.0** 加載器，Shizuku 整合最完整 |
| `ghostlock-skeleton-v2` | Java + C | archived | UserService + JNI native bridge（dlopen payload） |
| `ghostlock-honor-aak` | C | archived | exploit 本體源碼（offsets/利用鏈），GPL-3.0 |
| `GhostLock-H80GT` | C | 活躍 | KernelSU 模組加載 PoC（未細看） |
| 其餘 | — | — | Minecraft 模組、圍棋、launcher 等，**與本需求無關** |

---

## 2. 目標 repo 靜態分析

### 2.1 語言與結構

- **語言**：純 Java（無 Kotlin、無 C/NDK、無 assets 二進位）。
- UI 用純程式碼構建（`MainActivity.onCreate` 手動 new LinearLayout/TextView/Button），不依賴 res 目錄。
- 無 `LICENSE` 檔，GitHub API 回傳 `license: null` → **默認「保留所有權利」，不可合法複製/衍生**（見 §6）。

### 2.2 exploit 二進位如何打包

- **根本沒有打包**。README 第 25 行要求使用者「自行 `cp /path/to/ghostlock app/src/main/assets/ghostlock`」，且 `.gitignore` 第 16 行把 `app/src/main/assets/ghostlock` 排除出庫。
- 程式碼層面：`MainActivity.extractAssetToTarget()`（211–232 行）從 `getAssets().open("ghostlock")` 讀取，先寫到 app 私有目錄（`getFilesDir()`），再用 `cat <私有檔案> > /data/local/tmp/a/ghostlock` 由 shell 身份複製到目標路徑（`TARGET_DIR=/data/local/tmp/a`，`TARGET_BIN=/data/local/tmp/a/ghostlock`）。
- 因此：**這個 repo 裡不存在任何可用的 exploit ELF**。

### 2.3 支援的 kernel/機型（offsets 表在哪）

- 目標 repo **沒有任何 offsets 表、沒有 targets/ 目錄、沒有 C 源碼**。
- 唯一機型資訊在 `README.md` 第 7 行：「榮耀 AAK-AN00 / MagicOS 10 / Linux 6.6.89 / 4KB 頁」。
- 真正的 offsets 在同作者 `ghostlock-honor-aak`（C 源碼）的 **`exploit/src/targets/AAK-AN00/target.h`**（單機型硬編碼），由 `exploit/src/offset.h` 透過 `#include TARGET_CONFIG_H` 選擇。該表是 Honor AAK-AN00（kernel 6.6.89-android15, arm64, KIMAGE_TEXT_BASE=0xffffffc080000000）的常數，**與我們的 Rokid RV101（Android 12 / kernel 5.10.209-perf / arm64）完全不通用**。

### 2.4 Shizuku 整合方式

**A. 使用哪些 API**
- 僅 `dev.rikka.shizuku:api:13.1.5`（`app/build.gradle` 第 48 行）。
- 呼叫鏈：`Shizuku.getVersion()` / `pingBinder()` / `addBinderReceivedListener()` / `checkSelfPermission()` / `requestPermission(0)` / `bindUserService(UserServiceArgs, ServiceConnection)` / `getUid()`。
- **未使用 `newProcess`**；用的是 **UserService**（`bindUserService`）。沒有 rish。

**B. 如何取得 shell 身份**
- `MainActivity.bindService()`（144–170 行）：

```java
mServiceArgs = new Shizuku.UserServiceArgs(
    new ComponentName("com.ghostlock.app", "com.ghostlock.app.ShellService"))
    .daemon(false)
    .debuggable(true)
    .version(1);
...
Shizuku.bindUserService(mServiceArgs, mConn);
```

- `ShellService.exec()`（`ShellService.java` 17–39 行）用 `Runtime.getRuntime().exec(new String[]{"sh","-c",command})`。當 Shizuku 以 adb 啟動時，UserService 跑在 Shizuku server 進程、以 **shell uid=2000** 執行 → `sh -c` 即 shell 身份。
- `MainActivity` 在 `runExploitChain()` 開頭先 `mService.exec("id")` 打印身份驗證。

**C. 授權處理（假設已授權？引導？預授權 hack？）**
- 標準流程，**無預授權 hack**：`checkPermission()` → 未授權則 `requestPermission(0)` 並註冊 `addRequestPermissionResultListener`；授權後才 `bindService()`。假設使用者在 Shizuku 管理器裡點同意。

**D. 致命缺陷（與 STRUCTURAL_NOTICE 一致）**
1. **缺少 `dev.rikka.shizuku:provider` 依賴**。Shizuku 官方 demo 的 `build.gradle` 明確標註 `:api`「required by Shizuku and Sui」、`:provider`「required by Shizuku only」；client 端要靠 `ShizukuProvider` 才能拿到 binder。
2. **AndroidManifest.xml 沒有宣告 `rikka.shizuku.ShizukuProvider`**（`${applicationId}.shizuku` authority）。官方 demo manifest 有、`ghostlock-aak-apk`/`ghostlock-skeleton-v2` 都有，唯獨目標 repo 沒有。
3. 無 `gradle-wrapper.jar`、無 `debug.keystore`（CI 才現場生成）、README 的 `shizuku-api.aar` 13.5.4 與 build.gradle 的 maven 13.1.5 自相矛盾、`assets.srcDirs` 指向不存在的目錄。
   → 綜合結果：**這個 APK 無法連上 Shizuku，`pingBinder()` 會失敗，且 CI 無法出包**，與它自己寫的「12 次全失敗」相符。

### 2.5 exploit 執行細節（目標 repo）

| 環節 | 目標 repo 現況 | 與我們需求對照 |
|---|---|---|
| 寫出 ELF 路徑 | `/data/local/tmp/a/ghostlock`（先 `mkdir -p`） | 路徑可改；shell 對 `/data/local/tmp` 可寫 ✓ |
| 權限 | `chmod 755`（在 `extractAssetToTarget` 與 `runExploitChain` 各做一次） | ✓ |
| env 變數 | **無任何 env 傳遞**；`exec()` 固定 `{"sh","-c",cmd}` | ✗ 我們要 `GL_SETUP_ATTEMPTS=1` |
| stdout 捕獲 | `ShellService.exec` 先整段讀 stdout、再整段讀 stderr，`waitFor()` 後 append `exit=N` | ⚠ **若 stderr 寫滿 pipe buffer 會死鎖**；無即時回顯 |
| 超時/等待 | 無 timeout，`waitFor()` 阻塞 | 對 100–150s 長跑「不會中途砍」這點勉強可用，但同步阻塞 binder |
| 錯誤處理 | `try/catch` 包住整鏈；僅 log 到 UI；無重試、無 reboot 處理 | ✗ 我們要 kernel-panic/重啟偵測 |
| 執行方式 | `mService.exec(TARGET_BIN + " 2>&1")` | 直接把 ELF 當命令跑（我們也是這樣跑 ELF）✓ |
| 執行緒 | `new Thread(this::runExploitChain).start()`（UI 不凍結） | ✓ 但服務端 binder 同步阻塞 |
| 提權後動作 | `mService.exec("su -c 'id' 2>&1")` 驗證 | ✗ 我們要攔截 `.ghostlock_root.sh` 注入 payload |

### 2.6 提權成功後做什麼（目標 repo）

- **只有 `su -c 'id'` 驗證**。沒有 SELinux 處理、沒有持久化、沒有模組簽名繞過。
- 對比：同作者 `ghostlock-honor-aak` 的 exploit 本體（`EXPLOIT_CN.md` §三）會走完整鏈：UAF → 任意寫 → 劫持 `boot_id` sysctl → KASLR 洩露 → `cred=init_cred` → `selinux_state.enforcing=0` → `sig_enforce=0` → 釋放 `su_daemon` 到 `/data/local/tmp/su`。但那是 **Honor 版 exploit 的行為，不在任何 APK 的 Java 層**。
- 對照我們：我們靠**預先置換 `/data/local/tmp/.ghostlock_root.sh`** 讓 exploit 成功後以 root 執行我們的 payload —— 這個腳本在目標 repo 中**完全沒被觸及**，需自建。

---

## 3. 相關 repo 快速對照（找「UserService/daemon 版」）

### 3.1 `ghostlock-aak-apk`（**最佳參考**，GPL-3.0）

Shizuku 整合層最完整、最接近官方模式：

```java
// CommandService.java
@Keep public CommandService(Context context) { ... }   // v13+ Context 建構子
@Override public void destroy() { System.exit(0); }    // 16777114 保留碼

// writeFile：以 shell 身份直接 FileOutputStream 寫二進位 + chmod 755（返回 bool）
@Override public boolean writeFile(String path, byte[] data) { ... }

// runCommandStream：oneway 回呼，逐行即時回傳，雙線程並行讀 stdout/stderr
@Override public void runCommandStream(String cmd, ICommandCallback callback) {
    new Thread(() -> {
        Process p = Runtime.getRuntime().exec(new String[]{"sh","-c",cmd});
        // outThread / errThread 並行 readLine → callback.onLine()
        int exit = p.waitFor(); ...; safeExit(callback, exit);
    }).start();
}
```

```aidl
// ICommandService.aidl
void destroy() = 16777114;                     // Shizuku server 保留
void exit() = 1;
String runCommand(String cmd) = 2;
boolean writeFile(String path, in byte[] data) = 3;
void runCommandStream(String cmd, ICommandCallback callback) = 4;

// ICommandCallback.aidl
oneway interface ICommandCallback {
    void onLine(String line);
    void onExit(int exitCode);
}
```

- `AndroidManifest.xml` 正確宣告 `rikka.shizuku.ShizukuProvider`；`build.gradle` 同時引入 `api:13.1.5` + `provider:13.1.5`。
- `MainActivity` 用 `UserServiceArgs(component).daemon(false).processNameSuffix("ghostlock").version(1)`；payload 是**外部挑選的 preload.so**，用 `LD_PRELOAD=<so> /system/bin/sh -c 'echo exploit_triggered'` 觸發其 constructor `run_exploit()`（見 `EXPLOIT_CN.md` §4.3）。
- 對我們最有價值的三點：**oneway 流式回呼**（長跑不阻塞、即時看進度）、**writeFile 以 shell 身份寫二進位**、**正確的 provider 設定**。

### 3.2 `ghostlock-skeleton-v2`（archived）

- UserService + JNI：`UserService.exec()` 分流——`ghostlock` 前綴走 `NativeBridge.execute()`（`System.loadLibrary("ghostlock")`），其餘走 `sh -c`。
- `ghostlock-bridge.c` 只實作了 `load`（`dlopen` payload 並 `dlsym("ghostlock_entry")` 呼叫）與 `info`；`trigger/spray/write/pwn` **均「尚未實現」**（132 行直接印「尚未實現」）。
- 它走的是 **dlopen 進程內執行**，不是「寫出獨立 ELF 再 exec」。對我們「以 shell 身份跑獨立 ELF `ghostlock_rv101`」的模式**不適用**；僅其「UserService 內 try/catch(Throwable) 防 native crash 導致 binder 斷連」與 `destroy()` 保留碼可參考。

### 3.3 `ghostlock-honor-aak`（exploit 本體，GPL-3.0）

- 單機型 offsets 在 `exploit/src/targets/AAK-AN00/target.h`（含 `CONFIGFS_READ_ITER_OFF`、`CONFIGFS_BIN_WRITE_ITER_OFF`、`ASHMEM_*_FOPS_OFF` 等 —— 與我們 `ghostlock_rv101` 提到的 configfs fops 是同一利用技法家族）。
- 嘗試次數是**編譯期 `#define`**（`common.h`：`KERNEL_PAGE_SETUP_ATTEMPTS 6` / `FOPS_KERNEL_PAGE_SETUP_ATTEMPTS 72` / `PIPE_MAX_ATTEMPTS 12` 等），**不讀環境變數**。→ 我們的 `GL_SETUP_ATTEMPTS=1` 是自家 build 的擴充，參考實現沒有這項。

---

## 4. 關鍵程式碼片段引用（目標 repo）

**ShellService.exec（uid 2000 執行核心）** — `ShellService.java:17-39`：

```java
@Override
public String exec(String command) {
    java.lang.Process p = Runtime.getRuntime().exec(new String[]{"sh", "-c", command});
    BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
    BufferedReader e = new BufferedReader(new InputStreamReader(p.getErrorStream()));
    StringBuilder sb = new StringBuilder();
    String line;
    while ((line = r.readLine()) != null) sb.append(line).append("\n");   // 先 stdout
    while ((line = e.readLine()) != null) sb.append("[E] ").append(line).append("\n"); // 後 stderr
    p.waitFor();
    sb.append("exit=").append(p.exitValue());
    return sb.toString();
}
```

**UserService 綁定** — `MainActivity.java:144-170`（見 §2.4 B）。

**exploit 釋放與執行** — `MainActivity.java:178-208`：

```java
String id = mService.exec("id");                      // 驗證 uid
mService.exec("mkdir -p " + TARGET_DIR);              // /data/local/tmp/a
extractAssetToTarget();                                // assets→私有→cat 到 /data/local/tmp/a/ghostlock
mService.exec("chmod 755 " + TARGET_BIN);
String exploitResult = mService.exec(TARGET_BIN + " 2>&1");  // 執行提權
String suCheck = mService.exec("su -c 'id' 2>&1");     // 驗證
```

**AIDL** — `IShellService.aidl`：`String exec(String command); void destroy() = 16777114;`（16777114 為 Shizuku server 保留的 destroy 事務碼，與官方 demo `IUserService.aidl` 一致）。

---

## 5. 與我們需求差異清單（要跑 `ghostlock_rv101` 需改哪些）

前置事實：Shizuku UserService 的 shell 身份、`/data/local/tmp` 寫權、`chmod 755`、把 ELF 當命令執行 —— 這些都與我們需求一致；下面的差異是「必須改/加」的部分。

| # | 需求 | 目標 repo 現況 | 需改動 |
|---|---|---|---|
| 1 | 跑我們的 `ghostlock_rv101`（arm64 ELF） | 無二進位，`assets/ghostlock` 被 gitignore | 把 `ghostlock_rv101` 放 `app/src/main/assets/`，改 `EXPLOIT_BIN`/`TARGET_BIN` 常量（如 `/data/local/tmp/ghostlock_rv101`） |
| 2 | 必須 **shell 身份**執行 | UserService 提供 ✓（但整包缺 provider 已壞） | 補 `dev.rikka.shizuku:provider` 依賴 + manifest 宣告 `ShizukuProvider`（以 `ghostlock-aak-apk` 為模板） |
| 3 | 傳 **env 變數**（`GL_SETUP_ATTEMPTS=1`） | `exec()` 固定 `{"sh","-c",cmd}`，無 env | AIDL 增參（如 `exec(String cmd, Map<String,String> env)`），或服務端改用 `Runtime.exec(String[], String[])` 傳 envp；或最簡：cmd 前綴 `"GL_SETUP_ATTEMPTS=1 /data/local/tmp/ghostlock_rv101"` |
| 4 | 等 **100–150s** | 同步 `waitFor()`，無 timeout（不會砍，但阻塞 binder + 無進度） | 改用流式：採用 `ghostlock-aak-apk` 的 `runCommandStream` + `oneway ICommandCallback`；把命令放服務端 `new Thread` 執行，`waitFor()` 保留（不要加會砍進程的硬 timeout，或 timeout 設 >200s） |
| 5 | **stdout 即時捕獲** | 先 stdout 後 stderr 順序讀 → 有死鎖風險、無即時回顯 | 雙線程並行讀 stdout/stderr（aak 版已實作） |
| 6 | 成功後**攔截 `/data/local/tmp/.ghostlock_root.sh`** | 完全沒這邏輯 | 在執行 exploit **前**先以 shell 身份寫入我們自己的 `.ghostlock_root.sh` payload（覆蓋內容），再跑 exploit；成功後由 exploit 以 root 執行它。目標 repo 的 `su -c id` 驗證可保留作成功判定 |
| 7 | kernel-panic / 重啟偵測（App uid 直跑會 panic，需確保走 shell + 失敗可重啟） | 無重試、無 reboot | 自建：記錄執行前 boot 時間戳，exploit 後比對是否重啟；按鈕「重試」；必要時對 panic 加防護（確保永不從 app uid 執行 ELF） |
| 8 | 錯誤處理/重試 | 僅 try/catch + UI log，無重試策略 | 加「身份檢查 → 寫檔 → chmod → 跑 → 判定」逐步狀態機，失敗可重跑 |

---

## 6. 複用評估（結論）

- **目標 repo `ghostlock-apk` 的 Shizuku 執行層：不可直接複用，僅可參考「UserService + sh -c 的整體思路」。** 理由：結構性破損（缺 provider、無二進位、無 LICENSE、無 wrapper jar）、作者已歸檔並自承失敗。
- **建議以 `ghostlock-aak-apk` 為骨架重寫**：它具備正確的 `ShizukuProvider` 設定、`writeFile(byte[])`（shell 身份寫二進位）、`runCommandStream + oneway callback`（流式、防死鎖、長跑不阻塞 UI）。在此骨架上做 §5 的 8 項改動。
- **`ghostlock-skeleton-v2` 的 JNI/dlopen 路線不採納**（它進程內 dlopen payload，非獨立 ELF exec；且核心階段未實現）。
- **授權約束（重要）**：
  - `ghostlock-apk`：無 LICENSE → 默認版權保留，**不可複製其程式碼**；只能借鑒其公開 API 用法（Shizuku API 本身是 RikkaApps 的 Apache-2.0，用法不受此限制）。
  - `ghostlock-aak-apk` / `ghostlock-honor-aak`：**GPL-3.0**（含完整 LICENSE，版權 `Copyright (C) 2026 oopnv70-lab`）。若把其 `CommandService`/流式回呼等程式碼納入我們（衍生）作品並**對外發佈/傳播**，整個作品需以 GPL-3.0 開源並保留版權聲明。**僅內部研究、不分發**時 copyleft 傳播義務通常不觸發，但為合規與乾淨起見，建議**自己重寫執行層**（反正 §5 本來就要大改）。
  - `ghostlock-skeleton-v2`：無 LICENSE。
  - 歸屬：若引用上述 repo 的設計/程式碼，應在文件與原始碼標注 `oopnv70-lab` 出處與各自許可證。

---

## 7. 落地建議（給我們自己的最小改造路徑）

1. 以 `ghostlock-aak-apk` 為模板新建（或改造）一個 loader APK：`api:13.1.5` + `provider:13.1.5`、manifest 宣告 `ShizukuProvider`。
2. `assets/` 放 `ghostlock_rv101`（arm64，替換掉 aak 的「外選 preload.so」與 LD_PRELOAD 路線，改回「寫獨立 ELF 再 exec」——我們 ELF 是獨立可執行檔）。
3. `ICommandService` 增 `runCommandStream(String cmd, Map<String,String> env, ICommandCallback cb)`（或 `writeFile` + 帶 env 的 exec），服務端用 `Runtime.exec(new String[]{...}, envp)`。
4. 執行鏈：`id` 驗證 → 寫 `.ghostlock_root.sh` payload（`chmod 755`）→ 寫 `ghostlock_rv101` → `GL_SETUP_ATTEMPTS=1 /data/local/tmp/ghostlock_rv101`（流式回顯）→ 等待 100–150s → 判定 payload 副作用 / 比對 boot 時間偵測 panic → 清理。
5. 全程確保只經 UserService 執行（永不從 app uid 直接 exec ELF），符合「App uid 10053 直跑會 kernel panic」的約束。

---

## 附錄：本地複本位置

- 目標：`/home/kali/rokid_root/research/shizuku/ghostlock-apk`
- 參考：`/home/kali/rokid_root/research/shizuku/ghostlock-aak-apk`
- 參考：`/home/kali/rokid_root/research/shizuku/ghostlock-skeleton-v2`
- 參考（C 源碼/offsets）：`/home/kali/rokid_root/research/shizuku/ghostlock-honor-aak`
