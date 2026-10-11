# GhostLock (CVE-2026-43499) RV101 Root Exploit — 交接 Prompt（給後續智能體）
> ⚠ **2026-09-13 更新**：工作目錄已變更為 `/home/kali/rokid_root`（WSL/Kali；舊的
> `C:\Users\<USER>\Documents\codebase\rokid_root` 已不使用）。**執行期提權已完成且穩定**
> （bridge/Shizuku 兩條路徑、每 boot 以 adb 武裝一次），目前的維護重點是**維運層**
> （存活看守、引擎自動探測、提權度量），見 `docs/deploy.md` §提權維運 P0–P3。


> 本文件是完整的「接手指令」。請直接以本文件為起點繼續工作，勿重做已完成的分析。
> 工作目錄：`C:\Users\<USER>\Documents\codebase\rokid_root`

---

## 0. 任務總覽

為 **Rokid RV101（RG-glasses，代號 neo，Android 12，Qualcomm QCM4290 級 SoC，4 核心）**
撰寫 **CVE-2026-43499（GhostLock）本地提權 exploit**，取得 root。
kernel：`5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201`（boot.img 與裝置一致）。
裝置經 adb 連線（serial `<DEVICE_SERIAL>`），shell uid=2000、SELinux Enforcing、
kptr_restrict=2、dmesg_restrict=0、seccomp=0、**perf_event_paranoid=3（perf 被擋，實測 EACCES）**。

**現狀：exploit 鏈已完成約 90%，最後一道障礙是「fake kernel page 的 slab 回收（reclaim）」。
修好 reclaim 後即可驗證 W1（SELinux permissive）→ 再做 W2（cred = init_cred）。**

---

## 1. 已完成的研究成果（勿重做）

### 1.1 GhostLock 漏洞本質（已確認）
- CVE-2026-43499：rt_mutex futex PI **stack-UAF**。`remove_waiter()`（kernel/locking/rtmutex.c）
  錯誤地清除 `current->pi_blocked_on` 而非 `waiter->task->pi_blocked_on`。
  觸發：3-futex PI 環 + `FUTEX_CMP_REQUEUE_PI` 造成 -EDEADLK 回滾在 **requeue 方（非 waiter 方）
  的 context** 執行 → 真正 waiter（A）的 `pi_blocked_on` 懸空指向 A kernel stack 上已死的 `rt_mutex_waiter`。
- 修復 commit：mainline `3bfdc63936dd`（7.1 修復）；影響範圍 2.6.39~7.1；Android GKI 6.12.x 當時仍未修。
- 修復 diff 全文見 `research/ghostlock_refs/extracted/openvz_patch/.../CVE-2026-43499-vzkernel3.10.patch`。
- **RV101 的 kernel 經反組譯確認未修補**：`remove_waiter` @ `0xffffffc0081cffb0` 使用
  `mrs x20, sp_el0`（current）+ `str xzr, [x20, #0x888]`（current->pi_blocked_on = NULL），
  完全沒有 deref `waiter->task`（[waiter+0x30]）。證據：`research/ghostlock_refs/remove_waiter.disasm.txt`。

### 1.2 成功案例（網路研究，參考碼已在本地）
`research/ghostlock_refs/extracted/` 下已 clone（zip 解包）：
- **OnePlus** `oneplus/GhostLock-for-OnePlus-main/`（6.12 SM8845，鎖 BL 提權，兩階段寫入，verified）
- **Honor** `honor/cve-2026-43499-honor-main/`（6.1.128 SM8650，含 docs/report.md、exploit 源碼）
- **POCO** `aresin/ghostlock-aresin-main/`（4.14 MT6893，觸發細節與 KernelSnitch）
- 其他：`emerald/`、`m3q/`、`selinuxdis/`（SELinux disabler 思路）
- 原始研究：NebuSec CyberMeowfia「IonStack」系列。

### 1.3 RV101 kernel 靜態分析（全部完成，產物在 `research/ghostlock_refs/`）
- kallsyms 全量提取：`rv101_kallsyms.txt`（176,575 symbols；工具 vmlinux-to-elf，pip 可裝）。
- 反組譯腳本：`scripts/ghostlock_analysis/disasm_func.py`（capstone 5.0.7）；關鍵函數反組譯在
  `rv101_disasm/*.txt`（rt_mutex_adjust_prio_chain、task_blocks_on_rt_mutex、futex_*、core_sys_select、
  hash_futex、get_futex_key、__sched_setscheduler、commit_creds、sel_write_enforce 等）。
- 原始 kernel：`binaries/extracted_1.22/kernel`（未壓縮 ARM64 Image，`ARMd` magic @0x38，
  link base 0xffffffc008000000）。
- 所有推導出的 offset/常數已集中於 `source_code/ghostlock_rv101/target_rv101.h`（以下皆為 disasm 驗證值）。

### 1.4 關鍵地址空間事實（實測驗證）
- **VA_BITS = 39**（洩漏的 mm 在 `0xffffff80..`、kernel 映像 runtime 在 `0xffffffde..` 級）。
- `PAGE_OFFSET = 0xffffff8000000000`、`PHYS_OFFSET = 0x80000000`（/proc/zoneinfo start_pfn=524288）、
  `KIMAGE_TEXT_BASE = 0xffffffc008000000`、`P0_KERNEL_PHYS_LOAD = 0x80800000`
  （TEXT_OFFSET=0x800000；**0x80080000 曾導致 W1 寫入隨機 RAM 而 panic，0x80800000 不 panic——
  但尚未成功翻轉 enforcing，原因可能是 reclaim 失敗而非 alias 錯，見 §3**）。
- `futex_hash`（private）= `jhash2({mm_lo,mm_hi,addr_lo,addr_hi}, 4, addr & 0xfff)`，
  bucket = hash & (hashsize-1)，hashsize=1024（4 核）。**注意詞序是 mm 在前、無 |2 flag**
  （|2 是 shared futex 匿名映射路徑專用）。
- `mm_struct` slab：**order-2（16KB）**、objsize **0x3C0**、17 obj/slab（/proc/slabinfo 實測）。

### 1.5 利用鏈各環節狀態
| 環節 | 狀態 | 證據/位置 |
|---|---|---|
| KASLR 洩漏（printk_formats） | ✅ 完成 | `/sys/kernel/tracing/printk_formats` 可讀，錨點 "Rescheduling interrupts"@0x1c2ac0c、"Function call interrupts"@0x1cca100；每次重啟 slide 會變，重洩漏即可 |
| KernelSnitch mm 洩漏 | ✅ 完成 | futex hash 碰撞計時（3/3 collisions 穩定）+ 線性映射窮舉命中（single_matches≈1.2萬、mm=FOUND） |
| GhostLock 觸發 | ✅ 完成 | requeue 回傳 -EDEADLK（errno 35）穩定復現 |
| pselect 偽造 | ✅ 完成 | `rt_waiter = stack_fds + 0x30`（frame 數學見 target_rv101.h 註解）；PWS_* word 映射已正確 |
| punch（sched_setattr→rt_mutex_adjust_pi） | ✅ 完成 | 反組譯確認 pi=true 路徑；punch_done 同步旗標避免 unlock clobber 偽造位元組 |
| walk 到達 forged waiter / rb_erase 執行 | ✅ 證明 | **poison 測試**：`GL_POISON=1`（lock=0xdead→trylock crash）與 `GL_POISON2=1`（tree_pc=0xdead→erase parent deref crash）都導致 kernel panic 重啟 → walk 確實到達 |
| **fake page 回收（reclaim）** | ❌ **唯一障礙** | freed mm slab 未被子噴灑載體取得（keyctl_read 讀回零寫入痕跡） |
| W1（selinux_state.enforcing=0） | ⏸ 待 reclaim 修復後驗證 | target = data_addr(selinux_state)=`0xffffff802b4a0140`（enforcing byte @+0，經 sel_write_enforce 反組譯確認） |
| W2（cred=init_cred） | ⏸ 待做 | perf 被擋 → 需 FOPS 路線（見 §5） |

---

## 2. exploit 代碼現狀

位置：`source_code/ghostlock_rv101/`
- `target_rv101.h` — 所有 offset/常數（含 PWS_* 映射、KEY_PAYLOAD_DELTA=0x18 等）
- `src/main.c` — 流程：`--selftest`（寫入自己的 sprayed page）/ `--write1` / 完整流程（W1→W2 骨架）
- `src/route.c` — 觸發執行緒 + pselect 偽造 + consumer punch + punch_done 同步
- `src/util.c` — KASLR 洩漏、KernelSnitch、mm 噴灑、**key-payload 回收**、perf（被擋）、forensic
- `src/perf_probe.c`、`Makefile`

建置（Windows，NDK r27d）：
```powershell
cd source_code\ghostlock_rv101
$env:ANDROID_NDK_HOME='C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\android-ndk-r27d'
mingw32-make -B
adb push build\ghostlock_rv101 /data/local/tmp/ghostlock_rv101
adb shell chmod 755 /data/local/tmp/ghostlock_rv101
```
執行：`adb shell /data/local/tmp/ghostlock_rv101 --selftest`（安全驗證）或 `--write1`。
環境變數：`GL_POISON`、`GL_POISON2`（毒藥測試）、`W1_ATTEMPTS`、`SELFTEST_RUNS`。

**重要注意**：
- 每次 kernel panic 後裝置自動重啟（約 2-5 分鐘），用 `adb wait-for-device`；slide 會變，KASLR 會自動重洩漏。
- pstore 為空（panic 不留痕跡），無法靠 crash log 除錯——只能靠 poison 二分法。
- 修改 `target_rv101.h` 後務必 `mingw32-make -B`（header 有依賴規則，但 -B 最保險）。

---

## 3. 當前阻塞點：reclaim 失敗（重點任務）

**現象**：selftest 模式（寫入目標 = 自己噴灑的頁）跑完後，`keyctl_read` 讀回的 payload 與
pristine 副本**逐位元組相同**（無任何 kernel 寫入痕跡：child_pc=0、wait_lock=0、usage=0x100 不變）。
而 poison 測試證明 walk 有到達 forged waiter 並執行到 rb_erase。→ 結論：**key payload 沒有取得
freed mm slab**（寫入落到了仍在 mm_cachep 手裡的舊 slab 內容上，被靜默吸收）。

**已嘗試的順序**：
1. SKB sendmsg 回收（~16KB payload → kmalloc-16k）＋各種釋放順序 → 失敗。
2. key payload 回收（`add_key("user",..., 16360B, KEY_SPEC_PROCESS_KEYRING)`，
   data 位於 kmalloc object+0x18，`KEY_PAYLOAD_DELTA=0x18`）＋ flood 大量 mm slab → 仍失敗。
3. 釋放順序：leak slab 先放 vs **最後放**（現版本為 leak-last：先關 pre/post/spray/prepare 的
   memfd 填滿 node partial，最後 close(memfd_leak) 觸發 discard）→ 上次測試仍無寫入。

**下一步建議（按優先序）**：
1. **大量迴圈跑 selftest**（`SELFTEST_RUNS=20`）觀察是否有幸運命中（difff 出現即證明 reclaim 成功，
   立即轉 W1）。注意每輪會重新噴灑，統計上只要釋放路徑偶爾把 leak slab 丟回 buddy 就能命中。
2. **觀測 buddy 變化**：/proc/pagetypeinfo 的 order-2 free 計數在 flood 前後是否上升；
   若完全不升 → SLUB 把 slab 留在 partial（調大 flood：`MM_PARTIALS`、prepare 數量放大 3-5 倍）。
3. **換 reclaim 載體**（kmalloc-16k 家族，依 kmalloc size 精確落入 order-2）：
   - `setxattr` 大 value（Android 有上限，先試）、
   - `sendmsg` 調整 payload 使 skb head 恰好 ≤16KB（skb_shared_info 開銷約 +0x200）、
   - BPF ringbuf/array map（`unprivileged_bpf_disabled=0`，但需 verifier 可行）、
   - `keyctl` big_key 類型（注意 quota：`/proc/sys/kernel/keys/`）。
4. **反向定位 reclaim 載體的實體位址**：若載體本身可讀回（key 可、socket 可 recv），先確認
   載體內容是否被 walk 寫過；若無 → 載體沒拿到 slab；若有但偏移怪 → 校準 SKB_DATA_DELTA/KEY_PAYLOAD_DELTA。
5. 若 reclaim 徹底無解：評估「不噴灑」的替代——把 fake lock/fake task 放到**已知線性位址的真實物件**
   （如 init_task.pi_waiters 區域附近）不可行（會破壞 init_task）；或改用 OnePlus SLIDE 路線的
   boot_id 洩漏式寫法（僅 KASLR，無助於本問題）。

---

## 4. W1 驗證（reclaim 修復後立即執行）

1. selftest 的 forensic 應出現：`child_pc=(target-8)|1`、`w0_right=<stack waiter addr>`、
   `fake_task_usage=0x101`、`wait_lock=1`、`scratch=<寫入值>` → 證明整條原語正確。
2. `adb shell /data/local/tmp/ghostlock_rv101 --write1` → 期望 `getenforce` 變 **Permissive**
   （/sys/fs/selinux/enforce 讀回 0）。
3. 若 enforcing 沒翻轉且無 panic：檢查 target alias（`0xffffff802b4a0140`）與
   `P0_KERNEL_PHYS_LOAD`（候選：0x80800000、0x80080000、0x80000000；panic 即 alias 錯）。
   判據：panic = 寫進非映像 RAM；無效且無 panic = walk 未寫或 reclaim 問題。

---

## 5. W2 計畫（perf 被擋後的 FOPS 路線）

- `perf_event_open` 實測 **EACCES**（paranoid=3），W2 無法用 OnePlus 的 perf_find_task。
- 採用參考專案的 **FOPS 路線**（OnePlus fops.c / Honor root.c）：
  1. 用 W1 同一寫入原語把 **fake file_operations 表位址**寫入 `ashmem_miscs[0].fops`
     （data_addr(ASHMEM_MISC_FOPS)，從 kallsyms 找 RV101 的符號）。
  2. fake fops 表放於噴灑頁：`llseek=noop_llseek`、`read_iter=configfs_read_iter`、
     `write_iter=configfs_bin_write_iter`、其餘 ashmem 原函數（符號從 rv101_kallsyms.txt 查）。
  3. 透過 ashmem `SET_NAME` blob 控制 configfs 欄位 → 任意 kernel 讀寫
     （configfs_write_once/configfs_read_once，見 OnePlus util.c）。
  4. 從 `init_task.tasks` 走 task list（需 `TASK_TASKS_OFF/TASK_PID_OFF/TASK_TGID_OFF`，
     用 `__arm64_sys_getpid`/`sys_kill` 等反組譯推導，或直接寫 configfs 讀取驗證）找 child task_struct。
  5. `task->cred`（0x780）← data_addr(init_cred)（或 patch 原 cred：uid/caps/sid），
     並清 seccomp（shell 無 seccomp 可略）。
  6. child 寫 `/sys/fs/selinux/enforce 0`、exec `/data/local/tmp/.ghostlock_root.sh`（已內建）。
- 所需符號位址直接從 `research/ghostlock_refs/rv101_kallsyms.txt` grep。
- 亦可先探索：W1 成功後（permissive）是否有更簡單的提權捷徑。

---

## 6. 關鍵檔案索引

| 檔案 | 內容 |
|---|---|
| `binaries/extracted_1.22/kernel` | 未壓縮 ARM64 Image（分析源） |
| `research/ghostlock_refs/rv101_kallsyms.txt` | 全量符號表 |
| `research/ghostlock_refs/rv101_disasm/*.txt` | 關鍵函數反組譯 |
| `research/ghostlock_refs/extracted/{honor,oneplus,aresin,...}/` | 參考 exploit 源碼與報告 |
| `research/ghostlock_refs/remove_waiter.disasm.txt` | 未修補證據 |
| `scripts/ghostlock_analysis/disasm_func.py` | capstone 反組譯工具 |
| `source_code/ghostlock_rv101/` | 本 exploit（見 §2） |
| `reports/REVERSE_ANALYSIS_1.22_009.md` 等 | 既有設備逆向背景（fastboot 閹割、EDL 等） |

---

## 7. 給接手智能體的注意事項

1. **不要重做** §1 的分析；先跑一次 `--selftest` 熟悉輸出與流程。
2. 裝置每次 panic 會重啟（正常，勿慌）；重啟後 slide 改變屬正常。
3. poison 測試是唯一可靠的 walk 進度探針（pstore 不可用）。
4. 修改常數後務必 `mingw32-make -B`；`target_rv101.h` 是所有 offset 的唯一來源。
5. 驗證標準：selftest forensic 出現 §4 的寫入痕跡 → reclaim 修復；`getenforce` 回 0 → W1 成功；
   `id` 回 uid=0 → W2 成功。
6. 最終目標交付：能在 adb shell（uid 2000）下執行、穩定取得 root shell 的單一 ELF（`ghostlock_rv101`），
   並附上使用說明與研究總結（成功案例歸納見 `research/ghostlock_refs/` 各 repo README 與本文 §1.2）。
