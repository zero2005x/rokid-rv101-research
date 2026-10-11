# Communication Protocol — Phone ↔ Glasses (Bluetooth RFCOMM + WiFi TCP / JSON)

> 中文版：[protocol.md](protocol.md)

> This document describes the **current** protocol (last verified: 2026-09-13). Historical changes and the research process are in `reports/`.

## 1. Transport Layer

- **Two available channels**:
  - Bluetooth Classic RFCOMM (SPP UUID `00001101-0000-1000-8000-00805F9B34FB`, service name `RokidCapture`)
  - WiFi TCP (glasses SoftAP `RokidCam` → `192.168.43.1:8555`, glasses-side `WifiServer`)
- ⚠ **Channels are mutually exclusive (single connection)**: the glasses side has **only one** `AppGraph.connection`, and both
  `BluetoothServer.accept()` and `WifiServer.accept()` first call `graph.connection?.close()` (**last one to connect wins**).
  Therefore **a command on either channel replaces the other** — if the phone reconnects over Bluetooth, it kicks out the WiFi socket.
- **Routing (phone-side `Remote.link()`)**: when TCP is connected, **both control and media go over WiFi**; only when it is not connected is Bluetooth used.
  `Remote.ensureMediaLink()` **automatically** establishes WiFi before a media operation (`ENSURE_WIFI_AP` → join AP → connect TCP,
  controlled by the `auto_wifi_media` preference, on by default), falling back to Bluetooth only on failure.
- Both channels use the **same frame protocol and command set** (the glasses-side `ConnectionHandler` serves both RFCOMM and TCP sockets).
- Commands and photo/video transfers all go over the same stream (the frame protocol is unchanged).
- All JSON is always compact (no newlines/extra whitespace), UTF-8.

## 2. Frame Format

Each frame:

```
┌─────────────────────┬────────┬──────────────────────────┐
│ payloadLength (4B)  │ opcode │ payload                  │
│ big-endian unsigned │ 1 byte │ N bytes                  │
└─────────────────────┴────────┴──────────────────────────┘
```

- `payloadLength` = **1 + N** (that is, the total byte count of opcode and payload, excluding its own 4 bytes).
- `opcode`:
  - `0x01`: JSON control message (payload = UTF-8 JSON string).
  - `0x02`: binary data block (payload = raw bytes, belonging to the transfer declared by the preceding `MEDIA_META`).

How to read: read 4B length `L` → then read `L` bytes → the first byte is the opcode and the rest is the payload.

## 3. JSON Message Envelope

All control messages are JSON objects:

```json
{ "t": "<type>", "id": 123, ... }
```

- `t`: message type (see the table below).
- `id`: request sequence number (long, monotonically increasing). A response corresponds to the original request `id` via `"replyTo"`.
- The remaining fields depend on the type.

Generic response type:

| `t` | Direction | Fields | Description |
|-----|------|------|------|
| `ACK` | glasses→phone | `ok`(bool), `error`(str?, on failure), `data`(object?, depends on the command) | Generic command response |

## 4. Command Reference (phone → glasses)

| `t` | Fields | Description | Response |
|-----|------|------|------|
| `HELLO` | `protoVersion`(int) | Handshake | `HELLO_ACK` |
| `GET_STATUS` | — | Query status | `STATUS` |
| `SET_GHOSTLOCK` | `enabled`(bool) | Enable/disable ghostlock LPE | `ACK` |
| `SET_ROOT_MODE` | `mode`(str: `AUTO`\|`MANUAL`) | Set the root acquisition mode | `ACK` |
| `ACQUIRE_ROOT` | — | Run the LPE once now to acquire root | `ACK`(with `rootAcquired`) |
| `SET_LED` | `on`(bool), `color`(str: `white`\|`green`\|`blue`\|`red`\|`all`, default `white`) | Turn the indicator LED of the given color on/off | `ACK` |
| `GET_DEVICE_INFO` | — | Query permission/SELinux/root/WiFi AP status | `DEVICE_INFO` |
| `ENSURE_WIFI_AP` | — | Ensure the glasses SoftAP is started (returns `ok:false` if root is not ready; AP startup is slow, so the phone polls) | `WIFI_AP_STATUS` |
| `SET_DISPLAY_CONFIG` | `display`(object: `conn`,`root`,`camera`,`storage`,`led` bool) | Set the glasses dashboard display content (persisted on the glasses side) | `ACK`(with `display`) |
| `SET_SILENT` | `enabled`(bool) | Toggle silent capture (force-suppresses photo/video tones by bind-mounting silent files; requires root) | `ACK`(with `enabled`/`applied`) |
| `SET_RING` | `enabled`(bool) | Enable/disable **glasses-side** ring control (path B: root reads evdev directly; `enabled=false` stops the `getevent` read loop) | `ACK`(with `enabled`) |
| `ENABLE_WIFI_ADB` | — | Ask the glasses to enable wireless adb: after root, set `persist.adb.tcp.port` (survives reboot) and `service.adb.tcp.port`; adbd in this build **does not bind TCP**, so it returns `ok:false` + `error:"adb-over-tcp not supported on this build"` (non-destructive: does not touch adbd) | `ACK`(with `ip`/`port`/`persistPort`/`autoAfterReboot`) |
| `CAPTURE_PHOTO` | — | Take a photo | `PHOTO_TAKEN` |
| `START_VIDEO` | — | Start recording | `VIDEO_STARTED` |
| `STOP_VIDEO` | — | Stop recording | `VIDEO_STOPPED` |
| `LIST_ALBUMS` | — | Album list | `ALBUM_LIST` |
| `CREATE_ALBUM` | `name`(str) | Create an album | `ACK`(with `album`) |
| `RENAME_ALBUM` | `albumId`(str), `name`(str) | Rename | `ACK` |
| `DELETE_ALBUM` | `albumId`(str) | Delete the album (including its media) | `ACK` |
| `LIST_MEDIA` | `albumId`(str) | Media in the album | `MEDIA_LIST` |
| `GET_THUMBNAIL` | `mediaId`(str), `maxW`(int), `maxH`(int) | Thumbnail | `MEDIA_META` + chunks + `TRANSFER_END` |
| `DOWNLOAD_MEDIA` | `mediaId`(str) | Original file download | `MEDIA_META` + chunks + `TRANSFER_END` |
| `DELETE_MEDIA` | `mediaId`(str) | Delete media | `ACK` |

## 5. Responses / Pushes (glasses → phone)

| `t` | Fields | Description |
|-----|------|------|
| `HELLO_ACK` | `protoVersion`, `deviceName`, `appVersion` | Handshake confirmation |
| `STATUS` | `ghostlockEnabled`(bool), `cameraBusy`(bool), `recording`(bool), `albumCount`(int), `mediaCount`(int), `storageFreeBytes`(long), `rootMode`(str), `rootAcquired`(bool), `ledOn`(bool), `silentEnabled`(bool), `silentApplied`(bool), `display`(object) | Status; `display` = the glasses dashboard's current display configuration (read back and synchronized when the phone connects) |
| `PROGRESS` | `message`(str) | Progress push for long operations (privilege escalation) | 
| `DEVICE_INFO` | `info`(object: `selinux`, `id`, `uid`, `rootAcquired`, `ghostlockEnabled`, `rootMode`, `wifiAp`) | Result of the `GET_DEVICE_INFO` query; `wifiAp` = `{up,ssid,pass,ip,port}` |
| `PHOTO_TAKEN` | `media`(MediaItem) | Photo capture complete |
| `VIDEO_STARTED` | — | Recording started |
| `VIDEO_STOPPED` | `media`(MediaItem) | Recording complete |
| `ALBUM_LIST` | `albums`(Album[]) | Albums |
| `MEDIA_LIST` | `albumId`, `items`(MediaItem[]) | Media |
| `MEDIA_META` | `mediaId`, `transferId`, `fileName`, `mime`, `size`(long) | File transfer started |
| `TRANSFER_END` | `mediaId`, `transferId`, `ok`(bool), `bytes`(long) | File transfer finished |
| `WIFI_AP_STATUS` | `ok`(bool), `wifiAp`(object) | `ENSURE_WIFI_AP` response (`ok` = root available and triggered; `wifiAp.up` = AP ready) |
| `ERROR` | `message`(str) | Unmatched error (`replyTo` optional) |

## 6. Binary File Transfer (glasses → phone)

1. After the glasses receive `GET_THUMBNAIL` / `DOWNLOAD_MEDIA`, they first reply with a `MEDIA_META` frame (opcode 0x01).
2. They then send raw bytes in chunks with opcode `0x02` (each chunk ≤ 256 KiB), **with no additional fields attached**; the order matches what `MEDIA_META` declared.
3. After everything has been sent, they reply with a `TRANSFER_END` frame (opcode 0x01) to indicate completion.
4. The phone assembles by `transferId`, writes the local file, and verifies `bytes` against `TRANSFER_END`.

> Currently only glasses→phone file transfer exists; the phone does not upload files to the glasses.

## 7. Data Model (JSON)

```jsonc
// Album
{ "id": "a1b2c3", "name": "旅行", "createdAt": 1720000000000, "mediaCount": 12 }

// MediaItem
{
  "id": "m9x8y7",
  "albumId": "a1b2c3",
  "type": "PHOTO",            // PHOTO | VIDEO
  "fileName": "IMG_20260824_193000.jpg",
  "mime": "image/jpeg",        // or video/mp4
  "sizeBytes": 2048000,
  "width": 1920,
  "height": 1080,
  "durationMs": 0,             // VIDEO only
  "createdAt": 1720000000000,
  "thumbReady": true
}
```

## 8. Error Handling

- Command failure: reply `ACK { ok:false, error:"..." }`, or `ERROR { message }`.
- Unknown `t`: reply `ACK { ok:false, error:"unknown_type" }`.
- Connection dropped: both ends clean up on their own; the glasses side closes that connection and returns to the accept state, while camera/album state is unchanged.
- File transfer interrupted: the phone discards the incomplete file; `TRANSFER_END.ok=false` is treated as failure.

## 9. Version Negotiation

- When `HELLO.protoVersion` and `HELLO_ACK.protoVersion` do not match, the phone warns of a version mismatch and disconnects.
- Currently `PROTO_VERSION = 1`.

## Ring Events (structured)

The ring fields of `STATUS` **do not carry translated text**, only codes and arguments, with the phone composing the text (see `app-phone` `ui/RingText.kt`):

| Field | Description |
|------|------|
| `ringEnabled` / `ringRunning` / `ringDevice` | Glasses-side ring control switch, whether it is running, and the evdev device path |
| `ringLastKey` | Most recent action code: `photo_ok`／`photo_fail`／`video_start`／`video_saved`／`stop_fail`／`silent_on`／`silent_on_pending`／`silent_off`／`display_switched`／`camera_not_ready`／`no_action`／`no_device`／`start_failed`／`stopped`／`running` |
| `ringLastArg` | Arguments: filename, error reason, gesture name, pattern, or the list of display flags (`conn,root,camera,storage,led`) |

## Progress Events (structured)

`PROGRESS` (glasses → phone) does not carry translated text, only codes and arguments:

| Field | Description |
|------|------|
| `code` | One of `GhostlockCodes`, such as `gl_skip_lpe`, `gl_ready_timeout`, `gl_kernel_panic`, `gl_shizuku_unavailable` |
| `arg` | Arguments (path/reason/count), may be empty |
| `message` | `code\|arg` (retained for logcat and backward compatibility) |

The phone maps these to strings in its own locale with `ui/GhostlockText.kt`.
Failure responses (ack error) are likewise conveyed in the `code\|arg` form and displayed after decoding by `ProgressCodec.decode()`.

## Root State and Privilege-Escalation Metrics (P0-b/P2)

`STATUS` additions (all structured codes, displayed by the phone in its own locale):

| Field | Description |
|------|------|
| `rootState` | `RootStates`: `root_ok`／`root_rearming`／`root_rearmed`／`root_lost_need_arm`／`root_lost_cooldown` |
| `rootHint` | Additional information: the suggested script to run (multiple separated by `|`) or the engine that can be triggered |
| `suEngine` | The engine used for the most recent privilege escalation (`shizuku`／`bridge`／`none`) |
| `suLastOk`／`suLastMs` | Whether the most recent attempt succeeded / how long it took (milliseconds) |
| `suAttempts`／`suSuccesses`／`suFirstTryOk` | Cumulative attempts / successes / first-try successes (the fast path is not counted, to avoid flattering the numbers) |
| `suKeeperChecks`／`suKeeperRearms` | daemon keeper check count / automatic-rebuild success count (stored in prefs, not reset to zero when the App is killed) |

`PROGRESS` additions for engine selection codes: `gl_engine_selected` (arg = engine name), `gl_engine_shizuku_running`,
`gl_engine_bridge_resident`, `gl_engine_none`, `gl_engine_retry_other`.
