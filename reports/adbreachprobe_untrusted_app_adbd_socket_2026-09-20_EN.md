# adbreachprobe — Can a normal third-party app connect to `/dev/socket/adbd`? (2026-09-20)

> 中文版：[adbreachprobe_untrusted_app_adbd_socket_2026-09-20.md](adbreachprobe_untrusted_app_adbd_socket_2026-09-20.md)

## Question

On the Rokid RV101 (Android 12 / SDK 32, serial `<DEVICE_SERIAL>`), `adbd` listens on
`/dev/socket/adbd` (`srw-rw---- system system`). If a normal third-party app
(`untrusted_app`, its own uid, no root) can connect to it, then a pure-APK
wireless-ADB relay is possible; otherwise the relay requires root or some other channel.

## Method

A new Gradle module `android/adbreachprobe/` was added (Java, no dependencies). The probe
runs **inside the app's own process** (uid = app uid, SELinux = `untrusted_app`, read
directly from `/proc/self/attr/current` rather than guessed).

- **Probe A (main question)**: connect to `/dev/socket/adbd`; on success, read ≤24 bytes within 3 seconds.
- **Probe B (control)**: bind + listen + connect + accept + echo 1 byte on `getFilesDir()/self.sock` → proving the test method itself is valid.
- **Probe C (secondary)**: connect to `/data/local/tmp/root_daemon.sock` (0777, no listener).

Key implementation decision: **the main probe uses native raw syscalls**
(`src/main/jni/adbreachprobe.c`, built for aarch64 with NDK r26b). Reason: on this ROM
`android.net.LocalSocket` throws a framework exception before it ever reaches the kernel
(see "Deviations" below), so the Java version cannot obtain an errno.

## Results (RAW)

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

Host-side read-only corroboration:

```
srw-rw---- 1 system system u:object_r:adbd_socket:s0  0 2026-09-20 10:08 /dev/socket/adbd
drwxr-xr-x 6 root   root   u:object_r:socket_device:s0 640 /dev/socket
srwxrwxrwx 1 root   root   u:object_r:shell_data_file:s0 0 /data/local/tmp/root_daemon.sock
getenforce → Enforcing
```

SELinux AVC: `logcat -d -b all | grep -i 'avc:'` and `dmesg | grep -i avc` showed
**no** `avc: denied` at all while the probe was running (the only "avc" strings were
msm_vidc video-decoder messages).

## Verdict

**No.** A normal third-party app cannot connect to `/dev/socket/adbd`; `connect()`
returns **`EACCES` / errno 13 / "Permission denied"** directly.

The blocking layer is **DAC (file permissions), not SELinux**:

- The socket is `0660 system:system` (uid=gid=1000); the app is uid=gid=10064 with
  supplementary groups `9997 20064 50064` — it is neither the owner nor in group 1000.
- `access("/dev/socket/adbd", W_OK)` → `EACCES`. The check in `access(2)` runs
  `generic_permission()` (pure DAC) first inside `inode_permission()`; if DAC fails it
  returns, and **the LSM hook is never called at all**, so this EACCES is a pure DAC
  conclusion.
- That same DAC check (`MAY_WRITE` on the socket inode) is exactly the condition the
  kernel `unix_find_other()` requires before the SELinux `connectto` check.
- No AVC at all → fully consistent with "DAC blocks first, SELinux is never consulted".

Therefore: **any app whose uid/gid is not system(1000) (including a platform-signed app
with a different uid) is blocked by DAC**; a wireless-ADB relay cannot be built as a pure
third-party APK.

## Deviations / Points to Note

1. `android.net.LocalSocket` never reaches the kernel at all on this ROM:
   - after `new LocalSocket(SOCKET_STREAM)`, `setSoTimeout()` / `connect()` throw
     `java.io.IOException: socket not created` (the fd has not been created yet);
   - the fd only appears after `bind()`, but the subsequent `connect()` throws
     `java.lang.UnsupportedOperationException` (no message).
   So the errno on the Java path is `?`, and a native raw syscall is required to obtain
   the real errno 13.
2. Only the `unix_stream_socket connectto` path was verified. Whether SELinux *additionally*
   forbids `untrusted_app → adbd_socket` cannot be separated out by this experiment (DAC
   blocks first). Separating the two would require reading the SELinux policy or connecting
   to a DAC-allowed system socket — both exceed this session's "read-only" authorization,
   so neither was done. `/sys/fs/selinux/policy` is in fact world-readable (it can be pulled
   and analyzed with setools); setools is not available on this machine and was not installed.
3. The first install attempt using the WSL path `/mnt/c/...` failed
   (`adb.exe: failed to stat /mnt/c/...: No such file or directory`); it only succeeded
   after switching to the Windows-style path `'C:\Users\...\adbreachprobe-debug.apk'`.
4. The probe's first version packed `st_mode/st_uid/st_gid` into a byte return value and
   truncated them (`0660` displayed as `0260`); this was fixed to a full int and re-run, and
   the table above is the corrected result.

## Artifacts

- Module: `android/adbreachprobe/` (`build.gradle.kts`, `src/main/AndroidManifest.xml`,
  `src/main/java/com/rokid/rc/adbreachprobe/{MainActivity,Probe,ProbeService,NativeProbe}.java`,
  `src/main/jni/adbreachprobe.c`, `src/main/jniLibs/arm64-v8a/libadbreachprobe.so`)
- `android/settings.gradle.kts` only gained `include(":adbreachprobe")`
- APK: `android/adbreachprobe/build/outputs/apk/debug/adbreachprobe-debug.apk`
  sha256 `6d2845b3d4fe41979ed6af208ea9a8d10ad93de2a6bac75a11d69665a5bf3b0f`
- On-device output: `/sdcard/Android/data/com.rokid.rc.adbreachprobe/files/probe_result.txt`
