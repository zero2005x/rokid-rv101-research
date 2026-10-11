# 提權「安全武裝」四道保護 — 實作與實機驗證

> English: [arm_safety_policy_2026-09-13_EN.md](arm_safety_policy_2026-09-13_EN.md)

- **日期**：2026-09-13
- **動機**：`reports/glasses_reboot_after_adb_unplug_2026-09-13.md`
  —— GhostLock（CVE-2026-43499）**每次嘗試約 50% 會 kernel panic，裝置 2–5 分鐘後自動重開機**；
  原本「每次開機自動提權 ＋ 每 5 分鐘無限重試」等於反覆去賭那個機率，可能形成開機迴圈。
- **產出**：**兩版可區分的 APK**（同一份原始碼、一個 build flag）
  | 檔案 | versionCode / versionName | 行為 |
  |------|---------------------------|------|
  | `dist/app-glasses-v1.0-safe.apk` | 3 / **`1.0-safe`** | **四道保護全開**（預設建置） |
  | `dist/app-glasses-v1.0-original.apk` | 1 / `1.0` | 修改前的原始版本（保留作為對照） |
  | `dist/app-phone-v1.1-original.apk` | 2 / `1.1` | 手機端**未變動**（本次改動全在眼鏡端） |

  sha256：`a82be4422012e34f5b18d5b6afcb34528301250b398d3978545817b66bdb755a`（safe）、
  `b9937d665f30e0de8872abfd4b2879d3501f70f3964703124f034014d357c2f0`（original）

---

## 1. 實作內容

### 1.1 build flag（單一來源、兩版可重現）

`android/app-glasses/build.gradle.kts`：

```kotlin
val safeArming: Boolean = (project.findProperty("safeArming") as String?)?.toBoolean() ?: true
...
versionCode = if (safeArming) 3 else 1
versionName = if (safeArming) "1.0-safe" else "1.0"
buildConfigField("boolean", "SAFE_ARMING", "$safeArming")
```

```bash
cd android
./gradlew :app-glasses:assembleDebug                      # → safe 版（預設）
./gradlew :app-glasses:assembleDebug -PsafeArming=false   # → 原行為版
```

因為兩版**同 applicationId**，可直接互相覆蓋安裝；`dumpsys package … | grep versionName` 即可辨識目前裝的是哪一版。

### 1.2 `ghost/ArmPolicy.kt`（新增，四道保護）

| # | 保護 | 實作 |
|---|------|------|
| 1 | **開機迴圈斷路器** | 開機識別 ＝ `currentTimeMillis() - elapsedRealtime()`（＝開機當時的牆鐘時刻，不需權限）。每次偵測到「新開機」時，若 `bootWall - lastArmWall ∈ 0..10 分鐘` ⇒ 記一次**可疑重開**；連續 **2** 次 ⇒ `arm_suspended=true`（reason `reboot-loop`），**停止自動武裝**直到手動重置。 |
| 2 | **每次開機重試上限 ＋ 指數退避** | `MAX_ARMS_PER_BOOT = 2`；退避 5 → 15 → 60 分鐘（`BACKOFF_MS`）。每次開機自動歸零。 |
| 3 | **僅充電時自動武裝** | `BatteryManager`（讀 sticky `ACTION_BATTERY_CHANGED` 的 `EXTRA_PLUGGED`，免權限）；吃電池時 `blockReason = "on-battery"`。 |
| 4 | **開機後延遲** | `uptime < 150 秒` 前不動作（`BOOT_SETTLE_MS`，採專案自身建議值）。 |

`blockReason()` 回傳 null＝允許，否則回原因代碼：`breaker-open`／`boot-settle`／
`max-arms-this-boot`／`on-battery`／`backoff-5m|15m|60m`。

### 1.3 接線（兩個自動路徑都受約束）

| 位置 | 改動 |
|------|------|
| `CaptureService`（AUTO 啟動時提權） | 先問 `ArmPolicy.blockReason()`；被擋就只記日誌、**不呼叫 `acquireRootMetered()`**；允許則先 `onAutoArmAttempt()` 記帳 |
| `RootDaemonKeeper.check()`（root 失效自動重建） | 同上；被擋時狀態設 `LOST_COOLDOWN`、`detail = "safe-arm(<原因>)"` |
| `AppGraph` | `app` 由 `private val` 改 `val`（政策需要 Context 讀充電狀態） |
| 眼鏡 `DebugReceiver` | 新增 `ARM_POLICY_STATUS`（查詢）／`ARM_POLICY_RESET`（重置斷路器與計數） |

> ⚠ **手動提權不受影響**：使用者在 App 按「取得 root」或由 `deploy/root_bridge.sh` 觸發時仍會執行
> —— 那是有意識的決定。本政策只約束**自動**武裝。
> `SAFE_ARMING=false` 時 `ArmPolicy` 全部放行，行為與舊版完全一致。

---

## 2. 實機驗證（每一道保護都單獨驗過，且**全程沒有真的跑 exploit**）

驗證手法：以 `run-as`（debug build）改寫 prefs 後**冷啟動** App，讓 `ArmPolicy` 在第一次
`ARM_POLICY_STATUS` 時刷新開機狀態並做出決策；再從 logcat 讀實際決策。
斷路器測試用「模擬上次武裝後 60 秒就重開」的 prefs 植入（`build-cache/sim_boot.sh`）。

| 測試 | 植入條件 | 實際輸出 | 結果 |
|------|----------|----------|------|
| 政策上線 | — | `ARM_POLICY safeArming=on up=773s charging=true arms=0/2 suspicious=0 suspended=false` | ✅ |
| **斷路器 1/2** | `arm_last_wall_ms = 本次開機 − 60s`、`arm_boot_wall_ms = 0` | `W ArmPolicy: 可疑重開：上次自動武裝後 60s 就重開（累計 1/2）` | ✅ |
| **斷路器 2/2** | 同上再一次 | `E ArmPolicy: ⛔ 開機迴圈斷路器跳脫 …` → `suspended=true reason=reboot-loop` | ✅ |
| **端到端阻擋** | `root_mode=AUTO` ＋ 斷路器已跳脫 → 啟動 App | `CaptureService: AUTO 提權暫停：breaker-open（… suspended=true reason=reboot-loop）`；logcat 中 `ghostlock_rv101/.ghostlock_root.sh` 出現 **0** 次、`ps` 無殘留程序 | ✅ **確實沒有跑 exploit** |
| **重試上限** | `root_mode=AUTO`、`arm_arms_this_boot=2` | `AUTO 提權暫停：max-arms-this-boot（arms=2/2）` | ✅ |
| **指數退避** | `root_mode=AUTO`、`arms=1`、`arm_last_wall_ms = now−30s` | `AUTO 提權暫停：backoff-5m` | ✅ |
| 重置 | `ARM_POLICY_RESET` | `suspended=false`、計數歸零、`reason` 不再顯示 | ✅ |

**未直接驗證的兩道**（需要環境配合，程式路徑與上面相同）：
- `boot-settle`：需在開機後 150 秒內啟動 App。
- `on-battery`：需在**拔掉 USB**（純電池）時啟動 App。← **這一項剛好是你回報的情境，建議由你在拔線狀態下確認一次**
  （做法：拔線 → 開 App → `adb` 不可用時可看手機端「眼鏡端」狀態，或之後插線抓
  `logcat -s CaptureService:* | grep 暫停` 應看到 `on-battery`）。

---

## 3. 目前裝置狀態（驗證後刻意留下的安全狀態）

```
$ adb -s <DEVICE_SERIAL> shell dumpsys package com.rokid.rc.glasses | grep versionName
    versionName=1.0-safe
$ ARM_POLICY_STATUS
    ARM_POLICY safeArming=on up=1050s charging=true arms=0/2 suspicious=0 suspended=false
$ prefs root_mode = MANUAL
```

- **已安裝 safe 版**（`1.0-safe`）。
- **`root_mode` 設為 `MANUAL`**：不主動跑 exploit，所以在你決定之前不會再有「為了提權而重開機」。
- 斷路器與計數**已重置**（乾淨狀態，等同全新安裝）。
- 要回到自動提權：手機 App「設定 → 拍攝與權限 → root 模式」改 AUTO，或用
  `am broadcast … --es cmd ARM_POLICY_RESET`（若斷路器曾跳脫）。

---

## 4. 已知限制

1. **App 在重開機後仍不會自動啟動**（眼鏡系統擋掉第三方 `BOOT_COMPLETED`，見
   `reports/glasses_reboot_after_adb_unplug_2026-09-13.md` §1.6）。本政策只讓「不會因為自動重試而
   反覆重開機」，**沒有**解決「重開機後要重新插線武裝」。這需要另一條復活途徑（尚未找到可靠的）。
2. **無法降低單次嘗試的 panic 機率**（~50% 是本質風險；P3 已評估放棄改寫 exploit）。
3. `root_mode=AUTO` ＋ 插著 USB ＋ 開機逾 150 秒 ＋ 未達重試上限時，**仍會嘗試一次**（這是設計：
   有界且可預期）；若要完全零風險，請維持 `MANUAL`。

## 5. 檔案

| 檔案 | 說明 |
|------|------|
| `android/app-glasses/src/main/kotlin/com/rokid/rc/glasses/ghost/ArmPolicy.kt` | 新增：四道保護 |
| `android/app-glasses/build.gradle.kts` | `safeArming` flag／`SAFE_ARMING`／版本字串 |
| `CaptureService.kt`、`ghost/RootDaemonKeeper.kt` | 兩個自動路徑接上政策 |
| `DebugReceiver.kt`（眼鏡） | `ARM_POLICY_STATUS`／`ARM_POLICY_RESET` |
| `build-cache/sim_boot.sh`、`build-cache/seed_prefs.sh` | 驗證用（模擬重開／植入 prefs） |
| `dist/app-glasses-v1.0-safe.apk`、`dist/app-glasses-v1.0-original.apk` | 兩版 APK |
