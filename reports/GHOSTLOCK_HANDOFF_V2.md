# GhostLock RV101 — 工作狀態交接文件 v2
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> 本文件更新於 2026-08-16（第二任智能體工作結束時）。
> 第一任交接文件（§0-§7 全量）仍是有效背景，本文件**只記錄本輪新增/推翻的內容**。
> 工作目錄：`C:\Users\<USER>\Documents\codebase\rokid_root`

---

## 1. 本輪結論（一句話）

**reclaim 失敗的根因不再是「SLUB 不把 slab 還給 buddy」這類猜測；本輪用實測 + 原始碼 + 反組譯
定位出三個真實機制，並設計了尚未實測的 v3 回收方案。但同時發現一個更基礎的未解異常：
RV101 上 fork 的 mm_struct 分配/釋放計數異常（見 §4），必須先釐清。**

---

## 2. 新增的確定性事實（已驗證，勿重做）

### 2.1 SLUB 5.10.209 行為（原始碼已下載：`research/ghostlock_refs/slub_5.10.209.c`）
- mm_struct cache：order-2（16KB）、objsize 0x3C0、17 obj/slab、**min_partial=5**
  （`set_min_partial(s, ilog2(s->size)/2)` → ilog2(960)/2=4 → clamp 到 MIN_PARTIAL=5）、
  **cpu_partial=13**（set_cpu_partial：size≥256 → 13；單位是「free objects」）。
- **空 slab 被 discard 回 buddy 的唯一閘門**是 `n->nr_partial >= s->min_partial`（三處一致）：
  - `__slab_free()`：最後一個 object 被 free 且 slab 在 node list 上時（`goto slab_empty` 前）；
  - `unfreeze_partials()`：per-CPU partial 鏈被 drain 時，空頁且 nr_partial≥5 才 discard，否則
    **空頁進 node partial 永遠卡住**；
  - `deactivate_slab()`：M_FREE 路徑，同閘門。
- `put_cpu_partial()`：per-CPU partial 鏈（LIFO，頁鏈）在 `drain && pobjects > 13` 時整體
  unfreeze；新頁在 drain 之後才入鏈。
- **被其他 CPU 拿走**：node partial 上的頁（含空頁）會被任何 CPU 的 mm 分配
  （`get_partial_node`）凍結取走；per-CPU partial 鏈上的頁（frozen）其他 CPU 碰不到。

### 2.2 RV101 的 order-2（16KB）buddy 被「子程序的 kernel stack」汙染（關鍵！）
- ~~RV101 **沒有 VMAP_STACK**~~ → **2026-09-13 更正：本機 `CONFIG_VMAP_STACK=y`（`research/kernel_configs/kernel_config_1.22.009.txt`），此前提錯誤，§2.2 的 order-2 stack-pollution 機制不成立**：每個 fork 的子程序 kernel stack 直接從 buddy order-2 拿 16KB。
  實測：fork 544 個子程序，`/proc/pagetypeinfo` order-2 Unmovable 從 1872 → 47 → 1 → 0。
- 子程序被 kill 後，stack 經 **RCU 延遲釋放**（`put_task_struct_rcu_user`，幾十 ms 後在別的
  CPU 上）回到 buddy **LIFO 頭部**——這會把「最後才被 discard 的 leak slab」從 buddy 頭部擠掉。
  實測：kill 完 800 個子程序後，order-2 又憑空 +299 塊（RCU 回流）。
- ⇒ 舊版 exploit 的 key payload 每次都搶到「剛釋放的 stack 頁」而不是 leak slab——這是舊 reclaim
  「靜默吸收」的最可能解釋之一。

### 2.3 舊流程實測（slabprobe v1/v2，裝置詳情見 §6）
- 舊流程（pre/post/spray/prepare 大量 flood、leak-last、close(memfd_leak)）：
  - kill flood 確實讓大量 slab discard 回 buddy（mm slabs 57→26）；
  - 但 **settle 1.5s 後 close(memfd_leak) 沒有把 leak slab discard**（slabs 27→27 不動）——
    最後一次 free 時 `nr_partial < 5`（node partial 被系統其他 CPU 的 fork 消耗掉了），
    leak slab 變成「空頁卡在 node partial」→ 永久吸收寫入。
- key + 4×SKB vehicles 只吃掉 5 個 order-2 塊（都不是 leak slab）→ 讀回無寫入痕跡。
  這完全符合原手冊 §3 的「寫入被靜默吸收」現象。

### 2.4 反組譯驗證（產物在 `research/ghostlock_refs/rv101_disasm/`）
- `copy_mm` @0xffffffc0081508c0：每個 fork 無條件 `kmem_cache_alloc(mm_cachep
  [0xffffffc00aa6e158], 0xcc0)`（0xcc0 = GFP_KERNEL，無 __GFP_ACCOUNT）→ memcpy 0x3b0 → mm_init。
  非 CLONE_VM 時必然分配（`tbnz w0,#8` 才是 share 路徑）。
- `__mmdrop` @0xffffffc00814cd08：`kmem_cache_free(mm_cachep, mm)`
  （0xffffffc0083b6384）正常，無 RCU 延遲。
- `exit_mm` @0xffffffc008158f34：開頭 `futex_exit_release(current)`（0xffffffc0082336b4）+
  `mm_release`（0xffffffc00814d978）+ munmap 迴圈 + mmap_lock 操作 + `current->mm=NULL`。
  **尾部（mmput 呼叫處）尚未反組譯**：`0xffffffc0081590cc..0xffffffc008159280`。

---

## 3. v3 回收設計（已推導完畢、**尚未實測**，優先實作）

思路：**完全不做大 flood**；把 leak slab 弄到「per-CPU partial 鏈上唯一/最新的空頁」，
再用「anchord 自我 re-exec」觸發一次 drain，使 leak slab 成為該次 discard 中**最後一個被
free 回 buddy** 的頁，接著**立刻**噴灑 8 個 SKB vehicle（其中一個必然拿到它）。

步驟（在 `prepare_good_kernel_page()` 裡替換現有實作）：
1. fork pre(16) + leak child（KernelSnitch，pipe 回傳 mm）+ post(17)。
   → leak slab 17/17 滿 → post 的分配把它 deactivate 到 **node full list**。
2. kill pre(16) → leak slab 第 1 個 free → freeze → 進 **CPU0 per-CPU partial 鏈**（inuse=1）。
3. `close(memfd_leak)` → 最後一個 free（frozen free）→ **leak slab 空、停在鏈頭**。
   其他 CPU 完全碰不到它（鏈是 per-CPU 的）。
4. 起 anchord：`ghostlock_rv101 --anchor 16 <pipefd>`，pin CPU0；每輪 fork 17 個 pause
   子程序後 re-exec 自己（狀態放在 argv）。**每次 exec 會 free 一個「滿的」mm slab**
   （anchord 的舊 mm 所在 slab = 17/17 滿，在 full list → 1st-free → 進鏈）。
5. 第 15 次 exec 時 `pobjects > 13` 觸發 drain：14 個 E-slab（inuse=16）→ node partial
   （nr_partial ≥ 14 ≥ 5 ✓）；**鏈上唯一的空頁 = leak slab → discard-collect → discard 迴圈
   最後 free 它 → buddy 頭部**。
6. anchord 寫 pipe 通知 done；主程序**立刻**（中間不得有 printf/其他 syscall）
   `socketpair(AF_UNIX, SOCK_DGRAM)` × 8，每個 send 15872B payload（skb head kmalloc =
   15872+320=16192 ≤ 16384 → kmalloc_large order-2）。
   - 若 leak slab 與相鄰塊合併：8 個 vehicle 會把它拆開後逐塊拿到（覆蓋 ≤64KB 合併）。
7. payload 用「**page-view 佈局**」（見下）填 8 份相同內容；`fake_lock = slab_base + LOCK_OFF`
   （不再有 +0x18 delta；KEY_PAYLOAD_DELTA 對 SKB 不適用——SKB data 在 block+0）。
8. route 跑完後 `recv` 讀回 8 個 socket，與 pristine 比對：**有 diff 的那個就是拿到 slab 的
   vehicle**；forensic 照舊（child_pc/w0_right/usage/wait_lock/scratch）。
9. 不再用 key vehicle（key quota maxbytes=20000/uid → 只能 1 把 16KB key；SKB 無限制）。
   也不要保留 prepare/spray 大 flood（stack RCU 汙染來源）。

代碼修改點：`target_rv101.h`（SKB_DATA_DELTA=0、fake_* = base+OFF）、`util.c`
（prepare_good_kernel_page 重寫 + anchord mode + 8 socket 管理 + 全 vehicle 比對）、
`main.c`（--anchor 模式入口）。slabprobe 已含可參考的 anchord 骨架（`source_code/slabprobe/`）。

---

## 4. ⚠️ 未解異常：fork 的 mm_struct 計數（**必須先查**）

實測（RV101，重開機後、load 0.2 乾淨環境，工具 `source_code/slabprobe/forktest.c`）：
- fork 34 個子程序（libc `pause()`，子程序存活）：`mm_struct act` 每次只 **+17**（3 次一致：
  204→221、221→238、238→255）。
- fork 34 個子程序（**純 raw syscall 空轉**，/proc 確認 state=R 活著）：act **+0**！
- kill 掉所有子程序（waitpid 完成）：act **永不下降**（觀察 10s+ 不動）；系統自身
  `sh -c "exit 0"` 連跑 10 次也不動。
- 但同機 task_struct / signal_cache / pid 計數都正常移動（boot 期間 fork 測試 +30/+28/+32）。
- 反組譯證明 copy_mm 無條件 kmem_cache_alloc(mm_cachep)、__mmdrop 無條件 kmem_cache_free。
- v1 舊 boot（可能已被 0x80080000 alias 實驗打壞的環境）反而有「221 fork → +221、34 → +34」
  精確計數——所以異常**不是**恆常的。

優先實驗（按順序）：
1. **記憶共享測試（已寫好、已編譯在 `build/forktest`，未 push）**：子程序寫 global 變數後退出，
   父程序讀取。若父程序看到子程序寫的值 → **子程序其實在共享父 mm（CLONE_VM 生效?!）**，
   這會解釋一切（沒有新 mm 分配、沒有 free、order-2 全被 stack 吃掉）；若看不到 → 計數器/
   會計路徑問題。
2. 若共享 mm：查 clone 旗標傳遞（`syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0)` 在 aarch64
   的引數順序）＋對照 exploit 裡 `clone_child()` 同款呼叫是否中招。
3. 若各自有 mm：直接跑 slabprobe v3 流程實測（§3），若 reclaim 成功則異常不影響利用鏈
   （可能只是 slabinfo 計數怪）；失敗則反組譯 exit_mm 尾部 + mm_cache_init 旗標。

---

## 5. 環境注意事項（本輪新發現）

- **現在有兩台裝置同時接在 adb 上**：`<DEVICE_SERIAL>`（RV101，目標）與
  `<PHONE_SERIAL>`（Redmi Note 11 Pro，kernel 4.14，**不是目標**）。
  所有命令務必 `adb -s <DEVICE_SERIAL> ...`！
- RV101 剛重開機（uptime ~14min，乾淨狀態，mm_struct 187/187、11 slabs）——正好重跑 §4.1。
- `/sys/kernel/slab/*/*` 全部 Permission denied（shell）；`/proc/slabinfo`、`/proc/pagetypeinfo`
  可讀；dmesg 可讀（dmesg_restrict=0）。
- key quota：maxbytes=20000、maxkeys=200（每 uid 只能同時 1 把 16KB key）。
- ~~裝置無 VMAP_STACK（§2.2）~~ → **更正：有 VMAP_STACK（見上）**；`ulimit -p` 6608、nofiles 32768；adb shell cgroup 無 pids.max。

## 6. 本輪新增檔案

| 檔案 | 內容 |
|---|---|
| `source_code/slabprobe/{slabprobe.c,forktest.c,Makefile}` | 診斷工具（mm slab 生命週期觀測、fork 計數測試、anchord 骨架）；device 上 `/data/local/tmp/{slabprobe,forktest}` |
| `research/ghostlock_refs/slub_5.10.209.c` | 官方 5.10.209 mm/slub.c（回收機制對照） |
| `research/ghostlock_refs/rv101_disasm/{copy_mm,mm_init,__mmdrop,exit_mm}.txt` | 本輪新反組譯 |

exploit 原始碼（`source_code/ghostlock_rv101/`）本輪**尚未修改**。
