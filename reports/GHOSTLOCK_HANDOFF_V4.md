# GhostLock RV101 — 工作狀態交接文件 v4

> 本文件更新於第四任智能體工作結束時（2026-08-17 凌晨）。
> v1/v2/v3 交接（GHOSTLOCK_HANDOVER_PROMPT.md / GHOSTLOCK_HANDOFF_V2.md /
> GHOSTLOCK_HANDOFF_V3.md）仍為有效背景，但 v3 的兩個關鍵假設已被本輪推翻（見 §2）。
> 工作目錄：`C:\Users\<USER>\Documents\codebase\rokid_root`

---

## 1. 總結論（一句話）

**v3 的「walk 在 fake page 上 kernel panic」其實是「回收失敗造成的垃圾頁 walk」：payload 從未被
vehicle forensic 證實過在位——v9 的 o2 證據只是推論，真正的 payload-walk 至今一次都沒被觀測到。
本輪把 punch 時序、回收 close 順序、buddy 搶頭競態逐一修掉，gate 已能偶發觸發（run 10 的
o2 2470→2471 硬證據），但「payload 在位 + walk 寫入 + forensic 可見」的最終一步尚未達成。**

---

## 2. 本輪已結案的事實（勿重做）

### 2.1 推翻 v3 的兩個假設
1. **dmesg 抓不到 panic**：本裝置 panic 是硬 reset，ring buffer 乾淨結束、無任何 oops 輸出
   （`ghostlock_work/dmesg_run3.log` 第 565551 行證實）。v3 的「並行 dmesg -w」計畫無效，
   **panic PC 在本裝置不可得**，進度只能靠 forensic（vehicle diff）+ controlled-fault 二分。
2. **「fake wait_lock spinlock debug 檢查」假說死亡**：鎖 helpers 已反組譯
   （`rv101_disasm/spinlock_helpers.txt`），全是 vanilla qspinlock（`ldaxr/stxr` 4-byte cmpxchg），
   無 DEBUG_SPINLOCK magic/owner 檢查、無 lockdep。0x9409df0=trylock、0x9409f70=lock、
   0x940a0c8=lock_irq、0x940a208=unlock_irq、0x940a2ac=unlock、0x940a004=lock_irqsave。

### 2.2 trigger / walk 的事實
- **errno 35 = EDEADLK（不是 EAGAIN！）**：requeue 每次穩定回 -EDEADLK（v1 §67 早已記載，
  本輪重新確認）。vendor futex_requeue 的 EDEADLK fixup（0x235180）會
  `str xzr,[rt_waiter+0x38]`（waiter->lock=NULL）後直接回傳。
- **walk 確實到達 forged waiter**：GL_POISON=1（PWS_LOCK=0xdead）在 run 5/9 都在 punch 1
  即 panic（trylock [0xdead] fault）——注意 poison 只證明 walk 讀了 forged stack bytes，
  **不證明 payload 在位**（fault 在 0xdead，與頁內容無關）。
- **payload-walk 的靜態路徑完整無 fault**（已逐條核對 rt_mutex_adjust_prio_chain 反組譯）：
  永久寫入 5 處：leftmost(0xE10)=forged、w0->left(0x1010)=forged、usage(0x1240)=0x101、
  scratch(0x1300)=write_value、child_pc(0x1500)=(target-8)|1；wait_lock 先 1 後 0（淨零）。
  NOOWNER=1 路徑尾段呼叫的是 wake_up_process(init_task)（0x9228cf8 veneer→0x193854，
  w1=3=STATE_NORMAL，無害，**不是 put_task_struct**，init_task usage 不會被消耗）。
- punch 時序已修：原 5ms 常打在 A 進 select 之前（walk 走 stale rt_waiter，因
  waiter->prio==task->prio 在 0x1cebbc 良性返回）。現為 multi-punch：+20/55/90/125/160ms
  共 5 發，交替 nice 19/18（每發都是真實 policy 變更、都觸發 PI walk），並有
  「pselect: calling select now / select returned」標記可對齊視窗。

### 2.3 回收機制的事實（本輪核心工作）
- **v9 的兩個 race（本輪定位並修掉）**：
  1. node-partial pop race：v9 順序讓 leak slab 在最早一次 drain 上 node partial 鏈頭，
     任何併發 mm 分配都會 pop 走它 → gate 永不觸發。已修（v9.1 close 重排，見 §5）。
  2. buddy-LIFO 竊取：leak-close 後、首台 vehicle send 之前，系統 fork 的 kernel stack
     （order-2 buddy 分配）會搶走 LIFO 頭的 leak slab；o2 debug 讀取還把視窗拉到 ms 級。
     已修（v9.2：settle + 首 send 前零讀取，µs 級視窗）。
- **o2 檢查不可靠**（本輪血淚教訓）：flood 子進程 kernel stack 的 RCU 回流會造成 ±數百塊
  的 o2 噪音（run 7 attempt 2 見 +312；run 12 attempt 1 見 +63）。**唯一可靠判據 = vehicle
  forensic（有 diff ⟺ payload 在位 + walk 寫了我們的頁）**。do_one_write 已改為 forensic
  驅動重試（KERNEL_PAGE_SETUP_ATTEMPTS=12）。
- **gate 觸發的硬證據（run 10）**：tight probe `o2 2470 -> 2471`——leak-close 瞬間 +1，
  leak slab 確實被 slab_empty gate discard 回 buddy。這證明 v9.1/v9.2 的機制鏈
  （pobjects 14>13 → drain → leak slab 沉到 node partial 底部 → gate）可以打通。
- SLUB 反組譯已核對（`rv101_disasm/{put_cpu_partial_real,__slab_free_real,unfreeze_partials}.txt`）：
  drain 條件 `pobjects > cpu_partial(13)` ✓、unfreeze_partials 空頁閘門 `inuse==0 &&
  nr_partial >= min_partial(5)` ✓、add_partial 是 add-at-head（鏈頭頁沉到 list 底部 =
  pop-proof）✓。
- clone/memfd 失敗計數已加（run 10/11/12 全部 fail=0）：slab 數學（45 flood slab +
  leak slab 17 obj + post slab）完好，不是失敗的 clone 破壞的。

---

## 3. 新阻塞點：payload 放置仍未達成（唯一任務）

### 3.1 現象（runs 7/8/10/11/12）
- run 7（v9.1+gate 檢查）：4 次嘗試 o2 -1 / +312 / -1 / +1——「+1 但零寫入」（可能是
  buddy 竊取：gate 後 leak slab 被系統 stack 拿走，vehicle[0] 拿到別的塊）。
- run 8（v9.2）：attempt 1 零寫入（garbage 良性 walk）、attempt 2 panic（garbage fault）。
- run 10（v9.2+tight probe）：gate +1 確認，但 tight 讀取（~1ms）重開 buddy 竊取視窗 →
  punch 1 panic。
- run 11（boot 0 分鐘）：punch 1 panic（忙亂 boot，垃圾頁 walk）。
- run 12（穩定 boot 5 分鐘）：attempt 1 全序列 o2 +63（**RCU 噪音洪流——150ms settle 明顯
  不足**）零寫入；attempt 2 punch 2 panic。

### 3.2 嫌疑排序（下一任直接照做）
1. **settle 不足**（頭號）：150ms 沖不掉 765 個 child 的 RCU stack 回流（run 12 的 +63
   證明關閉序列期間仍有大批 stack 返回，隨時可能落在 leak-close→首 send 的 µs 視窗內）。
   → 改 **adaptive settle**：kill 後輪詢 o2（每 100ms 讀一次），連續兩次讀值相同（安靜）
     才進 close 序列；或乾脆固定 1-2s。
2. **gate 的 nr_partial 裕度**：晚段 flood 每 slab 留 1 object（16/17 關閉）已有 ~33 片
   緩衝，理論上夠；若仍偶發 miss，把 V9_PREPARE_CNT/SPRAY_CNT 微調（但 ulimit -p 6608
   逼近上限，勿加太多）。
3. **buddy 竊取的殘餘機率**：µs 級視窗 + 安靜 settle 後應可忽略；若仍發生，可考慮在
   首 send 前先把 768 個 socketpair 全部建好（已做）並用 sendmmsg 一次送出首發。

### 3.3 若放置達成後 walk 仍 panic →「安全分流」無 PC 二分（預案）
payload 在位時 walk 的 fault 點可以用「把某欄位改成安全值使 walk 提前乾淨分流」來二分
（每變體一次 run，panic=未分流到那點）：
- 變體 A：RTMUTEX_WAITERS_LM_OFF=0（leftmost=0 → 0x1cec5c cbz 分流，跳過 w0 讀取；
  若不再 panic → fault 在 w0 相關讀取 0x1cec60/[w0+0x40] 區段）。
- 變體 B：GL_NOOWNER=1（跳過 fake-task 區段）。
- 變體 C：W0_OFF+WAITER_LOCK_OFF=0xdead…（fault 移到 0x1cec60 的可控讀取）。
- 判據仍是 forensic 的 5 處寫入（見 §2.2）——能寫到哪一處就代表 walk 走到哪。

---

## 4. 本輪改動的檔案（源碼，均已建置並推送，勿盲目回退）

| 檔案 | 內容 |
|---|---|
| `src/route.c` | multi-punch（5 發 +20..160ms、交替 nice 18/19）、punch 前後標記、PUNCH_COUNT/INTERVAL 常數、GL_PUNCH_DELAY_USEC 可調 |
| `src/util.c` | v9.1 close 重排（post[0]→flood F0-F11→**pre[0] 觸發 drain**→pre[1..15]→post[1..16]→晚段 flood 16/17）；v9.2 settle 150ms + 首 send 前零讀取；debug_pagetype_o2()；clone/memfd 失敗計數；cleanup_page_prepare_state 回傳 diff 數 |
| `src/main.c` | do_one_write 改為 forensic 驅動重試（無 diff → 重試，最多 12 次） |
| `src/common.h` | cleanup_page_prepare_state 簽名改 int |
| `target_rv101.h` | PSELECT_ENTER_DELAY_USEC 5000→20000 |

建置（勿忘 -B 與 md5 比對）：`$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d';
cd source_code\ghostlock_rv101; mingw32-make -B`。
⚠️ PowerShell 管線會吃掉 make 失敗碼（v3 §7 已記載），務必 Get-FileHash 與裝置 md5sum 比對。
本輪末 binary md5：`01bd9e97992f79d1a83f66589d011922`（裝置上已同步）。

---

## 5. 本輪新增產物

| 位置 | 內容 |
|---|---|
| `reports/GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md` | 本輪完整分析筆記（含 run 1-12 證據表） |
| `reports/ghostlock_runlogs/exploit_run{2..12}*.log` | 11 份裝置實測日誌 |
| `research/ghostlock_refs/rv101_disasm/{spinlock_helpers,fn_193854,qspinlock_slowpath,rt_mutex_adjust_pi,rb_helper_77512c,caller_9228cf8,unfreeze_partials}.txt` | 7 份新反組譯 |
| （工作區）`RideFlux/ghostlock_work/dmesg_run{2,3}.log` | dmesg 全串流（各 34/93MB，證明 panic 無輸出；run3 的 565551 行是舊 boot 最後一行） |

---

## 6. 環境注意事項（增補 v3）

- 兩台裝置：目標 `<DEVICE_SERIAL>`（RV101）；Redmi `<PHONE_SERIAL>` 本輪末已離線，
  若重新出現仍勿碰。
- 每次 panic → 2-5 分鐘自動重開機、KASLR slide 改變（exploit 自動重洩漏）。
- **dmesg/pstore 都拿不到 panic 訊息**：別再花時間；進度探針只有 forensic 與
  controlled-fault（poison 或安全分流變體）。
- 每次跑 selftest 前等 boot 穩定（uptime ≥ 4-5 分鐘）可顯著降低噪音（run 11 boot 0 分鐘
  直接炸）。
- 跑法（產生日誌）：
  `adb -s <DEVICE_SERIAL> shell "/data/local/tmp/ghostlock_rv101 --selftest" 2>&1 | Tee-Object exploit_runN.log`
  診斷開關：GL_DEBUG_DRAIN=1（o2 全表）、GL_POISON/GL_POISON2、GL_NOOWNER、GL_VEHICLES、
  GL_PUNCH_DELAY_USEC、SELFTEST_RUNS。
- selftest 通過判據（cleanup 的 forensic 已印）：有 vehicle diff 行 +
  `child_pc=(target-8)|1` + `scratch=0x…500`（SAFE_OFF）+ `fake_task_usage=0x101`。
