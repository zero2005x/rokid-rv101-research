# adbreachprobe — 一般第三方 App 能否 connect `/dev/socket/adbd`？（2026-09-20）

> English: [adbreachprobe_untrusted_app_adbd_socket_2026-09-20_EN.md](adbreachprobe_untrusted_app_adbd_socket_2026-09-20_EN.md)

## 問題

Rokid RV101（Android 12 / SDK 32，serial `<DEVICE_SERIAL>`）的 `adbd` 在
`/dev/socket/adbd` 監聽（`srw-rw---- system system`）。若一般第三方 App
（`untrusted_app`、自己的 uid、無 root）能連上它，就能做出純 APK 的
wireless-ADB relay；否則 relay 必須有 root 或別的管道。

## 方法

新增 Gradle 模組 `android/adbreachprobe/`（Java，無依賴），探針在 **App 自己的
process** 內執行（uid = App uid、SELinux = `untrusted_app`，由
`/proc/self/attr/current` 實讀，不用猜）。

- **Probe A（主問題）**：connect `/dev/socket/adbd`，成功則 3 秒內讀 ≤24 bytes。
- **Probe B（對照組）**：在 `getFilesDir()/self.sock` 自己 bind + listen + connect +
  accept + echo 1 byte → 證明測試手法本身有效。
- **Probe C（次要）**：connect `/data/local/tmp/root_daemon.sock`（0777、無 listener）。

關鍵實作決定：**主探針走 native raw syscall**（`src/main/jni/adbreachprobe.c`，
NDK r26b 編 aarch64）。原因：這個 ROM 的 `android.net.LocalSocket` 在真正碰到 kernel
之前就丟 framework 例外（見下方「未如預期」），Java 版本拿不到 errno。

## 結果（RAW）

```
UID=10064 CONTEXT=u:r:untrusted_app:s0:c64,c256,c512,c768 PID=5025 WHERE=ProbeService APPID=10064
NATIVE_LIB available=true
SOCKSTAT java path=/dev/socket/adbd mode=0660 uid=1000 gid=1000
SOCKSTAT java path=/data/local/tmp/root_daemon.sock mode=0777 uid=0 gid=0
SOCKSTAT native(DAC) path=/dev/socket/adbd stat_errno=0 (Success) mode=0660 uid=1000 gid=1000 access(W_OK)_errno=13 (Permission denied) W_OK=0
SOCKSTAT native(DAC) path=/data/local/tmp/root_daemon.sock stat_errno=0 (Success) mode=0777 uid=0 gid=0 access(W_OK)_errno=0 (Success) W_OK=1
SELF Uid:	10064	10064	10064	10064 | Gid:	10064	10064	10064	10064 | Groups:	9997 20064 50064
SELF os uid=10064 euid=10064 gid=10064 egid=10064
PROBE_A_NATIVE_RAW errno=13 stage=2 (connect) nbytes=-1
PROBE_A_JAVA step=new LocalSocket OK
PROBE_A_JAVA FAILED errno=-2147483648 java.io.IOException: socket not created
PROBE_A=ERR errno=13 Permission denied [stage=connect (native raw syscall)]
PROBE_B_PATH=/data/user/0/com.rokid.rc.adbreachprobe/files/self.sock
PROBE_B_NATIVE_RAW errno=0 stage=7 (echo) bind_mode=0700 nbytes=1
PROBE_B_JAVA step=new LocalSocket OK
PROBE_B_JAVA step=bind OK fd=non-null
PROBE_B_JAVA step=Os.listen OK
PROBE_B_JAVA FAILED errno=-2147483648 java.lang.UnsupportedOperationException: null
PROBE_B=OK via=B-native filesystem-path bind_mode=0700 stage=echo echo=ok
PROBE_C_NATIVE_RAW errno=111 stage=2 (connect) nbytes=-1
PROBE_C=ERR errno=111 Connection refused [stage=connect]
PROBE_SUMMARY A=ERR:13 B=OK C=111
```

主機端唯讀佐證：

```
srw-rw---- 1 system system u:object_r:adbd_socket:s0  0 2026-09-20 10:08 /dev/socket/adbd
drwxr-xr-x 6 root   root   u:object_r:socket_device:s0 640 /dev/socket
srwxrwxrwx 1 root   root   u:object_r:shell_data_file:s0 0 /data/local/tmp/root_daemon.sock
getenforce → Enforcing
```

SELinux AVC：`logcat -d -b all | grep -i 'avc:'` 與 `dmesg | grep -i avc`
在探針執行期間 **完全沒有** `avc: denied`（唯一的 "avc" 字串是 msm_vidc 影像解碼器訊息）。

## 判斷

**不行。** 一般第三方 App 連不上 `/dev/socket/adbd`，`connect()` 直接回
**`EACCES` / errno 13 / "Permission denied"**。

阻擋層是 **DAC（檔案權限），不是 SELinux**：

- socket 是 `0660 system:system`（uid=gid=1000）；App 是 uid=gid=10064，
  supplementary groups `9997 20064 50064` — 既非 owner 也不在 group 1000。
- `access("/dev/socket/adbd", W_OK)` → `EACCES`。`access(2)` 的檢查在
  `inode_permission()` 裡先跑 `generic_permission()`（純 DAC），DAC 失敗就
  return，**根本不會呼叫 LSM hook**，所以這個 EACCES 是純 DAC 結論。
- 同一條 DAC 檢查（`MAY_WRITE` on the socket inode）正是 kernel
  `unix_find_other()` 在 SELinux `connectto` 之前要求的條件。
- 沒有任何 AVC → 與「DAC 先擋住、SELinux 沒被諮詢」完全一致。

因此：**任何 uid/gid 不是 system(1000) 的 App（包含平台簽章但 uid 不同的 App）
都會被 DAC 擋下**；wireless-ADB relay 不可能做成純第三方 APK。

## 未如預期 / 需要注意的地方

1. `android.net.LocalSocket` 在這個 ROM 上根本到不了 kernel：
   - `new LocalSocket(SOCKET_STREAM)` 之後 `setSoTimeout()` / `connect()` 丟
     `java.io.IOException: socket not created`（fd 尚未建立）；
   - `bind()` 之後 fd 才出現，但接著 `connect()` 丟
     `java.lang.UnsupportedOperationException`（無 message）。
   所以 Java 路徑的 errno 是 `?`，必須靠 native raw syscall 才能拿到真 errno 13。
2. 只有 `unix_stream_socket connectto` 這一條路被驗證。SELinux 是否 *另外* 也禁止
   `untrusted_app → adbd_socket`，本實驗無法分離（DAC 先擋住）。要分離需要讀
   SELinux policy 或連一個 DAC 允許的系統 socket — 兩者都超出本次「唯讀」授權，故未做。
   `/sys/fs/selinux/policy` 其實 world-readable（可拉下來用 setools 分析），
   本機沒有 setools，未安裝。
3. 第一次 install 用 WSL 路徑 `/mnt/c/...` 失敗
   （`adb.exe: failed to stat /mnt/c/...: No such file or directory`），
   改用 Windows 形式路徑 `'C:\Users\...\adbreachprobe-debug.apk'` 才成功。
4. 探針第一版把 `st_mode/st_uid/st_gid` 塞進 byte 回傳而截斷（`0660` 顯示成 `0260`），
   已修正為完整 int 後重跑，上表是修正後的結果。

## 產出

- 模組：`android/adbreachprobe/`（`build.gradle.kts`、`src/main/AndroidManifest.xml`、
  `src/main/java/com/rokid/rc/adbreachprobe/{MainActivity,Probe,ProbeService,NativeProbe}.java`、
  `src/main/jni/adbreachprobe.c`、`src/main/jniLibs/arm64-v8a/libadbreachprobe.so`）
- `android/settings.gradle.kts` 只加了 `include(":adbreachprobe")`
- APK：`android/adbreachprobe/build/outputs/apk/debug/adbreachprobe-debug.apk`
  sha256 `6d2845b3d4fe41979ed6af208ea9a8d10ad93de2a6bac75a11d69665a5bf3b0f`
- 裝置端輸出：`/sdcard/Android/data/com.rokid.rc.adbreachprobe/files/probe_result.txt`
