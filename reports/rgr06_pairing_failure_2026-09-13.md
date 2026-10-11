# RGR06 戒指配對失敗 — log 取證、我們程式的真 bug、以及可行的解法

- **日期**：2026-09-13 17:05–17:14
- **戒指**：`RGR06` ＝ `D1:03:CB:ED:C7:97`（LE-only、HOGP `0x1812`、Appearance `0x03C2` Mouse、random static address）
- **主機**：手機 `<PHONE_SERIAL>`（Android 13 / MIUI V816）· 眼鏡 `<DEVICE_SERIAL>`（Android 12 / API 32）
- **方法**：兩端 logcat（含 BT 堆疊 `bt_btm_sec` / `bt_smp` / `btif_dm`）、`dumpsys bluetooth_manager`、
  `getevent -pl`、App 內建除錯廣播。**未**變更手機的任何既有配對。

---

## 0. 結論（三句話）

1. **手機配不上不是我們 App 的問題**：戒指每次都**接受 LE 連線、但在 SMP 配對交換時中止**
   （`smp_reason:SMP_FAIL`、`sec_level:0x0`、SMP 固定通道 `CID 0x0006` 從未建立）→ 手機清掉金鑰後放棄。
   同一時間同一支手機連**其他兩台** BLE 裝置都正常，所以不是手機的 BLE 壞了，是**這顆戒指拒絕該主機**。
2. **但過程中抓到我們程式的一個真 bug**：`RingBonder.bond()` 在 **Android 12** 被 `BroadcastReceiver`
   呼叫時會丟 `ReceiverCallNotAllowedException` **讓 App 直接崩潰**（眼鏡端必現；手機 Android 13 不會）。
3. **眼鏡端修好後，RGR06 立刻配對成功**，現在已是眼鏡的 **HID 輸入裝置**，眼鏡端戒指控制也已切到它
   → 也就是說**用眼鏡當主機是可行解**（而且路徑 B 本來就是背景可用的那條路）。

---

## 1. 手機端：堆疊層級的失敗原因

三次嘗試（UI 掃描清單、`PAIR_RING --es mac`、再一次）行為完全一致：

```
17:08:53.486 RingBonder: 發起配對 D1:03:CB:ED:C7:97（RGR06）
17:08:53.492 bt_btm_sec: BTM_SecBond: Forcing transport LE (was auto) because of the address type
17:08:53.492 bt_btm_sec: btm_sec_change_pairing_state: Pairing state changed IDLE => WAIT_AUTH_COMPLETE pairing_flags:0x81
17:08:54.369 bluetooth : on_connection_complete: Le connection completed to device:d1:03:cb:ed:c7:97   ← ACL 連上了
17:08:54.691 bluetooth : smp_proc_pairing_cmpl: Pairing process has failed to remote:xx:xx:xx:xx:c7:97
                         smp_reason:SMP_FAIL sec_level:0x0                                            ← 322 ms 後 SMP 失敗
17:08:54.691 bt_stack  : [WARNING:l2c_api.cc] L2CA_RemoveFixedChnl BDA: d1:03:cb:ed:c7:97 CID: 0x0006 not connected
17:08:54.691 bt_btif_storage: btif_storage_remove_ble_bonding_keys: Removing bonding keys for bd addr:d1:03:cb:ed:c7:97
17:08:54.692 bt_btm_sec: btm_sec_disconnected: Got uncommon disconnection reason:Connection Failed Establishment
17:08:54.725 DebugReceiver: PAIR_RING D1:03:CB:ED:C7:97 ok=false note=配對失敗（BOND_NONE）
```

**判讀**
- `Le connection completed` → 射頻層沒問題（RSSI −53，廣告正常、可連線）。
- `CID 0x0006 not connected` ＋ `sec_level:0x0` → **SMP（配對）固定通道根本沒建立**，
  所以不是「金鑰/密碼錯」，而是**對方沒有進入配對流程就斷線**。
- 這是 BLE 周邊「**已經綁定其他主機、只認舊主機**」的典型行為；也與 2026-09-12 報告
  `reports/ring_control_report.md` §1.1 的診斷一致（當時症狀為 `BOND_BONDING → BOND_NONE` 1.2 秒）。

**對照組（證明不是手機壞了）**——同一分鐘、同一支手機的 GATT 探測：
```
17:08:15.949 RingGatt: D1:03:CB:ED:C7:97 連線狀態 status=133 state=0     ← 只有 RGR06 失敗
17:08:17.118 RingGatt: 61:74:23:0C:40:71 連線狀態 status=0 state=2 → 服務數=4   ✅
17:08:20.152 RingGatt: 78:F6:D8:15:BB:7A 連線狀態 status=0 state=2 → 服務數=9   ✅
```

**戒指本身的廣告完全正常**（所以不是沒進配對模式）：
```
裝置 D1:03:CB:ED:C7:97 rssi=-53 name=RGR06 uuids=00001812-… connectable=true
raw=020104 0319C203 03031218 0609524752303 6…
      │      │        │        └ 09 "RGR06"（完整名稱）
      │      │        └ 03 0x1812 = HOGP（HID over GATT）
      │      └ 19 0x03C2 = Appearance: Mouse
      └ 01 Flags = 0x04（BR/EDR Not Supported → LE-only）
```

---

## 2. 眼鏡端：先修掉**我們自己的 bug**，然後就配上了

### 2.1 崩潰

第一次在眼鏡上嘗試配對，App **直接 FATAL**：

```
E/AndroidRuntime: FATAL EXCEPTION
    at android.app.ReceiverRestrictedContext.registerReceiver(ContextImpl.java:129)
    at com.rokid.rc.shared.ring.RingBonder.bond(RingBonder.kt)
```

**根因**：`RingBonder.bond()` 用傳入的 `context` 呼叫 `registerReceiver`。當它是從
`DebugReceiver.onReceive()` 進來時，那個 context 是 **`ReceiverRestrictedContext`**：
Android 12（API < 33）**只允許帶 flag 的三參數版本**，而程式碼在 `SDK_INT < 33` 分支走的是
**兩參數版本** → 丟 `ReceiverCallNotAllowedException`。
手機是 Android 13（API 33），走三參數分支所以不會崩 —— 這也是為何先前只在眼鏡上看到。

### 2.2 修正（已實作、已重編、已安裝、已驗證）

```kotlin
// shared/ring/RingBonder.kt
val regCtx = context.applicationContext ?: context      // ← 用 application context
if (Build.VERSION.SDK_INT >= 33) regCtx.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED)
else                             regCtx.registerReceiver(receiver, filter)
// unregister 也改用 regCtx
```
另外把手機端早就有的 `try/catch` 也補到**眼鏡端 `DebugReceiver.onReceive`**（除錯廣播不該讓 App 崩潰）。

### 2.3 修正後：**配對成功**

```
17:12:57.762 RingBonder: 發起配對 D1:03:CB:ED:C7:97（RGR06）
17:12:57.798 BOND_STATE_CHANGED: bondState 11            （BONDING）
17:12:58.374 BLE key type 0x01 updated  (LTK)
17:12:58.499 BLE key type 0x02 updated  (IRK)
17:12:58.501 BLE key type 0x10 updated  (CSRK)
17:12:58.501 smp_send_enc_info / smp_send_id_info
17:12:58.556 bt_btm_sec: transport=le, btm_status=0
17:12:58.570 BOND_STATE_CHANGED: bondState 12            （BONDED）✅
```

**戒指已成為眼鏡的 HID 裝置**：
```
Bonded devices:  D1:03:CB:ED:C7:97 [ LE ] RGR06
mInputDevices:   D1:03:CB:ED:C7:97 : 2                     ← STATE_CONNECTED
/dev/input/event2: "RGR06 Keyboard"           （完整鍵盤）
/dev/input/event3: "RGR06 Consumer Control"   （媒體鍵：PLAYPAUSE/NEXTSONG/VOLUME…）
/dev/input/event4: "RGR06"                    （觸控面：BTN_TOUCH/BTN_DIGI + ABS_X/Y 0–4095）
```

**已把眼鏡端戒指控制切到它**（`event4` 是觸控面，`RingEvdevReader.TAP_LIKE` 含 `BTN_TOUCH`）：
```
RingEvdevReader: getevent -lt /dev/input/event4 >> /data/local/tmp/ring_events.log
RingControlManager: RING_START ok=true device=/dev/input/event4
ring_ctrl prefs: pattern=RGR, device=/dev/input/event4, enabled=true
```

> ⚠ 注意：`RING_START` 在**已執行中**時只會更新 prefs、**不會換裝置**（`start()` 見到 `pollJob` 還活著就
> 直接 return）。換裝置要先 `RING_STOP` 再 `RING_START`——本次就是踩到這點才發現。

---

## 3. 給使用者的處置

### A. 建議走法（已完成，待你按一下戒指確認）
RGR06 現在綁在**眼鏡**上，眼鏡端路徑 B（root 直讀 evdev）已指向 `event4`。
**請按一下戒指的觸控面**，應該會直接拍照；我可以再幫你確認 log。

### B. 若你堅持要綁在**手機**上
1. **BLE 周邊通常只保留一份綁定**：戒指現在已綁眼鏡，**要先依說明書重置戒指**（長按觸控區 5–10 秒
   直到 LED 快閃），否則手機仍然配不上。
2. 重置後再試手機，並先排除干擾：
   - 手機藍牙**關閉再開啟**（清掉堆疊狀態）後立刻配對；
   - 暫停其他 BLE 相關 App（`com.smartring`（AIVELA）、Meshtastic 等）；
   - 遠離其他 BLE 裝置後重試（本機同時連著 AIVELA／Meshtastic／maz4／yua4，
     `reports/ring_control_report.md` §5.3 已記錄過手機單天線的 BLE 排程干擾）。
3. 若仍失敗，**從系統設定頁配對**再試一次：若系統頁也一樣失敗，即可確定是手機堆疊／戒指配對層，
   與本 App 無關。

### C. 不建議
不要為了讓手機配得上而對戒指做任何非官方操作（例如猜測的按鍵組合）——風險由你承擔，且本專案
已有眼鏡端可行路徑。

---

## 4. 信心

| 陳述 | 標記 | 信心 | 依據 |
|------|------|------|------|
| 手機端失敗是**戒指中止 SMP**，非 App／非手機 BLE 故障 | FACT | **Very High** | `smp_reason:SMP_FAIL`、`sec_level:0x0`、`CID 0x0006 not connected`；同機同時間其他 BLE 裝置正常 |
| 戒指廣告正常且在配對模式 | FACT | **Very High** | 掃描到 `RGR06`、HOGP、connectable、RSSI −53，raw 已逐欄解碼 |
| `RingBonder` 在 Android 12 從 receiver context 會崩 | FACT | **Very High** | `FATAL EXCEPTION … ReceiverRestrictedContext.registerReceiver … RingBonder.bond` |
| 修正後眼鏡端配對成功且成為 HID 裝置 | FACT | **Very High** | `bondState 12`、三組 BLE 金鑰寫入、`mInputDevices : 2`、三個 `/dev/input/eventN` |
| 眼鏡端戒指控制已指向 `event4` | FACT | **Very High** | 行程列 `getevent -lt /dev/input/event4` ＋ prefs |
| 戒指的「單一綁定」限制導致手機在重置前無法再配 | INFERENCE | **High** | BLE 周邊通則；本機未做「綁眼鏡後再試手機」的破壞性驗證 |
| 實際手勢（按一下→拍照）尚未實測 | 未驗證 | — | 需實體按壓戒指，無法自動化 |

## 5. 本次未執行
未解除手機上任何既有配對、未對戒指做任何重置或非官方操作、未關閉使用者的其他 BLE App。
