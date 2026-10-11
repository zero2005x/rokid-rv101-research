# WiFi Media Channel Fails to Transfer — Log Forensics and Fixes (Two Real Bugs)

> 中文版：[wifi_transfer_fix_2026-09-13.md](wifi_transfer_fix_2026-09-13.md)

- **Date**: 2026-09-13 22:16–22:25
- **Symptom**: The phone and the glasses cannot transfer over WiFi
- **Log starting point**: after the phone's `WifiLink: specifier: network available` there is **no further activity at all**

---

## 0. Conclusion First

Joining the AP actually **succeeded** (the phone obtained `192.168.43.205`, and the glasses-side `hostapd` also reported `AP-STA-CONNECTED`),
but **two independent bugs** made everything after that point impossible:

| # | Bug | Consequence |
|---|-----|------|
| **1** | `NEARBY_WIFI_DEVICES` **is declared in the Manifest but is never requested at runtime** (`granted=false`) | On Android 13, `WifiInfo.getSSID()` returns only `<unknown ssid>` → `isOnAp()` is **forever false** → even after joining the AP it is still judged a failure (`manual join required`) |
| **2** | The TCP socket **is not bound to the AP network that was just joined** | A network joined via `WifiNetworkSpecifier` is **not** the app's default network → the socket goes out over the cellular network; measured error: `failed to connect to /192.168.43.1 from /10.225.98.247` |

---

## 1. Forensic Process (Every Step Has a Log)

### 1.1 Glasses Side: the AP and the Service Are Both Actually Fine

```
22:14:37 CaptureService: WiFi media server ready (:8555)
22:14:37 WifiServer: WifiServer listening on :8555
22:16:23 wificond: New station 9a:42:2c:7d:f1:57 connected to hotspot using interface wlan2
22:16:23 hostapd : wlan2: STA 9a:42:2c:7d:f1:57 WPA: pairwise key handshake completed (RSN)
$ ip addr show wlan2   → inet 192.168.43.1/24  state UP
```
→ The hotspot, the password handshake, and the server listener are **all normal**; the problem is not on the glasses.

### 1.2 Phone Side: Join Succeeds, Then Drops

```
22:16:23 wpa_supplicant: wlan0: CTRL-EVENT-CONNECTED - Connection to c2:7a:40:cb:3d:cd completed
         TransportInfo: SSID "RokidCam", IP /192.168.43.205, Supplicant state COMPLETED, RSSI -34
22:16:23 WifiLink: specifier: network available          ← after this there are no further WifiLink/TcpClient messages
```
The glasses side also has **no** `wifi connection accepted` → the phone never actually established TCP.

### 1.3 After Fixing Bug 1, Bug 2 Is Exposed

```
22:21:55 WifiLink: specifier: network available (localIp=fe80::...)
22:22:03 WIFI_CONNECT result=Failure(tcp connect failed:
         failed to connect to /192.168.43.1 (port 8555) from /10.225.98.247 (port 48082) after 6000ms)
```
`from /10.225.98.247` is a **cellular network** address → confirming the socket was not bound to the glasses AP.

---

## 2. Fixes

### 2.1 `NEARBY_WIFI_DEVICES` Must Be Requested at Runtime (`MainActivity.requestBlePermissions`)

```kotlin
if (Build.VERSION.SDK_INT >= 33) needed += Manifest.permission.NEARBY_WIFI_DEVICES
```
Measured after installation: `android.permission.NEARBY_WIFI_DEVICES: granted=true` ✅

### 2.2 No Longer Relying on SSID Alone to Decide "Are We Already on the AP" (`WifiLink`)

- Added `joinedNetwork`: record that `Network` in `onAvailable(network)`.
- `isOnAp()` now **prefers** deciding by "does this Network's local IPv4 fall inside the glasses AP subnet (`192.168.43.x`)"
  —— this check **requires no permission at all**; SSID comparison is demoted to a fallback.

### 2.3 Bind the TCP Socket to That Network (`TcpClient` + `Remote`)

```kotlin
// TcpClient.connect(..., network: Network?)
val s = Socket()
network?.let { runCatching { it.bindSocket(s) } }    // ← the key
s.connect(InetSocketAddress(host, port), timeoutMs)
```
`Remote.ensureWifiMedia()` passes in `wifiLink.joinedApNetwork()`.

---

## 3. Measured After the Fix (End-to-End Success)

```
22:24:08 WifiLink: specifier: network available (localIp=fe80::...)
22:24:09 Remote  : wifi media: apNet=116 localIp=fe80::...
22:24:09 DebugReceiver: WIFI_CONNECT result=Success(WifiApInfo(up=true, ssid=RokidCam, ip=192.168.43.1, port=8555))

# glasses side
22:24:09 WifiServer: wifi connection accepted from /192.168.43.205:60284
$ netstat -tn | grep 8555 → ::ffff:192.168.43.:8555 … ESTABLISHED

# actual transfer
22:24:40 CommandRouter: recv: LIST_ALBUMS id=4 → LIST_MEDIA id=6 → DOWNLOAD_MEDIA id=7
22:24:40 DebugReceiver: TEST_CHAIN albums=[Camera(7)] / media count=7
22:24:40 DebugReceiver: TEST_CHAIN 縮圖=Success(.../thumbs/med-a3fbaa79.jpg)
```

---

## 4. Confidence

| Statement | Label | Confidence | Basis |
|------|------|------|------|
| The glasses-side AP/server is normal | FACT | **Very High** | hostapd / wificond / netstat |
| `NEARBY_WIFI_DEVICES` not being granted invalidates the SSID check | FACT | **Very High** | `dumpsys package` shows `granted=false`; behavior changed after granting |
| The socket originally went out over the cellular network | FACT | **Very High** | The source address `10.225.98.247` in the error message |
| WiFi transfer works after the fix | FACT | **Very High** | `wifi connection accepted` + TCP ESTABLISHED + thumbnail/list actually transferred successfully |
| The UI flow of "the button actually downloads photos to the album" | Not verified item by item | — | The underlying channel works; the user is advised to confirm once more through the UI |

## 5. Not Performed
No glasses-side AP settings were changed and no existing pairing was touched; only the phone-side permission request and socket binding were changed.

---

# Rev.2 (22:50–23:00) — The Third Bug, and the One That Really Kept Everything "on Bluetooth"

- **User report**: "Photos are currently being transferred over Bluetooth, not the WiFi I expected; sending a single 10-second video is extremely slow"
- **Conclusion**: The WiFi channel itself is fine (Rev.1 already fixed it). The problem is **channel selection and lifecycle**:
  the two glasses-side channels are **mutually exclusive**, while the phone side **tears down WiFi on every ordinary command**, and **no media operation automatically establishes WiFi**.

## 6. Root-Cause Chain (Every Link Has a Source-Code Basis)

| Link | Evidence |
|------|------|
| The glasses-side BT/WiFi servers are **mutually exclusive**; on accept each closes the previous one | `BluetoothServer.kt:43` and `WifiServer.kt:39` are both `graph.connection?.close()` |
| **All** ordinary phone-side commands are hard-wired to Bluetooth | `Remote.request()` originally called `client.request(...)` directly (including `LIST_ALBUMS` / `LIST_MEDIA` / `GET_STATUS`) |
| When Bluetooth drops, it automatically reconnects Bluetooth | `Remote.ensureConnected()` redials whenever `client.connected == false` |
| The media channel only falls back when "Bluetooth is also down" | `requestFile()` originally used `mediaClient()` → `tcp.connected ? tcp : client` |
| WiFi **is not established automatically** | `ensureWifiMedia()` has only two call sites: `DebugReceiver(WIFI_CONNECT)` and the switch in `SettingsFragment` (off by default) |

**Flip-flop loop**: WiFi connects → the glasses close Bluetooth → the phone's next `Remote.status()` (triggered by a tab refresh) →
`ensureConnected()` reconnects Bluetooth → the glasses' `BluetoothServer.accept()` closes the WiFi socket →
`mediaClient()` falls back to Bluetooth. Measured on device, this loop completes within seconds; what the user saw as "Transport: Bluetooth" is exactly this result.

## 7. Fixes

| # | Fix | File |
|---|------|------|
| 1 | Added `Remote.link()` (WiFi first, Bluetooth fallback) and changed `request()` to go through it; when sending an ordinary command over WiFi fails it falls back to Bluetooth (long-running commands are not resent, to avoid triggering the LPE twice) | `Remote.kt` |
| 2 | `ensureConnected()` **no longer redials Bluetooth** when WiFi is already connected | `Remote.kt` |
| 3 | Added `ensureMediaLink()`: before a media operation, if there is no channel, **automatically** ask the glasses to start the hotspot → join the AP → connect TCP; only on failure fall back to Bluetooth (`Mutex` deduplication, 60-second failure cooldown, 30-second cap on hotspot wait in automatic mode) | `Remote.kt` |
| 4 | `isConnected` changed to `client.connected \|\| tcp.connected` (when WiFi connects, Bluetooth is normally closed by the peer anyway, otherwise the UI wrongly shows "not connected") | `Remote.kt` |
| 5 | The WiFi switch on the settings page became a **persisted preference** `auto_wifi_media` (**enabled by default**); turning it off reconnects the Bluetooth control link | `SettingsFragment.kt`, `Remote.kt` |
| 6 | `WifiLink`: the specifier callback is **deliberately kept registered** (unregistering makes the system tear down that WiFi connection); the old one is released only just before issuing a new join request | `WifiLink.kt` |
| 7 | `LogStore.onChanged` is now always posted to the main thread (automatic connection records come from a background thread and previously triggered `CalledFromWrongThreadException`) | `RemoteFragment.kt` |
| 8 | The settings-page explanatory text became "Media and control both prefer WiFi; Bluetooth serves only as a fallback when WiFi is unavailable" (synchronized across 14 locales) | `strings.xml`, `scripts/i18n/` |

## 8. Measured (Same 10.6-Second Video, Same Device, Same Night)

Sample: `VID_1789308941304.mp4`, **16,736,320 bytes (16.0 MiB), duration 10,562 ms** (`mediaId=med-f593c229`).
Measurement method: the `TEST_TRANSFER` DebugReceiver command (newly added; it prints the actual transport channel, elapsed time and throughput).

| Channel | Elapsed | Throughput | Source |
|------|------|------|------|
| **WiFi (automatically established after the fix)** | **3,786 ms** | **4,420.6 KB/s (≈4.3 MB/s)** | `TEST_TRANSFER ... elapsed=3786ms throughput=4420.6KB/s link=wifi` |
| Bluetooth (forced with `SET_AUTO_WIFI false` + `WIFI_DISCONNECT`) | **323,956 ms (5 min 24 s) → timed out and failed** | **51.7 KB/s** | `TEST_TRANSFER ... result=Failure(TimeoutCancellationException: Timed out waiting for 323938 ms) elapsed=323956ms throughput=51.7KB/s link=bt`; the `.part` sampling also agrees (22:54:04→5,505,024; 22:54:24→6,553,600; 22:54:45→7,340,032) |

→ **For the same video, WiFi is about 85 times faster** (3.8 s vs 324 s), consistent with the user's "extremely slow" impression.

**Incidental finding (a bug in the Bluetooth fallback path itself)**: `transferTimeout()` originally estimated at **60 KB/s**,
but measured Bluetooth is only **51.7 KB/s** → this 16.7 MB file **failed by just barely hitting the timeout** (the `.part` indicates the bytes had in fact all arrived;
`TRANSFER_END` was just 18 ms late). The estimated rate has been lowered to **45 KB/s** so that the fallback path can at least actually complete.

### 8.1 Measured Sequence of Automatically Establishing WiFi (no manual switch toggling, no manual AP join)

```
22:50:26 DebugReceiver: CONNECT <DEVICE_ALIAS> result=Success
22:50:29 Remote  : 媒體通道：建立 WiFi 中…            ← triggered automatically by the media operation (the settings-page switch was never touched)
22:50:31 WifiLink: specifier: network available
22:50:33 Remote  : wifi media: apNet=120
22:50:33 Remote  : 媒體通道：WiFi 192.168.43.1:8555    ← completed in 4 seconds, and no system join dialog appeared (the phone was already on the AP)
22:51:11 TEST_TRANSFER ... link=wifi → elapsed=3786ms
```

### 8.2 Final Validation (**Shipped APK `1.2`**, 23:00–23:02, Same Video)

| Case | Elapsed | Throughput | Note |
|------|------|------|------|
| First transfer after cold start | 16,532 ms | 1,012 KB/s | About **11 seconds of it is rejoining the AP** (the previous test had just done `WIFI_DISCONNECT`) |
| **Second transfer in the same session** | **2,990 ms** | **5,597 KB/s (5.5 MB/s)** | The log starts already at `link=wifi`, and `DebugReceiver: connected=false` (**Bluetooth is down**) |

```
23:01:25 DebugReceiver: TEST_TRANSFER ... link=wifi          ← starts out on WiFi
23:01:28 DebugReceiver: TEST_TRANSFER result=Success elapsed=2990ms throughput=5597.4KB/s link=wifi
```

The second entry also proves that **fix 1 in §7 is effective**: with Bluetooth entirely disconnected (`connected=false`),
ordinary commands such as `LIST_ALBUMS` / `LIST_MEDIA` **are still delivered** (over WiFi), and the transfer completes in 3 seconds.

> The 11-second "join the AP" cost of a cold start is paid only once per renegotiation; within the same session everything afterwards is on the 3-second scale.

## 9. Confidence

| Statement | Label | Confidence | Basis |
|------|------|------|------|
| The two glasses-side channels are mutually exclusive (the later one to connect wins) | FACT | **Very High** | `graph.connection?.close()` in two places in the source |
| An ordinary command over Bluetooth kills the WiFi socket | FACT | **Very High** | The source code above + the flip-flop behavior in the phone log before the fix |
| Media operations originally did not establish WiFi automatically | FACT | **Very High** | `ensureWifiMedia()` has only 2 call sites in the whole repo |
| After the fix, media automatically goes over WiFi | FACT | **Very High** | The §8.1 sequence + `link=wifi` |
| WiFi is about 85 times faster than Bluetooth (3.8s vs 324s) | FACT | **Very High** | Both are self-reported by `TEST_TRANSFER` as `elapsed`/`throughput`, and agree with the `.part` sampling |
| The UI (album gesture download) takes the same path | Inferred | High | Both go through `Remote.downloadMedia()` → `requestFile()` |

## 10. Not Performed / Not Verified
- No glasses-side program was modified (this round was entirely phone-side).
- The fallback experience when "the user refuses to join the AP" was not measured (it will only retry after a 60-second cooldown).
- Old `.part` leftover files (previously interrupted transfers, including the one from this Bluetooth timeout) are still on the device and were not cleaned up.
- The album UI gesture was not rerun once (the UI and `TEST_TRANSFER` share the same path, `Remote.downloadMedia()` → `requestFile()`).

---

# Rev.3 (23:03–23:10) — "Download Is Fast but the Text Says Bluetooth" = Stale UI Label (Fixed) + an Incidental Catch of Index Data Loss

## 11. Symptom and True Cause

User: "When downloading a video it is fast, but the text shows it is transferring over Bluetooth."

**The transfer itself is fine (it really is WiFi); it is the "transfer method" line that fails to update.** The mechanism is a **race condition**:

1. The glasses side closes the Bluetooth connection in `WifiServer.accept()` → the phone-side `client.onDisconnected` fires.
2. `RemoteFragment` registered **only** `onDisconnected` (no `onConnected`) → so it calls `refresh()` at exactly that moment.
3. At this point the WiFi TCP is only just about to connect (`tcp.connected` is still false) ⇒ the screen writes "Transport: **Bluetooth**".
4. After WiFi connects, **no callback** refreshes it again ⇒ that line stays "Bluetooth" forever, while the transfer has long been running over WiFi (which is why it is fast).

## 12. Fixes

| # | Fix | File |
|---|------|------|
| 1 | `LinkClient` gained `onConnected` (fired in `attachStreams()`) | `net/LinkClient.kt` |
| 2 | `Remote` gained a single notification point `onTransportChanged`, and **centrally** registers the four callbacks (client/tcp × connect/disconnect) in `ensureInit()`; `RemoteFragment` no longer overrides `onDisconnected` itself | `Remote.kt`, `ui/RemoteFragment.kt` |
| 3 | Added `Remote.wifiNegotiating`: during the few seconds of negotiation it shows **"Transport: WiFi connecting…"** (reusing the existing string `transport_wifi_connecting`) instead of mislabeling Bluetooth | `Remote.kt`, `ui/RemoteFragment.kt`, `ui/SettingsFragment.kt` |
| 4 | `MainActivity` receives `Remote.onTransportChanged` → refreshes all fragments on the main thread via `refreshAllFragments()`, and now also includes the **Settings tab** in the refresh (previously it refreshed only Remote/Album/Local) | `MainActivity.kt` |

### 12.1 On-Device Validation (Three Screenshots, No Manual Tab Switching)

| Time point | On-screen text | Screenshot |
|------|----------|------|
| Before download (just after `WIFI_DISCONNECT`) | Transport: Bluetooth | `reports/ui_transport_2026-09-13/01_before_bt.png` |
| Download triggered, negotiating (t+2s) | **Transport: WiFi connecting…** | `reports/ui_transport_2026-09-13/02_negotiating_wifi.png` |
| After connecting (t+11s) | **Transport: WiFi 192.168.43.1:8555** | `reports/ui_transport_2026-09-13/03_wifi_connected.png` |

Log correspondence: `23:04:42 媒體通道：建立 WiFi 中…` → `23:04:48 wifi media: apNet=126` → `23:04:48 媒體通道：WiFi 192.168.43.1:8555` → download `link=wifi`.

> The screenshots have been filed under `reports/ui_transport_2026-09-13/` (with a README); the A/B index samples are in `reports/media_index_repair_2026-09-13/`.

## 13. Incidental Finding: 6 Glasses-Side Media Index Entries Lost (Fixed, and Self-Healed Back to 6)

While investigating "only 1 entry remains in the album", it was found that the **glasses-side `media/index.json` was rewritten at 23:04 to contain only 1 entry**,
even though every file in the album directories was still present:

```
23:04 index.json: {"albums":[{"id":"alb-3b6dfe09",...}],"media":[only one entry, IMG_1789311878598.jpg]}
Phone LIST_MEDIA_INFO: total=7 (22:51) → total=1 (23:05)
Disk: 7 files still present
```

**Why this happens**: `index.json` is a **single full JSON overwrite**, and it has two fragile points——
① The parse loop in `load()` **shares one try/catch**; if any single `MediaItem.fromJson` throws,
   the outer catch swallows it and **everything after it is not loaded** (in this case albums were loaded and media was entirely empty);
② `persist()` uses `File.writeText()` (truncate first, then write); if the process is killed midway (LMK / force-stop / APK install)
   it leaves an empty window or a half-written file.

**Fixes** (`MediaRepository.kt`):
| # | Fix |
|---|------|
| 1 | Parsing changed to **per-entry try/catch**: a single bad item now only skips itself and no longer drags everything down |
| 2 | Added **`reconcileWithDisk()`**: after loading, scan each album directory and re-register files that are "on disk but not in the index" (the index is no longer the single source of truth) |
| 3 | `persist()` changed to an **atomic write**: write `index.json.tmp` first, then `renameTo()` |

**On-device validation** (restarting the glasses app after installing the new build):
```
23:07:20 W MediaRepository: index reconciled: 6 file(s) re-registered from disk
Phone LIST_MEDIA_INFO: total=8 (including two long videos of 636MB/664MB)   ← the list has been restored
```

## 14. Still Unexplained: 3 Files Disappeared from Disk Between 22:58 and 23:05

`VID_1789308941304.mp4` (16.7 MB, the one used for this benchmark), `IMG_1789308934884.jpg`,
and `IMG_1789308959814.jpg` were still present at 22:58 (the Bluetooth benchmark was writing its `.part`), and by 23:05 they were no longer on disk.

- **The app's delete path can be ruled out**: `DELETE_MEDIA` → `MediaRepository.deleteMedia()` **deletes the file and synchronously persists the index**;
  had it gone through that path, the index would not have become "only 1 entry".
- **Automatic glasses-side cleanup can be ruled out**: across the whole repo only `deleteMedia()`, `deleteAlbum()` and `CameraController`'s
  "discard the current partial file when recording is aborted" delete files; there is no logic that scans for and deletes unknown files.
- The logcat buffer has rotated, so there are no `DELETE_MEDIA` records to consult ⇒ **cause undetermined** (possibly the user deleted them through another UI/tool).

⚠ Two extremely long videos, `VID_1789290250579.mp4` (**636 MB**) and `VID_1789304656692.mp4` (**664 MB**), are still on the glasses
(about 1.3 GB combined; the latter has `durationMs=0` in the index, judged to be a previously interrupted recording). To clean up, delete from inside the app so the index is synchronized.

---

# Rev.4 (23:14–23:20) — Answering "Is There Anything Else Abnormal?": the Index Problem Was Earlier and Worse Than Previously Believed

## 15. User Confirms: the Missing Files Were Deleted by the User ⇒ External Cause Ruled Out

But re-reconciling revealed **another, earlier anomaly**:

| Time point | App-captured files on disk | App index entries | Difference |
|------|--------------------|-------------|------|
| 22:45 (during Rev.2 testing) | **13** | 7 (22:51 `LIST_MEDIA_INFO total=7`) | **6 files on disk but completely invisible to the app** (neither browsable nor downloadable) |
| 23:04 | 7 (the user deleted 7) | 1 | The index was lost wholesale |
| 23:18 (after the fix) | 9 | 9 | ✅ consistent |

In other words: **the index/disk inconsistency did not first appear at 23:04; by 22:45 there were already 6 "ghost files"**
(`VID_1788822607406`, `VID_1788822710658`, `VID_1789152641290`, `VID_1789290250579`,
`VID_1789304656692`, `CameraXeb9697ea-…`), and these are exactly the 6 that my post-fix `reconcileWithDisk()`
recovered.

## 16. A/B Experiment: Proving "One Bad Entry → the Whole Rest Silently Disappears"

Removing the required field `fileName` from the **4th entry** in the index (`MediaItem.fromJson` uses `getString`, which throws `JSONException`),
leaving the other 8 entries intact, and then launching with each of the two APK builds:

| Version | Log after launch | List seen by the phone |
|------|-----------|---------------|
| **Old `1.0-safe` (code 3)** | `E MediaRepository: index load failed` + full stack trace (swallowed by the outer catch) | **total=4** ← **all 5 entries after the 4th disappeared** |
| **New `1.0-safe.2` (code 4)** | `E media entry #4 skipped: No value for fileName`<br>`W index reconciled: 1 file(s) re-registered from disk` | **total=9** ← not one entry missing |

**This is the cause of the ghost files**: the parse loop in `load()` shares a single try/catch, and if any entry throws,
**all subsequent entries are not loaded**; then any subsequent `persist()` (triggered by taking a photo, recording a video, or generating a thumbnail)
writes this "already shortened" list **back permanently**, and from then on the files on disk do not exist as far as the app is concerned.

## 17. Other Anomalies (Confirmed, Not Program Errors)

1. **3 unplayable videos, about 1.4 GB combined** (recording was interrupted → the MP4 has no `moov` atom written):

   | File | Size | Index durationMs | Trailing moov |
   |------|------|----------------|-----------|
   | `VID_1789290250579.mp4` | 636.9 MB | **0** | **none** |
   | `VID_1789304656692.mp4` | 664.7 MB | **0** | **none** |
   | `VID_1789152641290.mp4` | 111.9 MB | **0** | **none** |
   | `VID_1788822710658.mp4` (control) | 25.0 MB | 15139 | present |

   `durationMs=0` is consistent with "no trailing `moov`" ⇒ these three recordings were interrupted before finalization
   (device reboot / app killed), the **files are incomplete and unplayable**, and deleting them directly is recommended.

2. **5 phone-side `.part` leftovers totaling 63 MB**: transfers "write `.part` first and rename only on completion",
   so any failure (including the Bluetooth timeout) leaves a full-size leftover. These were manually cleaned up, and `LocalStore.cleanupStaleParts()` was added to the app
   (clears `.part` files older than 24 hours at app startup;
   the per-transfer timeout cap is about 4 hours, so in-progress files are not mistakenly deleted).

3. On the glasses, the two useless 636 MB + 664 MB files occupy 1.3 GB and are recommended for deletion as well.

---

# Rev.5 (23:29–23:30) — Deleting the 3 Unfinalized Videos as Instructed (Through the App Path, Index Synchronized)

- **Authorization**: The user explicitly instructed deletion of the following 3; the **control `VID_1788822710658.mp4` was kept**.
- **Method**: **`rm` was not used**; instead the app's delete path was used (`DELETE_MEDIA` → `MediaRepository.deleteMedia()`),
  because that path **deletes the file, deletes the thumbnail, and persists the index at the same time**; using `rm` would leave 3 dangling index entries pointing at nonexistent files.

| File | Size | mediaId | Result |
|------|------|---------|------|
| `VID_1789290250579.mp4` | 636,873,536 B | `med-eba0d12e` | ✅ deleted |
| `VID_1789304656692.mp4` | 664,663,857 B | `med-0e50b012` | ✅ deleted |
| `VID_1789152641290.mp4` | 111,906,896 B | `med-a918b5ef` | ✅ deleted |
| `VID_1788822710658.mp4` (control) | 25,043,832 B | `med-86d1521c` | ⬜ **kept** |

**Space freed: 1,348.0 MB (about 1.3 GB)**

## 18. Post-Deletion Verification

```
Glasses disk: 6 files (was 9)
Index       : 6 entries (consistent with disk)
Phone list  : LIST_MEDIA_INFO total=6
DELETE_MEDIA reported: Success (3/3)
Control VID_1788822710658.mp4 still present
Restarting the glasses app: no reconcile message at all ⇒ disk and index are consistent (reconcile only acts when there really is an inconsistency)
Storage: /storage/emulated 19G, 7.3G used, 11G available
```

Remaining 6 entries: `CameraXeb9697ea-….jpg`, `IMG_1789311878598.jpg`, `IMG_1789312579881.jpg`,
`VID_1788822607406.mp4`, `VID_1788822710658.mp4`, `VID_1789311924352.mp4`.

---

# Rev.6 (2026-09-13 23:4x) — Reinforcement After Adversarial Verification (3 Real Gaps, Fixed and Rebuilt)

An independent agent **attempted to falsify §6–§18 of this report and FACTS P27–P30 item by item**; the result: of 8 claims, 5 held, 2 were "partially true",
and 1 held with a caveat. Below are the **points that were falsified or found insufficient**, together with the reinforcements.

## 19. Real Gaps Caught by Verification (All Fixed)

| # | Gap | Why it is a real problem | Fix |
|---|------|---------------|------|
| **1** | `Remote.link()` is **dead code**: `request()` inlines its own `tcp.connected` check, and its only caller `mediaClient()` has zero call sites ⇒ the report/FACTS statement that "`request()` was changed to go through `link()`" **does not match the program** | Documentation inconsistent with the program (the class of problem this project cares about most) | `request()` changed to `if (link() === tcp)`; the **dead code `mediaClient()` was deleted** |
| **2** | **`tcp.onPush` was never registered**: `ensureInit()` only attached the four connection callbacks, and `onPush` was set only on `Remote.client`; while the glasses' `AppGraph.push()` pushes over the **current connection** ⇒ **when WiFi becomes the current channel, all pushes such as `PROGRESS` (LPE progress) are dropped** | This is a **newly introduced functional regression** from moving control onto WiFi (not an old bug) | Added a centralized `Remote.onPush` property whose setter attaches to **both** `client` and `tcp`; `RemoteFragment` now sets `Remote.onPush` |
| **3** | **Index parsing only protected the `media` array**: the `albums` loop still shares the outer try, and `Album.fromJson` uses `getString("id"/"name")`, which likewise throws ⇒ **one bad album prevents all albums and the entire media array from loading** | The §16 A/B tested only the media array, so the fix's scope was insufficient when it claimed "no longer drags everything down" | The `albums` loop also changed to per-entry try/catch (`album entry #i skipped`); tests were added |

## 20. Three Further Reinforcements (Incidental Findings of the Verification Report)

| # | Problem | Fix |
|---|------|------|
| 4 | **An explicit disconnect does not notify the UI**: `LinkClient.disconnect()` does not call `onDisconnected` (only the readLoop's finally does) ⇒ when "Disconnect" is pressed or the WiFi channel is closed, other tabs may remain on the old transport method | `disconnect()` now calls `onDisconnected` when the state really transitions from connected to disconnected (recording `wasConnected` first to avoid duplicate notifications) |
| 5 | **Ghost entries never disappear**: `reconcileWithDisk()` only adds, never subtracts ⇒ files deleted by means other than the app are still shown in the list and written back on every persist; and `getThumbnail()` **returns the old cached thumbnail directly** (without checking the source file) ⇒ the image is still visible after the file is deleted | `reconcileWithDisk()` gained **reverse reconciliation** (present in the index, absent on disk → remove that entry and delete the thumbnail, logging `-$dropped`); `getThumbnail()` now checks that the source file exists before returning the cache |
| 6 | `lastAutoWifiFailAt` was not `@Volatile` yet was written in `closeWifiMedia()` (outside the lock) and read inside the lock | Added `@Volatile` |

## 21. Caveats in the Verification Report That "Need No Change but Must Be Recorded Faithfully"

1. **The mutual exclusion of the two channels is not an atomic operation**: although `graph.connection` is `@Volatile`, the accept side is
   check-then-act (read the old handler → close → install the new handler); if two coroutines enter at the same time, in theory both could stay alive
   (`connection` only points at the last one). The window is extremely small and **was not observed in testing**; "mutual exclusion" in this report and in FACTS should be read as
   "mutually exclusive under the normal sequence", not a thread-safety guarantee.
2. **`WifiServer.stop()` does not close `graph.connection`**, whereas `BluetoothServer.stop()` does (asymmetric).
   No actual consequence was observed; recorded for further investigation.
3. The **manual call paths of `ensureWifiMedia()`** (`SettingsFragment`, `DebugReceiver`) **do not take `wifiEnsureLock` and are not subject to the cooldown**
   ⇒ manual and automatic negotiation can run in parallel. The automatic path's own deduplication is effective.
4. The precise statement of "fall back to Bluetooth only on failure" is "fall back to Bluetooth if no TCP was obtained" — during cooldown or when `autoWifi=false` it also goes straight to Bluetooth.
5. **The boundary of `.part` cleanup**: it looks only at mtime, and every chunk of a `.part` updates mtime ⇒ an in-progress transfer will not be mistakenly deleted;
   a mistaken deletion would require "24 consecutive hours of zero writes and the withTimeout not yet expired", which by the formula requires `size > 3.62 GiB`
   (the current largest file is 664 MB, and its timeout is 4.115 h). Therefore §17-2's "per-transfer timeout cap of about 4 hours" should be corrected to
   **"about 4.1 hours based on the current largest file; the formula itself has no cap"**.
6. **The "three-state display" applies only to the Remote tab**; the settings page is **four-state** (one extra "Automatic mode standing by: media and control both prefer WiFi…" hint).
   The "three-state" wording in this report and in FACTS is superseded by this.

## 22. Rebuild and Verification

- Glasses `1.0-safe.3` (versionCode 5), phone `1.3` (versionCode 4).
- Verification focus: ① a bad album no longer drags down media; ② when WiFi is the current channel, `PROGRESS` still reaches the phone;
  ③ the screen updates immediately after "Disconnect" is pressed; ④ after an external file deletion the entry is automatically removed from the list (`-$dropped`).

## 23. Installation and Verification Status of Rev.6 (Recorded Faithfully)

| Item | Status |
|------|------|
| Glasses `1.0-safe.3` (code 5) | ✅ Built and copied to `dist/`; could not be installed that night (glasses offline). **Installed later on 2026-09-16 07:51 and all of A–E verified**, see `reports/index_resilience_verification_2026-09-16.md` |
| Phone `1.3` (code 4) | ✅ Built, installed, launched without crashing (`versionName=1.3`, process alive) |
| Runtime verification (bad-album tolerance / push over WiFi / disconnect notification / automatic removal after external deletion) | ⏳ **To be done once the glasses are back online** (see §24 to-do) |

> The purpose of this section: to avoid mistaking "built" for "verified". Anything not marked ✅ in the table above has not yet been confirmed on device.
>
> ⚠ **Correction of the offline cause (2026-09-16)**: at the time it was inferred to be an adb/USB connection problem, but it was actually the **glasses powering themselves off**——
> `ro.boot.bootreason=shutdown,leg_fold_timeout` (automatic shutdown on fold/not-worn timeout), with `uptime` of only 9 minutes (they only booted after being reconnected to power).
> The shutdown caused root to be lost (SELinux reverted to `Enforcing`), so during the catch-up run **SoftAP could not start** (`WifiHotspot: root daemon 未就緒`),
> and item D was instead validated by creating a real TCP channel with an **adb tunnel** (see §2.2 of the catch-up report).

## 24. To-Do (After the Glasses Come Back Online) — **All Completed on 2026-09-16**

> **Automation is ready**: `scripts/verify_index_resilience.sh` (covers items 1–5 below, touches only the self-created test file `TEST_reconcile.jpg`,
> backs up the index before every change, and restores it when finished). Two test hooks were also added for this:
> the glasses-side `PUSH_TEST` debug command (so push over WiFi can be verified without actually running the LPE),
> and the phone side logging `RemotePush: progress code=… arg=…` to logcat when a push is received (so it can be judged automatically).

1. Install `dist/app-glasses-v1.0-safe.3.apk` and confirm `versionName=1.0-safe.3`.
2. **Bad-album tolerance**: remove the required `name` from index `albums[0]` → `album entry #0 skipped` should appear,
   and **media should still all load** (the old version would lose both albums and media).
3. **Reverse reconciliation**: place a test file (such as `TEST_reconcile.jpg`) in the album directory → at startup `+1 re-registered` should appear;
   then delete that file from outside the app → at the next startup `-1 removed (file missing on disk)` should appear, and the phone's list should drop one entry accordingly.
4. **Push over WiFi**: with WiFi as the current channel, trigger one LPE progress event (`ACQUIRE_ROOT`); the phone's operation log should show a progress message
   (before the fix there would be none at all).
5. **Disconnect notification**: after pressing "Disconnect", the transport method on the Remote tab should update immediately, with no need to switch tabs manually.
