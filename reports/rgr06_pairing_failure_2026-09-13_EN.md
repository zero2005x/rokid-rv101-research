# RGR06 Ring Pairing Failure — Log Forensics, a Real Bug in Our Code, and a Workable Solution

> 中文版：[rgr06_pairing_failure_2026-09-13.md](rgr06_pairing_failure_2026-09-13.md)

- **Date**: 2026-09-13 17:05–17:14
- **Ring**: `RGR06` = `D1:03:CB:ED:C7:97` (LE-only, HOGP `0x1812`, Appearance `0x03C2` Mouse, random static address)
- **Hosts**: phone `<PHONE_SERIAL>` (Android 13 / MIUI V816) · glasses `<DEVICE_SERIAL>` (Android 12 / API 32)
- **Method**: logcat on both ends (including the BT stack `bt_btm_sec` / `bt_smp` / `btif_dm`), `dumpsys bluetooth_manager`,
  `getevent -pl`, and the app's built-in debug broadcasts. **No** existing pairing on the phone was changed.

---

## 0. Conclusion (in three sentences)

1. **The phone's failure to pair is not a problem with our app**: every time, the ring **accepts the LE connection but aborts during the SMP pairing exchange**
   (`smp_reason:SMP_FAIL`, `sec_level:0x0`, the fixed SMP channel `CID 0x0006` is never established) → the phone clears the keys and gives up.
   At the same time the same phone connects to **two other** BLE devices normally, so the phone's BLE is not broken; **this particular ring is rejecting that host**.
2. **But in the process we caught a real bug in our own code**: when `RingBonder.bond()` is called from a `BroadcastReceiver`
   on **Android 12**, it throws `ReceiverCallNotAllowedException` and **crashes the app outright** (always reproducible on the glasses; not on the Android 13 phone).
3. **Once the glasses side was fixed, RGR06 paired successfully immediately**, it is now the glasses' **HID input device**, and glasses-side ring control has been switched to it
   → in other words, **using the glasses as the host is a workable solution** (and path B was already the route that works in the background).

---

## 1. Phone Side: Stack-Level Cause of the Failure

Three attempts (UI scan list, `PAIR_RING --es mac`, and one more) behaved identically:

```
17:08:53.486 RingBonder: 發起配對 D1:03:CB:ED:C7:97（RGR06）
17:08:53.492 bt_btm_sec: BTM_SecBond: Forcing transport LE (was auto) because of the address type
17:08:53.492 bt_btm_sec: btm_sec_change_pairing_state: Pairing state changed IDLE => WAIT_AUTH_COMPLETE pairing_flags:0x81
17:08:54.369 bluetooth : on_connection_complete: Le connection completed to device:d1:03:cb:ed:c7:97   ← ACL connected
17:08:54.691 bluetooth : smp_proc_pairing_cmpl: Pairing process has failed to remote:xx:xx:xx:xx:c7:97
                         smp_reason:SMP_FAIL sec_level:0x0                                            ← SMP failed 322 ms later
17:08:54.691 bt_stack  : [WARNING:l2c_api.cc] L2CA_RemoveFixedChnl BDA: d1:03:cb:ed:c7:97 CID: 0x0006 not connected
17:08:54.691 bt_btif_storage: btif_storage_remove_ble_bonding_keys: Removing bonding keys for bd addr:d1:03:cb:ed:c7:97
17:08:54.692 bt_btm_sec: btm_sec_disconnected: Got uncommon disconnection reason:Connection Failed Establishment
17:08:54.725 DebugReceiver: PAIR_RING D1:03:CB:ED:C7:97 ok=false note=配對失敗（BOND_NONE）
```

**Reading**
- `Le connection completed` → the radio layer is fine (RSSI −53, advertising normal, connectable).
- `CID 0x0006 not connected` + `sec_level:0x0` → **the fixed SMP (pairing) channel was never established at all**,
  so this is not a "wrong key/passcode" problem; **the peer disconnected without ever entering the pairing procedure**.
- This is typical behavior for a BLE peripheral that is **already bonded to another host and only recognizes the old host**; it also matches the diagnosis
  in §1.1 of the 2026-09-12 report `reports/ring_control_report.md` (the symptom then was `BOND_BONDING → BOND_NONE` within 1.2 seconds).

**Control group (proving the phone is not broken)** — GATT probes from the same phone in the same minute:
```
17:08:15.949 RingGatt: D1:03:CB:ED:C7:97 連線狀態 status=133 state=0     ← only RGR06 fails
17:08:17.118 RingGatt: 61:74:23:0C:40:71 連線狀態 status=0 state=2 → 服務數=4   ✅
17:08:20.152 RingGatt: 78:F6:D8:15:BB:7A 連線狀態 status=0 state=2 → 服務數=9   ✅
```

**The ring's own advertising is perfectly normal** (so it is not failing to enter pairing mode):
```
裝置 D1:03:CB:ED:C7:97 rssi=-53 name=RGR06 uuids=00001812-… connectable=true
raw=020104 0319C203 03031218 0609524752303 6…
      │      │        │        └ 09 "RGR06" (complete name)
      │      │        └ 03 0x1812 = HOGP (HID over GATT)
      │      └ 19 0x03C2 = Appearance: Mouse
      └ 01 Flags = 0x04 (BR/EDR Not Supported → LE-only)
```

---

## 2. Glasses Side: Fix **Our Own Bug** First, and Then It Paired

### 2.1 The Crash

On the first pairing attempt on the glasses, the app went **straight to FATAL**:

```
E/AndroidRuntime: FATAL EXCEPTION
    at android.app.ReceiverRestrictedContext.registerReceiver(ContextImpl.java:129)
    at com.rokid.rc.shared.ring.RingBonder.bond(RingBonder.kt)
```

**Root cause**: `RingBonder.bond()` calls `registerReceiver` on the `context` it is given. When it is entered from
`DebugReceiver.onReceive()`, that context is a **`ReceiverRestrictedContext`**:
Android 12 (API < 33) **only allows the three-argument version with a flag**, while the code's `SDK_INT < 33` branch takes the
**two-argument version** → `ReceiverCallNotAllowedException` is thrown.
The phone is Android 13 (API 33) and takes the three-argument branch, so it does not crash — which is also why this was previously only seen on the glasses.

### 2.2 The Fix (implemented, rebuilt, installed, validated)

```kotlin
// shared/ring/RingBonder.kt
val regCtx = context.applicationContext ?: context      // ← use the application context
if (Build.VERSION.SDK_INT >= 33) regCtx.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED)
else                             regCtx.registerReceiver(receiver, filter)
// unregister also switched to regCtx
```
In addition, the `try/catch` that the phone side already had was added to the **glasses-side `DebugReceiver.onReceive`** as well (a debug broadcast should not crash the app).

### 2.3 After the Fix: **Pairing Succeeds**

```
17:12:57.762 RingBonder: 發起配對 D1:03:CB:ED:C7:97（RGR06）
17:12:57.798 BOND_STATE_CHANGED: bondState 11            (BONDING)
17:12:58.374 BLE key type 0x01 updated  (LTK)
17:12:58.499 BLE key type 0x02 updated  (IRK)
17:12:58.501 BLE key type 0x10 updated  (CSRK)
17:12:58.501 smp_send_enc_info / smp_send_id_info
17:12:58.556 bt_btm_sec: transport=le, btm_status=0
17:12:58.570 BOND_STATE_CHANGED: bondState 12            (BONDED) ✅
```

**The ring is now a HID device of the glasses**:
```
Bonded devices:  D1:03:CB:ED:C7:97 [ LE ] RGR06
mInputDevices:   D1:03:CB:ED:C7:97 : 2                     ← STATE_CONNECTED
/dev/input/event2: "RGR06 Keyboard"           (full keyboard)
/dev/input/event3: "RGR06 Consumer Control"   (media keys: PLAYPAUSE/NEXTSONG/VOLUME…)
/dev/input/event4: "RGR06"                    (touch surface: BTN_TOUCH/BTN_DIGI + ABS_X/Y 0–4095)
```

**Glasses-side ring control has been switched to it** (`event4` is the touch surface, and `RingEvdevReader.TAP_LIKE` includes `BTN_TOUCH`):
```
RingEvdevReader: getevent -lt /dev/input/event4 >> /data/local/tmp/ring_events.log
RingControlManager: RING_START ok=true device=/dev/input/event4
ring_ctrl prefs: pattern=RGR, device=/dev/input/event4, enabled=true
```

> ⚠ Note: when it is **already running**, `RING_START` only updates the prefs and **does not switch devices** (`start()` sees that `pollJob` is still alive and
> returns immediately). To switch devices you must `RING_STOP` first and then `RING_START` — we only found this because we ran into it here.

---

## 3. What the User Should Do

### A. Recommended course (already done; waiting for you to press the ring once to confirm)
RGR06 is now bonded to the **glasses**, and glasses-side path B (reading evdev directly as root) already points at `event4`.
**Please press the ring's touch surface once**; it should take a photo directly, and I can check the log for you again.

### B. If you insist on bonding it to the **phone**
1. **BLE peripherals usually keep only one bond**: the ring is now bonded to the glasses, so **you must first reset the ring per its manual** (long-press the touch area for 5–10 seconds
   until the LED flashes rapidly); otherwise the phone still will not pair.
2. After resetting, try the phone again, and rule out interference first:
   - Turn the phone's Bluetooth **off and back on** (clearing the stack state) and pair immediately afterwards;
   - Suspend other BLE-related apps (`com.smartring` (AIVELA), Meshtastic, etc.);
   - Move away from other BLE devices and retry (this machine is simultaneously connected to AIVELA/Meshtastic/maz4/yua4,
     and §5.3 of `reports/ring_control_report.md` already documented BLE scheduling interference from the phone's single antenna).
3. If it still fails, **pair from the system settings page** and try once more: if the system page fails the same way, then it is certainly the phone stack / the ring's pairing layer,
   and unrelated to this app.

### C. Not recommended
Do not perform any unofficial operations on the ring just to make the phone pair (for example guessing button combinations) — you bear the risk, and this project
already has a workable glasses-side path.

---

## 4. Confidence

| Statement | Label | Confidence | Basis |
|------|------|------|------|
| The phone-side failure is the **ring aborting SMP**, not an app fault or phone BLE fault | FACT | **Very High** | `smp_reason:SMP_FAIL`, `sec_level:0x0`, `CID 0x0006 not connected`; other BLE devices work fine on the same phone at the same time |
| The ring advertises normally and is in pairing mode | FACT | **Very High** | `RGR06` was scanned, HOGP, connectable, RSSI −53; the raw data was decoded field by field |
| `RingBonder` crashes on Android 12 when called from a receiver context | FACT | **Very High** | `FATAL EXCEPTION … ReceiverRestrictedContext.registerReceiver … RingBonder.bond` |
| After the fix the glasses side paired successfully and it became a HID device | FACT | **Very High** | `bondState 12`, three sets of BLE keys written, `mInputDevices : 2`, three `/dev/input/eventN` |
| Glasses-side ring control already points at `event4` | FACT | **Very High** | the running process line `getevent -lt /dev/input/event4` plus prefs |
| The ring's "single bond" limitation is why the phone cannot pair again before a reset | INFERENCE | **High** | general rule for BLE peripherals; no destructive "try the phone after bonding to the glasses" verification was performed on this device |
| The actual gesture (one tap → photo) has not yet been tested on hardware | Unverified | — | requires physically pressing the ring; cannot be automated |

## 5. Not Performed in This Session
No existing pairing on the phone was removed, no reset or unofficial operation was performed on the ring, and the user's other BLE apps were not closed.
