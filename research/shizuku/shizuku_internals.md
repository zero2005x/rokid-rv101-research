# Shizuku 框架內部機制研究報告

> 目標設備：Rokid RV101 (Android 12 / SDK 32, kernel 5.10.209-perf, 1.8GB RAM, SELinux Enforcing, 無 Google Play, 無網路, adb 僅 USB)
> 研究目的：評估把 Shizuku 整合進「眼鏡端 App」，讓 App 以 shell 身份執行 ghostlock_rv101 exploit（CVE-2026-43499）。

## 方法與源碼版本

- 主倉庫 `RikkaApps/Shizuku`：`git clone --depth 1` 於 `/home/kali/rokid_root/research/shizuku/shizuku-src`，commit `b844bc491f1790c72328e1a8e5b2349f8978f0ea`（2025-06-18，master）。
- API 倉庫 `RikkaApps/Shizuku-API`（`api` 子模組，因 SSH 失效改以 HTTPS 單獨 clone）：commit `a27f6e4151ba7b39965ca47edb2bf0aeed7102e5`（2025-05-29）。
- 模組結構：`server`（伺服器本體）、`starter`（UserService 啟動器）、`shell`（rish）、`manager`（`moe.shizuku.privileged.api`，同時內含伺服器啟動用 native starter 與伺服器 class）、`common`；`api/` 下含 `api`、`provider`、`shared`、`server-shared`、`aidl`、`rish`。

### 版本對照（關鍵）

| 項目 | 值 | 出處 |
|---|---|---|
| Server 協議版本 `SERVER_VERSION` | 13 | `api/shared/.../ShizukuApiConstants.java:9` |
| Server patch 版本 | 6 | 同上 `:10` |
| API 函式庫發佈版本 | **13.1.5**（`api_version_major.minor.patch`） | `api/manifest.gradle` |
| Manager APK 版本 | **13.6.0**（`baseVersionName = "${major}.6.0"`） | 主倉庫根 `build.gradle` |
| Manager minSdk / targetSdk | **24 (Android 7.0) / 36** | 主倉庫根 `build.gradle` |
| 官方宣稱最低 Android 版本 | 6.0+（API 23） | Shizuku-API README |

官方文件：使用者手冊 https://shizuku.rikka.app/guide/setup/ （原始檔 https://raw.githubusercontent.com/RikkaApps/websites/refs/heads/master/shizuku/guide/setup.md ）；下載 https://shizuku.rikka.app/download/ 。

---

## 1. Server 啟動方式

### 1.1 三種啟動路徑（以最新版 v13.x 為準）

**重要變更**：舊版（≤ v11.2.0）的 `start.sh` 腳本已不存在於 v13.x 源碼樹（`find . -name '*.sh'` 為空；`grep start.sh` 無結果）。最新版用一個 **native starter `libshizuku.so`** 取代 start.sh，該 starter 以 C++ 實作，直接 fork + exec `app_process`。官方 setup.md 仍寫著 `adb shell sh /sdcard/Android/data/moe.shizuku.privileged.api/start.sh`（標注 "Command for Shizuku v11.2.0+"），是**過時文件**；13.6.0 起啟動指令已改成 libshizuku.so（見 issue [#1206](https://github.com/RikkaApps/Shizuku/issues/1206)「Strange and very long Shizuku launch command in 13.6.0」與 [#1867](https://github.com/RikkaApps/Shizuku/issues/1867)）。

三條路徑的指令在 `manager/src/main/java/moe/shizuku/manager/starter/Starter.kt:8-14` 定義：

```kotlin
private val starterFile = File(application.applicationInfo.nativeLibraryDir, "libshizuku.so")
val userCommand: String = starterFile.absolutePath
val adbCommand = "adb shell $userCommand"
val internalCommand = "$userCommand --apk=${application.applicationInfo.sourceDir}"
```

1. **adb 啟動**：`Starter.adbCommand` = `adb shell <nativeLibraryDir>/libshizuku.so`
   （例如 `adb shell /data/app/~~<hash>/moe.shizuku.privileged.api-<hash>/lib/arm64/libshizuku.so`）。由 `StartAdbViewHolder.kt` 顯示/複製給使用者。
2. **root 啟動**：`StarterActivity.startRoot()`（`StarterActivity.kt:135-164`）用 libsu 執行
   `su -c "<nativeLibraryDir>/libshizuku.so --apk=<manager apk path>"`。
3. **無線調試啟動**：`StarterActivity.startAdb(host, port)`（`StarterActivity.kt:166-195`）先用 `AdbClient` 走 mDNS/ADB-pairing（`AdbPairingClient.kt`、`AdbMdns.kt`）連上無線 adb port，再 `shellCommand(internalCommand)` —— 即在無線 adb shell 上執行同一支 `libshizuku.so --apk=...`。**此路徑本質上仍是 adb shell，只是走無線通道**。

### 1.2 native starter 的完整機制（`manager/src/main/jni/starter.cpp`）

`starter.cpp` 的 `main()`：

1. 解析 `--apk=`（`starter.cpp:185-190`）。
2. **身份檢查**：`uid != 0 && uid != 2000` 就 exit（`EXIT_FATAL_UID`，`starter.cpp:192-196`）——即**只能以 root(0) 或 shell(2000) 執行**。
3. 若 root：`switch_cgroup()`（見第 6 節）+ Android 10+ `switch_mnt_ns(1)`（切到 init 的 mount namespace，`starter.cpp:200-207`、`misc.cpp:107-118`）。
4. 若 root：檢查 `selinux_check_access("u:r:untrusted_app:s0", <current context>, "binder", "call"/"transfer")`，失敗就 exit（`EXIT_FATAL_BINDER_BLOCKED_BY_SELINUX`，`starter.cpp:209-224`）——要求 su 允許 app↔su 的 binder 通訊。
5. **殺掉舊 server**：掃 `/proc`，找 process name == `shizuku_server` 並 `SIGKILL`（`starter.cpp:229-250`）。
6. **取得 manager APK 路徑**：`pm path moe.shizuku.privileged.api`（或 `--apk=`），`starter.cpp:257-279`。
7. `start_server()` fork，子程序 `setsid()`、`chdir("/")`、stdio 導向 `/dev/null`（`starter.cpp:116-142`），再 `run_server()`：
   - 設 `CLASSPATH=<manager apk path>`、`-Djava.class.path=<apk>`、`-Dshizuku.library.path=<apk>/lib/<ABI>`；
   - `execvp("/system/bin/app_process", ...)`，參數依序為（`starter.cpp:95-113`）：
     ```
     /system/bin/app_process
       -Djava.class.path=<manager apk>
       -Dshizuku.library.path=<apk dir>/lib/arm64   (ABI: arm64 for aarch64)
       /system/bin
       --nice-name=shizuku_server
       rikka.shizuku.server.ShizukuService          (main class)
     ```

**關鍵結論**：
- **「server jar」不是獨立 jar**：server 的 class 直接打包在 manager APK 內（`CLASSPATH` 指向 manager APK）。`server/build.gradle` 表明 `server` 是 library module，被 `manager` 依賴（`manager/build.gradle` 的 `implementation project(':server')`）。舊版所謂 `server.dex` / `start.sh` 已不存在。
- `ShizukuService.main()` 把 process name 設為 `shizuku_server`（`server/.../ShizukuService.java:61-68`），並在 manager 未安裝時 `System.exit(50)`（`ShizukuService.java:104-107`、`ServerConstants.MANAGER_APP_NOT_FOUND=50`）。

### 1.3 常駐性與 OOM 重啟

- **不常駐到 reboot**：server 是 bare `app_process`（`shizuku_server`），重開機即消失，需重新 adb/root 啟動。官方 setup.md 明言「Due to system limitations, the boot steps need to be performed again after each reboot」；issue [#363](https://github.com/RikkaApps/Shizuku/issues/363)「Shizuku turns off automatically after device restart」。
- **開機自動重啟（有限）**：`BootCompleteReceiver.kt` 在 `LOCKED_BOOT_COMPLETED/BOOT_COMPLETED` 時，若上次啟動模式為 root 則自動 `su -c ...` 重啟；若為 adb 模式**僅限 Android 13+ 且已授 WRITE_SECURE_SETTINGS** 才走無線 adb 自啟（`BootCompleteReceiver.kt:39-48`）。Android 12 的 adb 模式無法自動重啟。
- **被 lmkd/OOM 殺掉後**：server 直接死亡，client 端 binder 死亡（`Shizuku.DEATH_RECIPIENT` 觸發 `onBinderReceived(null)`），**沒有 watch dog，無法在無 adb/root 下自癒**。只有 (a) 開機廣播，(b) 再次手動 adb/root 才能重啟。詳見第 6 節。

---

## 2. 執行身份（uid 與 SELinux context）

### 2.1 Server 本身身份 = 啟動者身份

Server 以 `app_process` 執行，`OsUtils`（`api/server-shared/.../util/OsUtils.java`）在靜態初始化時快取：

```java
private static final int UID = android.system.Os.getuid();
private static final String SELINUX_CONTEXT = SELinux.getContext();
```

並在 client 綁定時回傳（`server/.../ShizukuService.java:233-235`）：
`BIND_APPLICATION_SERVER_UID` / `BIND_APPLICATION_SERVER_SECONTEXT`，client 端存到 `Shizuku.serverUid` / `Shizuku.serverContext`（`api/api/.../Shizuku.java:54-57`），可經 `Shizuku.getUid()` / `Shizuku.getSELinuxContext()` 讀取。

因此：
- **(a) adb 啟動**：server uid = 2000（shell），context = **`u:r:shell:s0`**。
- **(b) root 啟動**：server uid = 0，context = 該 su 的 context（Magisk 為 `u:r:magisk:s0`）。本裝置 root 是 exploit 產生的 **`u:r:kernel:s0`**（且 Enforcing 已被改成 Permissive）。

官方 API javadoc 明確寫（`api/api/.../Shizuku.java:555-566` `getSELinuxContext()`）：

> "For adb, context should always be `u:r:shell:s0`. For root, context depends on su the user uses. E.g., context of Magisk is `u:r:magisk:s0`. If the user's su does not allow binder calls between su and app, Shizuku will switch to context `u:r:shell:s0`."

（註：最新 master 的 `starter.cpp` 若 binder 檢查不過是直接 exit，不再自動切 context；「切到 shell context」是舊版/Sui 行為。）

### 2.2 `newProcess` / rish 的 child 身份 = server 身份

- **`newProcess`**：AIDL `IShizukuService.newProcess(in String[] cmd, in String[] env, in String dir)`（`api/aidl/.../IShizukuService.aidl`，transaction code 7）。server 端實作在 `api/server-shared/.../Service.java:306-323`：
  ```java
  process = Runtime.getRuntime().exec(cmd, env, dir != null ? new File(dir) : null);
  ```
  即**在 server 行程內 spawn 子程序**，因此 child 繼承 server 的 uid 與 SELinux context，**沒有 setuid/setcon**。

- **rish**：server 端 `RishService`（`Service.java:41-58` 建構 `RishService`），其 native 實作 `api/rish/src/main/cpp/rikka_rish_RishHost.cpp` 的 `RishHost_startHost()`：`fork()`（`:117`）→ 子程序 `setsid()` → `execvpe("/system/bin/sh", argv, envv)`（`:235/:240`），`argv[0]` 固定為 `/system/bin/sh`（`:96`）。**child 是 server 的 fork 子程序**，同樣繼承 server 的 uid+context，無 setuid/setcon。

- **UserService**：**不同**——見 2.3。

### 2.3 UserService 身份 = 呼叫 App 的 uid（非 shell/root）

> ⚠ **本節結論有誤，見文末「勘誤」**：實證 UserService 進程 uid/SELinux =
> server 的身份（adb 啟動時 = uid 2000 + u:r:shell:s0），非 App uid。
> 下列原文保留供對照。

`ShizukuUserServiceManager.getUserServiceStartCmd()`（`server/.../ShizukuUserServiceManager.java:26-39`）組出（`starter/.../ServiceStarter.java:44-46`）：

```
(CLASSPATH='<manager apk>' /system/bin/app_process /system/bin \
  --nice-name='<package>:<suffix>' moe.shizuku.starter.ServiceStarter \
  --token='<uuid>' --package='<clientPkg>' --class='<cls>' --uid=<callingUid>)&
```

`--uid=<callingUid>` 是**呼叫方 App 的 uid**（例如 10053）。`UserService.java:33-99` 用 `ActivityThread.systemMain()` + `createPackageContextAsUser(pkg, ...)` + `makeApplication()` 把該 App 的 Application 拉起來，載入 App 自己的 class。因此 UserService 執行於 **App 的 uid / App 的 SELinux domain（untrusted_app）**，絕非 shell/root。

### 2.4 對 exploit 的決定性結論

| 啟動路徑 | server 身份 | `newProcess`/`rish` 跑的 native ELF 看到的身份 |
|---|---|---|
| **adb shell 啟動** | uid 2000, `u:r:shell:s0` | **uid 2000, `u:r:shell:s0` — 完全等同 adb shell** ✅ |
| **root 啟動（本裝置）** | uid 0, `u:r:kernel:s0` | **uid 0, `u:r:kernel:s0` — 變成 root/kernel** ❌ |

**答案**：
1. **是**——只要 Shizuku server 是由 **adb shell**（`libshizuku.so`，非 root）啟動，則「經 Shizuku 跑一個 native ELF」與「adb shell 跑同一個 ELF」的身份**完全一致（uid 2000 + `u:r:shell:s0`）**，ghostlock_rv101 所需的 `perf_event_open` 等 shell 權限、以及「以 shell 身份起跑」的前置條件都能滿足。這正是可利用的啟動路徑。
2. **root 啟動會污染子程序身份**：若用 root（本裝置 `u:r:kernel:s0`）啟動 server，`newProcess`/rish 的 child 會是 uid 0 + `u:r:kernel:s0`，**對 exploit 是反效果**——它要求 unprivileged 起跑（且你已觀察到 untrusted_app 直接跑會 kernel panic，root/kernel context 亦非預期起跑身份）。所以要跑 exploit，**Server 必須走 adb/shell 啟動，不能走 root**。

---

## 3. API 用法（最新版）

### 3.1 Gradle 依賴與 minSdk

官方 Shizuku-API README（https://github.com/RikkaApps/Shizuku-API ）：

```groovy
def shizuku_version = "13.1.5"   // 見 Maven Central: dev.rikka.shizuku
implementation "dev.rikka.shizuku:api:$shizuku_version"
implementation "dev.rikka.shizuku:provider:$shizuku_version"  // 若要走 Shizuku(非 Sui) 必加
```

- 座標 `dev.rikka.shizuku:api` 與 `dev.rikka.shizuku:provider`，發佈於 Maven Central（https://central.sonatype.com/namespace/dev.rikka.shizuku ），本 repo clone 到的 library 版本為 **13.1.5**（`api/manifest.gradle`）。
- `provider` 提供 `rikka.shizuku.ShizukuProvider`，manifest 需宣告（README / `api/provider/.../ShizukuProvider.java:24-44`）：
  ```xml
  <provider android:name="rikka.shizuku.ShizukuProvider"
      android:authorities="${applicationId}.shizuku"
      android:exported="true" android:multiprocess="false"
      android:permission="android.permission.INTERACT_ACROSS_USERS_FULL" />
  ```
- minSdk：官方 README 稱「Android 6.0+」；但 **manager APK 本身 minSdk=24（Android 7.0）**（主倉庫根 `build.gradle`）。目標 Android 12（SDK 32）兩者都滿足。
- aarch64：**支援**。`starter.cpp` 對 `__aarch64__` 定義 `ABI "arm64"`（`starter.cpp:44-45`）；`ShizukuUserServiceManager` 只有在要求 32-bit 時才找 `/system/bin/app_process32`，否則用 `/system/bin/app_process`（純 Java，ABI 無關）。

### 3.2 是否強制安裝 manager APK？sideload 是否可行？

- **強制**。server 的 class 就打包在 manager APK 內（`CLASSPATH` = manager APK），且 binder 是經由 manager 的 ContentProvider（`moe.shizuku.privileged.api.shizuku`）送達 app；`ShizukuService` 啟動時若找不到 manager 就直接 `System.exit(50)`（`ShizukuService.java:104-107`）。所以 **App 要 bind Shizuku，必須先裝 `moe.shizuku.privileged.api`**。
- **無 Google Play 可 sideload**：manager 是純 APK，manifest 無任何 GMS 依賴（`manager/src/main/AndroidManifest.xml` 僅需 `RECEIVE_BOOT_COMPLETED/FOREGROUND_SERVICE/INTERNET/POST_NOTIFICATIONS/WRITE_SECURE_SETTINGS`），`manager/build.gradle` 無 play-services 依賴。可從 https://shizuku.rikka.app/download/ 下載 APK 直接 `adb install`。
- 另一條路是 Sui（Magisk 模組，rooted 設備），但本裝置非 Magisk 環境，不適用。

### 3.3 `newProcess` 確切簽名、env、stdout、長時執行

- AIDL 層簽名（穩定）：`IRemoteProcess newProcess(in String[] cmd, in String[] env, in String dir)`（`IShizukuService.aidl`）。
- **Java helper 已被 `private` + `@deprecated`**：`api/api/.../Shizuku.java:491` 的 `private static ShizukuRemoteProcess newProcess(...)`，註解寫「planned to be removed from Shizuku API 14」「use transactRemote / bindUserService instead」。也就是說 v13 的公開 Java API 不再暴露 `newProcess`，但 **server 端的 binder capability（transaction 7）仍在**，可透過以下方式呼叫：
  1. 直接對 `Shizuku.getBinder()` 走 `transact` 呼叫 transaction 7（或 `Shizuku.transactRemote`）；
  2. 使用 `rish`（`rikka.shizuku.shell` / `moe.shizuku.manager.shell.Shell`，本質上是 `app_process` 起一個連到 server 的 shell，`shizuku rish`）；
  3. 用舊版 API（12.x，其 `newProcess` 為 public）；
  4. 反射呼叫該 private 方法（不推薦）。
  另見 [Shizuku-API issue #276](https://github.com/RikkaApps/Shizuku-API/issues/276)（請求保留 newProcess）與 [Shizuku issue #818](https://github.com/RikkaApps/Shizuku/issues/818)。
- **env vars**：**支援**。`newProcess` 的 `env` 參數原樣傳給 `Runtime.exec`；rish 也支援 env（`RishService.createHost` 讀 `env`，`RISH_PRESERVE_ENV=1/0` 控制是否保留，`RishService.java:37-49`）。因此 exploit 需要的 `GL_SETUP_ATTEMPTS` 等環境變數可以直接帶。
- **stdout/stderr**：**可取**。server 端 `RemoteProcessHolder`（`api/server-shared/.../RemoteProcessHolder.java`）用 `ParcelFileDescriptorUtil.pipeTo/pipeFrom` 把子程序的 stdin/stdout/stderr 透過 pipe + ParcelFileDescriptor 回傳；client 端 `ShizukuRemoteProcess.getInputStream()/getOutputStream()/getErrorStream()`（`ShizukuRemoteProcess.java:47-77`）可讀寫。
- **長時執行（100–150 秒）**：**無硬性時間上限**。`RemoteProcessHolder.waitFor()` 直接 `process.waitFor()`（阻塞至結束）；`waitForTimeout()` 只是 client 側輪詢。唯一限制是 **owner（呼叫 App 的 binder）死亡時會 kill 該 process**（`RemoteProcessHolder` 建構子對 token `linkToDeath` → `destroy()`）。所以 100–150 秒的 exploit 只要 App 行程存活即可；若 App 進背景被系統凍結/殺死，process 會連帶被殺，需用前台 service/wakelock 保命。

---

## 4. 授權流程與預授權 hack

### 4.1 Android 12 上的授權 UI 流程

1. App 呼叫 `Shizuku.requestPermission(requestCode)`（`Service.java:263-286`）：已授權→立即回 grant；config 標記 denied→立即回 deny；否則 `showPermissionConfirmation()`。
2. server 發 `Intent`（action `moe.shizuku.privileged.api.intent.action.REQUEST_PERMISSION`）給 manager（`ShizukuService.java:256-281`）。
3. `RequestPermissionActivity`（`manager/.../authorization/RequestPermissionActivity.kt`）彈出確認對話框。
4. 使用者點「允許」→ `Shizuku.dispatchPermissionConfirmationResult(uid, pid, requestCode, {allowed=true, onetime=false})`（`RequestPermissionActivity.kt:114-117`）。
5. server 端 `dispatchPermissionConfirmationResult`（`ShizukuService.java:284-335`）：把該 uid 所有 `ClientRecord.allowed=true`，並（非 onetime 時）：
   - `configManager.update(uid, packages, MASK_PERMISSION, FLAG_ALLOWED)` → **持久化到 JSON**；
   - 對該 App `grantRuntimePermission(pkg, "moe.shizuku.manager.permission.API_V23", userId)`。

### 4.2 授權存哪裡（**不是 SQLite DB**）

最新版**沒有** SQLite 資料庫。授權持久化在一個 **JSON 檔**：

- **路徑**：`/data/user_de/0/com.android.shell/shizuku.json`（`server/.../ShizukuConfigManager.java:42`，`AtomicFile`）。
- **schema**（GSON 序列化 `ShizukuConfig`，`server/.../ShizukuConfig.java`）：
  ```json
  {
    "version": 2,
    "packages": [
      { "uid": 10053, "flags": 2, "packages": ["com.example.app"] }
    ]
  }
  ```
  - `flags` 位元：`FLAG_ALLOWED = 1<<1 = 2`，`FLAG_DENIED = 1<<2 = 4`，`MASK_PERMISSION = 6`（`api/server-shared/.../ConfigManager.java:13-15`）。
  - `version`：`LATEST_VERSION = 2`（`ShizukuConfig.java:12`）。

> 你假設的 `/data/user_de/0/moe.shizuku.privileged.api/databases/` 是**舊版（或誤記）**。現版（v12/v13）把授權移到 **`com.android.shell` 的資料目錄**（因為 server 以 shell/root 執行，寫得進這個目錄），檔名是 `shizuku.json` 而非 DB。

此外有第二套並行的授權：**Android runtime permission `moe.shizuku.manager.permission.API_V23`**（`ServerConstants.PERMISSION`）。`getFlagsForUidInternal()`（`ShizukuService.java:337-361`）先查 JSON config，**沒有 config 條目時才 fallback 查 runtime permission**。`ShizukuConfigManager` 建構子也會在啟動時把已授 runtime permission 的 App 寫回 config（`ShizukuConfigManager.java:151-175`）。

### 4.3 授權 token 是不是 binder token？會不會 server 重啟就失效？

- **不是 binder token**。授權本質是「**uid → flags**」的持久化旗標（JSON）＋ runtime permission，**與 binder 無關，server 重啟不會失效**。
- 每個 client 連線的 `ClientRecord.allowed` 是程序內快取（`ClientRecord.java:19`），在 `attachApplication`/`addClient` 時由 config 重建（`ClientManager.addClient` 讀 `configManager.find(uid)` 設定 `allowed`，`ClientManager.java:65-71`）。所以 App 重啟或 server 重啟後，只要 JSON 檔在，授權就還在。
- 程式碼裡唯一的「token」是 **UserService 註冊 token**（`UserServiceRecord.token = UUID.randomUUID() + "-" + currentTimeMillis()`，`UserServiceRecord.java:45`），用途是對應 `ServiceStarter --token=` 與 server 的 record，**與授權無關**。

### 4.4 免 UI 預授權 hack（可行，需 root 或 shell）

因 JSON 檔在 `com.android.shell` 資料目錄、且 server 以 shell/root 執行，**有 root（或 shell 身份）就能直接寫授權**：

1. **直接寫 config JSON**（最直接）：
   ```sh
   # 先停 server（避免其 10 秒後的寫入覆蓋；ShizukuConfigManager WRITE_DELAY=10s）
   echo '{"version":2,"packages":[{"uid":10053,"flags":2,"packages":["com.example.app"]}]}' \
     > /data/user_de/0/com.android.shell/shizuku.json
   # 確保 owner/權限為 com.android.shell 可讀寫（chown 2000:2000 / chmod 600）
   ```
   然後（重）啟動 server 即可。`uid` 用 `dumpsys package <pkg> | grep userId` 或 `stat -c %u /data/data/<pkg>` 取得。
2. **`pm grant` 授 runtime permission**（走 fallback 路徑，config 無條目時生效）：
   ```sh
   pm grant com.example.app moe.shizuku.manager.permission.API_V23
   ```
   此權限 protectionLevel 是 `dangerous`（`manager/.../AndroidManifest.xml`），`pm grant` 對 dangerous 權限可用；之後 server 啟動時 `ShizukuConfigManager` 建構子會把此 App 依 runtime permission 狀態寫進 config（`ShizukuConfigManager.java:151-175`）。

- **注意**：寫檔時若 server 正在執行且之後會觸發 `scheduleWriteLocked()`，可能 10 秒後用記憶體中的 config 覆蓋你的檔案；最穩做法是**先寫檔再啟動 server**，或寫檔後重啟 server。
- 官方**沒有** CLI 授權指令、`start.sh`/`libshizuku.so` 也**沒有**預授權選項（starter.cpp 的參數只有 `--apk=`）。因此預授權只能靠上面「直接寫 JSON」或「pm grant」。
- 對本裝置而言這很順：你已有 root（`root_daemon` 可 root 執行指令），在 App 首次 bind 前由 root 寫好 `shizuku.json`（或 `pm grant`）即可**跳過授權 UI**。

---

## 5. UserService 取代 root_daemon 的可行性

> ⚠ **本節身份前提有誤（見文末「勘誤」）**：UserService 是 shell/root 身份（server 身份），
> 不是 App 身份。「不能取代 root_daemon（後者為 root 身份、0777 socket）」的結論仍成立，
> 但理由應為「shell 身份 ≠ root daemon 的 root 身份」，而非「App 身份」。

**結論：不可行。UserService 是「以 App 自身身份」常駐，不是 shell/root，無法取代以 root 執行的 root_daemon。**

- **啟動身份**：見 2.3——`app_process --uid=<callingUid>`，UserService 執行於**呼叫 App 的 uid（untrusted_app）與其 SELinux domain**。它沒有 shell/root 的任何權限；反而與會 kernel panic 的 untrusted_app(10053) 同域，對 exploit 沒幫助。
- **生命週期**：server 用 `Runtime.exec("sh")` 啟動它（`UserServiceManager.startUserService`，`UserServiceManager.java:214-236`）。`daemon=true`（預設）時：App 行程死亡 service 仍存活，直到 `unbindUserService(..., remove=true)`；`daemon=false` 時：App 死亡即被移除（`UserServiceRecord.ConnectionList.onCallbackDied`，`UserServiceRecord.java:20-31`）。server 死亡時 `ServiceStarter` 對 server binder `linkToDeath` → `System.exit(0)`，所以 UserService 隨 server 一起消失。
- **限制**：它只能「暴露一個 binder 給其他 App（經 server 轉送）」——用途是跨 App 分享一個以**你 App 身份**跑的 service，而不是提權。要跑 root/shell 的常駐 daemon，**仍須自己用現有 root_daemon（或讓 server 以 shell 身份 `newProcess` 起一個長駐 ELF，那才是 shell 身份）**。

> 補充：若你要「以 shell 身份常駐」，正確做法不是 UserService，而是**讓 adb/shell 啟動的 Shizuku server 用 `newProcess`/rish 起你的 shell ELF**（見第 2.4 節，身份 = u:r:shell:s0）。但這仍需 server 先以 shell 身份被啟動。

---

## 6. 低記憶體裝置風險（1.8GB RAM）

- **server 是 Java 行程**（`app_process` 跑 `ShizukuService`），常駐記憶體；但它以 **shell(2000)/root(0) 執行，不屬於 app 的 oom_adj 類別**，因此不是 lmkd 優先獵殺對象（lmkd 主要殺 cached/empty 的 App）。1.8GB 裝置上仍可能因整體記憶體壓力被回收，風險**低於一般 App 但非零**。
- **內建防護只對 root 啟動生效**：`starter.cpp` 僅在 `uid==0` 時呼叫 `switch_cgroup()`（`starter.cpp:200-201`），把 server PID 移出 app cgroup、寫入根 cgroup（依序嘗試 `/acct/cgroup.procs`、`/dev/cg2_bpf/cgroup.procs`、`/sys/fs/cgroup/cgroup.procs`，`cgroup.cpp:7-19`），讓 lmkd 不把它當 app 殺。**adb/shell 啟動時不做 cgroup 切換**（shell 身分通常也不在 app cgroup，但少了這層主動保護）。
- **無「前台 service」機制**：server 是裸 `app_process`（`--nice-name=shizuku_server`），不是 Android 四大元件，沒有 `startForeground` 可提升 oom_adj。manager 的 `FOREGROUND_SERVICE` 權限只用於 AdbPairingService，與 server 無關。
- **已知案例/issue**：
  - reboot 後消失：issue [#363](https://github.com/RikkaApps/Shizuku/issues/363)。
  - UserService 在部分 OEM（Transsion/Android 15）被靜默殺掉：issue [#2048](https://github.com/RikkaApps/Shizuku/issues/2048)。
  - 一般「Shizuku 掉線」社群討論多指向 OEM 省電/後台管理，而非純 lmkd。
- **workaround（實務建議）**：
  1. **root 啟動**（自動獲得 cgroup 保護）；
  2. 關閉對 manager 與你自己 App 的電池最佳化（官方 rish 逾時提示也這樣建議，`ShizukuShellLoader.java:155-160`）；
  3. 由你的 App 掛一個**前台 service + wakelock** 保命，並監聽 `Shizuku.addBinderDeadListener` 後自動重拉（重拉仍需 adb/root，或靠 `BootCompleteReceiver`）；
  4. 提高存活率：把 exploit/長駐邏輯放 server 側（shell 身份）而非 App 側，App 只做薄控制層，App 被殺也不影響 shell ELF（`RemoteProcessHolder` 會在 owner binder 死時 `destroy()`，所以長駐 ELF 應避免與 App 生命週期綁死——見 3.3 的 token 綁定，需自行 `detach` 或用獨立 token）。

---

## 7. 授權與合規

- **主倉庫 `RikkaApps/Shizuku`：Apache License 2.0**（根目錄 `LICENSE`，第一行即 "Apache License, Version 2.0"）。
- **`RikkaApps/Shizuku-API`（`dev.rikka.shizuku:api/provider` 函式庫）：MIT License**（`api/LICENSE` 為 "MIT License, Copyright (c) 2021 RikkaW"；`api/build.gradle` 的 `POM_LICENCE_NAME = "MIT License"`）。
- **結論：允許嵌入自有研究 App**。兩者皆為寬鬆許可證。Apache-2.0 要求：保留版權與 NOTICE、標註修改（若有）、並附上許可證文字；MIT 要求保留版權聲明。無 copyleft 義務，可閉源商用（不提供法律意見，僅陳述許可證事實）。

---

## 附錄：關鍵源碼路徑索引

> **勘誤（2026-09-09，由主代理核對源碼後追加）**：
> §2.3 與 §5 的「UserService 執行於 App 的 uid / untrusted_app domain」結論有誤。
> 實證：`api/server-shared/.../UserServiceManager.java:214-236` 顯示 UserService 是由
> **server 進程內 `Runtime.getRuntime().exec("sh")`** 執行指令啟動；`UserService.java:33-99`
> 全程**無 setuid/setcon**，`--uid=<callingUid>` 僅用於 `userId = uid / 100000` 計算。
> 因此 UserService 進程 uid/SELinux domain = **server 的身份**：adb 啟動時為
> **uid 2000 + u:r:shell:s0**（與 newProcess/rish 相同），root 啟動時為 uid 0。
> 結論修正：**UserService 可以用來以 shell 身份跑 exploit**（adb-started server 下），
> 且它是 api 13.1.5 client 的推薦執行通道（`Shizuku.newProcess` 在 13.1.5 已是
> `private static` + deprecated，經 Maven sources jar 實證）。§5「UserService 不能取代
> root_daemon」的結論仍成立（它只是 shell/root 身份，不是 root daemon），但理由應改為
> 「shell 身份而非 App 身份」。

| 主題 | 檔案 |
|---|---|
| 啟動器（native） | `manager/src/main/jni/starter.cpp`（`run_server`/`start_server`/`main`） |
| 啟動器（Java 指令） | `manager/src/main/java/moe/shizuku/manager/starter/Starter.kt` |
| root/wireless 啟動 | `manager/.../starter/StarterActivity.kt`、`home/StartAdbViewHolder.kt`、`home/StartRootViewHolder.kt` |
| server 主程式 | `server/src/main/java/rikka/shizuku/server/ShizukuService.java` |
| server 常數 | `server/.../ServerConstants.java` |
| newProcess/授權/UserService 轉發 | `api/server-shared/src/main/java/rikka/shizuku/server/Service.java` |
| 身份快取 | `api/server-shared/.../util/OsUtils.java` |
| UserService 啟動器 | `starter/src/main/java/moe/shizuku/starter/ServiceStarter.java` |
| UserService 實作 | `api/server-shared/.../UserService.java`、`UserServiceManager.java`、`UserServiceRecord.java` |
| rish（native fork/exec） | `api/rish/src/main/cpp/rikka_rish_RishHost.cpp` |
| 授權 JSON | `server/.../ShizukuConfigManager.java`、`ShizukuConfig.java`、`api/server-shared/.../ConfigManager.java` |
| client 管理/授權快取 | `api/server-shared/.../ClientManager.java`、`ClientRecord.java` |
| API 公開介面 | `api/api/src/main/java/rikka/shizuku/Shizuku.java`、`api/shared/.../ShizukuApiConstants.java` |
| AIDL | `api/aidl/src/main/aidl/moe/shizuku/server/IShizukuService.aidl`、`IRemoteProcess.aidl` |
| client 端 provider | `api/provider/src/main/java/rikka/shizuku/ShizukuProvider.java` |
| 授權 UI | `manager/.../authorization/RequestPermissionActivity.kt`、`AuthorizationManager.kt` |
| 開機重啟 | `manager/.../receiver/BootCompleteReceiver.kt` |
