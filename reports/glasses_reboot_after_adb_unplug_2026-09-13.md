# 眼鏡「拔掉 adb 後過沒多久就重開機」— 原因分析與可修正範圍

- **日期**：2026-09-13
- **裝置**：Rokid RV101 `<DEVICE_SERIAL>`（Android 12 / API 32，kernel `5.10.209-perf`，韌體 `1.22.009`）
- **現象**：與 PC／手機斷開 adb 後，眼鏡過一段時間自行重開機

---

## 0. 結論

**主因（信心 High～Very High）：GhostLock（CVE-2026-43499）提權失敗時的 kernel panic。**
本專案自己的紀錄寫得很清楚：

> `~/ZeroLightRokidDesktop/docs/research-update.md`
> **成功率：每次開機約 50%；失敗 = kernel panic → 2–5 分鐘後自動重開機，KASLR slide 改變**

而本 workspace 的統計也一致：**143 次嘗試中 21 次 panic**（`reports/FACTS.md` §6 G9）。
「過沒多久」（2–5 分鐘）正是這個失敗模式的時間尺度。

**能修的部分**：**不能**讓 exploit 不 panic（本質風險，P3 已評估放棄改寫），
**但可以**把「自動、無限重試」拿掉，讓它不再反覆去賭那 50%，並修掉「重開機後其實不會自動恢復」的假象。

---

## 1. 證據

### 1.1 本機專案自己的紀錄（最直接）

| 來源 | 內容 |
|------|------|
| `ZeroLightRokidDesktop/docs/research-update.md` | 「**每次開機約 50%**；失敗 = kernel panic → **2–5 分鐘後自動重開機**」 |
| 同上（陷阱表） | 「約 50% panic → 重開機（2–5 分）｜重試前等 `sys.boot_completed=1` **且 uptime > 150 秒**」 |
| `reports/FACTS.md` G9 | 143 次歷史：verifier/reclaim 失敗 80、**panic 21**、timeout 0、OOM 0 |
| `deploy/test_shizuku_e2e.sh:7` | 「exploit ~50% 機率 kernel panic→reboot」 |

### 1.2 核心組態放大了 panic 的後果

```
Kernel command line: ... kernel.panic_on_rcu_stall=1 ...  ← 任何 RCU stall（預設 21 秒）直接 panic
```

GhostLock 是 **rt_mutex / futex-PI 的 `remove_waiter()` 清錯 `pi_blocked_on` → 懸空 `rt_mutex_waiter`**（CWE-416）。
競態輸掉時，那個腐敗的 PI 鏈會讓某個任務在核心裡卡住 → **RCU stall → 直接 panic → 重開機**。
`kernel.panic_on_rcu_stall=1` 讓「卡住」直接升級成「panic」，不必等 watchdog。

### 1.3 開機原因與前一次關機方式

```
$ getprop ro.boot.bootreason          → reboot
$ getprop persist.sys.boot.reason.history
  reboot,1789291362        ← 17:22:42（本次）
  reboot,shell,1789278832  ← 13:53:52（這筆有 shell = adb reboot）
  reboot,1789271315        ← 11:48:35
  reboot,1789271070        ← 11:44:30
$ dmesg | grep -i ps_hold
  qcom,pm8150@0:qcom,power-on@800: Power-off reason:
      Triggered from PS_HOLD (PS_HOLD/MSM Controlled Shutdown)
```

- 前一次關機是 **PS_HOLD / MSM 控制的關機**（＝乾淨的軟體式重置），不是硬體 watchdog 咬、也不是掉電。
- 這與「panic → 有序重開機」一致（若是 PMIC watchdog 咬，bootreason 通常會是 `watchdog`）。
- 17:22:42 那筆**沒有 `shell`**，所以**不是** `adb reboot`（`deploy/get_root_now.sh` 用的是 `adb reboot`，
  會在 history 留下 `reboot,shell,…`，例如 13:53:52 那筆）。

### 1.4 可以排除的項目

| 候選 | 判定 | 依據 |
|------|------|------|
| **RGR06 戒指** | ❌ 排除 | 今天的重開機有 11:44:30、11:48:35、13:53:52 —— **全部早於**把戒指綁上眼鏡（17:12:58） |
| **App 直接下 reboot** | ❌ 排除 | 全專案 grep 不到 `reboot`／`sys.powerctl`；唯一會重開機的是 `deploy/get_root_now.sh` 的 `adb reboot`（會留 `shell` 標記） |
| **電池／掉電** | ❌ 排除 | 目前 96%、`USB powered: true`，且關機原因是 PS_HOLD（軟體）而非 undervoltage |
| **殘留的 PC 迴圈** | ❌ 排除 | PC 上沒有 `get_root_now.sh`／`watch_root.sh` 在跑 |
| **熱** | ❌ 排除 | `ThermalEngine … actions shutdown` 那幾行是**開機時註冊監控器**，不是真的關機 |

### 1.5 為什麼「拔掉 adb 之後」才被注意到

1. 插著 USB 時裝置在**充電**、且 adb 讓系統保持較活躍 → 被破壞的核心狀態不一定馬上有機會爆。
2. 拔線後進入待機／排程狀態改變 → 懸空的 rt_mutex waiter 更容易在下一次**排程或 suspend 路徑**上被踩到 → 卡死 → RCU stall → panic。
3. 拔線同時也失去了 `adb`，使用者只能靠「裝置自己重開」察覺 → 直覺上就歸因給「拔線」。
4. 真正的**觸發源是更早的那次提權嘗試**（插線時跑的），重開機只是它的延遲後果。

### 1.6 一個被忽略的事實：重開機後我們的 App **根本不會自動起來**

```
W/BroadcastQueue: Background execution Third-Party APP not allowed:
    receiving Intent { act=android.intent.action.BOOT_COMPLETED ... }
    to com.rokid.rc.glasses/.BootReceiver
```

實測（本文撰寫時）：

```
app pid: none            ← App 沒在跑
uptime: 277.22           ← 距上次重開 4.6 分鐘
root_daemon 存活: 0      ← root 也沒了
```

→ 這台眼鏡的系統**禁止第三方 App 收到 BOOT_COMPLETED**，所以：
- 「重開機後自動恢復 root／戒指控制」目前**在實務上是無效的**；
- 但 `root_mode = AUTO`（prefs 實測值）＋ keeper 每 5 分鐘無限重試的設計，
  在 App 還活著時會**反覆重跑 exploit** → 等於每次都在賭那 ~50% → 可能演變成**開機迴圈**。

---

## 2. 是否能修正？

### 2.1 不能修（本質風險）

exploit 的成功率與穩定度是**同一個競態的兩面**：要贏得 race 就得打進那個時間窗，
而輸掉時留下的就是 UAF 現場。P3 已評估過「改寫以提高效率／成功率」並**放棄**（見
`reports/su_escalation_roi_assessment.md` §8）。**無法保證不重開機。**

### 2.2 能修（可大幅降低重開機頻率，全部在 App／腳本層）

| 優先 | 措施 | 為什麼有效 |
|------|------|-----------|
| **P0** | **開機迴圈斷路器**：持久化「上次 LPE 時間」與「本次開機時刻」；若**開機後 10 分鐘內又重開**連續 2 次 → 自動把 `rootMode` 轉 `MANUAL`、停用自動武裝，並在手機顯示原因 | 直接切斷「重開機 → 自動再賭 → 再重開」的迴圈 |
| **P0** | **每次開機的重試上限 ＋ 指數退避**（例如上限 2 次；5 → 15 → 60 分鐘），取代目前每 5 分鐘無限重試 | 把「無限次賭 50%」變成有界 |
| **P1** | **只在充電／插線時自動武裝**（`BatteryManager` 判斷 `USB/AC powered`）；拔線時 keeper 不自動重建 | 正好對應「拔線後才出事」；拔線時不再製造新的腐敗現場 |
| **P1** | **開機後延遲武裝**：等 `uptime > 150 s`（專案自己在 `research-update.md` 的建議） | 避開開機負載高峰，降低 race 失敗率 |
| **P1** | **修掉「重開機後不會自動起來」的假象**：既然 BOOT_COMPLETED 被系統擋掉，就不要假裝能自動恢復 —— 改成明確告知使用者「重開機後請插線執行 `deploy/root_bridge.sh`」，並讓手機顯示正確指引 | 目前的自動恢復是**無效的**，卻讓狀態顯示與實際不符 |
| **P2** | **讓使用者可一鍵停用自動提權**（設定頁開關，不需改 code） | 不需要 root 時就不必承擔 panic 風險 |

### 2.3 使用建議（立即可做）

- **不需要 root 常駐時**：把 `root_mode` 切到 `MANUAL` 並停用 keeper 自動重建 → 幾乎不會再無故重開機。
  做法：手機 App「設定 → 拍攝與權限 → root 模式」改為手動，或直接把 App 停用。
- **需要 root 時**：接受「每次嘗試約 50% 會重開機」，且**每次重開機都要重新武裝**
  （因為自動啟動被系統擋掉）；重試前先確認 `uptime > 150 s`。

---

## 3. 可執行的驗證實驗（低風險）

1. **A/B 測試**：把 App 的自動提權關掉（MANUAL）→ 拔線放 30–60 分鐘 → 觀察是否還會重開機。
   - 不再重開 ⇒ 確認是 exploit panic（同時也證明 P0 措施有效）。
   - 仍會重開 ⇒ 需要往硬體／系統層查（但目前所有證據都不支持）。
2. **記錄開機歷史**：每次重開後抓 `getprop persist.sys.boot.reason.history`（本檔已示範解碼方式），
   累積「有無 shell 標記」與時間分佈即可區分「`adb reboot`」與「panic 重開」。

---

## 4. 信心

| 陳述 | 標記 | 信心 | 依據 |
|------|------|------|------|
| 主因是 exploit 失敗的 kernel panic | INFERENCE（強） | **High** | 專案自身文件明載 ~50% panic→2–5 分重開；本 workspace 21/143 panic；`panic_on_rcu_stall=1` 讓卡住直接 panic |
| 前一次關機是 PS_HOLD 乾淨重置（非 watchdog／掉電） | FACT | **Very High** | dmesg 原文 |
| 17:22 那次不是 `adb reboot` | FACT | **Very High** | boot history 該筆無 `shell`，而 13:53:52 有 |
| 不是 RGR06 戒指造成 | FACT | **Very High** | 11:44／11:48／13:53 三次都早於戒指綁定（17:12:58） |
| App 在重開機後不會自動啟動 | FACT | **Very High** | `BOOT_COMPLETED … not allowed` ＋ 實測 `app pid: none` |
| `root_mode=AUTO` ＋ keeper 無限重試會放大風險 | FACT | **Very High** | prefs 實測 `root_mode=AUTO`；`RootDaemonKeeper` 僅有 5 分鐘 cooldown、無次數上限 |
| 「拔線」本身是觸發源 | INFERENCE | Medium | 較可能是「延遲後果 ＋ 拔線後才觀察到」；未做拔線 A/B |

## 5. 未執行
未停用任何自動提權、未解除戒指配對、未做拔線 A/B 實驗（需使用者配合觀察 30–60 分鐘）。
