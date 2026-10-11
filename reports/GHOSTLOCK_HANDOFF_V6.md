# GhostLock RV101 — 工作狀態交接 v6（第六任 → 第七任）

> 更新於 2026-08-18 深夜。本輪（第六任）自 v9.19 接手，歷經 v9.20→v9.26 共 7 版。
> 本輪**以靜態分析為主解開了 v5 交接的三個未定點**（±64 pad 的真相、rb_erase 寫入幾何、
> W1 未落地的靜態側全貌），並把回收工程化推進到「每次新開機 attempt 1 穩定 dip」——
> **但 payload 落地（vehicle diff）與 W1 enforcing 翻轉仍未達成**。v9.19 的四次成功
> （run 32/33/36/37）在本輪的 9 次裝置實測中**一次都沒能重現**；唯一新增的失敗模式
> 是「punch 1 即重置」（run 39/41/45），本輪已歸因（見 §3）。
>
> 工作目錄：`C:\Users\<USER>\Documents\codebase\rokid_root`
> 源碼：`source_code\ghostlock_rv101\`（v9.26，binary md5 `B6DCFBFC3F9F5B415F79621D4019CEC6`，裝置已同步）
> 目標裝置：`<DEVICE_SERIAL>`（RV101 / RG_glasses；Redmi `<PHONE_SERIAL>` 勿碰）

---

## 1. 本輪最重要的靜態結論（直接引用，勿重做）

全部由 `research/ghostlock_refs/rv101_disasm/*.txt` + 自建工具 `scripts/rv101_disasm.py`
（python3 + capstone 5.0.7，`python scripts/rv101_disasm.py <off> <len>`，offset=Image 檔內偏移）
逐條核對。**裝置 kernel = `binaries/extracted_1.22/kernel`（47MB raw Image）。**

### 1.1 rb_erase 的寫入幾何（0x774b38，已完全解開）

- Linux rb_node 佈局：`__rb_parent_color` @ +0x00、`rb_right` @ +0x08、`rb_left` @ +0x10。
- 我們 forged waiter 的 tree_entry：`tree_pc=(target-8)|1`、`tree_right=write_value`、
  `tree_left=0` → rb_erase 走 **case A**（left==NULL）：
  - `0x774b9c: ldr x12,[parent+0x10]!` 讀 `[target+8]` 與 node 比對（幾乎必不等於 stack addr）
    → `0x774bac: str x8,[x11]` 把 **write_value 寫到 `target+0`（rb_right 槽）**；
  - `0x774d84: str x9,[x8]` 把 `(target-8)|1` 寫到 `write_value`（= page_base+SAFE_OFF，
    即 forensic 的 child_pc 訊號）。
- **W1 的 target=data_addr(SELINUX_STATE) 時，8-byte write_value 落在 selinux_state+0，
  byte0=0x00 → enforcing=0。** 這條鏈靜態上無懈可擊。
- v4 交接的「寫到哪一槽靠運氣」已排除：只要 `[target+8] != node`（W1 恆真），就是 target+0。

### 1.2 SKB data 在 block+0（v5 的 ±64「修正」是錯的）

- run 32/33/36 的 diff 只在 recv `0x0E08` 出現 8 bytes（`60 bc .. c0 ff` = **forged waiter 的
  kernel stack 位址**），pristine 全 0。這只可能是「空樹 enqueue 的 `root->rb_node=waiter`」寫在
  **slab+0x0E08**（fake_lock+8）——即 recv[j] = slab[j]，**資料從 block+0 開始，沒有 NET_SKB_PAD**。
- v5 把 payload 與 forensic 同步 −64 後，payload 落在 `slab+OFF−64`，而 walk 的位址是
  `page_base+OFF = slab+OFF`：walk 讀到的 fake lock 全為 0（空鎖、owner=0）→ 這正是
  run 32/33「usage=0x100、leftmost 原值、只有 rb_node 一個 diff」的完整解釋。
- `GL_SKB_PAD=0` 會把 payload 對齊（walk 走完整的 owner 鏈）；**但對齊幾何會死**：punch 1 的
  walk 用 RB_CLEAR_NODE/rb_link_node 污染 forged waiter（stack 槽），punch 2（若 nice 再變）會把
  它重新插回樹產生**自環 rb_node**，punch 3 的 rb_erase case-C 下降迴圈
  （0x774b50-0x774b60）`x11=[x11+0x10]=S` 永真 → **死循環 → watchdog 重置**。
  → **v9.21 已把 punch 改成固定 nice（不再交替）**：punch 2-5 在 rt_mutex_adjust_pi 的
  `waiter->prio == task->prio` 前置檢查即退出，不會二次 walk。
- 注意：`__sched_setscheduler` 裡 prio store（0x199b68）**早於** rt_mutex_adjust_pi 呼叫
  （0x199d80，gate 於 pi=true，sched_setattr 恆真）→ walk 時 A->prio=139（nice 19）。

### 1.3 walk 全路徑（rt_mutex_adjust_prio_chain @ 0x1ceaf4）已逐條核對

- 本裝置是 5.9+ 新式簽名：`(task, chwalk=0, orig_lock=NULL, next_lock, orig_waiter=NULL, top_task)`；
  rt_mutex_adjust_pi（0x1ce9fc）以 x4=NULL 呼叫 → 全部 top_waiter/pi_waiters 前置檢查被跳過。
- **pad 64（現行幾何）**：leftmost=0 → x27=0 → dequeue 的 RB_EMPTY_NODE 檢查
  `[S]==S?` 否 → rb_erase（**target+0 寫入**）→ enqueue（空樹：root=leftmost=S）→
  owner=0 → `1cf2a0` 的 wake 路徑因 x27=0 短路 → 乾淨返回。**全程無 fault**。
- **pad 0（對齊幾何）**：erase 同 → enqueue 插入 {w0}（prio 139>130 → **右子**，
  rb_insert 黑父路徑 `0x774a18 tbnz → 0x774ad4 str node,[root]` 安全但把 root 改成 S）→
  owner=ftask|1 → usage 0x100↔0x101 往返、wait_lock 0↔1 往返 → forged≠leftmost → pi-block
  與 0x1985f0（rt_mutex_setprio 式、會碰 runqueue）**全部跳過** → 乾淨。靜態上 pad 0 也安全；
  run 39 的 punch-1 死亡歸因於 gate flood 的連帶（見 §3），**不是 walk 本身 fault**。
- 固定 nice 後每次 attempt 只有 punch 1 產生一次 walk。

### 1.4 enforcing @ selinux_state+0 已裝置實證

`/proc/config.gz`：`CONFIG_SECURITY_SELINUX_DISABLE` **未設**、`CONFIG_SECURITY_SELINUX_DEVELOP=y`
→ `struct selinux_state { bool enforcing; ... }`，enforcing 就在 +0；sel_read_enforce 反組譯的
`ldarb [state+0]&1` 相符。**target 的位址算術是對的。**

---

## 2. 本輪的改動（v9.20 → v9.26）

| 版 | 內容 |
|---|---|
| v9.20 | GL_SKB_PAD 預設 0；strict forensic（walk-address 視角 rb+OFF 讀 child_pc/scratch/w0/pi 欄位、diff cap 64、child_pc==(target-8)\|1 才判過）；GL_TARGET_DELTA env；enforce 狀態列印 |
| v9.21 | **punch 固定 nice**（修 punch-3 rb_erase 自環死循環）；banner 版本化 |
| v9.22 | V9_PURGE_CNT 700→1800（**548 clone 失敗**：snitch 4096 線程 + 子進程 ≈ 6608 RLIMIT_NPROC 上限）；settle cap 60 |
| v9.23 | purge 回 700；dummy 450→300；settle soak 28 + quiet 10（**waves 需 ~12-24s**） |
| v9.24 | 移除 pre-send 的 /proc/pagetypeinfo 讀取（它在 dip 偵測與首 send 之間重開 ~1-5ms 偷竊窗）；dip poll 嘗試 sysfs（/sys/kernel/slab/* 對 shell **EACCES**，自動回退 slabinfo） |
| v9.25 | purge 700→1000；settle soak 40 + quiet 30（**quiet 必須大於 ~10-15s 的 lazy drain 波週期**，否則波間假安靜）+ cap 120 |
| v9.26 | purge 1000→1100（phase-1 stacks ≈30.3MB，剛過實測 ~29-31MB 的 vmap purge 閾值）；dummy 300→200（GL_DUMMY_KILLS=180 需 < 200-224 flush 閾值，避免早燃） |

另產出 `scripts/ghostlock_reboot_run.ps1`（**reboot-per-attempt 跑批**：每 cycle reboot→等
uptime≥150s→`GL_SETUP_ATTEMPTS=1` 跑一次→host tee 存 log；成功字串已改為
`carried the STRICT walk signature|SELinux DISABLED`）。

## 3. 本輪 9 次裝置實測矩陣（全部 pad 64 除非註明）

| run | 配置 | 結果 |
|---|---|---|
| 39 | pad **0** | punch 1 死亡（gate flood +620 連帶） |
| 40 | dummy 0 | 8 attempts：attempt 1 dip=1，其餘 dip=0；**全部零 diff** |
| 41 | dummy 100 | punch 1 死亡（gate flood +1041） |
| 42 | dummy 250, purge 1800(僅1251成功) | attempt 1 dip=1；零 diff |
| 43 | dummy 250, purge 700 | attempt 1 dip=1；零 diff |
| 44 | dummy 180 | attempt 1 dip=1（flood +960）；零 diff |
| 45 | dummy 180, purge 1000, **mode 1 (W1)** | cycle1 punch 1 死亡（flood +1410）；cycle2 五 attempts 全零 diff、**enforce 始終 '1'** |
| 46 | dummy 180, purge 1100 | 4 cycles（reboot-per-attempt）：**每次 attempt 1 dip=1**；零 diff |

**規律**：(a) 只有新開機 attempt 1 會 dip（attempt 2+ 的 leftover 使 flush 早燃，pre+leak 留在
queue 尾永不被 drain）；(b) dip 後 gate 總有 +800~+1400 的 o2 flood（vmap purge 在 gate 才跨閾），
leak slab 被埋、首 send 拿不到 → 零 diff；(c) punch-1 死亡 = flood 後 leak slab 被重分配（多半
變 kernel stack），walk 的四個寫入打進活 stack → 受害者崩潰 → 重置。**run 40 起的零 diff 是
payload 沒落地，不是 walk 沒跑**（v4 的 GL_POISON trylock panic + run 39/41/45 的死亡都證明
walk 會執行）。

## 4. 留給下一任（按優先序）

1. **首要：證明/證偽 punch 1 的 erase 真的執行**。做 `GL_POISON2=1`（route.c 已備好：
   tree_pc=0xdead000000000001 → erase 的 `[parent+0x10]` 讀 0xdead…010 → fault）：
   punch 1 即 panic ⟺ erase 執行；平安 ⟺ walk 沒走到 erase（重新審 punch 前置條件）。
   這是 W1 未翻轉之謎的決定性二分。
2. **gate flood 的殘餘**：settle 的 o2+objs 安靜對 vmap list 的靜默累積是盲的——run 46 的
   settle quiet=35（17.5s 穩定）後 phase-3 的 ~3MB 仍把近閾 list 推過（+825）。方向：
   (a) settle 尾追加「強制跨越小隊」（再殺 ~300 使其必跨閾 → purge 落 settle 內 → 二次安靜）；
   (b) 或把 purge 提到 1300（stacks 33.5MB，注意 6608 預算：snitch 4096+子進程；
   run 42 的 1800→548 失敗是 RLIMIT_NPROC）。
3. **payload 落地後（diff 出現）**：selftest strict 簽名（child_pc@recv[SAFE_OFF]==(target-8)|1、
   mode3 另驗 scratch==page_base+SAFE_OFF）→ 立刻轉 W1（`--write1`，同一套 env，
   `W1_ATTEMPTS=1` + reboot-per-attempt；判據 `SELinux DISABLED`）。若 strict 過而 enforce 仍 1，
   用 `GL_TARGET_DELTA` 掃 {−8,0,+8,−64,+64}（§1.1 說 0 是對的，但這是廉價的最後驗證）。
4. W1 穩定後 → W2（mode 2 已就緒，write_value=data_addr(INIT_CRED)）。
5. **不要回頭用 pad 0**（對齊幾何有 punch-3 死循環風險，除非同時改為單發 punch）；
   也不要恢復 nice 交替（同因）。

## 5. 產物 / 檔案

| 位置 | 內容 |
|---|---|
| `source_code/ghostlock_rv101/` | v9.26 源碼（md5 `B6DCFBFC3F9F5B415F79621D4019CEC6`，裝置已同步） |
| `scripts/rv101_disasm.py` | 可複用的 kernel 反組譯工具（capstone） |
| `scripts/ghostlock_reboot_run.ps1` | reboot-per-attempt 跑批（已修 $Args 自動變數坑） |
| `reports/ghostlock_runlogs/exploit_run{39..46}.log` | 本輪 9 份裝置日誌（run 45 含 mode-1 W1 樣本） |

建置（照舊）：`$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d';
cd source_code\ghostlock_rv101; mingw32-make -B`；push 後務必 md5 比對。

跑法（現行）：
```
adb -s <DEVICE_SERIAL> shell "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180 /data/local/tmp/ghostlock_rv101 --selftest"
```
reboot-per-attempt：`& scripts/ghostlock_reboot_run.ps1 -Cycles 4 -Env "GL_SKB_PAD=64 GL_DEBUG_DRAIN=1 GL_DUMMY_KILLS=180" -ExploitArgs "--selftest" -Log runNN`

操作紀律照舊：panic 2-5 分鐘自動重開、KASLR 自動重洩漏、dmesg/pstore 無用、
**每個 exploit 進程只跑 attempt 1（同一 boot 的 attempt 2+ 不會 dip）**、裝置曾出現過
自發性 ~5-6 分鐘 reboot 週期（疑似歷次 run 的 pinned mm 累積 + 低記憶），跑前驗 uptime。
