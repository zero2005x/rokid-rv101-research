# GhostLock RV101 — v3「fake page panic」根因分析（第四任）

## 結論（一句話）

v3 交接的「walk 在 fake page 上 kernel panic」其實是**回收失敗造成的垃圾頁 walk**：
payload 根本不在 walk 讀寫的那頁上（v9 回收閘門多數時候 MISS）。真正的
payload-walk 尚未被觀測到；v3 的二分結論（BLACK-root、NOOWNER、鎖/erase 區段）
全部基於垃圾頁 panics，只能當作「walk 在垃圾頁上會死」的證據。

## 實測證據鏈（本輪 runs）

| Run | 內容 | 結果 |
|---|---|---|
| 1 | v9 原 binary | requeue EDEADLK ✓（errno 35 = **EDEADLK**，v1 交接 §67 記載）；punch 卡死內核（punch_done=0）→ ~20s 後硬 reset（軟鎖死型） |
| 2 | v9 | punch 成功、零寫入、無 panic → walk 沒碰我們的頁 |
| 3 | multi-punch（5 發，視窗內） | 5 發全成功、零寫入 → 同上 |
| 4 | GL_POISON=1 | kernelsnitch 誤報（ffff0000...）→ 提前失敗 |
| 5 | GL_POISON=1 | **punch 1 即 panic** → walk 確實到達 forged waiter 的 trylock（PWS_LOCK=0xdead fault） |
| 6 | GL_DEBUG_DRAIN=1（fresh boot） | o2 2096→2093（**gate MISS**）→ 之後 panic（垃圾頁 walk = v3 簽名） |
| 7 | v9.1（close 重排+gate 檢查+重試） | 4 次嘗試：o2 -1 / +312（RCU 噪音）/ -1 / **+1 但零寫入** → o2 檢查不可靠 |
| 8 | v9.2（settle+首送搶頭+forensic 重試） | 進行中 |

## 關鍵機制發現

1. **errno 35 = EDEADLK（不是 EAGAIN）**：trigger 每次都成功（v1 已記載，本輪再確認）。
   本 kernel 的 futex_requeue EDEADLK fixup（0x235180）會 `str xzr,[rt_waiter+0x38]`
   （waiter->lock=NULL）後直接回 -EDEADLK。
2. **dmesg 抓不到 panic**：本裝置的 panic 是硬 reset，ring buffer 無 oops 輸出
   （dmesg_run3.log 565551 行乾淨結束）。v3 的「並行 dmesg -w」計畫無效。
   PC 只能靠 controlled-fault 二分 + vehicle forensic。
3. **鎖 helpers 是 vanilla qspinlock**（0x9409df0/0x940a2ac/0x940a208/0x940a0c8 已反組譯）：
   無 DEBUG_SPINLOCK magic 檢查、無 lockdep → v3 頭號嫌疑排除。
4. **punch 時序 race**：原 5ms 延遲常打在 A 進 select 之前（walk 走 stale rt_waiter，
   因 waiter->prio==task->prio 在 0x1cebbc 良性返回，或 lock=NULL 時在
   0x1ceb7c-0x1ceb84 對 NULL==NULL 通過）。已改 multi-punch（+20/55/90/125/160ms，
   交替 nice 18/19 使每次 punch 都觸發 PI walk）。
5. **v9 回收的兩個 race**（根因）：
   - **node-partial pop race**：v9 順序讓 leak slab 在最早一次 drain 上 node partial
     的**鏈頭**，任何併發 mm 分配都會 pop 走它 → gate 永遠不觸發。
     → v9.1：重排 close 順序（post[0] → flood F0-F11 → **pre[0] 自己觸發 drain**
     （pobjects 13→14），leak slab 在鏈頭 → add_partial 後沉到 list 底部 = pop-proof；
     晚段 flood 每 slab 留 1 object（16/17）→ nr_partial 深緩衝 ~33。
   - **buddy LIFO race**：leak-close 後、首台 vehicle send 之前，任何系統 fork 的
     kernel stack（order-2）都會搶走 buddy 頭的 leak slab；o2 debug 讀取還把視窗
     拉長到 ms 級。run 7-attempt-4 的「+1 但零寫入」最可能就是這個。
     → v9.2：kill 後 settle 150ms（沖掉 RCU stack 回流）→ close 序列 →
     **leak-close 後立刻 send vehicle[0]**（µs 視窗）→ 其餘 767 台。
6. **o2 檢查不可靠**：flood 子進程 kernel stack 的 RCU 回流（±數百塊）污染
   o2_before/o2_after 差分。**唯一可靠判據 = vehicle forensic**（有 diff ⟺
   payload 在位 + walk 寫了我們的頁）。do_one_write 已改為 forensic 驅動重試
   （KERNEL_PAGE_SETUP_ATTEMPTS=12）。
7. **walk 靜態路徑（payload 在位時）應完整走通**：NOOWNER=0 路徑的永久寫入 =
   leftmost(0xE10)=forged、w0->left(0x1010)=forged、usage(0x1240)=0x101、
   scratch(0x1300)=write_value、child_pc(0x1500)=(target-8)|1；wait_lock 先 1 後 0
   （淨零）。NOOWNER=1 路徑尾段呼叫的是 wake_up_process(init_task)（0x9228cf8
   veneer → 0x193854，w1=3=STATE_NORMAL），無害（不是 put_task_struct）。

## 殘餘風險

- 垃圾頁 walk 仍可能 panic（硬 reset，每 run 最多燒掉 12 次嘗試×1 次開機）。
  v9.2 應使 gate 接近確定性，但 boot 尖峰期仍有機會。
- pi_blocked_on 的存活是 race：run 5 poison 證明會存活；目前假定穩定（v1 多次
  poison 成功背書）。

## 檔案

- 改動：src/util.c（v9.1 close 重排 + v9.2 settle/首送 + debug_pagetype_o2 +
  cleanup 回傳 diff 數）、src/main.c（do_one_write forensic 重試）、
  src/route.c（multi-punch + 標記）、src/common.h、target_rv101.h（延遲 5→20ms）。
- 新反組譯（RideFlux/ghostlock_work/）：spinlock_helpers.txt、fn_193854.txt、
  qspinlock_slowpath.txt、rt_mutex_adjust_pi.txt、rb_helper_77512c.txt、
  caller_9228cf8.txt。
- 日誌（RideFlux/ghostlock_work/）：exploit_run{2..8}_*.log、dmesg_run{2,3}.log。
