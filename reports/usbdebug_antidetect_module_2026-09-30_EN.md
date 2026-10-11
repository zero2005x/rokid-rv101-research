# USB Debugging Anti-Detection Module (Self-Built LSPosed/Vector Module) Implementation and On-Device Validation — 2026-09-30

> 中文版：[usbdebug_antidetect_module_2026-09-30.md](usbdebug_antidetect_module_2026-09-30.md)

> **Status update (2026-09-30 17:15): the software described in this report was "fully removed" on that day, on request.**
> Removal scope: `com.rokid.rc.usbdebughider` (module), `com.rokid.rc.debugdetectprobe` (probe),
> the framework modules `zygisk_vector` (Vector) + `zygisksu` (NeoZygisk), `/data/local/tmp/usbdebug_hider.conf`
> and all pushed temporary files, `/data/adb/lspd` and `/data/adb/neozygisk`.
> Independent verification after removal: `apd module list` = `[]`, no `vectord`/`zygiskd` processes, the app's private data directory deleted,
> this project's app absent from `pm list packages`, `/data/adb` back to its original layout; the device's adb state remains
> `adb_enabled=1` / `sys.usb.config=adb` / `init.svc.adbd=running`.
> Command: `./deploy/usbdebug_hider_ctl.sh remove-all` (log: `build-cache/remove_all_2026-09-30.log`).
> Host-side project files (source, APK, framework zip, this report) were **not deleted**.

## 0. Conclusion (TL;DR)

On a **Redmi Note 11 Pro+ 5G (`21091116UG` / pissarropro, Android 13 / SDK 33, MIUI)** with the root solution **APatch**
(`su -v` = `11224:APatch`), a self-built Xposed module **DebugMask** (`com.rokid.rc.usbdebughider`) was created
so that **designated apps** always read USB debugging / wireless debugging / developer options as "off",
while **adb itself is entirely unaffected** (shell/root/system still read the true values).

On-device A/B comparison (same probe app, same machine, the only difference being whether scope was applied):

| Detection path | Before masking (A) | After masking (B) |
|---|---|---|
| `Settings.Global.getInt("adb_enabled")` | `1` | **`0`** |
| `Settings.Global.getInt("development_settings_enabled")` | `1` | **`0`** |
| `ContentResolver.query(content://settings/global, name=?)` | `1` | **`0`** |
| `SystemProperties.get("sys.usb.config")` (Java) | `adb` | **`mtp`** |
| `__system_property_get("init.svc.adbd")` (native) | `running` | **`stopped`** |
| shell (uid 2000) / root reading the same values | `1` / `adb` / `running` | **unchanged (true values)** |

- Both hook layers take effect: the **Java layer** (Settings provider and `SystemProperties`) plus the
  **native layer PLT/GOT** (intercepting `__system_property_get` and others; 128–131 GOT slots rewritten in measurement).
- **Hot reload** of configuration: after editing `/data/local/tmp/usbdebug_hider.conf` it takes effect **within 2 seconds**,
  and the same app process **does not need to restart** (measured: after switching `mask_settings=0`, settings immediately return to the true value `1`,
  while the properties remain masked).
- **adb.exe alone was used for the whole automation**: framework installation used `apd module install`,
  module enablement and scope used Vector's built-in CLI `/data/adb/lspd/cli`, and configuration used `adb push`.
- **It takes effect automatically after reboot** (measured across a third reboot): framework, module, scope and configuration file all persist,
  with no need to plug in again to arm it; shortly after boot it had already automatically injected into real background apps (e.g. `com.ubercab`).
- The scope has been **narrowed from "all 165 third-party apps" to 21 banking + payment apps** (see §5.6);
  the configuration list is at [`deploy/usbdebug_hider_scope_banks.txt`](../deploy/usbdebug_hider_scope_banks.txt).

> For security research on **owned/authorized devices** only.

---

## 1. Starting Point: Surveying the Device's Current State

| Item | Measured value |
|---|---|
| Model / OS | `21091116UG` (pissarropro, Redmi Note 11 Pro+ 5G) / Android 13 / SDK 33 |
| Root solution | **APatch 11224** (`su -v` → `11224:APatch`; SELinux context `u:r:magisk:s0`) |
| `/data/adb/modules` | empty (no modules at all) |
| Xposed framework | **none** (no LSPosed / no Zygisk) |
| `adb_enabled` / `development_settings_enabled` | `1` / `1` |
| `sys.usb.config` / `init.svc.adbd` | `adb` / `running` (USB adb, not TCP) |
| On-device sqlite3 | **absent** (busybox absent too; only `unzip`) |
| APatch CLI | `apd module install|enable|disable|list` **available** |

Two key constraints were confirmed before starting work:

1. **The `magisk` command does not exist** (APatch only provides compatible subcommands such as `resetprop`/`magiskpolicy` under `/data/adb/apd`),
   so the Magisk module installation method cannot be copied verbatim.
2. **No sqlite3**: LSPosed/Vector store modules and scope in SQLite, so without a CLI we had to find our own way.

---

## 2. Solution Selection

Existing packages were reviewed first, and then a self-built module was decided on:

| Option | Conclusion |
|---|---|
| [StealthDebug](https://github.com/sp11xy/StealthDebug) (Magisk shell module) | **Not adopted.** It uses a `resetprop` loop to directly modify the **global property area**: `sys.usb.config=mtp`, `service.adb.tcp.port=-1`, `init.svc.adbd=stopped`. On Android, `sys.usb.config` is init's trigger property; changing it may actually switch the USB gadget to MTP and **break adb**; moreover it **does not touch the settings provider**, so `adb_enabled` still reads `1` (the author's own README notes that an LSPosed module must be configured separately). |
| [DuckUSB](https://modules.lsposed.org/module/com.strawing.duckusb/) (LSPosed module) | **A usable alternative.** Correctly designed (hooks at a single point in `system_server`, uid<10000 reads true values), but it requires installing the full framework first, and its "hook only designated apps" behavior and auditability are inferior to a self-built module. |
| **Self-built module (this report)** | This is the path chosen: the code is in `android/usbdebug-hider/`, auditable, able to target only designated apps, hot-reloadable, and fully automatable over adb. |

For the framework chain there was a measured case to consult: on APatch, **Dr-TSNG/ZygiskNext + LSPosed failed,
while the JingMatrix series succeeded** ([case](https://blog.383494.xyz/flash-lsposed-on-apatch/)).
Combined with the fact that the `Dr-TSNG/ZygiskNext` repository has disappeared (GitHub API 404), the following were adopted:

- **[NeoZygisk](https://github.com/JingMatrix/NeoZygisk) v2.4 (289)** — injects into zygote with ptrace,
  providing the Magisk Zygisk API for APatch/KernelSU.
- **[Vector](https://github.com/JingMatrix/Vector) v2.2 (3080)** — the maintained fork of LSPosed (formerly JingMatrix/LSPosed),
  supporting Android 8.1–17 and **both the legacy Xposed API and the modern libxposed API**,
  so we can write the module with the most stable classic API (`de.robv.android.xposed:api:82`).

---

## 3. Implementation

### 3.1 Module Architecture (`android/usbdebug-hider/`)

```
src/main/assets/xposed_init           → com.rokid.rc.usbdebughider.MaskEntry
src/main/AndroidManifest.xml          → xposedmodule / xposedminversion=82
src/main/java/.../MaskEntry.java      → entry point: installs hooks only for target apps with uid>=10000
src/main/java/.../MaskConfig.java     → configuration model: built-in defaults + runtime conf hot reload
src/main/java/.../SettingsMask.java   → Settings masking (3 paths)
src/main/java/.../PropMask.java       → property masking (Java layer)
src/main/java/.../NativeMask.java     → property masking (native-layer loading and control)
src/main/jni/debugmask.c              → PLT/GOT hook (libdebugmask.so)
```

**Things it only injects into and never touches** (safety boundary):

- `system_server` (`packageName == "android"`) is **never hooked** —— to avoid affecting system-wide behavior and the risk of a bootloop.
- uid < 10000 (root/system/shell) is **not masked** —— this is why adb, the settings page and system file transfer still work normally.
- The module's own process is not masked —— the module status page always shows the **device's true values**, so it can serve as a debugging control.
- `com.android.mtp`, the media provider, downloads and others are excluded by default (to avoid affecting MTP/media).

### 3.2 Settings Masking: Three Paths

| # | Hook point | Detection styles covered |
|---|---|---|
| 1 | `android.provider.Settings$NameValueCache.getStringForUser` | **All** `getInt/getLong/getFloat/getString` on `Settings.Global/Secure/System` (the real value-retrieval point) |
| 2 | The static getters of `Settings$Global/Secure/System` | Insurance in case a few ROMs take a different path |
| 3 | `ContentResolver.query` | Detectors that read the `content://settings/global` cursor directly |

The third path only inspects queries whose authority is `settings` / `com.android.providers.settings`,
and it **first scans to confirm there really is a matching row before wrapping**; on a match it copies into a `MatrixCursor` (preserving field types, copying extras,
and closing the original cursor), and returns it untouched when there is no match, minimizing the behavioral impact on callers.
It also supports the "key in the path" form, such as `content://settings/global/adb_enabled`.

Masked keys (defaults, adjustable via conf):

```
adb_enabled=0   adb_wifi_enabled=0   development_settings_enabled=0
```

### 3.3 Property Masking: Two Layers Used Together

**Java layer**: hook `get/getInt/getLong/getBoolean` of `android.os.SystemProperties`.

**Native layer (`debugmask.c`)**: because a real detector calls libc's `__system_property_get()` directly,
which a Java hook cannot see at all. The approach is:

1. `dlopen` to resolve the real function pointer (`RTLD_DEFAULT`).
2. `dl_iterate_phdr` to walk every ELF module already loaded in the process, parse `PT_DYNAMIC` →
   `DT_SYMTAB/DT_STRTAB/DT_JMPREL/DT_RELA`, and find the `JUMP_SLOT`/`GLOB_DAT` relocations whose symbols are
   `__system_property_get` / `__system_property_read` / `__system_property_read_callback`,
   then `mprotect` and rewrite the GOT.
3. Skip `libdebugmask.so` itself and the linker (to avoid self-recursion).
4. Also hook `dlopen` / `android_dlopen_ext` so that `.so` files loaded later are also patched.

The masking table is passed in from the Java side as `key=value\n` and swapped in via an atomic pointer, so **hot updates require no re-patching**.

Measured (Vector framework log):

```
UsbDebugMask: native 層已載入 -> /data/user/0/com.rokid.rc.debugdetectprobe/cache/libdebugmask.so
UsbDebugMask: native PLT hook 完成，改寫 128 個 GOT slot
```

Masked properties (defaults):

```
sys.usb.config / sys.usb.state / persist.sys.usb.config / vendor.usb.config → mtp
init.svc.adbd → stopped        service.adb.root → 0        service.adb.tcp.port → -1
sys.usb.ffs.ready / sys.usb.ffs.adb.ready → 0
ro.debuggable → 0              ro.adb.secure → 1
ro.build.type → user           ro.build.tags → release-keys
```

> These hold only **inside the target app's process**; the property area itself is not modified,
> so there is none of the risk that `resetprop` carries of breaking USB/adb.

### 3.4 Two Implementation Pitfalls (Both Found Only Through On-Device Measurement)

**(a) How does the native layer get into the target process?**

The target app's process does not contain our `.so`, and all three intuitive paths failed (measured):

1. `PackageManager.getApplicationInfo(module package name)` → **`NameNotFoundException`**
   —— Android 11+'s **package visibility** restriction; the target app cannot see our package name.
2. Finding the module APK path via `/proc/self/maps` → **not found**
   —— Vector loads the module using an in-memory dex, so the APK is never mmap'd.
3. The module APK's `nativeLibraryDir` → AGP 8 defaults to `extractNativeLibs=false`, so there is no extracted `.so` on disk at all.

So the approach was changed to: at build time, embed `libdebugmask.so` into the dex as **Base64** (Gradle generates
`EmbeddedSo.java` from `src/main/jniLibs/arm64-v8a/libdebugmask.so`), and at runtime extract it into
**the target app's own cacheDir** and then `System.load()` it.
No path discovery, no root, and no writing into anyone else's directory.

**(b) A missing `-llog` at link time makes `dlopen` fail**:
The first load failure message was
`dlopen failed: cannot locate symbol "__android_log_print"`,
and only after adding `-llog -ldl` did `DT_NEEDED` correctly pull in `liblog.so`.

### 3.5 Full adb Automation: The Vector CLI

Vector's module zip includes a scriptable CLI (`/data/adb/lspd/cli`,
main class `org.matrix.vector.daemon.Cli`):

```
status / modules ls|enable|disable / scope add|set|rm|ls / config / db / log cat
```

So "enabling the module + setting the scope of 165 apps" was done entirely over adb, without tapping through the UI:

```bash
su -c '/data/adb/lspd/cli modules enable com.rokid.rc.usbdebughider'
su -c '/data/adb/lspd/cli scope set com.rokid.rc.usbdebughider pkg1/0 pkg2/0 ...'
```

A fallback was prepared as well: `deploy/mini_sqlite3.c` (a self-made on-device SQLite CLI using
`dlopen("/system/lib64/libsqlite.so")`, already measured to read and write `/data/adb/**/config/*.db`),
so if the CLI ever stops working we can still edit the DB directly. **Not having sqlite3 on the device is not an obstacle.**

---

### 3.6 Two Real Bugs Fixed During Hardening (Both Caught by On-Device Logs)

**(a) Hooking `dlopen` breaks the app's linker namespace (fixed)**

To let ".so files loaded later also be patched", the first version of the native layer GOT-hooked
`dlopen` / `android_dlopen_ext` directly. On-device testing on Taiwan Mobile Payment produced:

```
E linker: library "/vendor/lib64/egl/libMEOW_qt.so" ... needed or dlopened by
          "/data/user/0/tw.com.twmp.twhcewallet/cache/libdebugmask.so"
          is not accessible for the namespace: [name="clns-6", ...]
```

Cause: bionic's `dlopen` uses the **caller's address** to decide which linker namespace to use. Our replacement
function lives in the module's own isolated namespace, so intercepting it made all of the app's subsequent `dlopen` calls use the wrong
namespace and fail to load vendor/private `.so` files.

Fix: switch to the loader API bionic provides for `libnativeloader`
(`__loader_dlopen(filename, flags, caller_addr)` /
`__loader_android_dlopen_ext(..., caller_addr)`), passing
`__builtin_return_address(0)` (a GOT/PLT jump does not clobber LR, so this is the app's return address)
as `caller_addr` —— the namespace is then correct, and we still get the "load finished" timing for a supplementary scan.

And since `dlsym(RTLD_DEFAULT, "__loader_dlopen")` cannot be obtained on Android 13 (measured `0x0`),
the approach was changed to **looking it up in reverse from the GOT of an already-loaded module**: `libnativeloader` imports these symbols, and its GOT
contains the already-resolved real addresses (measured `0x7de0d941b0` / `0x7de0d940b0`).
If neither can be obtained, the dlopen hook is **not installed at all** (never let `dlopen` return NULL and crash the app).

**(b) Hooking Java's `Runtime.loadLibrary0` / `System.loadLibrary` is not viable (removed)**

A Java hook was once attempted to trigger a supplementary scan when "the app loads a native library", and the probe app crashed immediately:

```
java.lang.UnsatisfiedLinkError: dlopen failed: library "libframework-connectivity-jni.so" not found
  at java.lang.Runtime.loadLibrary0(Runtime.java:1123)
  at java.lang.reflect.Method.invoke(Native Method)   ← caller class lost
```

The cause is that after hooking, the original method is invoked reflectively, so the "caller class" that
`loadLibrary0` uses to determine the classloader/namespace becomes the reflection framework, causing the load path to be resolved incorrectly. That path is a dead end, so it went back to the native side
(the loader API) plus a background rescan (every second, deduplicated by module base, reducing the cost from O(all) to O(new)).

## 4. Installation Procedure (Measured Steps; Both Reboots Booted Normally)

```bash
ADB=/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe
A() { "$ADB" -s <PHONE_SERIAL> "$@"; }

# 1) Push and install NeoZygisk (Zygisk API for APatch)
A push NeoZygisk-v2.4-289-release.zip /data/local/tmp/
A shell "su -c '/data/adb/apd module install /data/local/tmp/NeoZygisk-v2.4-289-release.zip'"
A reboot        # after boot, confirm zygisk-ptrace64 / zygiskd64 are running

# 2) Install Vector (Xposed framework)
A push Vector-v2.2-3080-Release.zip /data/local/tmp/
A shell "su -c '/data/adb/apd module install /data/local/tmp/Vector-v2.2-3080-Release.zip'"
A reboot        # after boot, confirm vectord is running and /data/adb/lspd/cli is available

# 3) Build and install our module + the control-group probe
./deploy/usbdebug_hider_ctl.sh build
./deploy/usbdebug_hider_ctl.sh install     # install the APK, push the config file, enable the module
./deploy/usbdebug_hider_ctl.sh scope-all   # scope = all third-party apps
./deploy/usbdebug_hider_ctl.sh verify      # control verification
```

A `scope` change **requires no reboot**: the target app is injected the next time it starts (measured: force-stop then restart takes effect immediately).

---

## 5. Verification

### 5.1 A/B Comparison (Same Probe App, Third-Party uid 10362)

Evidence files: [`detect_A_baseline_unmasked.txt`](media_usbdebug_hider/detect_A_baseline_unmasked.txt),
[`detect_B_masked_ondevice.txt`](media_usbdebug_hider/detect_B_masked_ondevice.txt),
[`detect_diff_A_vs_B.patch`](media_usbdebug_hider/detect_diff_A_vs_B.patch)

The probe (`android/debugdetectprobe/`) deliberately splits detection paths into four classes, to avoid "verifying only one path and declaring success":

| Class | Number of vectors | A (unmasked) | B (masked) |
|---|---|---|---|
| 1. Settings getters (getInt/getString × 3 keys) | 6 | `1/1/0` | **`0/0/0`** |
| 2. Cursor direct reads (single-row / path-style / whole-table / LIST) | 8 | `1/1/1`, whole table `adb_enabled=1` | **all `0`** |
| 3. Java `SystemProperties` (reflection) | 13 | `adb` / `running` | **`mtp` / `stopped`** |
| 4. **native** `__system_property_get` | 11 | `adb` / `running` | **`mtp` / `stopped`** |

Unaffected (as it should be): `query.list_global = rows=489` (LIST queries are unaffected),
`build.TYPE/TAGS/FINGERPRINT` (compile-time constants, already `user`/`release-keys` on this device),
`debug.isDebuggerConnected=false`.

**Two screenshots from the same phone at the same moment** (left: the fake values read by the injected probe; right: the module's own status page,
which shows the device's true values because the module does not mask itself):

| Probe (third-party app perspective) | Module status page (true-value control) |
|---|---|
| ![probe](media_usbdebug_hider/probe_masked_ui.png) | ![module](media_usbdebug_hider/module_ui_real_state.png) |

### 5.2 The Reverse Condition: Must Not Blind Our Own adb

| Perspective | `adb_enabled` | `sys.usb.config` | `init.svc.adbd` |
|---|---|---|---|
| shell (uid 2000) | `1` | `adb` | `running` |
| root (`su -c`) | — | `adb` | `running` |
| Injected third-party app | `0` | `mtp` | `stopped` |

The USB connection remained usable throughout (every command in this report is the result of the same USB adb connection),
and the MTP/media-related packages are already in the default exclusion list.

### 5.3 Hot Reload (Same Process, No Restart)

1. On the device, change `mask_settings` in `/data/local/tmp/usbdebug_hider.conf` from `1` to `0`.
2. Wait 3 seconds (the module checks every 2 seconds).
3. Use `input tap` to press the probe's "re-detect" (**the same process**).

Result: all three settings paths immediately returned to the **true value `1`**, while both property layers still held `mtp`/`stopped`.
→ This proves that the Java hook re-reads the configuration on every value retrieval and that the native table can also be hot-updated, **with no app restart and no reboot**.

### 5.4 Injection Does Not Break Real Apps (Smoke Test)

Each of 3 scoped real apps was put through one "force-stop → launch → check" cycle:

| App | Foreground running | Crashes (crash buffer) | Module injected |
|---|---|---|---|
| `com.duckduckgo.mobile.android` | ✅ | 0 | ✅ |
| `org.telegram.messenger` | ✅ | 0 | ✅ |
| `tw.gov.invoice` | ✅ | 0 | ✅ |

### 5.5 Reboot Persistence (Measured Across a Third Reboot)

The whole configuration **takes effect automatically** after a reboot, with no need to plug in again to arm it (boot takes about 16 seconds):

| Check | After reboot |
|---|---|
| Vector status | `Framework Version 2.2 / API 102 / Enabled Modules: 1` |
| Module | `com.rokid.rc.usbdebughider  enabled` |
| scope | 165 entries (unchanged) |
| `/data/local/tmp/usbdebug_hider.conf` | present and read (`source=` points at it) |
| Probe perspective | `adb_enabled=0`, `sys.usb.config=mtp`, `init.svc.adbd=stopped` (Java + native) |
| shell perspective | `adb_enabled=1`, `sys.usb.config=adb`, `init.svc.adbd=running` (true values) |

Shortly after the reboot, the module had already automatically injected into **real background apps** (framework log examples):

```
UsbDebugMask: 已遮蔽 com.ubercab (uid=10386, settings=true, props=true,
              source=/data/local/tmp/usbdebug_hider.conf)
UsbDebugMask: native 層已載入 -> /data/user/0/com.ubercab/cache/libdebugmask.so
UsbDebugMask: native PLT hook 完成，改寫 128 個 GOT slot
```

### 5.6 Scope Narrowed to "Banks + Payments" (21) and Acceptance Testing on Real Apps

Narrowed from "all third-party apps" to an allowlist; the app names were not guessed from package names but verified one by one by opening
`Settings → App info` and reading the title (2026-09-30).

| Category | App (package) |
|---|---|
| Banks (11) | Cathay United Bank `com.cathaybk.mymobibank.android`, CTBC Bank `com.chinatrust.mobilebank`, E.SUN Bank `com.esunbank`, Taipei Fubon Bank `com.fubon.aibank`, Richart Life `tw.com.taishinbank.ccapp`, Richart `tw.com.taishinbank.richart`, Taiwan Business Bank `com.tbb.mb.prod`, Land Bank Mobile Banking `com.landbank.newrelease.android`, Hua Nan Bank+ `tw.com.hncb.nmbank`, New New Bank `com.ubot.dbank`, Post Office Mobile App `tw.gov.post.mpost` |
| Payment / transit card / wallet (9) | Taiwan Mobile Payment `tw.com.twmp.twhcewallet`, Easy Wallet `com.easycard.wallet`, iPASS MONEY `com.ipass.ipassmoney`, O'Pay `com.allpay.tw`, OPENPOINT `tw.net.pic.m.openpoint`, FamilyMart `grasea.familife`, Hi-Life VIP `com.hilife.vipapp`, PX Mart `com.pxmart.android`, Google Wallet `com.google.android.apps.walletnfcrel` |
| For verification (1) | DebugDetectProbe `com.rokid.rc.debugdetectprobe` (this project's probe, removable at any time) |

Command: `./deploy/usbdebug_hider_ctl.sh scope-from-file deploy/usbdebug_hider_scope_banks.txt`
(optional groups such as cryptocurrency / e-commerce wallets / 2FA are listed in the comments of the list file.)

**Acceptance testing on real apps** (each one force-stop → launch → observe foreground and crash buffer):

| App | Foreground | Crashes | Namespace errors |
|---|---|---|---|
| CTBC Bank | ✅ | 0 | 0 |
| E.SUN Bank | ✅ | 0 | 0 |
| Easy Wallet (EasyCard) | ✅ | 0 | 0 |
| iPASS MONEY | ✅ | 0 | 0 |
| PX Mart | ✅ | 0 | 0 |
| Taiwan Mobile Payment | ✅ (see below) | 0 (unrelated to the module) | **0** |

**The Taiwan Mobile Payment special case (A/B proof that it is not our problem)**: on this rooted machine it crash-loops
by itself —— `SplashActivity.zxl`'s `NullPointerException: String.equals(null)`
(the app's own integrity check crashes after obtaining null in a root environment). **After `disable`-ing this module and retesting,
it still crashed 14 times within 12 seconds**, so it is definitely a pre-existing problem of that app and unrelated to the masking.
(Incidental gain: this A/B process caught and fixed a real bug in this module, see §3.6.)

### 5.7 Device Facts Measured Along the Way

- **Ordinary apps cannot read `/proc/net/tcp`** (`Permission denied`, SELinux) →
  the detection path of "enumerating adb TCP port 5555 listeners" is inherently unavailable to third-party apps (root can do it).
- There is no adb-over-TCP listener on this device (`grep :15B3 /proc/net/tcp` returns nothing), only USB adb.
- `ro.debuggable` is already `0` on this device, and `ro.adb.secure=1` (MIUI user build).

---

## 6. Known Limitations (Important — Do Not Be Overly Optimistic)

1. **Boundaries of the native GOT patch**: if the target app obtains `__system_property_get` via `dlsym`
   and caches the function pointer before we patch, or reads the property area directly in pure JIT/native code,
   it may still obtain the true values. We already hook `dlopen`/`android_dlopen_ext` to patch newly loaded `.so` files,
   but "function pointers that have already been cached" cannot be reclaimed.
2. **Android 14+ may no longer allow loading .so from the app data directory**: it works on this device (API 33), but the system warns
   `Attempt to load writable file: .../cache/libdebugmask.so. This will throw on a
   future Android version.` If the target machine is API 34+, the native layer may fail to load entirely
   (the Java layer still works, which is a graceful degradation).
3. **The `.so` on-disk artifact (the biggest hole)**: the native layer leaves a file at
   `/data/user/0/<target app>/cache/libdebugmask.so`.
   A powerful detector that checks its own cacheDir / `/proc/self/maps` (for example an app with its own integrity check)
   may discover this file that should not exist. DuckUSB's single-point `system_server` hook does not have this problem.
   If the target app is of that kind, it is recommended to switch to DuckUSB or turn off the native layer in the conf (`mask_props_native=0`).
4. **The module package itself is visible**: `pm list packages` can see `com.rokid.rc.usbdebughider`.
   Greater stealth requires separate work (for example `pm hide` / a more neutral package name / LSPosed's module hiding mechanism); this was not done here.
5. **It only handles debug state, not root**: this module does not hide APatch/KernelSU root traces.
   Things like Play Integrity will fail (this phone is already unlocked + rooted), and if a banking app also checks for root,
   a Shamiko/DenyList-type solution is still needed alongside.
6. **Framework injection itself is detectable**: Vector injection leaves ART hooks and classloader anomalies in the target process,
   which high-grade anti-cheat may still notice. What this solution improves is concealment of the "USB debugging state" item specifically, not full invisibility.
7. `Build.TYPE` / `Build.TAGS` are `static final` constants and are inlined by javac, so hooking them has no effect
   (they are already `user`/`release-keys` on this device, so no handling is needed).
8. This solution does **not** mask MIUI's "Developer options" UI —— the settings page is read by `system_server`,
   which we deliberately do not touch. That is: **the app sees "off", while the system settings page still shows "on".**
   This is precisely why adb still works; if the settings page also had to be fooled, the DuckUSB `system_server` route would be required.

---

## 7. Risk and Recovery

- **When the module causes a boot hang**:
  ```bash
  ./deploy/usbdebug_hider_ctl.sh panic     # disable all APatch modules one by one
  ```
  Or inside adb shell, `su -c '/data/adb/apd module disable zygisk_vector'` (then reboot).
  In the worst case, use APatch safe mode (press the volume key at boot) to disable the module.
- **Disabling this module**: `./deploy/usbdebug_hider_ctl.sh disable` (the framework is retained and it can be re-enabled at any time).
- **Full removal**: `./deploy/usbdebug_hider_ctl.sh uninstall`.
- Both reboots this time (installing NeoZygisk, installing Vector) **booted normally**, with no bootloop.

---

## 8. Output Files

| Path | Description |
|---|---|
| [`android/usbdebug-hider/`](../android/usbdebug-hider/) | Module source (Java + JNI) |
| [`android/debugdetectprobe/`](../android/debugdetectprobe/) | Control-group detector (4 detection classes, including a native probe) |
| [`dist/usbdebug-hider-v1.1.apk`](../dist/usbdebug-hider-v1.1.apk) | Installable module APK (v1.1: includes the dlopen namespace fix) |
| [`dist/debugdetectprobe-v1.0.apk`](../dist/debugdetectprobe-v1.0.apk) | Installable control-group probe APK |
| [`deploy/usbdebug_hider_ctl.sh`](../deploy/usbdebug_hider_ctl.sh) | Control script (build/install/scope/verify/panic) |
| [`deploy/usbdebug_hider.conf`](../deploy/usbdebug_hider.conf) | On-device configuration file template (hot reload) |
| [`deploy/usbdebug_hider_scope_banks.txt`](../deploy/usbdebug_hider_scope_banks.txt) | Bank + payment scope allowlist (21 entries, with optional group comments) |
| [`deploy/build_debugmask_so.sh`](../deploy/build_debugmask_so.sh) | Native-layer build script |
| [`deploy/mini_sqlite3.c`](../deploy/mini_sqlite3.c), [`deploy/build_mini_sqlite3.sh`](../deploy/build_mini_sqlite3.sh) | On-device SQLite CLI (fallback when sqlite3 is absent) |
| [`tools/framework/`](../tools/framework/) | Framework installation packages (NeoZygisk / Vector zip) |
| [`reports/media_usbdebug_hider/`](media_usbdebug_hider/) | Verification evidence (raw A/B output, diff, device state) |

---

## 9. Follow-Up Recommendations

1. ~~Narrow the scope~~ **Done**: currently 21 (banks + payments, §5.6). To add groups such as cryptocurrency / e-commerce wallets / 2FA,
   just edit the list file and run `scope-from-file` (no reboot needed).
2. **If an app self-checks its cacheDir**: add that app to the exclusions, or turn off the native layer globally
   (`mask_props_native=0`) and keep only the Java layer.
3. **Hide the module package name**: switching to a neutral package name or adding `pm hide` can further reduce the "an LSPosed module is installed" fingerprint.
4. **Cover paths beyond `_system_property_read_callback`**: the current GOT patch already covers
   the three forms `get/read/read_callback`; if an extreme sample that uses `__system_property_area__` directly is encountered,
   a kernel-level (KPM) solution would be required instead.
5. This combination of "self-built module + Vector CLI + mini_sqlite3" can be reused directly on the glasses side (RV101)
   or on other APatch/KernelSU devices.
