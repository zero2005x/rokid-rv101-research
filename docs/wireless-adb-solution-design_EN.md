# Generic Wireless ADB Solution Design (RV101 Glasses × Developer Laptop)

> 中文版：[wireless-adb-solution-design.md](wireless-adb-solution-design.md)

> Goal: **developers can run `adb connect` / `adb install` / `logcat` / privilege-escalation arming
> against the glasses without plugging in a cable.** User decisions (2026-09-20): ① prioritize "the PC
> keeps internet access"; ② a **one-time USB/OTG** connection is acceptable; ③ the target is an
> **external developer with no root who does not run exploits**; ④ the glasses-side App may be modified freely.

---

## 0. Current-State Inventory (measured today; this determines the solution space)

| Fact | Value | Implication for the solution |
|------|-----|--------------|
| adbd TCP listener | **Already present** (`persist.adb.tcp.port=5555` → IPv6 `[::]:5555`) | This part of connectivity **does not need root at all** |
| `cmd adb is-wifi-supported` | `false` | Android 11's official "wireless debugging + pairing code" is **not built into this device** |
| `settings get global adb_wifi_enabled` | `0` | Same as above |
| `/dev/socket/adbd` | Exists, `srw-rw---- system system` | Can serve as a userland relay backend; **only root can connect** (both shell and app are blocked by DAC) |
| `cmd wifi connect-network <ssid> wpa2 <pass>` | **Exists** | The glasses can programmatically join the developer's subnet (STA) → **the PC does not have to leave its current WiFi** (satisfies priority 1) |
| STA ↔ SoftAP | Mutually exclusive | Only one of the two topologies can be chosen |
| adb_keys | 6 pre-authorized keys, `ro.adb.secure=1` | As long as the key is in there, connecting needs no authorization dialog (and the glasses have no screen to pop one up anyway) |

---

## 1. Three Topologies (choose this first; everything else is detail)

| Topology | Does the PC keep internet access? | Need root? | Applicable |
|------|------------------|-----------|------|
| **T1: glasses as STA, on the same subnet as the PC** (**recommended, priority 1**) | ✅ Fully retained | ❌ App only | A developer's daily work; the glasses join `CHT6746` / a phone hotspot / office WiFi |
| **T2: glasses as SoftAP, the PC joins** | ⚠️ Briefly leaves the original network | ❌ (the properties are already set) | When there is no shared subnet on site; "connect → test → restore" can be scripted (implemented this time) |
| **T3: glasses as SoftAP, phone/Windows as gateway** | ✅ | The phone side needs root for DNAT; or use Termux `socat` without root | When the PC cannot change networks and the glasses cannot reach a shared subnet |

**T1 is the only topology that simultaneously satisfies "zero cable + zero root + PC stays online"**, so this solution uses it as the default.

```
┌──────────────┐   Same WiFi subnet (e.g. CHT6746)   ┌────────────────────────┐
│ Developer PC │ ─────────────────────────────────── │ Glasses RV101          │
│ adb connect  │        adb over TCP :5555           │  ├ GLASSES App         │
│ <glasses-ip> │                                     │  │   └ AdbWifiService  │
└──────────────┘                                     │  │        ↓ AF_UNIX    │
                                                     │  └─ /dev/socket/adbd   │
                                                     └────────────────────────┘
```

---

## 2. Component Design

### 2.1 Glasses Side (modify our GLASSES App)

| Component | Responsibility |
|------|------|
| `AdbWifiService` (foreground service) | Open a TCP listener (default 5555) → forward to `/dev/socket/adbd`; one thread per connection; `oom_score_adj` protection |
| `AdbWifiController` | State machine: `OFF → RELAY_UP → NETWORK_READY`; reports `ip:port`, `boundOn`, `relayPid` externally |
| `WifiJoin` (STA) | Put the glasses on the developer's subnet via `cmd wifi connect-network` (root/shell) or `WifiNetworkSuggestion` (App, requires one user consent) |
| `NsdAdvertiser` (mDNS) | Advertise `_adb._tcp` (instance `adb-<serial>`) → the PC side can **discover it automatically**, with no need to ask for the IP |
| Existing `DebugReceiver` | Add `ADB_WIFI_START` / `ADB_WIFI_STOP` / `ADB_WIFI_STATUS`, so the phone App or a developer can toggle it with one action |
| Existing root path (optional) | If the App has root (research build), switch to `setprop persist.adb.tcp.port 5555` plus authorizing keys when necessary |

**Key design decision**: the relay runs **in the App's own process**, therefore
- binding 5555 does not require root (the privileged-port restriction above 1024 applies only below 1024);
- as long as SELinux allows `untrusted_app` to connect to `adbd_socket`, the whole path is **zero root**;
- if SELinux does not allow it (measured today; see §4), the fallback is "a one-time root to set `persist.adb.tcp.port`",
  after which adbd listens on TCP by itself and **the developer side is still zero root end to end**.

### 2.2 PC-Side Tool `adbtool` (single file, cross-platform)

```
adbwifi discover            # mDNS lookup for _adb._tcp (or scan the 5555 subnet)
adbwifi connect             # automatic connect + verify get-state=device
adbwifi status              # list all transports, which device each is, USB or WiFi
adbwifi keys install        # hand this machine's public key to the glasses (needs root on the glasses or App permissions)
adbwifi off                 # turn off the glasses relay (via the App channel)
```
- The Windows build can wrap another layer of `netsh portproxy` (WSL reaching Windows directly has been verified feasible),
  so that **Linux adb inside WSL** can use the same wireless path.

### 2.3 Key Authorization (the real barrier to "generic")

`ro.adb.secure=1` → every new PC needs a key in the glasses' `/data/misc/adb/adb_keys`. Three approaches:

| Approach | Requires | Assessment |
|------|------|------|
| **A. On the first cable connection, the install script writes a "shared team development key"** | one USB | ✅ Recommended: all developers then use the same key, and **no further authorization is ever needed** |
| B. The App shows a pairing code, the PC uses `adb pair` | official wireless debugging (this device's `cmd adb` has no such subcommand) | ❌ Not available on this generation |
| C. Each developer authorizes once over a cable | one USB per person | Acceptable, but it defeats "generic" |

→ **Recommendation: adopt A**: the install script does all three things at once — "push the App, write adb_keys, set the persist property" — in a single pass.

---

## 3. First-Time Installation (the only step that needs a cable)

```bash
# Developer receives the glasses for the first time (PC side)
bash tools/bootstrap_glasses.sh            # one USB
#  1) Install the GLASSES App (contains AdbWifiService)
#  2) Write the team development public key → /data/misc/adb/adb_keys (needs one-time root or App assistance)
#  3) setprop persist.adb.tcp.port 5555 (needs one-time root)
#  4) Unplug → everything afterwards goes over WiFi
```

After that, the daily routine:
```bash
adbwifi discover && adbwifi connect && adb shell id     # no cable, no root, PC stays online
```

---

## 4. Settled: a Pure-APK Relay Is **Not Feasible** (measured 2026-09-20)

Measured with a real third-party App (`com.rokid.rc.adbreachprobe`, uid 10064, `u:r:untrusted_app:s0`):

```
PROBE_A_NATIVE_RAW errno=13 stage=2 (connect)     ← connect() returns EACCES directly
PROBE_A=ERR errno=13 Permission denied
PROBE_B=OK  (its own socket; the control group passes → the test instrument works)
PROBE_C=ERR errno=111 Connection refused (0777 socket; DAC lets it through, there is simply no listener)
SOCKSTAT /dev/socket/adbd mode=0660 uid=1000 gid=1000 access(W_OK)_errno=13
```

**Verdict: what blocks this is DAC, not SELinux.** `/dev/socket/adbd` is `0660 system:system`; the App (uid 10064, groups 9997/20064/50064) is neither the owner nor in group 1000; `access(W_OK)` (pure DAC, which returns before the LSM hook) likewise returns EACCES, and there is **no `avc: denied` anywhere** in the whole run.

| Conclusion | Impact |
|------|------|
| ❌ A pure APK (no root) cannot put adbd on TCP | Option 3 is cancelled; `AdbWifiService` is retained as a **root-required** research-build feature |
| ✅ But "zero root on the developer side throughout" still holds | because adbd listening on TCP by itself is handled by `persist.adb.tcp.port` (one-time root) or by the **existing default value** |
| ✅ First-time trust still requires a cable (or a key already in `adb_keys`) | consistent with §0: "first contact is physically impossible over wireless alone" |

Evidence report: [`reports/adbreachprobe_untrusted_app_adbd_socket_2026-09-20.md`](../reports/adbreachprobe_untrusted_app_adbd_socket_2026-09-20.md).

---

## 4.1 Development-Mode Measurements (2026-09-20, end to end)

| Test | Result |
|------|------|
| Glasses join a subnet, **path B** (`adb shell cmd wifi connect-network LouisaCoffee wpa2 …`) | ✅ Success; obtained `wlan0=10.0.0.120/24`, 5 GHz RSSI −51, link 325 Mbps |
| Glasses join a subnet, **path A** (`WifiNetworkSuggestion` in the App) | ❌ **Failed** — the headless glasses have no tappable system notification; this path is only practical on a **device with a screen** |
| PC-side connection (Windows adb, same LAN subnet) | ✅ `adb connect 10.0.0.120:5555` → `device`; `uid=2000(shell) u:r:shell:s0` |
| `tools/adbwifi discover 10.0.0` | ✅ Found and written to cache: `<DEVICE_SERIAL> → 10.0.0.120` |
| `tools/adbwifi connect <serial>` (daily multi-device usage) | ✅ Resolved the IP from the cache and connected |
| `tools/adbwifi verify <serial>` | ✅ Printed serial/uid/listener/URL/sdcard read |
| Sensitive-command origin guard | ✅ Third-party App broadcast rejected (`拒絕 DEV_MODE_START：來源不可信`); adb shell allowed |
| Development-mode failure fallback | ✅ No credential → `ERROR 沒有目標網段`; join failed → `已回退（加入失敗）`, state returns to OFF |
| Development mode off | ✅ `state=OFF msg=已關閉（user-stop）` |

**Conclusion**: take "**adb shell path B**" as the automation mainline (no root, reliable, no screen interaction required), and keep path A for devices that have a screen / a user. This fits the "one cable bootstrap, then everything wireless" architecture exactly: after bootstrap, every dev-mode trigger goes through adb (wired or wireless).

---

## 5. Final Solution: **One Cable Plug-In → Permanently Wireless Afterwards** (zero root)

**Core insight**: adbd is **already** listening on TCP:5555 (this device: `persist.adb.tcp.port=5555`), and the development machine's key is **already** in the glasses' `adb_keys` (measured: the Windows-side key `cb94291f…` is one of the 6 keys). So the only thing "generic wireless adb" is missing is the step of **getting the glasses onto a subnet** — and `cmd wifi connect-network` happens to be available.

```
① One-time USB (the only one)      tools/bootstrap_glasses.sh
   ├─ Install the glasses App
   ├─ Write the development machine public key → /data/misc/adb/adb_keys      ← no authorization needed afterwards
   └─ setprop persist.adb.tcp.port 5555 (one-time root; this device is already 5555)
                    ↓  unplug the cable
② Glasses join a subnet (no root)  adb shell cmd wifi connect-network <SSID> wpa2 <pass>
③ Developer connects (no root)     tools/adbwifi connect <glasses-ip>  → adb install / logcat / shell
```

**Why ② needs no root**: `cmd wifi` only requires shell privileges; and shell privileges come from the **adb connection that is already established** (once over USB, or wirelessly after that). The App can also use `WifiNetworkSuggestion` to join automatically after one user consent.

**The PC keeps internet access**: under topology T1 (glasses as STA), the PC does not need to change subnets at all — satisfying your priority 1.

---

## 6. Component Design (final version)

### 6.1 Glasses Side
| Component | Status | Description |
|------|------|------|
| `AdbWifiService` (TCP→`/dev/socket/adbd` relay) | ✅ Implemented, **requires root** | For the research build; without root it explicitly reports `adbd socket 不可連` (DAC) |
| `WifiJoin` (`cmd wifi connect-network`) | 📋 Design | Lets the glasses join the developer's subnet (no root, via shell) |
| `NsdAdvertiser` (`_adb._tcp`) | 📋 Design | Automatic discovery on the PC side |
| `DebugReceiver`: `ADB_WIFI_START/STOP/STATUS` | ✅ Implemented | One-action toggle (relay version) |
| Existing `ENABLE_WIFI_ADB` (persist property) | ✅ Detection fixed | ACK returns `boundOn` (ipv4/ipv6) |

### 6.2 PC Side
| Component | Status | Description |
|------|------|------|
| `tools/adbwifi` (discover / connect / status / shell / install / keys / off) | ✅ Implemented and tested | Uses Windows adb; `discover` uses Windows' real LAN prefix for a parallel /24 scan |
| `tools/bootstrap_glasses.sh` | ✅ Implemented | One-time USB on the first run: App + keys + properties + verification |

### 6.3 Key Authorization (the barrier to "generic")
`ro.adb.secure=1` → every new PC needs a key in `adb_keys`. **Recommendation: use a "shared team development key"**: the bootstrap script writes it into the glasses, and all developers then share the same one → no further authorization is ever needed. The official pairing-code route is unavailable (this device's `cmd adb` has no `pair`/`enable-wifi`).

---

## 7. Risks and Caveats

| Risk | Description | Mitigation |
|------|------|------|
| Security | Once the relay is on, anyone on the same subnet can reach adb (the key handshake is still required, but **a shared key = shared trust**) | By default it only opens on the "developer subnet"; provide a one-action `off`; an IP allowlist can be added |
| Glasses power saving | WiFi sleep / the App being killed by LMK → the relay disappears (this project already has an LMK precedent, P15) | Foreground service + `oom_score_adj=-1000` + automatic restart + status reporting |
| STA/SoftAP mutual exclusion | Turning on the hotspot kicks out the STA | Managed mutual exclusion inside the service; update the external IP on switch |
| Glasses have no screen | Cannot display a pairing code / authorization dialog | Use the shared key (§2.3 A) |
| Version fragmentation | Different OTAs may change adbd behavior | `adbwifi status` verifies the listener + `get-state` every time, and states the cause explicitly on failure |

---

## 8. Implementation Status (2026-09-20)

| Item | Status |
|------|------|
| Removing the persistent exposure (`persist.adb.tcp.port`) | ✅ Cleared by default in bootstrap; cleared after privilege escalation this time (see §9) |
| `tools/adbwifi` multi-device (cache / `--all` / `verify`) | ✅ Done and measured |
| `tools/bootstrap_glasses.sh` multi-device + safe defaults | ✅ Done |
| Glasses `WifiJoin` A/B | ✅ A done (unusable when headless) / B measured successfully |
| Glasses `DevModeController` (timeout / fallback / status reporting) | ✅ Done and measured |
| Sensitive-command origin guard | ✅ Done and measured |
| Phone-side `DEV_MODE_*` protocol and commands | ✅ Done (`Remote` + `DebugReceiver`) |
| mDNS | ⛔ **Not done**, by decision (replaced by a serial→IP cache) |
| Development-mode UI entry point in the phone App | 📋 Not done (currently driven by DebugReceiver commands; the UI is follow-up work) |
| Closing the listener immediately without root | ⚠ **Not possible** (requires root or a reboot); `listenerManaged` is reported explicitly |

### Known Limitations (honest disclosure)
1. **Headless limitation**: path A (`WifiNetworkSuggestion`) requires tapping a system notification → the glasses have no screen,
   so automation always uses path B (adb shell).
2. **Without root the listener cannot be closed immediately**: even after `persist.adb.tcp.port` is cleared, you must wait for adbd
   to restart (reboot). This device deliberately **does not restart adbd** (that would drop USB enumeration).
3. **The App cannot start automatically in the background**: the glasses forbid third-party Apps from running in the background →
   a development-mode trigger requires the App in the foreground, or a trigger from the PC/phone through adb (after bootstrap, adb is already available).

## 9. Privilege-Escalation Cleanup Record
See `deploy/get_root_now.log`; after cleanup, a reboot was used to verify that the listener no longer appears.

---

## 10. Final Mechanism and Limitations (settled by measurement on 2026-09-20)

### 10.1 The Only Reliable "Development-Mode Switch" = Toggling the Native Listener
```sh
# ON  (root)
setprop persist.adb.tcp.port 5555 ; setprop ctl.restart adbd
# OFF (root)
setprop persist.adb.tcp.port ""   ; setprop ctl.restart adbd
```
**Measured**: ON → `persist=5555`, `listener6=1`, USB retained; OFF → `persist=[]`, `listener6=0`, USB retained.
`ctl.restart adbd` does change the PID (1711→21088) and **does not require a reboot**.

### 10.2 Why Not Use an In-App Relay (both routes were tried; neither is feasible)
| Attempt | Result |
|------|------|
| Java `LocalSocket` → `/dev/socket/adbd` | The file is `srw-rw---- system:system` → a non-system caller is **rejected by DAC (EACCES)**; moreover, on this ROM `LocalSocket` never even establishes `socket()` |
| Native `socket(AF_UNIX, SOCK_STREAM)` | **EPROTOTYPE(91)** |
| Native `socket(AF_UNIX, SOCK_SEQPACKET)` | connect succeeds but **immediate EOF** (adbd closes at once) → cannot forward |
| Native `SOCK_DGRAM` / abstract namespace | EPROTOTYPE / ECONNREFUSED |

→ `/dev/socket/adbd` **is not a usable relay backend**; the route "TCP → adbd socket" is a dead end.

### 10.3 Joining WiFi: Only Path B Is Usable on Headless Glasses
| Path | Result |
|------|------|
| A: `WifiNetworkSuggestion` (App) | `addNetworkSuggestions status=0` (registered) but `requestNetwork` fails immediately — **headless, there is no screen to approve the system notification**. Adding the `CHANGE_WIFI_STATE` + `NEARBY_WIFI_DEVICES` permissions still had no effect |
| **B: `cmd wifi connect-network` (adb shell or root)** | ✅ **Success**: `wlan0=10.0.0.120/24`, 5 GHz, RSSI −51 |

### 10.4 The Full Measured Flow (PC-driven, most stable)
```
① Plug in a cable once: bootstrap (App + development public key + clear persist)
② Escalate privileges once (deploy the root daemon; on this device it must be armed on every boot)
③ PC gets the glasses onto a subnet: adb shell cmd wifi connect-network <SSID> wpa2 <password>
④ App/PC turns on the listener: DEV_MODE_START (setprop + ctl.restart adbd)
⑤ PC: tools/adbwifi discover / connect <serial> / verify   ← no cable and no root throughout
⑥ Wrap up: DEV_MODE_STOP → the listener closes immediately (persist cleared; it will not come back automatically across boots)
```

### 10.5 Two Limitations You Must Know (honest disclosure)
1. **`ctl.restart adbd` kills the root daemon started via adb shell and its keepalive**
   (measured: after restart, `nc -U` → Connection refused; the socket file remains but there is no listener).
   → After each ON/OFF switch, if you want to switch again you must **re-arm privilege escalation** once (the PC runs `deploy/get_root_now.sh`, about 1–3 minutes).
   → This is inherent behavior of adbd being started by the vendor on this device; the App's keeper cannot fill the gap automatically because the glasses "forbid third-party Apps from running in the background".
2. **Root on this device does not survive a reboot** (it must be re-armed on every boot). Therefore "wireless automatically after boot" currently has only two routes:
   keep `persist.adb.tcp.port` permanently (= give up the safety switch), or plug in a cable once on every boot to arm.

---

## 11. Handover: Current Device State and Recovery Commands (2026-09-20 12:45)

| Item | Current value | Description |
|------|------|------|
| Glasses App | `1.0-safe.6` (versionCode 8) | Includes development mode; installed |
| `persist.adb.tcp.port` | **`5555`** | Development mode is currently ON (left over from the last test) |
| adbd TCP listener | **1 (`[::]:5555`)** | But WiFi is currently **disabled** → no network exposure |
| WiFi | disabled | The glasses are not on any subnet |
| root daemon | **Absent** (killed by `ctl.restart adbd`) | Needs re-arming |
| PC-side transport | USB `device` + `10.0.0.120:5555 offline` (can be ignored) | |

**To return to the "zero exposure" state** (requires re-arming root first):
```bash
SERIAL=<DEVICE_SERIAL> bash deploy/get_root_now.sh      # re-arm (1–3 minutes)
# then either:
#  a) App-driven: am broadcast … --es cmd DEV_MODE_STOP
#  b) Deploy script: printf 'setprop persist.adb.tcp.port ""\nsetprop ctl.restart adbd\n' | adb shell nc -U /data/local/tmp/root_daemon.sock
#  c) Just reboot (persist is already 5555 → it will still come up; so a or b is still needed first to clear persist)
```

**To restore the "wireless adb available" state**:
```bash
SERIAL=<DEVICE_SERIAL> bash deploy/get_root_now.sh                 # arm root
adb shell cmd wifi set-wifi-enabled enabled && adb shell cmd wifi connect-network <SSID> wpa2 <password>
am broadcast -n com.rokid.rc.glasses/.DebugReceiver -a com.rokid.rc.glasses.DEBUG --es cmd DEV_MODE_START
tools/adbwifi discover && tools/adbwifi connect <serial> && tools/adbwifi verify <serial>
```
