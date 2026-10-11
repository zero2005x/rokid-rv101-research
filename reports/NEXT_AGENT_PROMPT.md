# 交接 Prompt — GhostLock RV101 (CVE-2026-43499) 第三任智能體
> ⚠ **2026-09-13 更新**：工作目錄已變更為 `/home/kali/rokid_root`（WSL/Kali；舊的
> `C:\Users\<USER>\Documents\codebase\rokid_root` 已不使用）。**執行期提權已完成且穩定**
> （bridge/Shizuku 兩條路徑、每 boot 以 adb 武裝一次），目前的維護重點是**維運層**
> （存活看守、引擎自動探測、提權度量），見 `docs/deploy.md` §提權維運 P0–P3。


> 把以下全文直接餵給下一個 AI 智能體。背景細節請它讀 `reports/GHOSTLOCK_HANDOFF_V2.md`
> 與原始交接文件（工作目錄根下的第一份 GhostLock 交接檔）。工作目錄：
> `C:\Users\<USER>\Documents\codebase\rokid_root`

---

你接手一個針對 **Rokid RV101（RG-glasses，Android 12，kernel 5.10.209-perf，
Qualcomm 4 核）** 的 **CVE-2026-43499（GhostLock）本地提權 exploit** 專案。
GhostLock 是 rt_mutex futex PI 的 stack-UAF：`remove_waiter()` 誤清
`current->pi_blocked_on`（而非 waiter 的），經 3-futex PI 環 + FUTEX_CMP_REQUEUE_PI 的
-EDEADLK 回滾觸發後，waiter 的 `pi_blocked_on` 懸空指向已死 rt_mutex_waiter；
利用 pselect 偽造 waiter + sched_setattr punch 達成受控寫入（rb_erase 的
`parent->rb_right = child`）。RV101 kernel 確認未修補（`remove_waiter.disasm.txt`）。

**專案現狀：exploit 已完成約 90%（KASLR 洩漏、KernelSnitch mm 洩漏、GhostLock 觸發、
pselect 偽造、punch、walk 到達 forged waiter——poison 測試已證明）。唯一阻塞點是
「freed mm slab 的回收（reclaim）」，修好後即可驗證 W1（selinux_state.enforcing=0 →
Permissive）→ 再完成 W2（cred=init_cred → root shell）。**

## 必讀檔案（先讀完再動手）

1. `reports/GHOSTLOCK_HANDOFF_V2.md` — 上一任的工作狀態（含新發現與 v3 回收設計）。
2. `source_code/ghostlock_rv101/target_rv101.h` — 所有 offset/常數唯一來源。
3. `source_code/ghostlock_rv101/src/{main.c,util.c,route.c}` — exploit 主體。
4. `source_code/slabprobe/{slabprobe.c,forktest.c}` — 上一任寫的診斷工具。
5. `research/ghostlock_refs/slub_5.10.209.c` — SLUB 回收機制原始碼對照。

## 環境

- 裝置經 adb：**序列號 `<DEVICE_SERIAL>`**。⚠️ 現在有第二台裝置同時接著
  （Redmi Note 11 Pro，`<PHONE_SERIAL>`，kernel 4.14，不是目標）——
  **所有 adb 命令一律加 `-s <DEVICE_SERIAL>`**。
- shell uid=2000、SELinux Enforcing、kptr_restrict=2、dmesg 可讀、perf 被擋（paranoid=3）。
- 裝置剛重開機（乾淨狀態）。每次 panic 會自動重啟（2-5 分鐘），KASLR slide 會變
  （exploit 會自動重洩漏）。
- 建置（Windows + NDK r27d）：
  ```powershell
  $env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'
  cd source_code\ghostlock_rv101
  mingw32-make -B
  adb -s <DEVICE_SERIAL> push build\ghostlock_rv101 /data/local/tmp/ghostlock_rv101
  adb -s <DEVICE_SERIAL> shell chmod 755 /data/local/tmp/ghostlock_rv101
  ```

## 任務清單（按順序）

### A. 先解開「fork 的 mm_struct 計數異常」（1 小時內應有結論）
上一任實測發現：fork 34 個活子程序，slabinfo 的 `mm_struct act` 只 +17 或 +0；kill 後永不
下降；但 task_struct/pid/signal_cache 計數正常；copy_mm/__mmdrop 反組譯確認都是無條件
kmem_cache_alloc/free(mm_cachep)。詳情與假設見 HANDOFF_V2 §4。

1. 先把已編譯但未 push 的記憶共享測試推上去跑（這是最關鍵的判別實驗）：
   ```powershell
   cd source_code\slabprobe
   mingw32-make -B build/forktest
   adb -s <DEVICE_SERIAL> push build\forktest /data/local/tmp/forktest
   adb -s <DEVICE_SERIAL> shell chmod 755 /data/local/tmp/forktest
   adb -s <DEVICE_SERIAL> shell /data/local/tmp/forktest
   ```
   判讀：若父程序讀到子程序寫的值（≠0x11111111）→ 子程序竟在共享父 mm（clone 旗標問題）
   → 修 clone 呼叫（改用 libc fork() 對照驗證）再重測；若父程序仍見 0x11111111 →
   計數器/會計異常，繼續第 2 步。
2. 反組譯 `exit_mm` 尾部（0xffffffc0081590cc 之後，找 mmput@0xffffffc00814d2b0 呼叫）
   與 `mm_cache_init`（0xffffffc00a063a10，確認 cache flags 是否有 SLAB_TYPESAFE_BY_RCU
   或怪旗標），用 `scripts/ghostlock_analysis/disasm_func.py`。
3. 若仍無解：直接跳到 B 實測 reclaim（異常未必影響利用鏈——只要 leak slab 能到 buddy）。

### B. 實作並驗證 v3 回收方案（本輪核心）
v3 設計完整推導見 HANDOFF_V2 §3。重點：
- 捨棄大 flood（子程序 kernel stack 的 RCU 延遲釋放會污染 buddy LIFO 頭——本裝置無
  VMAP_STACK）；改用 pre(16)+leak+post(17) 把 leak slab 放上 per-CPU partial 鏈，
  再用 anchord（`--anchor 16`，自我 re-exec 16 次、每輪 fork 17 個 pause 子程序）觸發
  一次 drain，使 leak slab 成為最後一個 discard 回 buddy 的頁。
- 立刻（中間零 printf/零 syscall）噴 8 個 AF_UNIX DGRAM socket（各 send 15872B，
  skb head kmalloc=16192 → order-2），payload 全部相同、用 page-view 佈局
  （fake 結構在 block+LOCK_OFF 等偏移，SKB data delta=0；fake_lock=slab_base+LOCK_OFF）。
- route 後 recv 全部 8 個 socket 與 pristine 比對，有 diff 者即拿到 slab。
- 先在 slabprobe 重現（改 slabprobe.c 或直接改 exploit 後用 `--selftest`），
  成功判據：selftest forensic 出現 `child_pc=(target-8)|1`、`scratch=<寫入值>`、
  `wait_lock=1`、`fake_task_usage=0x101` 等寫入痕跡（對照 target_rv101.h 的偏移）。

### C. W1：SELinux Permissive
selftest 穩定有 diff 後：`adb -s <DEVICE_SERIAL> shell /data/local/tmp/ghostlock_rv101 --write1`
→ `getenforce` 應回 Permissive。若無效且無 panic：檢查 target alias
（data_addr(SELINUX_STATE)=0xffffff802b4a0140）與 P0_KERNEL_PHYS_LOAD（0x80800000；
0x80080000 曾寫隨機 RAM 而 panic，勿用）。

### D. W2：cred=init_cred（FOPS 路線，perf 被擋無法用）
照原始交接文件 §5：把 fake fops 表寫入 ashmem_miscs[0].fops → configfs 任意讀寫 →
從 init_task.tasks 走 task list 找 child → task->cred(0x780)=data_addr(INIT_CRED) →
child 寫 enforce=0、exec /data/local/tmp/.ghostlock_root.sh。
所需符號從 `research/ghostlock_refs/rv101_kallsyms.txt` grep。

### E. 交付
- 單一 ELF `build/ghostlock_rv101`（uid 2000 下穩定取得 root shell）+ 使用說明
  （README 或報告），並把本輪發現補進 `reports/GHOSTLOCK_HANDOFF_V2.md` 或新開 v3。

## 驗證標準
1. selftest 出現 §B 的寫入痕跡 → reclaim 修復。
2. `getenforce` 回 Permissive → W1 成功。
3. `id` 回 uid=0 → W2 成功。
4. 穩定 = 連續 3 次完整流程成功、無 panic。

## 注意事項
- 毒藥測試（GL_POISON/GL_POISON2）會故意 panic，僅在需要二分 walk 進度時用。
- pstore 為空，無法靠 crash log 除錯；唯一探針是 poison 與 selftest diff。
- 改 target_rv101.h 後務必 `mingw32-make -B`。
- 別碰另一台 Redmi 裝置（所有 adb 都帶 `-s <DEVICE_SERIAL>`）。
