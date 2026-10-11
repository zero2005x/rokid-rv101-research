# USB 偵錯防偵測模組（自建 LSPosed/Vector 模組）實作與實機驗證 — 2026-09-30

> English: [usbdebug_antidetect_module_2026-09-30_EN.md](usbdebug_antidetect_module_2026-09-30_EN.md)

> **狀態更新（2026-09-30 17:15）：本報告描述的軟體已於當日應要求「完整移除」。**
> 移除範圍：`com.rokid.rc.usbdebughider`（模組）、`com.rokid.rc.debugdetectprobe`（探針）、
> 框架模組 `zygisk_vector`（Vector）＋ `zygisksu`（NeoZygisk）、`/data/local/tmp/usbdebug_hider.conf`
> 與所有推送的暫存檔、`/data/adb/lspd` 與 `/data/adb/neozygisk`。
> 移除後獨立驗證：`apd module list` = `[]`、無 `vectord`/`zygiskd` 行程、App 私有資料目錄已刪除、
> 本專案 App 不在 `pm list packages`、`/data/adb` 回到原版版面；裝置 adb 狀態維持
> `adb_enabled=1` / `sys.usb.config=adb` / `init.svc.adbd=running`。
> 指令：`./deploy/usbdebug_hider_ctl.sh remove-all`（日誌：`build-cache/remove_all_2026-09-30.log`）。
> 主機端專案檔案（原始碼、APK、框架 zip、本報告）**未刪除**。

## 0. 結論（TL;DR）

在 **Redmi Note 11 Pro+ 5G（`21091116UG` / pissarropro，Android 13 / SDK 33，MIUI）**、
root 方案 **APatch**（`su -v` = `11224:APatch`）上，自建了一個 Xposed 模組
**DebugMask**（`com.rokid.rc.usbdebughider`），讓**指定 App** 讀到的
USB 偵錯／無線偵錯／開發人員選項一律是「關」，而 **adb 本身完全不受影響**
（shell/root/system 讀到的仍是真值）。

實機 A/B 對照（同一支探針 App，同一台機器，只差 scope 有無）：

| 偵測途徑 | 遮蔽前（A） | 遮蔽後（B） |
|---|---|---|
| `Settings.Global.getInt("adb_enabled")` | `1` | **`0`** |
| `Settings.Global.getInt("development_settings_enabled")` | `1` | **`0`** |
| `ContentResolver.query(content://settings/global, name=?)` | `1` | **`0`** |
| `SystemProperties.get("sys.usb.config")`（Java） | `adb` | **`mtp`** |
| `__system_property_get("init.svc.adbd")`（native） | `running` | **`stopped`** |
| shell（uid 2000）/ root 讀同一批值 | `1` / `adb` / `running` | **不變（真值）** |

- 兩層 hook 都生效：**Java 層**（Settings provider 與 `SystemProperties`）＋
  **native 層 PLT/GOT**（攔 `__system_property_get` 等，實測改寫 128–131 個 GOT slot）。
- 設定**熱重載**：改 `/data/local/tmp/usbdebug_hider.conf` 後 **2 秒內**生效，
  同一支 App 行程**不必重啟**（實測：切換 `mask_settings=0` 後，settings 立刻回到真值 `1`，
  屬性仍維持遮蔽）。
- 全程 **只用 adb.exe 自動化**：框架安裝用 `apd module install`，
  模組啟用與 scope 用 Vector 內建 CLI `/data/adb/lspd/cli`，設定用 `adb push`。
- **重開機後自動生效**（第三次重開機實測）：框架、模組、scope、設定檔都持久化，
  不需再插線武裝；開機後已自動注入真實背景 App（例：`com.ubercab`）。
- scope 已由「全部 165 個第三方 App」**收窄成 21 個銀行＋支付 App**（見 §5.6），
  設定清單在 [`deploy/usbdebug_hider_scope_banks.txt`](../deploy/usbdebug_hider_scope_banks.txt)。

> 僅供**自有/授權設備**的安全研究使用。

---

## 1. 起點：裝置現況勘查

| 項目 | 實測值 |
|---|---|
| 型號 / 系統 | `21091116UG`（pissarropro，Redmi Note 11 Pro+ 5G）/ Android 13 / SDK 33 |
| root 方案 | **APatch 11224**（`su -v` → `11224:APatch`；SELinux context `u:r:magisk:s0`） |
| `/data/adb/modules` | 空（無任何模組） |
| Xposed 框架 | **無**（無 LSPosed / 無 Zygisk） |
| `adb_enabled` / `development_settings_enabled` | `1` / `1` |
| `sys.usb.config` / `init.svc.adbd` | `adb` / `running`（USB adb，非 TCP） |
| 裝置端 sqlite3 | **沒有**（busybox 也沒有；只有 `unzip`） |
| APatch CLI | `apd module install|enable|disable|list` **可用** |

兩個關鍵限制在動手前就先確認：

1. **`magisk` 指令不存在**（APatch 只在 `/data/adb/apd` 提供 `resetprop`/`magiskpolicy` 等相容子命令），
   因此不能照抄 Magisk 模組的安裝方式。
2. **沒有 sqlite3**：LSPosed/Vector 的模組與 scope 存在 SQLite，若沒有 CLI 就得自己想辦法。

---

## 2. 方案選型

先查了現成套件，再決定自建：

| 方案 | 結論 |
|---|---|
| [StealthDebug](https://github.com/sp11xy/StealthDebug)（Magisk shell 模組） | **不採用**。它用 `resetprop` 迴圈直接改**全域屬性區**：`sys.usb.config=mtp`、`service.adb.tcp.port=-1`、`init.svc.adbd=stopped`。在 Android 上 `sys.usb.config` 是 init 的觸發屬性，改它可能讓 USB gadget 真的切成 MTP，**把 adb 弄斷**；而且它**不碰 settings provider**，`adb_enabled` 仍讀到 `1`（作者自己在 README 說明需另配 LSPosed 模組）。 |
| [DuckUSB](https://modules.lsposed.org/module/com.strawing.duckusb/)（LSPosed 模組） | **可用替代品**。設計正確（在 `system_server` 一處 hook、uid<10000 讀真值），但需先裝完整框架，且我們要的「只 hook 指定 App」與可審計性不如自建。 |
| **自建模組（本報告）** | 選這條：程式碼在 `android/usbdebug-hider/`，可審計、可只針對指定 App、可熱重載、可全 adb 自動化。 |

框架鏈的選擇有實測案例可參考：APatch 上 **Dr-TSNG/ZygiskNext + LSPosed 失敗、
JingMatrix 系列成功**（[案例](https://blog.383494.xyz/flash-lsposed-on-apatch/)）。
加上 `Dr-TSNG/ZygiskNext` 倉庫已消失（GitHub API 404），因此採用：

- **[NeoZygisk](https://github.com/JingMatrix/NeoZygisk) v2.4 (289)** — 用 ptrace 對 zygote 注入，
  為 APatch/KernelSU 提供 Magisk Zygisk API。
- **[Vector](https://github.com/JingMatrix/Vector) v2.2 (3080)** — LSPosed 的維護分支（原名 JingMatrix/LSPosed），
  支援 Android 8.1–17，且**同時支援 legacy Xposed API 與現代 libxposed API**，
  所以我們可以用最穩定的經典 API（`de.robv.android.xposed:api:82`）寫模組。

---

## 3. 實作

### 3.1 模組架構（`android/usbdebug-hider/`）

```
src/main/assets/xposed_init           → com.rokid.rc.usbdebughider.MaskEntry
src/main/AndroidManifest.xml          → xposedmodule / xposedminversion=82
src/main/java/.../MaskEntry.java      → 入口：只對 uid>=10000 的目標 App 安裝 hook
src/main/java/.../MaskConfig.java     → 設定模型：內建預設 + 執行期 conf 熱重載
src/main/java/.../SettingsMask.java   → Settings 遮蔽（3 條途徑）
src/main/java/.../PropMask.java       → 屬性遮蔽（Java 層）
src/main/java/.../NativeMask.java     → 屬性遮蔽（native 層載入與控制）
src/main/jni/debugmask.c              → PLT/GOT hook（libdebugmask.so）
```

**只注入、不碰的東西**（安全邊界）：

- `system_server`（`packageName == "android"`）**絕不 hook** —— 避免動到全系統行為與 bootloop 風險。
- uid < 10000（root/system/shell）**不遮蔽** —— 這是 adb、設定頁、系統檔案傳輸仍正常的原因。
- 模組自己的行程不遮蔽 —— 模組狀態頁顯示的永遠是**裝置真值**，可當除錯對照。
- `com.android.mtp`、media provider、downloads 等預設排除（避免影響 MTP/媒體）。

### 3.2 Settings 遮蔽：三條途徑

| # | Hook 點 | 覆蓋的偵測寫法 |
|---|---|---|
| 1 | `android.provider.Settings$NameValueCache.getStringForUser` | **所有** `Settings.Global/Secure/System` 的 `getInt/getLong/getFloat/getString`（真正的取值點） |
| 2 | `Settings$Global/Secure/System` 的靜態 getter | 少數 ROM 走不同路徑時的保險 |
| 3 | `ContentResolver.query` | 直接讀 `content://settings/global` cursor 的偵測器 |

第 3 條只查 authority 為 `settings` / `com.android.providers.settings` 的查詢，
而且**先掃描確認真的有命中列才包裝**；命中時複製成 `MatrixCursor`（保留欄位型別、複製 extras、
關閉原 cursor），沒命中就原封不動回傳，把對呼叫端的行為影響降到最低。
另支援 `content://settings/global/adb_enabled` 這種「key 在路徑上」的查法。

遮蔽的 key（預設，可用 conf 增減）：

```
adb_enabled=0   adb_wifi_enabled=0   development_settings_enabled=0
```

### 3.3 屬性遮蔽：兩層並用

**Java 層**：hook `android.os.SystemProperties` 的 `get/getInt/getLong/getBoolean`。

**native 層（`debugmask.c`）**：因為真偵測器會直接呼叫 libc 的
`__system_property_get()`，Java hook 完全看不到。做法是：

1. `dlopen` 解析真函式指標（`RTLD_DEFAULT`）。
2. `dl_iterate_phdr` 走訪行程內所有已載入 ELF 模組，解析 `PT_DYNAMIC` →
   `DT_SYMTAB/DT_STRTAB/DT_JMPREL/DT_RELA`，找出符號為
   `__system_property_get` / `__system_property_read` / `__system_property_read_callback`
   的 `JUMP_SLOT`/`GLOB_DAT` 重定位，`mprotect` 後改寫 GOT。
3. 跳過 `libdebugmask.so` 自己與 linker（避免自我遞迴）。
4. 順手 hook `dlopen` / `android_dlopen_ext`，之後才載入的 `.so` 也會補掛。

遮蔽表由 Java 端以 `key=value\n` 傳入，用原子指標換表，因此**熱更新不需要重新 patch**。

實測（Vector 框架 log）：

```
UsbDebugMask: native 層已載入 -> /data/user/0/com.rokid.rc.debugdetectprobe/cache/libdebugmask.so
UsbDebugMask: native PLT hook 完成，改寫 128 個 GOT slot
```

遮蔽的屬性（預設）：

```
sys.usb.config / sys.usb.state / persist.sys.usb.config / vendor.usb.config → mtp
init.svc.adbd → stopped        service.adb.root → 0        service.adb.tcp.port → -1
sys.usb.ffs.ready / sys.usb.ffs.adb.ready → 0
ro.debuggable → 0              ro.adb.secure → 1
ro.build.type → user           ro.build.tags → release-keys
```

> 這些都只在**目標 App 的行程內**成立；屬性區本體沒被改動，
> 因此不會像 `resetprop` 那樣有弄斷 USB/adb 的風險。

### 3.4 兩個實作上的坑（都靠實測才發現）

**(a) native 層怎麼進到目標行程？**

目標 App 行程裡沒有我們的 `.so`，而三條直覺路徑全部走不通（實測）：

1. `PackageManager.getApplicationInfo(模組包名)` → **`NameNotFoundException`**
   —— Android 11+ 的**套件可見性**限制，目標 App 看不到我們的包名。
2. `/proc/self/maps` 找模組 APK 路徑 → **找不到**
   —— Vector 用 in-memory dex 載入模組，APK 不會被 mmap。
3. 模組 APK 的 `nativeLibraryDir` → AGP 8 預設 `extractNativeLibs=false`，磁碟上根本沒有解出的 `.so`。

因此改成：建置期把 `libdebugmask.so` 以 **Base64 內嵌進 dex**（Gradle 由
`src/main/jniLibs/arm64-v8a/libdebugmask.so` 產生 `EmbeddedSo.java`），
執行期解到**目標 App 自己的 cacheDir** 再 `System.load()`。
不依賴任何路徑探索、不需 root、不寫別人的目錄。

**(b) 連結期漏 `-llog` 會讓 `dlopen` 失敗**：
第一次載入失敗訊息是
`dlopen failed: cannot locate symbol "__android_log_print"`，
補上 `-llog -ldl` 後 `DT_NEEDED` 才正確帶入 `liblog.so`。

### 3.5 全 adb 自動化：Vector CLI

Vector 的模組 zip 內建一支可腳本化的 CLI（`/data/adb/lspd/cli`，
主類別 `org.matrix.vector.daemon.Cli`）：

```
status / modules ls|enable|disable / scope add|set|rm|ls / config / db / log cat
```

於是「啟用模組 + 設定 165 個 App 的 scope」全部由 adb 完成，不必手動點 UI：

```bash
su -c '/data/adb/lspd/cli modules enable com.rokid.rc.usbdebughider'
su -c '/data/adb/lspd/cli scope set com.rokid.rc.usbdebughider pkg1/0 pkg2/0 ...'
```

備援方案也準備好了：`deploy/mini_sqlite3.c`（用 `dlopen("/system/lib64/libsqlite.so")`
自製的裝置端 SQLite CLI，已實測可讀寫 `/data/adb/**/config/*.db`），
若哪天 CLI 不能用，仍可直接改 DB。**裝置端沒有 sqlite3 不是阻礙。**

---

### 3.6 硬化過程中修掉的兩個真 bug（都由實機 log 抓到）

**(a) hook `dlopen` 會破壞 App 的 linker namespace（已修）**

第一版 native 層為了「之後才載入的 .so 也能補掛」，直接 GOT-hook 了
`dlopen` / `android_dlopen_ext`。實測在台灣行動支付上出現：

```
E linker: library "/vendor/lib64/egl/libMEOW_qt.so" ... needed or dlopened by
          "/data/user/0/tw.com.twmp.twhcewallet/cache/libdebugmask.so"
          is not accessible for the namespace: [name="clns-6", ...]
```

原因：bionic 的 `dlopen` 用**呼叫端位址**決定要用哪個 linker namespace。我們的替身
函式位於模組自己的 isolated namespace，一攔截就讓 App 後續的 `dlopen` 全部改用錯的
namespace，載不到 vendor／私有 .so。

修法：改用 bionic 給 `libnativeloader` 用的 loader API
（`__loader_dlopen(filename, flags, caller_addr)` /
`__loader_android_dlopen_ext(..., caller_addr)`），把
`__builtin_return_address(0)`（GOT/PLT 跳轉不打斷 LR，所以就是 App 的返回位址）
當 `caller_addr` 傳進去 —— namespace 正確，又能拿到「載入完成」的時機補掃。

而 `dlsym(RTLD_DEFAULT, "__loader_dlopen")` 在 Android 13 取不到（實測 `0x0`），
所以改成**從已載入模組的 GOT 反查**：`libnativeloader` 有匯入這些符號，其 GOT 內
就是已解析好的真位址（實測取得 `0x7de0d941b0` / `0x7de0d940b0`）。
若兩者都取不到，就**完全不安裝** dlopen hook（絕不讓 `dlopen` 回 NULL 而使 App 崩潰）。

**(b) hook Java 的 `Runtime.loadLibrary0` / `System.loadLibrary` 不可行（已移除）**

曾嘗試用 Java hook 在「App 載入 native 庫」時觸發補掃，結果探針 App 直接崩：

```
java.lang.UnsatisfiedLinkError: dlopen failed: library "libframework-connectivity-jni.so" not found
  at java.lang.Runtime.loadLibrary0(Runtime.java:1123)
  at java.lang.reflect.Method.invoke(Native Method)   ← caller class 遺失
```

原因是 hook 後原方法被以反射呼叫，`loadLibrary0` 用來決定 classloader/namespace 的
「呼叫端類別」變成反射框架，導致載入路徑解析錯誤。此路不通，改回 native 端
（loader API）＋ 背景重掃（每秒、以模組基底去重，成本從 O(全部) 降到 O(新增)）。

## 4. 安裝流程（實測步驟，兩次重開機都正常開機）

```bash
ADB=/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe
A() { "$ADB" -s <PHONE_SERIAL> "$@"; }

# 1) 推送並安裝 NeoZygisk（Zygisk API for APatch）
A push NeoZygisk-v2.4-289-release.zip /data/local/tmp/
A shell "su -c '/data/adb/apd module install /data/local/tmp/NeoZygisk-v2.4-289-release.zip'"
A reboot        # 開機後確認 zygisk-ptrace64 / zygiskd64 在跑

# 2) 安裝 Vector（Xposed 框架）
A push Vector-v2.2-3080-Release.zip /data/local/tmp/
A shell "su -c '/data/adb/apd module install /data/local/tmp/Vector-v2.2-3080-Release.zip'"
A reboot        # 開機後確認 vectord 在跑、/data/adb/lspd/cli 可用

# 3) 建置並安裝我們的模組 + 對照組探針
./deploy/usbdebug_hider_ctl.sh build
./deploy/usbdebug_hider_ctl.sh install     # 安裝 APK、推設定檔、啟用模組
./deploy/usbdebug_hider_ctl.sh scope-all   # scope = 所有第三方 App
./deploy/usbdebug_hider_ctl.sh verify      # 對照驗證
```

`scope` 變更**不需重開機**：目標 App 下次啟動即被注入（實測 force-stop 後重啟即生效）。

---

## 5. 驗證

### 5.1 A/B 對照（同一支探針 App，第三方 uid 10362）

證據檔：[`detect_A_baseline_unmasked.txt`](media_usbdebug_hider/detect_A_baseline_unmasked.txt)、
[`detect_B_masked_ondevice.txt`](media_usbdebug_hider/detect_B_masked_ondevice.txt)、
[`detect_diff_A_vs_B.patch`](media_usbdebug_hider/detect_diff_A_vs_B.patch)

探針（`android/debugdetectprobe/`）刻意把偵測途徑拆成四類，避免「只驗一條路就宣稱成功」：

| 類別 | 向量數 | A（未遮） | B（遮蔽後） |
|---|---|---|---|
| 1. Settings getter（getInt/getString × 3 key） | 6 | `1/1/0` | **`0/0/0`** |
| 2. Cursor 直讀（單筆／路徑式／全表／LIST） | 8 | `1/1/1`、全表 `adb_enabled=1` | **全 `0`** |
| 3. Java `SystemProperties`（反射） | 13 | `adb` / `running` | **`mtp` / `stopped`** |
| 4. **native** `__system_property_get` | 11 | `adb` / `running` | **`mtp` / `stopped`** |

未受影響（本來就該如此）：`query.list_global = rows=489`（LIST 查詢不受影響）、
`build.TYPE/TAGS/FINGERPRINT`（編譯期常數，本機本來就是 `user`/`release-keys`）、
`debug.isDebuggerConnected=false`。

**同一台手機、同一時刻的兩張截圖**（左：被注入的探針讀到的假值；右：模組自己的狀態頁，
因為模組不遮蔽自己，所以顯示裝置真值）：

| 探針（第三方 App 視角） | 模組狀態頁（真值對照） |
|---|---|
| ![探針](media_usbdebug_hider/probe_masked_ui.png) | ![模組](media_usbdebug_hider/module_ui_real_state.png) |

### 5.2 反向條件：不能把自己的 adb 弄瞎

| 視角 | `adb_enabled` | `sys.usb.config` | `init.svc.adbd` |
|---|---|---|---|
| shell（uid 2000） | `1` | `adb` | `running` |
| root（`su -c`） | — | `adb` | `running` |
| 被注入的第三方 App | `0` | `mtp` | `stopped` |

USB 連線在整個過程持續可用（這份報告的所有指令都是同一條 USB adb 連線下的結果），
MTP/媒體相關套件已在預設排除名單中。

### 5.3 熱重載（同一行程、不重啟）

1. 在裝置上把 `/data/local/tmp/usbdebug_hider.conf` 的 `mask_settings` 由 `1` 改成 `0`。
2. 等 3 秒（模組每 2 秒檢查一次）。
3. 用 `input tap` 按探針的「重新偵測」（**同一個行程**）。

結果：settings 三條路徑立刻回到**真值 `1`**，而屬性兩層仍維持 `mtp`/`stopped`。
→ 證明 Java hook 每次取值都重讀設定、native 表也能熱更新，**不必重啟 App、不必重開機**。

### 5.4 注入不弄壞真實 App（煙霧測試）

對 3 支 scoped 的真實 App 各做一次「force-stop → 啟動 → 檢查」：

| App | 前景執行 | 崩潰（crash buffer） | 模組注入 |
|---|---|---|---|
| `com.duckduckgo.mobile.android` | ✅ | 0 | ✅ |
| `org.telegram.messenger` | ✅ | 0 | ✅ |
| `tw.gov.invoice` | ✅ | 0 | ✅ |

### 5.5 重開機持久性（第三次重開機實測）

整套設定在重開機後**自動生效**，不需再插線武裝（開機約 16 秒）：

| 檢查項 | 重開機後 |
|---|---|
| Vector status | `Framework Version 2.2 / API 102 / Enabled Modules: 1` |
| 模組 | `com.rokid.rc.usbdebughider  enabled` |
| scope | 165 筆（不變） |
| `/data/local/tmp/usbdebug_hider.conf` | 存在且被讀取（`source=` 指向它） |
| 探針視角 | `adb_enabled=0`、`sys.usb.config=mtp`、`init.svc.adbd=stopped`（Java＋native） |
| shell 視角 | `adb_enabled=1`、`sys.usb.config=adb`、`init.svc.adbd=running`（真值） |

重開機後不久，模組已自動注入**真實背景 App**（框架 log 實例）：

```
UsbDebugMask: 已遮蔽 com.ubercab (uid=10386, settings=true, props=true,
              source=/data/local/tmp/usbdebug_hider.conf)
UsbDebugMask: native 層已載入 -> /data/user/0/com.ubercab/cache/libdebugmask.so
UsbDebugMask: native PLT hook 完成，改寫 128 個 GOT slot
```

### 5.6 scope 收窄成「銀行 ＋ 支付」（21 個）與真實 App 驗收

由「全部第三方 App」收窄成白名單；App 名稱不是用包名猜的，是逐一開
`設定 → 應用程式資訊` 讀標題核對（2026-09-30）。

| 類別 | App（package） |
|---|---|
| 銀行（11） | 國泰世華 `com.cathaybk.mymobibank.android`、中國信託 `com.chinatrust.mobilebank`、玉山銀行 `com.esunbank`、台北富邦 `com.fubon.aibank`、Richart Life `tw.com.taishinbank.ccapp`、Richart `tw.com.taishinbank.richart`、臺灣企銀 `com.tbb.mb.prod`、土銀行動銀行 `com.landbank.newrelease.android`、華南銀行+ `tw.com.hncb.nmbank`、New New Bank `com.ubot.dbank`、行動郵局 `tw.gov.post.mpost` |
| 支付／票證／錢包（9） | 台灣行動支付 `tw.com.twmp.twhcewallet`、Easy Wallet `com.easycard.wallet`、iPASS MONEY `com.ipass.ipassmoney`、歐付寶 `com.allpay.tw`、OPENPOINT `tw.net.pic.m.openpoint`、全家便利商店 `grasea.familife`、Hi-Life VIP `com.hilife.vipapp`、全聯福利中心 `com.pxmart.android`、Google 錢包 `com.google.android.apps.walletnfcrel` |
| 驗證用（1） | DebugDetectProbe `com.rokid.rc.debugdetectprobe`（本專案探針，可隨時移除） |

指令：`./deploy/usbdebug_hider_ctl.sh scope-from-file deploy/usbdebug_hider_scope_banks.txt`
（另有加密貨幣／電商錢包／2FA 等可選群組列在清單檔註解中。）

**真實 App 驗收**（每支都是 force-stop → 啟動 → 觀察前景與 crash buffer）：

| App | 前景 | 崩潰 | namespace 錯誤 |
|---|---|---|---|
| 中國信託 | ✅ | 0 | 0 |
| 玉山銀行 | ✅ | 0 | 0 |
| Easy Wallet（悠遊卡） | ✅ | 0 | 0 |
| iPASS MONEY | ✅ | 0 | 0 |
| 全聯福利中心 | ✅ | 0 | 0 |
| 台灣行動支付 | ✅（見下） | 0（與模組無關） | **0** |

**台灣行動支付的特例（A/B 證明不是我方問題）**：它在這台 root 機上會自己
crash-loop —— `SplashActivity.zxl` 的 `NullPointerException: String.equals(null)`
（App 自己的整合性檢查在 root 環境取到 null 後崩潰）。**把本模組 `disable` 後重測，
一樣 12 秒內崩 14 次**，所以確定是該 App 的既有問題，與遮蔽無關。
（附帶收穫：這個 A/B 過程抓到並修掉了本模組的一個真 bug，見 §3.6。）

### 5.7 順帶量到的裝置事實

- **一般 App 讀不到 `/proc/net/tcp`**（`Permission denied`，SELinux）→
  「枚舉 adb TCP 5555 監聽埠」這條偵測路徑對第三方 App 本來就不可用（root 可以）。
- 本機沒有 adb-over-TCP 監聽（`grep :15B3 /proc/net/tcp` 無結果），只有 USB adb。
- `ro.debuggable` 在本機本來就是 `0`、`ro.adb.secure=1`（MIUI user build）。

---

## 6. 已知限制（重要，別過度樂觀）

1. **native GOT patch 的邊界**：若目標 App 在我們 patch 之前就用 `dlsym` 取得
   `__system_property_get` 並快取函式指標，或在純 JIT/native 直讀屬性區，
   仍可能拿到真值。我們已 hook `dlopen`/`android_dlopen_ext` 補掛新載入的 `.so`，
   但無法回收「已被快取的函式指標」。
2. **Android 14+ 可能無法再從 App 資料目錄載入 .so**：本機（API 33）可用，但系統會警告
   `Attempt to load writable file: .../cache/libdebugmask.so. This will throw on a
   future Android version.`。若目標機是 API 34+，native 層可能整個載不起來
   （Java 層仍有效，屬優雅降級）。
3. **`.so` 落地痕跡（最大破口）**：native 層會在
   `/data/user/0/<目標App>/cache/libdebugmask.so` 留下檔案。
   會檢查自己 cacheDir／`/proc/self/maps` 的強力偵測器（例如自帶完整性檢查的 App）
   可能發現這個不該存在的檔案。DuckUSB 的 system_server 單點 hook 沒有這個問題。
   若目標 App 屬於此類，建議改用 DuckUSB 或在 conf 關掉 native 層（`mask_props_native=0`）。
4. **模組套件本身可見**：`pm list packages` 可以看到 `com.rokid.rc.usbdebughider`。
   要更隱蔽需另外處理（例如 `pm hide`／更中性的包名／LSPosed 的模組隱藏機制），本次未做。
5. **只處理 debug 狀態，不處理 root**：本模組不隱藏 APatch/KernelSU 的 root 痕跡。
   Play Integrity 之類會失敗（該手機本來就解鎖 + root），銀行 App 若同時檢查 root，
   仍需搭配 Shamiko/DenyList 類方案。
6. **框架注入本身可被偵測**：Vector 注入會在目標行程留下 ART hook 與 classloader 異常，
   高等級反作弊仍可能看出。此方案提升的是「USB 偵錯狀態」這一項的隱蔽性，不是全面隱形。
7. `Build.TYPE` / `Build.TAGS` 是 `static final` 常數，會被 javac 內聯，hook 無效
   （本機本來就是 `user`/`release-keys`，不需處理）。
8. 本方案**未**在 MIUI 的「開發人員選項」UI 上做遮蔽 —— 設定頁由 system_server 讀取，
   而我們刻意不碰 system_server。也就是：**App 看到「關」，系統設定頁仍顯示「開」**。
   這正是 adb 仍可用的原因；若要連設定頁也騙，就得走 DuckUSB 的 system_server 路線。

---

## 7. 風險與救援

- **模組造成卡開機時**：
  ```bash
  ./deploy/usbdebug_hider_ctl.sh panic     # 逐一 disable 所有 APatch 模組
  ```
  或 adb shell 內 `su -c '/data/adb/apd module disable zygisk_vector'`（再 reboot）。
  最壞情況用 APatch 安全模式（開機時按音量鍵）停用模組。
- **停用本模組**：`./deploy/usbdebug_hider_ctl.sh disable`（框架保留，可隨時再啟用）。
- **完整移除**：`./deploy/usbdebug_hider_ctl.sh uninstall`。
- 本次兩次重開機（裝 NeoZygisk、裝 Vector）**都正常開機**，沒有 bootloop。

---

## 8. 產出檔案

| 路徑 | 說明 |
|---|---|
| [`android/usbdebug-hider/`](../android/usbdebug-hider/) | 模組原始碼（Java + JNI） |
| [`android/debugdetectprobe/`](../android/debugdetectprobe/) | 對照組偵測器（4 類偵測途徑、含 native 探針） |
| [`dist/usbdebug-hider-v1.1.apk`](../dist/usbdebug-hider-v1.1.apk) | 可安裝的模組 APK（v1.1：含 dlopen namespace 修復） |
| [`dist/debugdetectprobe-v1.0.apk`](../dist/debugdetectprobe-v1.0.apk) | 可安裝的對照組探針 APK |
| [`deploy/usbdebug_hider_ctl.sh`](../deploy/usbdebug_hider_ctl.sh) | 控制腳本（build/install/scope/verify/panic） |
| [`deploy/usbdebug_hider.conf`](../deploy/usbdebug_hider.conf) | 裝置端設定檔範本（熱重載） |
| [`deploy/usbdebug_hider_scope_banks.txt`](../deploy/usbdebug_hider_scope_banks.txt) | 銀行＋支付 scope 白名單（21 個，含可選群組註解） |
| [`deploy/build_debugmask_so.sh`](../deploy/build_debugmask_so.sh) | native 層編譯腳本 |
| [`deploy/mini_sqlite3.c`](../deploy/mini_sqlite3.c)、[`deploy/build_mini_sqlite3.sh`](../deploy/build_mini_sqlite3.sh) | 裝置端 SQLite CLI（無 sqlite3 時的備援） |
| [`tools/framework/`](../tools/framework/) | 框架安裝包（NeoZygisk / Vector zip） |
| [`reports/media_usbdebug_hider/`](media_usbdebug_hider/) | 驗證證據（A/B 原始輸出、diff、裝置狀態） |

---

## 9. 後續建議

1. ~~收窄 scope~~ **已完成**：目前 21 個（銀行＋支付，§5.6）。要再加加密貨幣／電商錢包／2FA
   等群組，直接編輯清單檔後跑 `scope-from-file`（不必重開機）。
2. **若某支 App 會自我檢查 cacheDir**：把該 App 加進排除，或全域關閉 native 層
   （`mask_props_native=0`），只留 Java 層。
3. **隱藏模組包名**：改中性包名或加 `pm hide`，可再降低「裝了 LSPosed 模組」這個指紋。
4. **補 `_system_property_read_callback` 之外的路徑**：目前 GOT patch 已覆蓋
   `get/read/read_callback` 三種；若遇到直接用 `__system_property_area__` 的極端樣本，
   需改用 kernel 層（KPM）方案。
5. 這套「自建模組 + Vector CLI + mini_sqlite3」的組合可直接複用到眼鏡端（RV101）
   或其他 APatch/KernelSU 裝置。
