# 操作租約與 App 看守的互動

ZeroLightRokidDesktop（PC）與眼鏡 App 的 `RootDaemonKeeper` 都會驅動同一套 exploit。
本文件記錄兩者的共用契約、實際衝突、以及一次**失敗的端到端實測**。

> **2026-09-13 更新**：新增第三個操作者 —— **deploy 腳本**（`get_root_now.sh` 等），
> 補上它原本**完全沒有**的租約保護。過程中實測抓到 **App 側租約從未真正生效**的根因
> （App 對 `/data/local/tmp` 沒有目錄寫入權 → 無法建立、也無法刪除租約檔），
> 共修掉 deploy 側 2 個 + App 側 3 個缺陷。**keeper 端到端守衛已於實機驗證（15/15）**，
> 補上本文件先前標記為「無法觀測」的那一項。見下方各節。

## 共用資源與分工

| 資源 | 桌面工具 | 眼鏡 App | deploy 腳本 | 是否共用 |
|---|---|---|---|---|
| helper | `ghostlock_bridge_helper_desktop.sh` | `ghostlock_bridge_helper.sh` | `ghostlock_bridge_helper.sh` | ❌ 前兩者已分開 |
| 信箱目錄 | `ghostlock_bridge_desktop/` | `ghostlock_bridge/` | `ghostlock_bridge/` | ❌ 前兩者已分開 |
| helper 心跳 | `ghostlock_bridge_desktop/helper.alive` | `ghostlock_bridge/helper.alive` | 同 App | ❌ 前兩者已分開 |
| `ghostlock_rv101` | 同 | 同 | 同 | ✅ **三方共用** |
| `.ghostlock_root.sh` | 同 | 同 | 同 | ✅ **三方共用** |
| `gl_run.log` | 同 | 同 | 同 | ✅ **三方共用** |
| `root_daemon.sock` | 同 | 同 | 同 | ✅ 三方共用 |
| `ghostlock_bridge/ghostlock_op.lease` | `owner=desktop` | `owner=app` | `owner=deploy` | ✅ 三方共用 |

exploit 本體三個路徑**無法分開**（路徑寫死在 `ghostlock_rv101` 內），所以必須靠租約序列化。

## 租約契約

固定路徑：`/data/local/tmp/ghostlock_bridge/ghostlock_op.lease`
（三方程式碼各寫死一份，**改動時必須同步**；`deploy/test_lease.sh` 的 **T9** 會直接比對
三個檔案裡的**字面值**擋住漂移。路徑刻意寫成完整字面值，不用變數串接。）

```
owner=desktop|app|deploy
pid=<pid>
ts=<unix 秒>
```

- **有效期限 900 秒**；持有者需在工作期間續約
- 桌面工具：一鍵最長 80 分鐘，因此由既有的 **30 秒輪詢**負責續約
- App keeper：只在單次 `acquireRoot()` 期間持有（上限 5 分鐘），`finally` 釋放
- deploy 腳本：`lease.sh` 的背景續約（30 秒）＋ `EXIT` trap 釋放
- 未持有者看到**新鮮**租約就必須讓開

### 為什麼 helper 分開還需要租約

分開 helper 只解決「互相 pkill」與「信箱被搶」。exploit 本體仍共用，兩邊同時跑會：

- 互相覆寫 `.ghostlock_root.sh`（payload 會被對方換掉）
- 互相截斷 `gl_run.log`
- 桌面工具的 `pkill -9 -f ghostlock_rv10[1]` 會殺掉 App 正在跑的那一份

## 實測：衝突確實存在

在眼鏡上（APK 含 `RootDaemonKeeper`，classes5/6/7.dex）製造「root 失效、helper 常駐」情境後：

```
08:01:57.803  RootDaemonKeeper: removed stale socket /data/local/tmp/root_daemon.sock
08:01:57.809  RootDaemonKeeper: auto re-arm start (helper=true shizuku=false detail=deploy/root_bridge.sh)
08:01:57.812  AutoGhostlockEngine: acquireRoot via bridge (gl_engine_bridge_resident)
```

三行各證明一件事：keeper **主動刪 socket**、把**桌面工具裝的 helper** 當成可用觸發源、
並**透過同一條 bridge** 執行提權。隨後 `last.cmd` 被佔用、`last.progress=waiting-done`。

**修正前的行為**：keeper 的 `stale.delete()` 是無條件執行的，且位於租約判斷之前 ——
桌面工具剛把 daemon 拉起來、socket 尚未出現在 `/proc/net/unix` 的空窗期就會被刪掉。

## 端到端實測：**失敗，且是「無法觀測」而非通過或失敗**

**測試設計（B1）**：寫入新鮮的 `owner=desktop` 租約 → 建立一個 stale socket 檔案 →
觀察 keeper 是否 (a) 記錄「讓開」 (b) 不去刪那個 socket。

**結果**：

- socket 檔案**存活** —— 與守衛行為一致
- 但 **keeper 完全沒有產生任何日誌**，因此無法證明是守衛生效

**原因**：keeper 卡在它自己 08:01 那次 `acquireRoot()` 裡，**從頭到尾沒跑到租約檢查**。
我寫的守衛在 `check()` 內，而 `check()` 沒有返回。

**同時間發現的失控狀態**：

```
exploit_proc = 492 → 清掉後又長回 516
load         = 273 → 347（adb 指令開始逾時）
```

## 這次暴露的三個缺口（已修）

### 1. `acquireRoot()` 沒有逾時 ← 最關鍵

keeper 卡住 17 分鐘，期間：不再檢查租約、狀態永遠停在 `REARMING`、exploit 持續洩漏程序。

**這使租約機制在最需要它的時候失效** —— 租約只防「同時開始」，不防「卡住不放」。

修法：`withTimeoutOrNull(REARM_TIMEOUT_MS = 5 分鐘)`；逾時則清洩漏、釋放租約、回到
`LOST_NEED_ARM`（`detail=rearm-timeout`）。

### 2. 洩漏的 exploit 程序沒有清理

修法（兩邊都做）：

- **桌面工具**：一鍵開始前先 `cleanup_stray_exploits()`
- **App keeper**：逾時後 `cleanupStrayExploits()`（僅在 root 仍可用時有效 ——
  App 殺不掉 shell/root 擁有的行程，清不掉時交給桌面工具）

**教訓：一定要用 `ps` 取 PID 再逐一 `kill`，不能用 `pkill -f`。**
`pkill -f` 的 pattern 會匹配到執行它的 shell 指令本身，造成 shell 自殺
（實測退出碼 137，整個清理中斷）。

### 3. 租約預檢（TOCTOU）

從「讀租約」到「真正動手」之間可能被對方搶走。修法：在 `takeLease()` 前**再讀一次**，
被搶走就中止並回到 `LOST_COOLDOWN`（`detail=external-lease(desktop,precheck)`）。

## deploy 路徑的租約（2026-09-13 補上）

**問題**：deploy 側**完全沒有**租約。`grep -n lease deploy/*.sh` 在修正前沒有任何命中。
於是 `get_root_now.sh` 會與桌面板／App keeper 同時驅動同一支 exploit，
互搶 binary 內寫死的共用檔案 —— 正是本文件開頭描述的衝突。

**新增 `deploy/lease.sh`**（共用 primitive）：

| 函式 | 作用 |
|---|---|
| `lease_take <owner> [pid]` | 預檢 → 寫入（tmp+`mv` 原子）→ 回讀確認；被持有則回 1 |
| `lease_wait_free <owner> <秒>` | 等對方讓開（App keeper 單次重建上限 5 分鐘） |
| `lease_renew` | **只在自己仍持有時**續約；回 2 = adb 暫時失敗（reboot 中）不誤判為被搶 |
| `lease_autorenew_start` | 背景續約；真的被搶走才寫旗標檔 |
| `lease_cleanup` | 停續約 → 釋放 → 清旗標（掛 `EXIT` trap） |

**為什麼新增 `owner=deploy` 不必改動另外兩邊**：兩邊都是 **deny-by-default** ——
只把「owner 等於自己」算自己人（`strstr(txt,"owner=desktop")` / `txt.contains("owner=app")`），
所以未知 owner 一律視為別人而正確讓開。

**接線**：`get_root_now.sh` 取租約（`LEASE_WAIT_S` 預設 300s，等不到就 `exit 2`）、
背景續約、`EXIT` trap 釋放，且每輪 boot 前檢查 `lease_lost`。
新增 `DRY_RUN=1`（可搭 `DRY_RUN_HOLD_S`）只驗證租約而**完全不觸發 exploit**。

## 後續實測抓到的兩個新缺陷（2026-09-13，皆已修）

### A. 身分只比 `owner` → 同 owner 的多實例**完全不互斥**（**高**）

`test_lease.sh` 的 T3 抓到：兩個 deploy 實例都寫 `owner=deploy`，
若身分只比 owner，兩者會互相認為是「自己人」而**同時**跑 exploit。

桌面工具（單一 GUI，`g_busy` 守衛）與 App（單一 keeper，`rearming` 守衛）本身是 singleton，
不依賴 pid 就能互斥；但 **`get_root_now.sh` 是命令列腳本，可以被同時跑兩次**。

修法：身分改為 **(owner, pid)**。`lease_is_mine` / `lease_held_by_other` / `lease_renew` /
`lease_drop` 全部改為 owner 與 pid 都要相符（pid 省略時只比 owner，供模擬另兩側視角）。

### B. `pkill` pattern 漏殺 `_desktop` 變體（**高**）

實測在裝置上以 `grep -E` 驗證 pattern 的涵蓋範圍：

| pattern | 中 App helper | 中 `_desktop` helper |
|---|---|---|
| `ghostlock_bridge_helper.sh`（`root_bridge.sh` 原用） | ✅ | ❌ **漏** |
| `ghostlock_bridge_helper.s[h]`（`get_root_now.sh` 原用） | ✅ | ❌ **漏** |
| `ghostlock_bridge_helpe[r]` | ✅ | ✅ |

原因：兩者在 `"helper"` 之後分岔（`.sh` vs `_desktop.sh`）。
**後果**：桌面板 helper 整場存活並與 App helper 同時搶共用檔案；
裝置自行重開機後它被重新拉起，看到殘留的 `last.cmd` 就再觸發 exploit
→ 實測 **994 個子程序、`load` 75.8、adb 指令逾時**。

修法：新增 `deploy/proc_util.sh`，**在 host 端過濾、只把 PID 送到裝置端 kill**
（裝置端只執行 `ps -A -o PID,ARGS`，pattern 從不出現在裝置 cmdline 上，
結構上不可能自我匹配）；`root_bridge.sh` 啟動 helper **之前**先
`clear_trigger_mailboxes`（清掉兩個信箱的 `last.cmd`），斬斷重生迴圈。
另修掉「一個 PID 一次 adb」的效能問題（殺 500+ 程序會超過 60 秒而中斷）。

## App 側的權限陷阱 → **已用「換位置」根治**（2026-09-13）

`/data/local/tmp` 是 `drwxrwx--x shell shell`；眼鏡 App 是 **uid 10053**，屬 "other"，
只有 `--x`（可穿越）**沒有目錄寫入權**。以 `run-as`（App 為 debuggable）實測：

| 動作 | 結果 | 對租約的意義 |
|---|---|---|
| 建立新檔 | `Permission denied` | 租約檔不存在時 `takeLease()` **必然失敗** |
| 覆寫已存在的 666 檔 | **成功** | 只有檔案先被 shell 建好才有效 |
| 刪除檔案 | `Permission denied` | `dropLease()` **永遠無效** |

由此推出三個真實缺陷（皆已修）：

### 1. `takeLease()` 靜默失敗 → App 沒持租約就跑 exploit（**高**）

原本寫在 `runCatching {}` 裡，失敗被吞掉。於是只要租約檔不存在，
App 就在**未持有租約**的狀態下驅動 exploit —— 正是租約要防的互搶。
修法：回傳 `Boolean` 並在失敗時留下 `W` 級日誌。

### 2. `dropLease()` 永遠失敗 → 留下 `owner=app` 卡住別人 900 秒（**高**）

`delete()` 對 App 無效，於是重新武裝結束後租約仍以 `owner=app` + 新鮮 `ts` 殘留，
桌面工具與 deploy 會被擋住最多 15 分鐘。修法：**改寫「已釋放」標記而非刪除** ——
寫入 `owner=none pid=0 ts=0`（三方都視為無效），同時保留 666 檔案供後續覆寫。

### 3. `stale.delete()` 的日誌與事實不符（**中**）

原程式碼不看 `delete()` 回傳值就印 `removed stale socket`。
實測：日誌說刪了、**檔案還在**。這種假日誌會讓除錯往錯的方向走。
修法：檢查回傳值，失敗時改印
`stale socket present but not removable by app uid`。

### 配套：租約檔改為「只重設、不刪除」

因為 App 無法重建被刪掉的檔案，`deploy/lease.sh` 的 `lease_drop()` 也改為
**寫入過期標記**（`lease_reset`），並新增 `lease_ensure_file`（建檔 + `chmod 666`，
由 `root_bridge.sh` 在佈署時呼叫）。三方都不再刪除租約檔。

### 根治：租約搬到 777 的 `ghostlock_bridge/`

前面的 1./2./3. 都是「在舊位置把症狀壓下來」。真正的解法是把租約檔搬到
**App 有寫入權**的目錄：

| | 舊：`/data/local/tmp/ghostlock_op.lease` | 新：`/data/local/tmp/ghostlock_bridge/ghostlock_op.lease` |
|---|---|---|
| 目錄權限 | `drwxrwx--x`（App 只有 `--x`） | **`drwxrwxrwx`（777）** |
| App 建立檔案 | ❌ `Permission denied` | ✅ `CREATE_OK` |
| App 刪除檔案 | ❌ `Permission denied` | ✅ `RM_OK` |
| `takeLease()` 檔案不存在時 | **必然失敗** | 成功 |

搬遷後，即使租約檔被刪掉，**App 也能自己重建** —— 這是舊位置永遠做不到的事。

**遷移**：`deploy/lease.sh` 的 `lease_migrate_legacy` 與桌面工具的 `lease_write()`
都會順手清掉舊路徑的租約檔，避免兩份租約並存造成誤判。

### 三方一致性已由測試守住

路徑寫在三個語言裡（bash／Kotlin／C），歷史上就發生過
「只改了窄字串常數、其餘十幾處仍寫死舊路徑」而全盤失效。
`deploy/test_lease.sh` 新增兩項：

| 測項 | 內容 |
|---|---|
| **T9** | 直接比對三個檔案裡的租約路徑**字面值**是否一致（漂移防護）|
| **T10** | 用 `run-as` 以 **App 的 uid** 實際在租約目錄建立／刪除檔案、並直接寫入租約檔 |

> ✅ **桌面板已跟上**：`zero_light_desktop.c` 的 `lease_drop()` 已由 `rm -f` 改為
> **寫入 `owner=none pid=0 ts=0`**，`lease_write()` 也改為「先 `mkdir -p` + `chmod 777`，
> 再以暫存檔 + `mv` 原子寫入」並順手清掉舊路徑。三方現在用完全相同的約定。

## 驗證結果（2026-09-13，裝置 RV101）

### deploy 側：`deploy/test_lease.sh` — **15/15 通過**

| 測項 | 驗證內容 |
|---|---|
| T1 / T2 | 已釋放狀態可取得；自己持有不阻擋自己 |
| T3 | 另一個 deploy **實例**（不同 pid）取不到；非持有者無法清掉持有者的租約 |
| T4 / T5 | 以 desktop／app 視角看 `owner=deploy` 都判定為「別人」（deny-by-default） |
| T6 | 過期租約不再阻擋 |
| T7 | 背景續約使租約保持新鮮 |
| T8 | 程序被 `TERM` 時 `EXIT` trap 釋放（不再留下新鮮租約） |
| **T9** | **三處路徑字面值一致**（契約漂移防護）|
| **T10** | **App 可在租約目錄建立／刪除檔案、並直接寫入租約檔**（權限陷阱是否真的移除）|

### App keeper 側：`deploy/test_keeper_lease_guard.sh` — **15/15 通過**

這是本文件先前一直「**無法觀測**」的那一項。**守衛程式碼位於
`if (client.isRoot()) return` 之後**，所以必須製造「root 對 App 不可用」才會被執行。

**對照組設計**（判準是**日誌**，不是 socket 檔存不存在 ——
因為 App 根本沒有目錄寫入權，不守衛時它也刪不掉 socket）：

| 階段 | 條件 | 預期 | 結果 |
|---|---|---|---|
| 對照組 | 無租約 | keeper 越過守衛 → 記錄 `no trigger available` | ✓ |
| 實驗組 | 新鮮 `owner=deploy` 租約 | 停在守衛、**不**越過 | ✓ |

> **新路徑複驗（11:33）**：把租約搬到 `ghostlock_bridge/` 之後重跑，
> **14 項功能斷言全數通過**（對照組越過守衛、實驗組停在守衛且 owner 顯示 `deploy`）。
> 唯一的失敗是**測試結束後的 root 復原**：裝置在測試中途（11:37）自行重開機，
> 把負責復原的 `root_restore_delayed.sh` 一併帶走 —— 屬裝置層級問題，非程式邏輯。

實驗組實測日誌（**owner 正確顯示為 deploy**，寫死 `desktop` 的缺陷已修）：

```
10:18:11.237 RootDaemonKeeper: external operator(deploy) holds lease; skip re-arm and leave socket alone
10:18:41.249 RootDaemonKeeper: external operator(deploy) holds lease; skip re-arm and leave socket alone
```

測試以 `deploy/root_restore_delayed.sh` 在殺 daemon **之前**先以 root 身分留下
一個延遲復原行程，因此**不需重跑 exploit** 就能恢復 root（實測 240 秒後自動復原）。

### 另外兩個端到端檢查

- **守衛路徑**：投入新鮮 `owner=desktop` 租約後跑 `get_root_now.sh`
  → 記錄「已被他人有效持有」、`exit 2`、**沒偷走對方的租約**、**沒觸發任何 exploit**。
- **取得／釋放路徑**：`DRY_RUN=1 DRY_RUN_HOLD_S=15` → 持有期間裝置上確實是
  `owner=deploy pid=<腳本 pid>`；結束後租約被重設為 `owner=none`。

## 尚未完成

- **本機（RV101）的 `/data` 在重開機後會回滾**：實測連續數次 reboot 後，
  11:13 建立的租約檔消失、10:25 的舊檔回來、已安裝的 APK 回到 09:52 的版本
  （`lastUpdateTime` 由 11:15 退回 09:52），但同目錄較舊的檔案仍在。
  影響：**任何寫在 `/data/local/tmp` 的協調狀態（含租約）都可能無預警消失**。
  緩解：`lease_ensure_file`（`root_bridge.sh` 每次佈署都會呼叫）會重建租約檔，
  且搬到 777 目錄後 **App 自己也能重建**；讀者把「檔案不存在」視為「無租約」，
  因此是暫時失去協調而非故障。**這是裝置層級的問題，非本專案程式碼所致。**

- 逾時後的洩漏清理在 **root 已失效**時清不掉（App 無權限），這是已知限制。
- **跨操作者的即時衝突仍會發生**：驗證期間實測到桌面板在 deploy 釋放租約的空檔
  取得租約並改寫 `_desktop` helper。租約只保證「同時只有一個人在跑」，
  不保證「先搶先贏的順序符合預期」。
- `RootDaemonKeeper` 的 `externalLeaseOwner()` 仍是**每次檢查讀一次檔案**；
  頻率為 30 秒一次，實務上無妨，但未來若要提高頻率需注意。

## 給後續測試者的提醒

- 測試前先確認 `exploit` 程序數與 `load`，**不要在已經很亂的裝置上疊加新的嘗試**
  → 用 `bash deploy/proc_util.sh status`（**host 端過濾，不受 device 端 grep 自我匹配影響**）
- 要收斂失控的裝置：`bash deploy/proc_util.sh kill`（會殺兩個 helper 變體 + 所有 exploit
  並清空兩個信箱）
- 改動租約相關程式碼後，**先跑 `bash deploy/test_lease.sh`** 再做實機提權 ——
  它不需要 root、不觸發 exploit，可在任何狀態下安全執行
- 只想驗證租約、不想多跑一次 LPE：`DRY_RUN=1 bash deploy/get_root_now.sh`
- 若要製造「root 失效」，**root 側 keepalive 是 root 身分，adb shell 殺不掉**；
  必須經由 root 通道：`printf "kill -9 PID\n" | nc -U /data/local/tmp/root_daemon.sock`
- 相關工具見 `tools/wintest/README.md`
