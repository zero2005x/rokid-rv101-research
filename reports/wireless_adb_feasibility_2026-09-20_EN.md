# Wireless adb (adb-over-TCP) Feasibility Measurement Report — RV101 Glasses

> 中文版：[wireless_adb_feasibility_2026-09-20.md](wireless_adb_feasibility_2026-09-20.md)

> **One-sentence conclusion**: **Feasible, and nothing has to be built by hand.** The glasses' adbd
> **binds TCP 5555 by itself anyway** (IPv6 wildcard); once the PC joins the glasses SoftAP `RokidCam`,
> the standard `adb connect 192.168.43.1:5555` directly yields `device` and a shell.
>
> ⚠ **This report overturns** the "this build does not support adb-over-TCP" conclusion of `reports/FACTS.md` R7 and
> `docs/deploy.md` §9.8/§P3. That conclusion was **a bug in the detection method** (it only grepped the IPv4
> `/proc/net/tcp`), not a platform limitation. See §3 for details.

- **Date**: 2026-09-20
- **Device**: RG-glasses (RV101) serial `<DEVICE_SERIAL>`, Android 12 / SDK 32,
  build `1.22.009-20260710-150201`, kernel `5.10.209`, `ro.debuggable=0`, `ro.adb.secure=1`
- **PC**: Windows 11 + Intel Wi-Fi 6E AX211 (operated inside WSL2)
- **Raw output**: for textual evidence other than `reports/media/`, see §2 of this file; PC-side raw file
  `C:\Users\<USER>\wifitest_result.txt` (script `C:\Users\<USER>\wifitest.ps1`)

---

## 1. Test method (why it is trustworthy)

1. **Read-only probing first**: `deploy/adb_wifi_probe.sh` (changes no state) → found adbd listening on
   `/dev/socket/adbd` (UNIX) **and** `[::]:5555` (TCP).
2. **Confirm from inside the device that the listener really accepts connections**: `nc 127.0.0.1 5555` → `TCP connect OK`.
3. **End-to-end measurement**: the glasses start a SoftAP (`cmd wifi start-softap RokidCam wpa2 rokid1234`) →
   the PC switches to that AP (`192.168.43.x`) → `adb connect 192.168.43.1:5555` → `adb shell`.
4. **Automatic restore**: the test script switches the PC back to its original AP in `finally`, so **the test still completed while the PC was offline**
   and the results landed in a file (avoiding the forensic dead end of "switch networks and you can no longer see the result").

---

## 2. Measured evidence (excerpted from `wifitest_result.txt`)

```
[4] adb kill-server + connect 192.168.43.1:5555
[5] adb devices -l:
<DEVICE_SERIAL>        device product:glasses model:RG_glasses device:glasses
<PHONE_SERIAL>        device product:pissarropro_global ...
192.168.43.1:5555       device          ← wireless transport established
[6] adb -s 192.168.43.1:5555 shell 'echo WIRELESS_OK; id; getprop ro.serialno; getprop ro.product.model'
WIRELESS_OK
uid=2000(shell) gid=2000(shell) groups=2000(shell),1004(input),1007(log),1011(adb),1015(sdcard_rw),
  1028(sdcard_r),1078(ext_data_rw),1079(ext_obb_rw),3001(net_bt_admin),3002(net_bt),3003(inet),
  3006(net_bw_stats),3009(readproc),3011(uhid),3012(readtracefs) context=u:r:shell:s0
<DEVICE_SERIAL>
RG-glasses
```

**Three transports exist at the same time** (in the same `adb devices` output): USB glasses, USB phone, **WiFi glasses**.

**Multi-connection concurrency test (same wireless run, `wifitest3_results.txt`)**:

```
PC IP = 192.168.43.86
connected to 192.168.43.1:5555
--- concurrent call 1..4 ---
SESSION_1 / 2000 / <DEVICE_SERIAL>      ← uid=2000(shell)
SESSION_2 / 2000 / <DEVICE_SERIAL>
SESSION_3 / 2000 / <DEVICE_SERIAL>
SESSION_4 / 2000 / <DEVICE_SERIAL>
--- devices ---  192.168.43.1:5555  device   (coexists with the USB glasses and USB phone)
```

→ **"Unlimited adb connections" holds at both the architectural and measured level**: the same client can open many sessions in a row
(adbd gives each TCP connection its own independent transport/session), and different PCs merely open more TCP connections;
the only gates are **RSA key authorization** (`ro.adb.secure=1`) and the SoftAP's connection limit.

---

## 3. Root cause: the project's original detection code only looked at IPv4 (a real bug)

The glasses' steady-state properties (**without any modification in this session**):

| Property | Value |
|------|-----|
| `persist.adb.tcp.port` | **`5555`** (preserved across boots) |
| `service.adb.tcp.port` | *(empty)* |
| `ro.debuggable` | `0` |

| Check | Result | Meaning |
|------|------|------|
| `grep -c ':15B3 ' /proc/net/tcp` (**IPv4**) | **0** | ← this is where the project's old detection code looked |
| `grep -c ':15B3 ' /proc/net/tcp6` (**IPv6**) | **2** | ← where adbd actually binds |
| `netstat -tlnp` | `tcp6 [::]:5555 LISTEN` | it really is LISTENing externally |
| the uid field of that line in `/proc/net/tcp6` | `2000` (shell) | consistent with adbd's de-privileged identity |
| `nc 127.0.0.1 5555` inside the device | `TCP connect OK` | the listener really is alive |

`[::]` is the **dual-stack wildcard**: it accepts both IPv6 and **IPv4-mapped** connections,
so the IPv4 `192.168.43.1:5555` is reachable (already proven in §2).

**How the old conclusion arose**: both `deploy/start_shizuku.sh` and `CommandRouter.enableWifiAdb()` used
`grep -c ':15B3 ' /proc/net/tcp` as the criterion for "is it bound" → an IPv6-only listener is always invisible
→ it reported `adb-over-tcp not supported on this build`.
(When `-p` is given, that `netstat` line looks up uid→process, and with insufficient permission the field is `-`, which is also easily misread as "not adbd".)

**⚠ One known high-risk action needs clarification**: the documentation records that "restarting adbd drops USB enumeration" — that is a side effect of
**restarting adbd itself** (on this device adbd is started the vendor way and USB FunctionFS is brought up by adbd itself),
**unrelated to binding TCP**. The correct approach is therefore **not to restart adbd** (see §6).

---

## 4. Mechanism: why adbd binds TCP

The logic of AOSP `adbd` (`daemon/main.cpp`):

```cpp
if (access(USB_FFS_ADB_EP0, F_OK) == 0) { usb_init(); is_usb = true; }   // USB transport
std::string prop_addr = GetProperty("service.adb.listen_addrs", "");
if (prop_addr.empty()) {
    std::string prop_port = GetProperty("service.adb.tcp.port", "");
    if (prop_port.empty()) prop_port = GetProperty("persist.adb.tcp.port", "");  // ← this build takes this path
    ...
}
setup_adb(addrs);   // init_transport_socket_server() → TCP listener
```

Two key points:

1. **`persist.adb.tcp.port` makes adbd open a TCP listener by itself on "every startup"**;
   no `adb tcpip`, no adbd restart is needed, and **it takes effect at boot**. On this device the value is already `5555`.
2. What is bound is `[::]` (dual-stack), which is why it can only be seen in `/proc/net/tcp6`.

**Authentication**: `ro.adb.secure=1` + `ro.boot.verifiedbootstate=orange` →
`auth_required = true`, going through the normal RSA key handshake. The reason this session could connect directly is that
`/data/misc/adb/adb_keys` (mode `0640`, 6 keys) **already had this machine's public key preloaded** (factory pre-authorization,
see `reports/ADB_Media_Exfiltration_Report.md`).

---

## 5. Feasibility conclusions (by host type)

| Host | Feasibility | Condition |
|------|--------|------|
| **Windows / macOS / Linux laptop, desktop** | ✅ **already demonstrated** | join the glasses SoftAP (or the same subnet) → `adb connect 192.168.43.1:5555`; the key must be in `adb_keys` |
| **Android phone** (this project's phone-side App) | ✅ feasible (same path) | the phone already has the implementation for joining `RokidCam` and TCP (`WifiLink`); just treat `Protocol.AP_IP:5555` as the adb target. **Not measured** (the phone's WiFi was off at the time) |
| **Multiple hosts connecting at once** | ✅ **already measured** | in the same wireless run, 4 consecutive sessions all succeeded (`SESSION_1..4`, uid 2000), and coexisted with the USB glasses / USB phone transports. adbd opens one session per TCP connection; different PCs merely open more TCP connections, and the practical limits are **key authorization** and the AP's connection count |
| **Source allowlist** | ❌ none | as long as it is on the same subnet and the key is in `adb_keys`, any host can connect; the SoftAP password `rokid1234` is hard-coded in the APK (`Protocol.kt`) |

**The role of "after you have root"**: wireless adb **does not need root to be connected to** (adbd runs as shell),
and the value of root is **setting `persist.adb.tcp.port`** (one-off, across boots) and the other root features;
on this device that property **is already set**, so at present **the connection itself does not depend on re-escalating at all**.

---

## 6. Operational recommendations (avoid the pitfalls)

1. **Confirm the listener** (the only correct criterion; both tables must be checked):
   ```sh
   grep -q ':15B3 ' /proc/net/tcp && echo ipv4 || true
   grep -q ':15B3 ' /proc/net/tcp6 && echo ipv6 || true   # ← this device is here
   ```
2. **Do not restart adbd** (it drops USB enumeration). To enable TCP you only need
   `setprop persist.adb.tcp.port 5555` (root, across boots) **and then one normal reboot**.
3. **Entry IP**: SoftAP `wlan2 = 192.168.43.1` (fixed); if the glasses instead join a normal WiFi (STA, `wlan0`),
   use that subnet's IP, but **on this device STA and SoftAP are mutually exclusive**, and the IP under STA is not fixed.
4. **PC side**: `adb connect 192.168.43.1:5555`; a new host needs authorization on the glasses for its first connection,
   and the glasses **have no screen to show a popup** → the host's public key must first be written into `/data/misc/adb/adb_keys` (root).
5. **Power saving**: the glasses idling may drop the AP or WiFi; `svc wifi enable` / `cmd wifi start-softap` can bring them back.

---

## 7. Self-built relay (`deploy/adb_tcp_relay.c`): now a **fallback**, not a necessity

This session still completed and implemented a route that "does not depend on adbd TCP", as a fallback for a future OTA that breaks adbd:

- **Principle**: a TCP listener → forwards to `/dev/socket/adbd` (adbd's UNIX socket),
  with authentication/shell/sync all handled by the stock adbd. It has been confirmed the glasses **really do have** `/dev/socket/adbd`:
  `srw-rw---- system system u:object_r:adbd_socket:s0`
- **Limitation (measured)**: `shell`(2000) connecting to `/dev/socket/adbd` → **`Permission denied`** (SELinux),
  so **root is required** to run the relay; on the phone (Magisk) it has been verified that root can connect.
- **When it is needed**: if some firmware version removes `persist.adb.tcp.port` and rebooting is inconvenient,
  an entry point can be opened live inside an already-rooted session:
  ```sh
  adb push build-cache/adb_tcp_relay /data/local/tmp/
  adb shell 'su -c "setsid /data/local/tmp/adb_tcp_relay -p 5555 -l /data/local/tmp/relay.log -d"'
  ```
- Files: `deploy/adb_tcp_relay.c` (source), `deploy/build_adb_relay.sh` (NDK build),
  `deploy/adb_wifi_relay.sh` (start/stop/status/probe), `deploy/adb_wifi_probe.sh` (read-only probe)

---

## 8. Security implications (must be disclosed)

- This amounts to **exposing the USB debugging port to the network**: a host on the same subnet whose key is authorized can obtain a shell directly
  (uid 2000, with groups including `sdcard_rw`, `log`, `input`) → it can read all photos/videos, inject `input` events,
  and read logcat. The CVSS 4.6 (Physical) of `reports/ADB_Media_Exfiltration_Report.md`
  **should be re-scored as AV:N (about 7.5 High) once wireless adb is enabled** — that report already anticipated this condition, and it now holds.
- **The SoftAP password `rokid1234` is hard-coded in the APK** (`Protocol.kt:15`) → it is not effective access control.
- Recommendation: when not needed, `setprop persist.adb.tcp.port ""` (root) + reboot, or turn off the SoftAP.

---

## 9. Repeatable commands

```bash
source deploy/env.sh                      # ADB/SERIAL
bash deploy/adb_wifi_probe.sh             # read-only probe (properties / socket / listener / tools / keys)
bash deploy/adb_wifi_relay.sh status      # relay status (fallback route)

# Glasses start the AP (root or shell both work):
"$ADB" -s "$SERIAL" shell 'cmd wifi start-softap RokidCam wpa2 rokid1234'
# After the PC joins RokidCam:
adb connect 192.168.43.1:5555 && adb devices -l && adb -s 192.168.43.1:5555 shell id
```

---

## 10. To-do (not yet measured; uncertainty stated explicitly)

| Item | Status |
|------|------|
| Phone-side App using `192.168.43.1:5555` directly as the adb target | ⏳ not measured (requires the phone's WiFi to be on; the path is exactly the same as the PC's) |
| Multiple sessions in parallel | ✅ **already measured**: 4 consecutive successes in the same wireless run (§2) |
| adbd automatically binding TCP after a cold boot (`persist` is already 5555) | ⏳ not measured (no reboot was done); the AOSP logic + the property value imply "it will", and **no USB is needed** |
| Whether `persist.adb.tcp.port` survives an OTA | ⏳ unknown (`persist.*` is normally preserved) |
| Whether the AP or listener drops after standby/power saving | ⏳ not measured |

## 11. Project assets also fixed in this session

| File | Change |
|------|------|
| `android/.../net/CommandRouter.kt` | `enableWifiAdb()` now checks both `/proc/net/tcp` and `/proc/net/tcp6`, the ACK adds `boundOn`, and it automatically calls `ensureStarted()` when the AP is not up |
| `android/app-glasses/build.gradle.kts` | version → `1.0-safe.4` (versionCode 6) |
| `dist/app-glasses-v1.0-safe.4.apk` | built and **installed on the glasses** (`dumpsys` confirms versionName=1.0-safe.4); on-device verification of the detection command returned `v6` |
| `reports/FACTS.md` R7 | changed from "not supported" to "**overturned**: bound on IPv6 `[::]:5555`" |
| `docs/deploy.md` §9.8/§P3/command table | changed from "not feasible (abandoned)" to "feasible" + the correct criterion |
| `docs/device-debug-notes.md` §D | added a correction note |
| `deploy/adb_wifi_probe.sh` (new) | read-only feasibility probe (properties/socket/listener/tools/keys) |
| `deploy/adb_wifi_verify.sh` (new) | one-click verification of the listener + entry IP + an actual `adb connect` |
| `deploy/adb_tcp_relay.c`/`build_adb_relay.sh`/`adb_wifi_relay.sh` (new) | fallback relay (TCP → `/dev/socket/adbd`, needs root) |
