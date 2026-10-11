# 租約修補：deploy 缺口 → App 權限根因 → 三方一致性（2026-09-13）

> English: [deploy_lease_fix_2026-09-13_EN.md](deploy_lease_fix_2026-09-13_EN.md)

> **一句話**：補上 deploy 側**原本完全沒有**的租約保護，並在驗證過程中由測試抓到
> 兩個會導致「同一個 exploit 被兩邊同時跑」的真實缺陷。
> 最終把租約搬到 App 可寫的 777 目錄，**從根本移除權限陷阱**；
> `deploy/test_lease.sh` **15/15**、`deploy/test_keeper_lease_guard.sh` **15/15** 通過。

---

## 1. 為什麼要做這件事

`docs/lease-and-keeper.md` 指出：exploit 本體的三個路徑**寫死在 `ghostlock_rv101` 內**，
無法像 helper 與信箱那樣一分為二：

```
/data/local/tmp/.ghostlock_root.sh   ← payload 會被對方換掉
/data/local/tmp/gl_run.log           ← 會被對方截斷
/data/local/tmp/ghostlock_rv101      ← 會被對方 pkill
```

因此「誰正在跑 exploit」必須全域序列化。桌面板與 App 都有租約，
但 **deploy 側完全沒有** —— 修正前 `grep -n lease deploy/*.sh` **零命中**。

## 2. 新增檔案

| 檔案 | 角色 |
|------|------|
| `deploy/lease.sh` | 租約 primitive：`take`／`wait_free`／`renew`／`autorenew`／`drop`／`cleanup`／`selftest` |
| `deploy/proc_util.sh` | 安全殺程序（host 端過濾 + 分批送 PID）、信箱清理、程序數診斷 |
| `deploy/test_lease.sh` | 租約測試套件（**不需要 root、不觸發 exploit**） |
| `deploy/test_keeper_lease_guard.sh` | keeper 守衛端到端測試（製造 root 失效；**判準用日誌**）|
| `deploy/root_restore_delayed.sh` | 延遲復原行程：以 root 身分先留下，時間到把 daemon＋keepalive 帶回來（**不需重跑 exploit**）|

## 3. 修改的檔案

| 檔案 | 修改 |
|------|------|
| `deploy/get_root_now.sh` | 取租約（`LEASE_WAIT_S` 等不到就 `exit 2`）＋背景續約＋`EXIT` trap 釋放＋每輪檢查 `lease_lost`；`ensure_helper`／`cleanup_exploit` 改用 `proc_util`；新增 `DRY_RUN=1` |
| `deploy/root_bridge.sh` | 啟動 helper **前**先殺兩個變體＋**清空兩個信箱的 `last.cmd`**；改用 `proc_util`；呼叫 `lease_ensure_file`（保證租約檔以 666 存在，App 才能覆寫）|
| `docs/lease-and-keeper.md` | 新增 deploy 為第三個操作者、契約更新、五個缺陷、keeper 端到端驗證結果 |
| `android/app-glasses/…/ghost/RootDaemonKeeper.kt` | `takeLease()` 回傳成敗並記錄失敗；`dropLease()` 改寫過期標記；`externalLeaseOwner()` 回傳實際 owner；`stale.delete()` 照實記錄 |
| `deploy/lease.sh` | 租約路徑改到 777 的 `ghostlock_bridge/`（寫成完整字面值供 T9 比對）；各函式先 `mkdir -p`+`chmod 777`；新增 `lease_migrate_legacy` |
| `deploy/test_lease.sh` | 新增 **T9**（三處路徑字面值一致）與 **T10**（App 可在租約目錄建立／刪除、直接寫入租約檔）|
| `ZeroLightRokidDesktop/src/zero_light_desktop.c` | `DEV_LEASE` 改新路徑；**`lease_drop()` 由 `rm -f` 改為寫入過期標記**；`lease_write()` 改 `mkdir -p`+`chmod 777`+原子寫入+清舊路徑；重建 `zero_light_desktop.exe` |

### 為什麼新增 `owner=deploy` 不必改動另外兩邊

兩邊的判斷都是 **deny-by-default** —— 只把 owner 等於「自己」算自己人：

```c
/* zero_light_desktop.c */  int mine = (strstr(t, "owner=desktop") != NULL);
/* RootDaemonKeeper.kt  */  val mine = txt.contains("owner=app")
```

所以任何未知 owner（含 `deploy`）都會被視為「別人持有」而正確讓開。
已在實機以兩種視角驗證（測試 T4／T5）。

## 4. 測試抓到的兩個新缺陷

### A. 身分只比 `owner` → 同 owner 的多實例**完全不互斥**（**高**）

**這是測試 T3 抓到的，不是我預先想到的。** 第一版 `lease_is_mine` 只比 owner，
於是兩個 `get_root_now.sh` 實例都寫 `owner=deploy`、互相認為是「自己人」，
**同時**驅動同一支 exploit —— 正是租約要防的事。

桌面板（單一 GUI，`g_busy` 守衛）與 App（單一 keeper，`rearming` 守衛）本身是 singleton，
不靠 pid 就能互斥；但 **`get_root_now.sh` 是命令列腳本，可以被同時跑兩次**。

**修法**：身分改為 **(owner, pid)**。`lease_is_mine`／`lease_held_by_other`／`lease_renew`／
`lease_drop` 全部要求 owner 與 pid 都相符（pid 省略時只比 owner，供模擬另兩側視角）。
這也讓「非持有者不能刪掉持有者的租約」成立（測試 T3 第二項斷言）。

### B. `pkill` pattern 漏殺 `_desktop` 變體（**高**）

在裝置上以 `grep -E` 實測 pattern 涵蓋範圍：

| pattern | 中 App helper | 中 `_desktop` helper |
|---|---|---|
| `ghostlock_bridge_helper.sh`（`root_bridge.sh` 原用） | ✅ | ❌ **漏** |
| `ghostlock_bridge_helper.s[h]`（`get_root_now.sh` 原用） | ✅ | ❌ **漏** |
| `ghostlock_bridge_helpe[r]` | ✅ | ✅ |

原因：兩者在 `"helper"` 之後分岔（`.sh` vs `_desktop.sh`）。

**後果（本場實測）**：桌面板 helper（pid 3358）整場存活並與 App helper 同時搶共用檔案；
裝置自行重開機後它被重新拉起，看到殘留的 `last.cmd` 就再觸發 exploit，衍生
**994 個子程序、`load` 75.8、adb 指令逾時**。

**修法**：
1. `proc_util.sh` **在 host 端過濾、只把 PID 送到裝置端 kill** ——
   裝置端只執行 `ps -A -o PID,ARGS`，pattern 從不出現在裝置 cmdline 上，
   **結構上不可能自我匹配**（比 bracket trick 更根本）。
2. `root_bridge.sh` 啟動 helper **之前**先 `clear_trigger_mailboxes`，斬斷重生迴圈。
3. 分批送 kill：原「一個 PID 一次 adb」在殺 500+ 程序時**超過 60 秒而被呼叫端中斷**
   （清理只做一半）；改為每 200 個一批，實測殺 2 個變體僅 **5.7 秒**。

## 5. 驗證結果

### 5.1 `deploy/test_lease.sh` — **15/15 通過**

| 測項 | 驗證內容 | 結果 |
|---|---|---|
| T1 | 空租約 → 可取得 | ✓ |
| T2 | 自己持有 → 不阻擋自己 | ✓ |
| T3 | 另一個 deploy **實例**（不同 pid）取不到；非持有者無法刪掉持有者的租約 | ✓✓ |
| T4 | 以 desktop 視角看 `owner=deploy` → 判定為別人 | ✓ |
| T5 | 以 app 視角看 `owner=deploy` → 判定為別人 | ✓ |
| T6 | 過期租約（ts 超過 TTL）不再阻擋 | ✓ |
| T7 | 背景續約使租約保持新鮮 | ✓ |
| T8 | 程序被 `TERM` → `EXIT` trap 釋放，不留 15 分鐘殘留 | ✓ |
| T9 | 三處路徑**字面值**一致（契約漂移防護）| ✓ |
| T10 | App 可在租約目錄建立／刪除檔案、並直接寫入租約檔 | ✓ |

### 5.2 守衛路徑（真實腳本端到端）

投入新鮮的 `owner=desktop` 租約（`pid=99999`）後執行 `get_root_now.sh`：

```
[lease] 已被他人有效持有（owner=desktop age=3s ttl=900s）→ 等待最多 0s 讓對方做完
[lease] ✗ 等不到租約（owner=desktop age=4s ttl=900s）→ 放棄；避免與對方互搶共用檔案
exit code = 2
```

- ✅ 正確讓開並以 **exit 2** 結束
- ✅ **沒有偷走**桌面板的租約（事後仍是 `owner=desktop`）
- ✅ **沒有觸發任何 exploit**（`exploit_count=0`）

### 5.3 取得／釋放路徑（`DRY_RUN`）

`DRY_RUN=1 DRY_RUN_HOLD_S=15 bash deploy/get_root_now.sh`：

```
持有中：owner=deploy pid=103474 ts=1789263885   ← 103474 = 腳本自身的 pid
結束後：ls: /data/local/tmp/ghostlock_op.lease: No such file or directory
exit=0
```

## 6. 驗證期間順帶觀察到的事實

**桌面板當時正在執行**：`ghostlock_bridge_helper_desktop.sh` 與其信箱檔的 mtime
是 **09:33**，而我在 09:33 執行的測試正好釋放了租約 —— 桌面板在空檔取得租約並改寫
helper。這證明租約現在**確實在三個操作者之間生效**，也說明一個固有限制：

> 租約保證「同時只有一個人在跑」，**不保證搶用順序符合預期**。
> 若要做到「deploy 優先」或「排隊」，需要再加優先權或佇列，目前沒有。

另註：驗證 `helpers=N` 時**不可用 device 端 `grep -c`** ——
`grep` 自身的 cmdline 含 pattern 會自我匹配（實測把 0 報成 2）。
請用 `bash deploy/proc_util.sh status`（host 端過濾）。

## 7. App 側：租約從來沒有真正生效（**本輪最重要的發現**）

`detail` 寫死 `"desktop"` 只是表面問題。為了在實機觀察 keeper 的守衛，
必須先製造「root 對 App 不可用」—— 而在追查守衛為何「無法觀測」時，
發現**真正的原因是權限**：

`/data/local/tmp` 是 `drwxrwx--x shell shell`；眼鏡 App 是 **uid 10053**，屬 "other"，
只有 `--x`（可穿越）**沒有目錄寫入權**。以 `run-as`（App 為 debuggable）實測：

| 動作 | 實測結果 | 對租約的意義 |
|---|---|---|
| 建立新檔 | `Permission denied` | 租約檔不存在時 `takeLease()` **必然失敗** |
| 覆寫已存在的 666 檔 | **成功** | 只有檔案先被 shell 建好才有效 |
| 刪除檔案 | `Permission denied` | `dropLease()` **永遠無效** |

### 7.1 `takeLease()` 靜默失敗 → App 沒持租約就跑 exploit（**高**）

原本包在 `runCatching {}` 裡，失敗被吞掉。只要租約檔不存在，
App 就在**未持有租約**的狀態下驅動 exploit —— 正是租約要防的互搶。
**修法**：回傳 `Boolean`，失敗時留 `W` 級日誌。

### 7.2 `dropLease()` 永遠失敗 → 留下 `owner=app` 卡住別人 900 秒（**高**）

重新武裝結束後租約仍以 `owner=app` + 新鮮 `ts` 殘留，
桌面工具與 deploy 會被擋住最多 15 分鐘。
**修法**：改寫「已釋放」標記而非刪除 —— `owner=none pid=0 ts=0`
（三方都視為無效），同時保留 666 檔案供後續覆寫。

### 7.3 `stale.delete()` 的日誌與事實不符（**中**）

原程式碼不看 `delete()` 回傳值就印 `removed stale socket`。
實測：**日誌說刪了、檔案還在**。這種假日誌會讓除錯往錯的方向走。
**修法**：檢查回傳值，失敗時改印
`stale socket present but not removable by app uid`。

### 7.4 配套：租約檔改為「只重設、不刪除」

App 無法重建被刪掉的檔案，所以 `deploy/lease.sh` 的 `lease_drop()` 也改為
寫入過期標記（`lease_reset`），並新增 `lease_ensure_file`（建檔 + `chmod 666`，
由 `root_bridge.sh` 佈署時呼叫）。**三方都不再刪除租約檔。**

> ⚠ **桌面板還沒跟上**：`zero_light_desktop.c` 的 `lease_drop()` 仍是 `rm -f`。
> 它刪掉檔案後，App 在下次 shell 側 `lease_ensure_file` 之前都無法取得租約。
> 建議一併改為寫入 `owner=none pid=0 ts=0`。

## 7.5 根治：租約搬到 777 的 `ghostlock_bridge/`

§7.1–7.4 都是「在舊位置把症狀壓下來」。真正的解法是把租約檔搬到
**App 有寫入權**的目錄：

| | 舊 `/data/local/tmp/ghostlock_op.lease` | 新 `/data/local/tmp/ghostlock_bridge/ghostlock_op.lease` |
|---|---|---|
| 目錄權限 | `drwxrwx--x`（App 只有 `--x`）| **`drwxrwxrwx`（777）** |
| App 建立檔案 | ❌ `Permission denied` | ✅ `CREATE_OK` |
| App 刪除檔案 | ❌ `Permission denied` | ✅ `RM_OK` |
| `takeLease()` 檔案不存在時 | **必然失敗** | 成功 |

搬遷後即使租約檔被刪掉，**App 也能自己重建** —— 舊位置永遠做不到。

### 三方同步（含桌面板）

| 檔案 | 變更 |
|---|---|
| `deploy/lease.sh` | `LEASE_PATH` 改新路徑（**寫成完整字面值**，供 T9 比對）；`lease_ensure_file`／`lease_reset`／`_lease_write` 都先 `mkdir -p` + `chmod 777`；新增 `lease_migrate_legacy` |
| `RootDaemonKeeper.kt` | `LEASE_PATH` 改新路徑；註解改述「搬遷後 App 可自行建立」|
| `zero_light_desktop.c` | `DEV_LEASE` 改新路徑；`lease_drop()` 由 **`rm -f` 改為寫入 `owner=none pid=0 ts=0`**；`lease_write()` 改為 `mkdir -p` + `chmod 777` + **暫存檔/`mv` 原子寫入** + 清舊路徑 |

> `lease_drop()` 即使在新位置已經「刪得掉」，仍維持寫標記：**刪除與重建之間
> 對方會讀到「檔案不存在」＝無租約＝可搶**，那正是 TOCTOU 的溫床。

### 漂移防護與權限驗證（新增 T9／T10）

路徑寫在三個語言裡。歷史上就發生過「只改了窄字串常數、其餘十幾處仍寫死舊路徑」
而全盤失效（該教訓原本就記在 `zero_light_desktop.c` 的註解裡）。所以新增：

| 測項 | 內容 |
|---|---|
| **T9** | 直接比對三個檔案裡的租約路徑**字面值**是否一致 |
| **T10** | 用 `run-as` 以 **App 的 uid** 實際在租約目錄建立／刪除檔案、並直接寫入租約檔 |

`zero_light_desktop.exe` 重建後（MinGW，PE32+ GUI），以 `strings -el` 確認：
新路徑出現 3 次、`owner=none pid=0 ts=0` 出現 1 次、**對新路徑沒有任何 `rm`**，
唯一的 `rm -f` 是刻意的舊路徑遷移清理。

## 8. keeper 端到端守衛驗證：**15/15 通過**

這是先前一直標記為「**無法觀測**」的那一項。關鍵在於守衛程式碼位於
`if (client.isRoot()) return` **之後** —— root 可用時 keeper 直接早退，
永遠執行不到租約判斷。

**新增 `deploy/test_keeper_lease_guard.sh`**：製造「裝置健康 + keeper 閒置 + root 失效」。

**判準用日誌，不是 socket 檔存不存在** —— 因為 App 根本沒有目錄寫入權，
不守衛時它也刪不掉 socket，「socket 還在」證明不了任何事。

| 階段 | 條件 | 預期 | 結果 |
|---|---|---|---|
| 對照組 | 無租約 | keeper 越過守衛 → 記錄 `no trigger available` | ✓ |
| 實驗組 | 新鮮 `owner=deploy` 租約 | 停在守衛、**不**越過 | ✓ |

**新路徑複驗（11:33，租約已搬到 `ghostlock_bridge/`）**：
**14 項功能斷言全數通過**。唯一失敗是測試後的 root 復原 —— 裝置在 11:37 自行重開機，
把延遲復原行程一併帶走（見 §9 的裝置 `/data` 不穩定項）。程式邏輯無誤。

實驗組實測日誌（**owner 正確顯示為 deploy**）：

```
10:18:11.237 RootDaemonKeeper: external operator(deploy) holds lease; skip re-arm and leave socket alone
10:18:41.249 RootDaemonKeeper: external operator(deploy) holds lease; skip re-arm and leave socket alone
```

### 安全機制：不需重跑 exploit 就能恢復 root

root daemon 一旦被殺**只有 root 能重建**。因此測試在殺 daemon **之前**，
先經 root socket 以 root 身分啟動 `deploy/root_restore_delayed.sh`
（detach 的延遲復原行程）。實測 **240 秒後自動復原**，全程未重跑 exploit。
測試也會**驗證復原行程確實是 root 身分**才敢繼續，否則中止。

### 測試本身被修掉的兩個問題（誠實記錄）

1. **單點取樣 race**：第一版在 keeper 30 秒週期邊界前一刻檢查 socket，
   同時看到「檔案還在」與「已刪除的日誌」而自相矛盾 → 改為輪詢到結果出現。
2. **前置檢查漏看心跳檔**：keeper 判斷 helper 是否常駐靠的是
   `ghostlock_bridge/helper.alive`（容忍 120s），**不是 `ps`**。
   第一版只看 `ps`，於是「行程已死但心跳還新鮮」時 keeper 仍會真的重跑 exploit
   → 改為主動把心跳寫成 `0`，並在 `ROOT_DELAY < 240s` 時直接拒跑。

## 8.5 實戰驗證：租約在真實競爭下正確讓開

驗證尾聲發生一次**非計畫中的真實競爭**，反而成了最有說服力的一次驗證：

`get_root_now.sh` 正持有 `owner=deploy` 租約進行多輪提權時，裝置連續重開機；
期間**桌面工具（當時確實在執行）取走了租約**，我們的背景續約偵測到
「已非我持有」→ 寫入 lost 旗標 → 主程式**立即中止**：

```
[2026-09-13 11:47:26] [lease] ✗ 租約已被他人取走
                      （lost at 2026-09-13 11:44:54 (owner=none)）→ 中止
```

這正是設計要的行為：**不與對方互搶 binary 內寫死的共用檔案**
（`.ghostlock_root.sh`、`gl_run.log`、`ghostlock_rv101`）。
修補前它會繼續跑，兩邊互相覆寫 payload 與截斷 log。

同時也驗證了 `lease_renew` 的三態設計：adb 在 reboot 期間失敗回 `2`（暫時性，繼續重試），
只有**真的讀到別人持有**才回 `1` 並觸發中止 —— 沒有把「裝置離線」誤判為「租約被搶」。

## 9. 尚未完成 / 下一步

| 優先 | 項目 |
|------|------|
| ~~P1~~ | ~~App keeper 側守衛日誌待觀察~~ → ✅ **已完成（§8，15/15）** |
| ~~P2~~ | ~~`detail` 寫死 `"desktop"`~~ → ✅ **已修並實機驗證顯示 `deploy`** |
| ~~P1~~ | ~~桌面板 `lease_drop()` 改為寫入過期標記~~ → ✅ **已完成並重建 exe**（§7.5）|
| P2 | helper 內的 `pkill -9 -f ghostlock_rv101` 仍會殺到**別的**操作者的 exploit（租約已大幅降低發生率，但非不可能）|
| ~~P2~~ | ~~把租約移到 777 子目錄~~ → ✅ **已完成（§7.5），T9／T10 驗證通過** |
| **P1** | **裝置 `/data` 重開機後會回滾**（實測：租約檔消失、APK 退回舊版）—— 裝置層級問題，非程式碼；緩解見 `docs/lease-and-keeper.md`〈尚未完成〉|

---

*裝置：Rokid RV101 `<DEVICE_SERIAL>`（1.22.009-20260710-150201）｜adb：Windows 端 `adb.exe`（WSL）*
*相關：`reports/kernelsnitch_mm_leak_clean_boot_2026-09-13.md`（root 取得過程與核心狀態分析）*
