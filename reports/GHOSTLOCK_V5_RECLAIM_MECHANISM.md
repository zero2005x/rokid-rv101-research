# GhostLock RV101 — 回收機制全圖（第五任，2026-08-18）

> English: [GHOSTLOCK_V5_RECLAIM_MECHANISM_EN.md](GHOSTLOCK_V5_RECLAIM_MECHANISM_EN.md)
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> 本文件是 `GHOSTLOCK_HANDOFF_V5.md` 的機制詳版。所有結論均有裝置實測
> （reports/ghostlock_runlogs/exploit_run{13..35}.log + mmfree_probe{1,2,3,5}）背書。

## 1. 本裝置 mm_struct 生命週期（QCOM 5.10.209-perf + Google lazy 補丁）

kallsyms 證據：`rcu_cblist_flush_enqueue`、`lazy_list`、`lazy_vunmap_enable`、
`__purge_vmap_area_lazy`、`rcu_nocb_gp_kthread`/`rcu_nocb_cb_kthread`、
`do_nocb_deferred_wakeup_timer`；`/proc/config.gz`：`CONFIG_RCU_NOCB_CPU=y`、
`CONFIG_VMAP_STACK=y`。

1. **mm_struct free 走 lazy call_rcu**：free_mm 的 kmem_cache_free 被延遲到 lazy list
   的批量 flush（FIFO）。flush 的觸發：lazy list 的長度越過 ~100-224（probe2-A：128 個
   pending 不觸發、224 個觸發；probe5-P1：200 個 12s 不觸發）。每次 flush 的 drain
   ~150-200 個 callback（run19：766 殺 → ~480 釋放；run22：objs −449）。
2. **memfd pin（/proc/pid/mem）洩漏**：close(memfd) 不釋放 mm。probe2-B / probe3：
   pinned mm 在 30s+ 與 4096-file-burst 之後仍全數存活。→ 不能用作回收觸發；
   反過來利用：pin 一個 child 使其 slab 永不變空（守衛）。
3. **內核 stack 走 vmap-lazy**：free_task → vfree_atomic → lazy vmap list。list 超過
   ~32MB（lazy_max_pages）時一次性 purge（run26：phase-3 推過閾值 → 單波 +1456 個
   order-2 block）；另有 ~20-47 block 的小 burst 與 drain 同 wake（run23/27/28 的
   gate-window +16/+20/+47）。
4. **task_struct 走普通 RCU**（~0.3-0.5s grace），且其 slab 是 order-3（8 頁），
   對 o2（order-2）不可見——o2 的波只來自 stack 與 mm slab。

## 2. 現行 exploit 的回收序（v9.19）

「殺子進程序 = 回收序」：children 的 do_exit 依 kill 順序把 free_mm 的 lazy callback
排進 FIFO；drain 按 FIFO 執行 = 舊 v9.1 的 close 順序設計在 lazy 語意下的等價物。

- builders 288（nr_partial 的 18 個 1-inuse slab）→ early flood 192 → late flood
  剩餘 240 → post 16 → purge squad 659（把 vmap list 推過 ~32MB）→ settle（6-20s，
  o2+objs 雙安靜）→ dummy 觸發（0-450）→ pre+leak 最後殺（queue 尾）→ slabinfo
  active_slabs 的 **−1 dip**（leak slab discard 的唯一訊號）→ dip 當下 send。

## 3. 判定表（裝置實測）

| 觀察 | 意義 |
|---|---|
| slabinfo active_slabs −1（dip=1） | leak slab 被 slab_empty gate discard（進 buddy） |
| dip=0 + objs 大量下降 | drain 走完了但 leak slab 沒空（nr_partial<5 → park）或觸發不足 |
| gate-window o2 ±1 | 乾淨（send 取頭） |
| gate-window o2 +16~+47 | vmap purge 小 burst 埋了 leak slab |
| phase-3 o2 +1000~+1500 | purge 大波（dummy 把 vmap 閾值推過） |
| pselect ret=0 | walk 早期良性返回（頁內容非 payload） |
| vehicle diffs（0x0E08+） | payload 在位 + walk 寫入（成功！run 32/33）**（2026-09-13 更正：V6 §1.2 證明 payload 差 64 B、實際未落地；此處僅代表 walk 有跑）** |

## 4. 成功配置

```
GL_DEBUG_DRAIN=1 GL_SETUP_ATTEMPTS=8 GL_DUMMY_KILLS=0 --selftest
```
run 32/33 均 attempt 1 通過。成功路徑：settle 的 drain 停在 queue < 閾值時留下
~150-250 leftovers；phase-3 的 pre+leak 17 個 free + leftovers 自己把 flush 點火；
無 dummy 新 stack → 無 purge 洪流；leak slab 的 discard 是唯一的 −1 dip → send 乾淨取頭。

## 5. 已知殘餘（下一步）

- 成功率 ~30-50%/attempt（leftovers 大小的邊界運氣）+ 長開機後 pinned mm 累積
  （~5 slab/attempt）使後續 attempt 退化 → 每 attempt 前 reboot 或 cleanup 釋放守衛。
- ±64 pad 的 vehicle-diff 位置（0x0E08）待最後校準（GL_SKB_PAD 掃描）。
- W1/W2 未達（run 34/35 全敗）：payload 對齊修好後尚未在 mode-1 下驗證成功。
