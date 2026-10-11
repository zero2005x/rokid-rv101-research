# GhostLock RV101 — 工作狀態交接文件 v3
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> 本文件更新於第三任智能體工作結束時（2026-08-16 深夜，session 延續）。
> v1/v2 交接（GHOSTLOCK_HANDOFF_V2.md、GHOSTLOCK_HANDOVER_PROMPT.md）仍為有效背景。
> 工作目錄：`C:\Users\<USER>\Documents\codebase\rokid_root`

---

## 1. 總結論（一句話）

**「freed mm slab 回收」已被 v9 方案解決（裝置上實測：leak slab 在 close(memfd) 瞬間經
slab_empty gate 同步 discard 回 buddy、768 個 SKB vehicle 確實把它拿走）；但 walk 第一次
真正跑到「我們的 fake page」上後開始 kernel panic——這是 v2 遺留的**從未驗證過**的區段
（v2 的 poison 證明只到「到達 forged waiter」為止，因為舊回收一直失敗，fake page 內容
從來沒被 walk 碰過）。回收問題已關閉，新阻塞點是 walk 在 fake page 上的 panic。**

---

## 2. 本輪已解決的事實（勿重做）

### 2.1 任務 A（fork mm_struct 計數異常）— 已結案
- forktest 判別實驗：子程序寫 global 後退出，父程序讀到 0x11111111 → **子程序有獨立 mm**
  （COW 正常），排除 CLONE_VM 假說。`syscall(SYS_clone, SIGCHLD, ...)` 的 aarch64 引數
  順序沒有問題。
- `exit_mm` 尾部反組譯完畢：**確實呼叫 mmput**（0xffffffc0081590ec: `mov x0,x20; bl
  0xffffffc00814d2b0`，無條件路徑），v2 的疑慮解除。
- 計數異常的解釋：SLUB per-CPU 隱形（cpu_slab page + per-CPU partial 鏈不計入
  /proc/slabinfo）+ **phantom slab**（本 kernel 的 allocation 慢路徑把 full CPU page 用
  DEACTIVATE_BYPASS 丟棄成「inuse=17、無任何 list」的 phantom 頁，永遠不進 full list，
  slabinfo 完全看不到；1st-free 時才 freeze 進鏈）。
- 另有批次化 free 現象（見 §4.3），但不影響 v9 設計（v9 用 memfd close 同步 free）。

### 2.2 任務 B（回收）— 已解決：v9 方案（在裝置上驗證）
實測證據（exploit 內 GL_DEBUG_DRAIN=1 的 pagetypeinfo）：
```
[after-flood-closes] Unmovable ... o2=2344
[after-leak-close]   Unmovable ... o2=2345   ← close(memfd_leak) 瞬間 +1 個 order-2 block
[after-vehicles]     Unmovable ... o2=1584   ← 768 個 SKB vehicle 拿走 -761 blocks
```
o2 +1 出現在 **order-2**（不是更高階）→ leak slab 沒被 buddy 合併 → **第 1 個 vehicle
（kmalloc-16k 直接吃 buddy 頭）拿到的就是 leak slab，payload 在 slab base+0**。

**v9 流程（util.c `prepare_good_kernel_page()`，現行代碼）**：
1. fork flood(544+221=765) + pre(16) + leak + post(17)。
2. **對所有 child open `/proc/pid/mem`**（memfd 釘住全部 mm）。
3. leak child 跑 kernelsnitch → 寫回 mm → 退出（memfd 持 ref，mm 不釋放）。
4. **kill 全部 child**（memfd 釘著 → 退出時 mm 不釋放，沒有「批次/延遲 free」問題）。
5. 預先建好 768 個 AF_UNIX DGRAM socketpair。
6. 依序 close memfd：pre → post → flood → **立刻 close(memfd_leak)（中間不要 settle！）**。
   最後一次 free 時 leak slab 是「空 slab 在 node partial 上、nr_partial ≥ 5」→
   `__slab_free` 的 slab_empty gate 在 close() 系統呼叫內**同步** discard → buddy。
7. 立刻 send 768 × 15872B（payload 相同、page-view 佈局、SKB data 在 block+0）。
8. 回傳 base（= page_base）；route 跑完後 recv 全部 vehicle 與 pristine 比對。

**為什麼 v3/v4/v5/v6/v7/v8 全失敗、v9 成功**（血淚教訓，勿再踩）：
- v3/v4/v5 靠 per-CPU 鏈的 pobjects>13 drain：**此 kernel 的 pobjects drain 實證從不觸發**
  （960 keepers ≈ 56 pobjects、2000 ≈ 117 pobjects，slabinfo 全程不動；見 §4.1）。
- v6/v7 靠 kill 後的 free：本 kernel 的 child mm free 有**批次化/延遲**特徵（§4.3），
  時序不可控。
- v9 的關鍵：**memfd 釘住 → kill 不釋放 → close() 才是 free**，時序完全可控；
  且 v2 已實證「flood 流程能把 leak slab 弄上 node partial」，v9 只修掉 v2 的
  「settle 1.5s 讓系統把 node partial 吃光」這一個錯誤（改為立刻 close）。
- 關鍵常數（實測）：mm_struct objsize=960、17 obj/slab、order-2；min_partial=5；
  cpu_partial=13（set_cpu_partial 的 2/6/13/30 表在 kmem_cache_open 反組譯中證實）。

---

## 3. 新阻塞點：punch 的 walk 在 fake page 上 kernel panic（待解）

### 3.1 現象
- 舊版本（回收失敗、slab A 是垃圾）：walk 走到 fake_lock（垃圾內容）後**良性終止、無
  panic**（垃圾內容碰巧通過 bail 條件）。
- v9（回收成功、vehicle 持有 payload）：**punch 的 walk 直接 panic，裝置重開機**
  （輸出停在 "pselect route ..."，沒有 "pselect returned"）。
- 每次 panic → 2-5 分鐘自動重開機、KASLR slide 改變（exploit 自動重洩漏）。

### 3.2 已做的二分（結果）
| 變體 | 結果 |
|---|---|
| GL_POISON=1（PWS_LOCK=0xdead…） | panic（預期：walk 到達 forged waiter 後鎖 0xdead → fault） |
| 正常 fake page | panic |
| GL_NOOWNER=1（owner=0，繞過 fake-task 區段） | panic（→ 崩潰在 erase/insert/鎖區段，不在 fake-task 區段） |
| **BLACK-root 修復**（W0 的 parent_color 0→1） | 仍 panic |

**BLACK-root 修復（已套用，保留）**：payload 原本把 W0 的 `parent_color` 設 0（註解還
寫「BLACK (0)」——錯的）。Linux rbtree `RB_BLACK=1`。pc=0 是「RED 且無 parent」，
rb_insert_color（0x7749e4）看到 RED parent 會走旋轉路徑讀 grandparent：`ldr x10,[x8,#8]`
with x8=0 → **NULL deref → panic**。已改 `put64p(..., 1)`（util.c 內有註解）。

### 3.3 walk 的完整反組譯解讀（已做完，可直接引用）
`rt_mutex_adjust_prio_chain` @ **0xffffffc0081ceaf4**（punch 路徑：
rt_mutex_adjust_pi @ 0x1ce9fc → `rt_mutex_adjust_prio_chain(task, chwalk=0,
orig_lock=NULL, next_lock=x20=waiter->lock, orig_waiter=NULL, top_task=task)`）。
關鍵流程（x19=task=A, x25=A->pi_blocked_on=forged waiter）：
1. 0x1ceb7c：`next_lock` 必須 == waiter->lock（兩者同源，恆真）。
2. 0x1cec00：x24 = waiter->lock = fake_lock → 0x1cec08 trylock（`bl 0x9409df0`，
   回 0 才走 yield/retry）→ **寫 [fake_lock+0]=1**。
3. 0x1cec3c：`(owner & ~1)` vs top_task(A)；owner=fake_task|1 ≠ A → 繼續。
4. 0x1cec54：x0 = lock->waiters.rb_leftmost（=w0）；0x1cec60 讀 [w0+0x38]=w0->lock，
   必須 == fake_lock（否則 0x1cf380 `brk #0x800` → panic）。
5. 0x1cecb0：`[forged] != forged` 且 `w0 != forged` → 0x1ceccc **rb_erase
   (&forged, &lock->waiters)**（rb_erase @ 0x774b38）→ **受控寫入原語**。
6. 0x1cecdc-0x1ced68：重插回樹（搜 w0 的 prio=130 決定左右）、`[forged]=w0`（pc）、
   `[w0+child]=forged`、rb_insert_color(&forged, root)（root=x1=x24+8 正確）。
7. 0x1cedd4：owner ≤ 1 → 0x1cf2d0 解鎖返回；否則 x19=fake_task → usage++（0x101）、
   fake pi_lock(0x1A5C) spin、pi_blocked_on=0 → 0x1cf238 乾淨結束。
8. 兩個 brk 陷阱：0x1cf380（w0->lock≠fake_lock）、0x1cf384（插入後 leftmost->lock
   ≠fake_lock）。

`rb_erase` @ **0x774b38**（case-1：left==NULL）：parent=pc&~3=target-8；
parent->left==node ? parent+0x10 : parent+8 ← child → **8-byte store of write_value at
target**；child->pc=pc（寫 SAFE_OFF）。已證實。
`rb_insert_color` @ **0x7749e4**：parent BLACK → 直接 ret；parent RED → 旋轉路徑
（pc=0 時會 NULL deref，見 §3.2 BLACK 修復）。

### 3.4 下一步建議（按順序）
1. **抓 panic 的精確位置**：`dmesg` 可讀（dmesg_restrict=0）！下次 selftest 時
   **並行開另一個 adb shell 跑 `dmesg -w`**，panic 訊息（BUG/oops/PC）在重開機前
   會被印出——這是本裝置唯一能拿到 panic 現場的方法（pstore 恆空、無 ramoops）。
2. 頭號嫌疑：**trylock helper（0x9409df0）與 unlock（0x940a2ac）**——若 kernel 開了
   CONFIG_DEBUG_SPINLOCK / PROVE_LOCKING，fake wait_lock（純 4-byte 零）可能觸發
   debug 檢查（bad magic / 未註冊 lockdep_map）。反組譯 0xffffffc009409df0、
   0xffffffc00940a2ac、0xffffffc00940a208、0xffffffc00940a0c8。
3. 若鎖沒問題，再用「可控 panic」二分 walk 後段：
   - rb_leftmost 改 0xdead…（payload 或 PWS 路徑）→ 若 panic 在 [0xdead+0x38] 讀取
     （0x1cec60）→ 確認走到該點；
   - PWS_TREE_LEFT 改非零 → 走 rb_erase case-2/3（vendor 版在 0x774b48/0x774bb8）；
   - 把 erase 的 child 指向非法位址（write_value=0xdead）看 store fault 是否如期。
4. selftest 通過判據（cleanup 的 forensic，現有代碼已印）：
   `child_pc=(target-8)|1`、`scratch=<寫入值>`、`fake_task_usage=0x101`、有 vehicle
   出現 diff 行。
5. selftest 穩定後 → W1（--write1，寫 selinux_state.enforcing=0 → getenforce
   Permissive）→ W2（FOPS 路線，見 §5）。

---

## 4. 實證異常記錄（未完全解釋，但 v9 已繞過）

### 4.1 pobjects drain 從不觸發（反組譯卻像 vanilla）
- put_cpu_partial @ **0x3b9fd4** 反組譯：`pobjects > cpu_partial([kmem_cache+0x2c])` 才
  unfreeze——vanilla 邏輯；set_cpu_partial（kmem_cache_open @ 0x3b8a6c 內）寫 2/6/13/30
  到 +0x2c；mm_struct(size 960) → **13**。
- 但實測：microtest fork 238（=14 slabs）全 kill → 14 個 1st-free → pobjects 應=14 > 13
  → slabinfo 的 mm_struct slabs **全程不動**（25→25→25，2 秒每 50ms 採樣）；960/2000
  keepers 也一樣。推測與 §4.3 的批次 free 有關（free 可能落在別顆 CPU 的鏈上），
  **不要再用 pobjects drain 設計任何東西**。
- `unfreeze_partials` @ 0x3bac58：vanilla（空頁且 nr_partial>=min_partial(+0x10) 才
  discard，否則進 node partial）。
- `__slab_free` @ 0x3b93bc：1st-free（prior==NULL）→ freeze → put_cpu_partial(page,1)，
  vanilla。
- `rcu_free_slab` @ 0x3bcab4 / `__free_slab` @ 0x3bcae4：discard 對無
  SLAB_TYPESAFE_BY_RCU 的 cache 走 __free_slab 直接回 buddy（mm_struct 不需等 RCU）。
- `___slab_alloc` @ 0x3b7264：慢路徑會 pop 鏈頭（empty 頁被 pop 時 get_freelist 會
  偽造 inuse=17——**設計上任何 mm 分配都不能發生在 leak slab 上鏈之後**）。

### 4.2 fork/kill 的 slabinfo 隱形
microtest：fork 238（14 個新 slab）→ slabinfo 完全不變；kill 後也不變；o2 反而
-14。解釋：新 slab 全成 phantom（DEACTIVATE_BYPASS），1st-free 進 per-CPU 鏈（隱形）。
⇒ 一切以 pagetypeinfo 的 order-2 為主要觀測手段。

### 4.3 批次化/延遲的 child mm free
v6 flood（850 kill + 每 kill 採樣）軌跡：mm slabs 62→62(200 kills)→57→43→29→28，
**成批 -5/-14/-14**。推測 exit 後的 mm 釋放有批次化路徑（worker/RCU 風格），因此
v6/v7 的「kill 後立刻 close」時序不可靠；v9 用 memfd 完全繞開。

---

## 5. W2 前置資料（FOPS 路線，perf 被擋 paranoid=3）

OnePlus 參考實作：`research/ghostlock_refs/extracted/oneplus/.../src/core/fops.c`。
RV101 kallsyms 已備齊（offset = addr - 0xffffffc008000000）：
- `ashmem_fops` @ 0x1e977a8（d）、`ashmem_misc` @ 0x29bcbb0（miscdevice，fops 在 +0x18）、
  ashmem_ioctl/mmap/open/release/show_fdinfo @ 0xc67e2c/0xc687e0/0xc68a04/0xc68a90/0xc68bb4。
- `configfs_file_operations` @ 0x1d46840、`configfs_bin_file_operations` @ 0x1d46960、
  configfs_write_bin_file$… @ 0x49ee00、configfs_read_bin_file$… @ 0x49ec40。
- `init_task` @ 0x2a1eb840、`init_cred` @ 0x2a202058、`selinux_state` @ 0x2aca0140
  （與 target_rv101.h 一致）、`commit_creds` @ 0x18b328。
設計（v1 交接）：W2 用 mode-2 寫入把 `fake_fops`（fake page 內的 file_operations 表）
寫進 ashmem_miscs[0].fops → 開 /dev/ashmem 的 read/write 變成 configfs 任意讀寫 →
從 init_task.tasks 走 task list 找 child → 用 mode-2 寫 child->cred(0x780)=
data_addr(INIT_CRED)。也可先只靠 configfs 任意讀找 child task（perf 不可用的替代）。

---

## 6. 本輪改動的檔案

| 檔案 | 內容 |
|---|---|
| `source_code/ghostlock_rv101/src/util.c` | **v9 回收**（memfd flood + 立刻 close + 768 SKB vehicles）；recv-diff forensic（只印有 diff 的 vehicle）；GL_NOOWNER 開關；**BLACK-root 修復**；poll_mm_slabs()（讀 slabinfo 的 mm_struct active slabs）；debug_pagetype()（GL_DEBUG_DRAIN=1） |
| `source_code/ghostlock_rv101/target_rv101.h` | SKB_DATA_DELTA=0、SKB_RECLAIM_SENDS=768、v9 註解 |
| `source_code/slabprobe/slabprobe.c` | v4 流程（keeper 池 + 稀疏 kill + 分相採樣） |
| `source_code/slabprobe/microtest.c` | **新增**：fork 238/kill/50ms 採樣（mm slabs + o2）的判別工具 |
| `research/ghostlock_refs/rv101_disasm/{slab_free_path,put_cpu_partial_real,__slab_free_real,exit_mm_tail,rt_mutex_adjust_prio_chain,rb_erase,rb_erase_tail,rb_insert_tail,kmem_cache_alloc}.txt` | **新增**：本輪反組譯產物（地址見 §3.3/§4） |

exploit 建置（勿忘 -B）：`$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'; cd source_code\ghostlock_rv101; mingw32-make -B`。
⚠️ PowerShell 陷阱：`mingw32-make -B 2>$null | Select-Object -Last 1` 會把 make 的
失敗碼吃掉（$LASTEXITCODE 變成管線最後一節的 0）——本輪因此 push 過一次 stale binary。
務必：建置後 `Get-FileHash build\ghostlock_rv101 -Algorithm MD5` 與裝置
`adb shell md5sum /data/local/tmp/ghostlock_rv101` 比對（現值：237c80dae80a6e970a2f4cac1aa3c86a）。

---

## 7. 環境注意事項

- 兩台裝置：目標 `<DEVICE_SERIAL>`（RV101）；`<PHONE_SERIAL>` 是 Redmi，別碰。
  所有 adb 帶 `-s <DEVICE_SERIAL>`。
- 每次 panic → 自動重開機（2-5 分鐘），KASLR slide 會變（exploit 自動重洩漏）。
  重開機後確認 binary 還在（/data 會保留；若有疑慮 md5 比對）。
- shell uid=2000、SELinux Enforcing、kptr_restrict=2、dmesg 可讀（dmesg_restrict=0）、
  pstore 恆空、perf 被擋、/sys/kernel/slab 全部 Permission denied。
- 關鍵可讀探針：/proc/slabinfo（mm_struct active slabs）、/proc/pagetypeinfo（order-2
  Unmovable = 16KB block 數）、dmesg -w（並行抓 panic 訊息）。
- 毒藥測試（GL_POISON/GL_POISON2）仍可用於二分 walk 進度；每次代價是一次重開機。
- key quota：maxbytes=20000（v9 已不用 key，全改 SKB）。ulimit -p 6608：kernelsnitch
  的 4096 執行緒 + flood 765 子程序 ≈ 5000，接近上限，別再加大 flood。
- GL_VEHICLES env 可調 vehicle 數（預設 768）；GL_NOOWNER=1 繞過 fake-task 區段（二分用）。
