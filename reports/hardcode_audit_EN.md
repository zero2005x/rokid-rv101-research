# Hard-Coding Audit Report (whole project)

> 中文版：[hardcode_audit.md](hardcode_audit.md)

- Date: 2026-09-12
- Tool: `python3 scripts/audit_hardcode.py` (re-runnable; it outputs five sections: Chinese literals / paths / layouts / duplicated literals / scripts)
- Scope: `android/app-phone`, `android/app-glasses`, `android/shared`, `deploy/`, `scripts/`, layout XML

## 1. Summary of conclusions

| Category | Before the fix | After the fix |
|------|--------|--------|
| Hard-coded layout **colors** | 3 occurrences (`#CCFFFFFF`×2, `#591A73E8`) | **0 occurrences** (changed to `@color/on_scrim`, `@color/accent_soft`) |
| Hard-coded layout **text** | 3 occurrences | 3 occurrences (all decorative images with `contentDescription="@null"`, **deliberately retained**) |
| Scattered glasses-side **device paths** | 15+ places hard-written across 8 files | **all centralized** into `GlassesPaths.kt` (`/data/local/tmp` and `/sys/class/leds` each have only 1 definition left) |
| Phone-side **user-visible Chinese strings** | 31 occurrences (`RingController` status / Toast) + `Remote.kt` exception message | **all moved into resources** (22 new strings × 14 locales); only logcat literals remain |
| **Script** hard-coded serial numbers | 11 scripts each hard-coded `<DEVICE_SERIAL>` | centralized into `deploy/env.sh` (overridable with `SERIAL`; when no value is given it auto-detects the first adb device) |
| **Script** hard-coded adb / NDK paths | 12 scripts hard-coded `/mnt/c/Users/<USER>/...` | all changed to overridable `${VAR:-default}`; defaults remain only in `deploy/env.sh` and the two NDK build scripts |
| Other machine paths (`Documents/codebase/rokid_root`) | 3 scripts | changed to repo-relative paths (`REPO=$(cd "$(dirname "$0")/.." && pwd)`) |

## 2. Fix details

### 2.1 Phone side: ring control strings moved into resources (largest impact)
`RingController`'s status row and Toasts were originally hard-coded Chinese (for example `"已拍照 ${m.fileName}"`, `"拍照失敗：${e.message}"`),
so they did not follow the translation across 14 locales. 22 keys were added (`ring_state_enabled` / `ring_photo_ok` / `ring_video_saved` /
`ring_silent_on` / `ring_display_switched` / `ring_err_prefix`…), and strings are fetched via an `s(resId, …)` helper.

- `Remote.kt`: `"眼鏡端回報失敗"` in `requireOk()` → `R.string.err_unknown`
- `SettingsFragment`: `"ON"/"OFF"` in the ring enumeration / toggle messages → `R.string.ring_on/ring_off`
- On-device validation: `TAP → 已拍照 IMG_1789176096288.jpg`, `SCROLL_UP → 顯示設定失敗：not connected`
  (the strings come from `ring_photo_ok` / `ring_display_fail` respectively)

### 2.2 Glasses side: centralizing device paths
Added `app-glasses/.../GlassesPaths.kt` to centralize the previously scattered paths:

| Constant | Value |
|------|-----|
| `TMP` / `ROOT_DAEMON_SOCK` / `ROOT_DAEMON_BIN` | `/data/local/tmp`, `…/root_daemon.sock`, `…/root_daemon` |
| `GHOSTLOCK_BIN` / `GHOSTLOCK_ROOT_SCRIPT` / `BRIDGE_DIR` / `RUN_SHELL` | ghostlock LPE related |
| `SILENT_OGG` | the ogg used for silent capture |
| `RING_EVENTS_LOG` / `RING_READER_PID` / `RING_KEEPALIVE_SH` / `RING_KEEPALIVE_PID` | ring root direct read |
| `LED_BASE` / `ledBrightness(color)` / `LED_WHITE_CURRENT` | privacy indicator LED sysfs |

Updated in sync: `GhostlockEngine`, `RootDaemonClient`, `LedPayloads`, `LedSuppressor`, `ShizukuGhostlockEngine`,
`RingEvdevReader`, `RingControlManager`, `SilentPayloads` (a generated file, with the `scripts/gen_silent_payload.py` template synced as well).

### 2.3 Layout colors
`#CCFFFFFF` in `activity_viewer.xml` and `item_album_card.xml` → `@color/on_scrim`;
`#591A73E8` in `item_media_grid.xml` → `@color/accent_soft` (added to `colors.xml`).

### 2.4 Scripts
- Added `deploy/env.sh`: the single definition of `WIN_ADB_DIR` / `ADB` / `SERIAL` (when `SERIAL` is not given it automatically takes the first device from `adb devices`),
  and it documents the `source "$(dirname "$0")/env.sh"` usage.
- 11 `deploy/*.sh` and 3 `scripts/*.sh` now source it, and the hard-coded serial numbers and adb paths were removed;
  `NDK_BIN` / `WIN_DIR` in the NDK build scripts became `${VAR:-default}`.
- `scripts/Collect-RV101-Validation.ps1`: `$Serial` is now supplied by a parameter or the `SERIAL` environment variable.
- All scripts were syntax-checked with `bash -n`.

## 3. Deliberately retained (not bad hard-coding)

| Item | Reason |
|------|------|
| Native-language names in `LocaleHelper.LANGUAGES` (`繁體中文` / `日本語` / `العربية`…) | the language menu must be displayed in each language itself |
| `DebugReceiver`'s logcat and `TEST_LOG` test messages | developer tool messages, they never reach the user interface |
| The glasses-side ghostlock / ring / silent **logcat** messages | for adb debugging only |
| `AP_IP` / `WIFI_PORT` / `AP_SSID` / `AP_PASS` in `Protocol.kt` | **protocol constants**; being centralized in the shared module is the correct place |
| `/proc/uptime`, `127.0.0.1` | fixed platform values |
| The 3 `contentDescription="@null"` occurrences in layouts | decorative images; accessibility does not need to read them |
| The WSL default paths in `deploy/env.sh` and the two NDK scripts | already defaults that "can be overridden by an environment variable", centralized in one place |

## 4. Outstanding (**completed in the second and third rounds; see §7, §10–12**)

**Text pushed from the glasses side to the phone display was still hard-coded Chinese** (about 72 entries):

| Source | Content | Why it counts as bad |
|------|------|-----------|
| ~~`app-glasses` `RingControlManager.lastEvent`~~ (changed to `lastKey`/`lastArg`) | e.g. `已啟動（/dev/input/event1）`, `TAP → 已拍照 IMG_…jpg` | shown via `STATUS.ringLast` on the **phone's** ring card (visible on device) |
| `onProgress` of the three Ghostlock engines in `app-glasses` | e.g. `已偵測到常駐 root daemon，跳過 LPE`, `等待 ready 標記` | pushed via `PROGRESS` to the **phone's** operation log panel |

Two ways to fix it:

- **A (faster)**: put these glasses-side strings into the glasses App's existing 14-locale resources.
  Drawback: the phone then displays the **glasses' locale** (if the phone is set to English and the glasses to Traditional Chinese, locales get mixed).
- **B (more correct)**: change them into **structured events** (for example `ringLast = {"g":"TAP","a":"PHOTO","ok":true,"file":"IMG_…jpg"}`),
  with the phone composing the text from its own resources. Drawback: needs protocol changes + both ends, more work.

> Not touched yet; left until a decision is made. The audit tool can be re-run at any time to confirm.

## 5. How to re-run

```bash
python3 scripts/audit_hardcode.py            # everything
python3 scripts/audit_hardcode.py --only 2   # only paths/IPs/MACs
python3 scripts/audit_hardcode.py --only 5   # only scripts
```

---

# Second round: hard-coding re-review **inside the App** (2026-09-12 afternoon)

Scope expanded: besides Chinese strings, it also scans "literals written directly into the UI, unit/format strings, dp arithmetic, package names,
prefs names, timeout magic numbers, file extensions/MIME, and key-code magic values".

## 6. Fixes this round (all **cross-file consistency risks**)

| Item | Problem | Fix |
|------|------|------|
| **Bluetooth SPP UUID** | the phone's `BluetoothClient` and the glasses' `BluetoothServer` **each hard-coded their own copy** of `00001101-…`; changing one side silently breaks the connection | centralized as `Protocol.SPP_UUID`, and both ends now reference it |
| **MIME / file extensions** | `"image/jpeg"` / `"video/mp4"` / `".mp4"` / `".mov"` / `".part"` scattered across **7 places** in the two Apps, and the "is it a video" / "is it a partial file" logic was implemented twice | added `shared/protocol/MediaTypes` (constants + `isVideo()` / `isPartial()` / `mimeOf()`), and everything now calls it |
| **WiFi port** | the phone's `DebugReceiver` hard-coded `8555` | switched to `Protocol.WIFI_PORT` |
| **prefs name** | `"ring_ctrl"` was written once in each of two glasses-side files | centralized as `RingPrefs.NAME` (with `KEY_READER_OFFSET` / `KEY_PATTERN` as well) |
| **Mouse key-code offset** | `0x1000 + button` appeared as a magic number in 2 places | changed to `RingController.MOUSE_BUTTON_BASE` |

## 7. Structured events: the glasses side no longer sends "Chinese sentences" to the phone (the biggest fix this round)

**Problem**: the glasses-side `RingControlManager` composed its result into Chinese sentences (`已拍照 IMG_…jpg`, `顯示切換為 連線+指示燈`)
and sent them to the phone via `STATUS.ringLast` for display → with the phone set to English the **locales were mixed**, and the glasses side could not localize either (it has only zh-TW resources).

**Fix (structured events)**:

| Layer | Change |
|----|------|
| Glasses `RingControlManager` | actions return `RingOutcome(key, arg)` (such as `photo_ok` + the filename); display configuration parameters became machine-readable flags `conn,led`; the Chinese result strings were **all removed** (logcat switched to English) |
| `shared` `Status` | `ringLast` → `ringLastKey` + `ringLastArg` |
| Phone | added `ui/RingText.kt` lookup table: code → phone-locale string (reusing the existing `ring_*` resources plus 5 new keys) |
| Debug | the phone's `DebugReceiver` gained `TEST_RING_TEXT`, which lists the text of every code in the current locale |

**On-device validation** (same set of codes, phone language switched):

| Code | zh-TW | en |
|------|-------|-----|
| `photo_ok` | 已拍照 IMG_1.jpg | Photo captured: IMG_1.jpg |
| `silent_on_pending` | 靜音開啟（待 root） | Silent capture on (waiting for root) |
| `display_switched` (arg `conn,led`) | 顯示切換為 連線+指示燈 | Display switched to Connection+Indicator |
| `running` | （隱藏；狀態列已呈現執行中／裝置） | (hidden) |

The glasses-side `RING_STATUS` was likewise confirmed to send only structured fields: `enabled=true running=true device=/dev/input/event1 last=running//dev/input/event1`.

## 8. Not changed this round (with reasons)

| Item | Current state | Reason |
|------|------|------|
| ~~Chinese `onProgress` in the three Ghostlock engines (**49 entries**)~~ | **structured in the third round** (`ProgressEvent` / `GhostlockCodes` / phone `GhostlockText`), see §10 | — |
| Phone `DebugReceiver` (10), `RingController` logcat (10), `RingMediaSession` (3) | Chinese literals | **logcat debug messages**, they never reach the user interface |
| Native-language names in the phone's `LocaleHelper` (3) | Chinese literals | the language menu must be displayed in each language itself (`繁體中文` / `日本語` / `العربية`) |
| `shared/ring` scanners (RingBonder 14 / RingGatt 14 / RingWatcher 8) | Chinese literals | all are `Log.*` / exception messages (manually confirmed to have no UI outlet) |
| Timeout and size constants (6000ms, 320px, 512px…) | inline values | mostly single-use with clear semantics; the genuinely duplicated ones (port, UUID, MIME, prefs) have been centralized |

## 9. Scan results (inside the App)

| Check | Result |
|--------|------|
| Literals written directly into the UI such as `setText/Toast/contentDescription` | **0 occurrences** |
| Unit / format strings (B/KB/MB, time) | **0 occurrences** (all go through resources / `Format`) |
| In-code dp/sp arithmetic and `applyDimension` | **0 occurrences** |
| Hard-coded package / component names | 1 occurrence (the glasses `DebugReceiver` action string, a developer tool) |
| Scattered file extensions / MIME | 7 places before the fix → **0** (centralized in `MediaTypes`) |
| Protocol constants duplicated on both ends (UUID) | 2 copies before the fix → **1** (`Protocol.SPP_UUID`) |

---

# Third round: structuring the Ghostlock progress messages (2026-09-12 evening)

The **49 Chinese ghostlock progress/failure messages** that the second round listed as "not changed" were all structured this round.

## 10. Approach

| Layer | Change |
|----|------|
| `shared` | added `Progress.kt`: `ProgressEvent(code, arg)`, `GhostlockCodes` (37 codes), `ProgressCodec` (`code\|arg` encode/decode, used for ack failure messages) |
| Glasses `GhostlockEngine` | the `onProgress` type became `(ProgressEvent) -> Unit`; added `progress(code, arg)`; **all** progress and failure messages now send codes (`ExecResult.fail(stage, ProgressCodec.encode(code, arg))`); all Chinese result strings inside the engine were removed |
| Glasses `AppGraph.pushProgress` | sends `{code, arg, message}` (`message` is kept as `code\|arg` for logcat convenience and backward compatibility) |
| Phone | added `ui/GhostlockText.kt` (37 codes → phone-locale strings); `RemoteFragment` renders progress from codes, and `Remote.requireOk()`'s error message is localized after being decoded by `ProgressCodec` |
| Strings | added 37 `ghost_*` keys × 14 locales (a running total of **180 keys**; later P0-b/P2 added 13 more, **now 193 keys**) |
| Debug | the phone's `DebugReceiver` gained `TEST_PROGRESS`, which lists the text of every code in the current locale |

## 11. Verification

- **Comparison table**: `TEST_PROGRESS` was run once each in zh-TW and en, and all 37 codes have a mapping (no `(unmapped)`):
  `gl_skip_lpe → 已偵測到常駐 root daemon，跳過 LPE` / `Persistent root daemon detected; skipping LPE`;
  `gl_kernel_panic → kernel panic：裝置已重啟（正常失敗，需重試）` / `Kernel panic: device rebooted (expected failure, retry needed)`;
  `gl_display…` (ring) and so on likewise.
- **End to end**: tapping 「立即取得 root」 on the phone triggered the real privilege-escalation flow, and the phone's operation log panel showed
  `[進度] 已偵測到常駐 root daemon，跳過 LPE` — confirming the complete chain
  glasses `progress(gl_skip_lpe)` → `ProgressEvent` → `pushProgress` → phone `GhostlockText` → log panel.
- `assembleDebug` + `lintDebug` green on both ends; i18n verification passed (180 keys at the time; **now 193 keys**).
- Exact code-set numbers: `GhostlockCodes` defines **42** codes and the phone's `GhostlockText` maps **38** of them;
  the 4 unmapped ones are `ENGINE_SHIZUKU_RUNNING` / `ENGINE_BRIDGE_RESIDENT` / `ENGINE_NONE` / `ENGINE_RETRY_OTHER`
  (**diagnostic use**: written only to logcat and `lastReasonCode`, never to the user interface; only `ENGINE_SELECTED` is mapped).

## 12. Current-state summary (after three rounds)

| Category | Status |
|------|------|
| **All** display text from glasses → phone (ring results, ghostlock progress/failures) | **structured** (only codes + arguments are sent; the phone composes text in its own locale) |
| Chinese remnants in the glasses-side `ghost/` | logcat messages only (`SilentMode` 3 entries; the other engines no longer have Chinese result strings) |
| Chinese remnants on the phone side | logcat / developer tools (`DebugReceiver`), native language names (deliberate) |
| Cross-file duplicated constants | SPP UUID / MIME / prefs / port / key-code offset all centralized |
| Script ADB/SERIAL | **18/18 `deploy/*.sh` all `source deploy/env.sh`** (the "11 + 3" written in §2.4 of this document was the figure at the time) |

---

## 13. Addendum (2026-09-13 evening): the audit tool's blind spots and reinforcements

§5 of this document classified `DebugReceiver`'s action strings as a "developer tool" and **did not assess their reachability**,
and §12 also listed "phone-side Chinese remnants = developer tools" as acceptable. The ring audit
(`reports/ring_capture_security_audit_2026-09-13.md`) found that this classification missed the real problem:
**component reachability is not a "text" problem but a "who can trigger it" problem**.

### 13.1 Three new sections in `scripts/audit_hardcode.py`

| Section | Check | Why it was missed before |
|----|------|---------------|
| 6 | components with `android:exported="true"` and **no** `android:permission` (the launcher activity and receivers protected only by system broadcasts are marked "expected" to reduce noise) | the old version scanned only strings / paths / layouts |
| 7 | credential-like literals (`pass` / `password` / `token` / `secret` / `pin`; names ending in `key` only count if the value looks like a key) | the old version had no credential perspective |
| 8 | world-accessible `chmod` (mode containing other bits) | the old version **scanned only Kotlin** and never looked at native code |

### 13.2 Two items caught immediately after the addition (none of the previous three rounds saw them)

1. **`deploy/root_daemon.c:132` `chmod(..., 0777)`** → on the glasses, `/data/local/tmp/root_daemon.sock`
   is `srwxrwxrwx root root` on device, and `getenforce` = **Permissive** →
   **any installed App can connect and obtain a root shell** (an interface this project introduced itself).
2. **`scripts/rokid_ota_check.py:25-28` hard-codes OTA API credentials** (`KEY` / `SECRET` / `DEVICE_TYPE_ID`;
   two of them come from the bootloader properties `ro.boot.secret` / `ro.boot.devicetypeid`).

Both have been recorded in `reports/FACTS.md` §7 (P12 / P13) and added to the pending-fix list; **the code has not been changed yet**.

### 13.3 Zero-risk cleanups already applied

| Item | File | Verification |
|------|------|------|
| `"RGR"` centralized into a single constant (11 references rewritten) | added `shared/ring/RingIdentity.kt`; `RingBonder` / `RingWatcher` / `RingEvdevReader` / `RingControlManager` / `CommandRouter` / both `DebugReceiver`s / `SettingsFragment` | `./gradlew :app-glasses:compileDebugKotlin :app-phone:compileDebugKotlin --offline` → **BUILD SUCCESSFUL** |
| `chmod 0777 → 0600` (the unused backup daemon) | `deploy/ring_input_daemon.c` | zig cross-compile passed (aarch64 ELF) |
| Audit tool sections 6–8 | `scripts/audit_hardcode.py` | a live run immediately caught the two items above |

> One sentence in the §12 conclusion therefore needs correcting: **"phone-side Chinese remnants = developer tools" is right, but "developer tool" does not equal "low risk"** —
> `DebugReceiver` is an exported component with no permission, and it has been empirically triggered by an ordinary App (uid 10346 / 10064).
