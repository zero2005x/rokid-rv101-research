# GhostLock RV101 — 工作狀態交接 v11（第十一任：mm-leak flake 根因 + blind-write 攻堅）
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> 更新於 2026-08-22。本輪（第十一任）在實機（<DEVICE_SERIAL>）上完成/發現：
> 1. **定案 init_cred->uid = 0**（從 kernel Image 直接 dump，非推測）。
> 2. **發現 blind cred write（v9.50 主路徑）失敗：child uid = -128 = 0xFFFFFF80**，
>    一致於 run_r21 全部 10 輪。
> 3. **定位 mm-leak（kernelsnitch）flake 根因 = EAGAIN（errno=11）**：fork/pthread_create
>    在 ~2000 個 task 處就資源耗盡，遠低於 RLIMIT_NPROC=6608，**與 reclaim 無關**。
> 4. 加了兩項診斷程式碼（init_cred.usage before/after + clone/pthread errno 擷取）。

---

## 0. 最關鍵結論（先讀）

> **【第十一任後半重大突破 — -128 根因已定案，且 blind write 其實有落地！】**
> 並行代理的 run_r23_v952（11:23，成功 LANDED + 拿到完整 DIAG）證明：
> - `init_cred.usage before=0x4 → after_cred=0xffffff8039c262b9 after_real=0xffffff8039c262b1`
>   （= `(task+0x778)|1` / `(task+0x770)|1`）→ **mode-2 write#1 與 write#2 都確定落地**，
>   `task->cred = init_cred` 確實寫進去了。`prev==prev2 stable=1`（無 tail race）。
> - **-128 的真正根因**：這顆 kernel 的 `struct cred.usage` 是 **4-byte `atomic_t`**（不是
>   mainline 的 8-byte `atomic_long_t`），所以 **uid 在 offset +0x04**（不是 +0x08）。
>   mode-2 的 write#2 = `str x9,[x10]` 把 8-byte `tree_pc` 存到 `init_cred+0`，因此
>   **clobber 掉 uid**：`init_cred+4..7 = tree_pc>>32 = 0xffffff80 = -128`。
>   也就是 **child uid=-128 不是「write 沒落地」也不是「task 錯」，而是 write#2 把
>   init_cred->uid 打爆成 -128**。
>
> **【第十一任 v9.56 — repair 的正解：write_value=0 直接跳過 write#2】**
> 反組譯定案：rb_erase@0x774bb0 是 `cbnz x8, 0x774d84`（write#2 的 gate）。**只要
> write_value（=x8）= 0，write#2 就被跳過**，而 write#1 仍會 `str x8,[x11]` 把 8-byte 0
> 寫進 target。所以 repair 應該用 `set_pselect_write_mode(init_cred+0x04, 0, 4)`：
> - write#1 = `init_cred+4 = 0` → **uid(+4)=0 且 gid(+8)=0**。
> - write#2 **被跳過**（x8=0，cbnz 不跳）→ **完全避開 phys 0x80000000 的 fault**。
>
> 這取代並行代理 v9.52 的「write_value=0xffffff8000000000（寫 phys 0x80000000，會 fault）」
> 與 v952b 的「0xffffff8010000000（uid 變 0x10000000，非 0）」兩條死路。**v9.56 尚未實測**
> （run_r29 卡在 mm-leak EAGAIN，見下）。

---

## 0. 最關鍵結論（先讀）

### 0.1 init_cred->uid 已定案 = 0（排除「init_cred 非 root」假說）

直接 dump `binaries/extracted_1.22/kernel` 的 init_cred（link=0xffffffc00a202058，
file offset=0x02202058）：

```
init_cred bytes @ 0x2202058:
  +0x00: 0x0000000000000004   (usage=4)
  +0x08: 0x0000000000000000   (uid=0, gid=0)
  +0x10: 0x0000000000000000   (suid=0, sgid=0)
  +0x18: 0x0000000000000000   (euid=0, egid=0)
```

所以 **若 mode-2 write#1 真的落地（task->cred = init_cred），child getuid() 必回 0**。
child uid = -128 表示 **write#1 沒有落地（或 task 指標錯）**，不是 init_cred 的問題。

### 0.2 blind cred write（v9.50）一致失敗：child uid = -128 = 0xFFFFFF80

run_r21（v9.50，10 輪）每輪都 `child uid = 4294967168` = 0xFFFFFF80 = -128（signed）。
這是**系統性**失敗，不是 race（race 應隨機）。init_cred->uid=0 已排除「init_cred 錯」。
剩餘兩可能：
1. **reclaim 失敗 → fake page 內容不在 leak slab → walk 讀 garbage → 沒走到 rb_erase**
   → write#1 沒落地。（run_r21 的 mode-2 write 大多 `dip=0 calm=364` = leak slab 沒 discard，
   即便 round1 `dip=1`，`cleanup: 0 diffs` 也代表 SKB 沒吃下 leak slab。）
2. task 指標錯（`prev - TASK_TASKS_OFF` 指向非 child task）。

**尚未有 DIAG 資料可區分**（因為 mm-leak flake 讓我跑不到 DIAG）。DIAG 已寫好（見 §2），
只要一次 mm-leak 成功的 boot 就能跑出來。

### 0.3 mm-leak（kernelsnitch）flake 根因 = EAGAIN（errno=11），不是 reclaim

加了 errno 擷取後，失敗 boot 的證據：

```
v9.13 diag: dummy clones ok=125 fail=325 (first errno=11 Try again)
kernelsnitch: ks_increase 4096 threads, 3040 failed (first err=11)
Threads: 1057   ← 只有 1056 個 thread（不是 4097）
```

`errno=11` = EAGAIN。對 clone/pthread_create 而言 = RLIMIT_NPROC 或 thread/pid 資源上限。
實測：fail 點總 task 數 ≈ 765(flood)+16+1+17+125(dummy)+1056(threads) ≈ 1980，
**遠低於 RLIMIT_NPROC=6608、threads-max=13217、pid_max=32768**，也無 cgroup pids 控制器
（`/sys/fs/cgroup/cgroup.controllers` 空、無任何 `pids.max`）。所以是哪條上限被打到尚待
kernel 側定位（`Committed_AS` 28.5GB >> `CommitLimit` 2.48GB 過度 commit 是另一個嫌疑，但
那通常回 ENOMEM=12，不是 EAGAIN=11）。

**實務觀察（5 次 boot）**：mm-leak 成功 3 次（run_r21@396s、run_r22@397s、selftest2@454s，
`Threads:4097`）、失敗 2 次（selftest1@522s、run_r24@405s/run_r25@548s，`Threads:1043~1057`）。
成功 boot 幾乎都在 **~396-397s uptime**；405s 起常失敗。**建議每次都在 ~380-400s uptime 跑，
mm-leak 一失敗就直接 reboot 重來，不要在同 boot 重試 6 次。**

**v9.55 fork-limit 診斷（定案 flake 的變數）**：在第一次 clone 失敗時 dump
`RLIMIT_NPROC=6608/6608`、`loadavg`、`threads-max=13217`、`pid_max=32768`。關鍵證據
（run_r28 @770s、run_r29 @407s）：
```
loadavg=... 39/7839 ...   ← nr_threads = 7839（系統總 thread 數）
```
也就是 **mm-leak EAGAIN 的真正變數是「系統總 thread 數 nr_threads」**，不是 uptime：
- 好 boot（run_r21/r22/r23_v952）：nr_threads 低（~1241），mm-leak 的 ~5300 個 task 塞得下。
- 壞 boot（run_r28/r29）：nr_threads 高（**~5850 baseline，甚至 407s 就有 7841**），
  fork 在 ~1950 個 task 處就 EAGAIN。壞 boot 的 loadavg 還有 **24~91 個 runnable**（camera
  服務卡在 CAM-ISP 錯誤迴圈），是裝置狀態問題，不是 exploit bug。
- 實務對策：**跑之前先 `cat /proc/loadavg`，nr_threads < ~3000 才是好 boot**；>5000 直接
  reboot 重來。或考慮把 APPENDED_FUTEXES 4096 → 1024 讓 mm-leak 在壞 boot 也塞得下（未試，
  會弱化 collision 訊號）。

---

## 1. 本輪程式碼變更（`source_code/ghostlock_rv101/`）

- `src/main.c`：
  - **v9.51 DIAG（round loop）**：讀 `init_task.tasks.prev` 兩次（偵測 tail race），
    並在每個 mode-2 write 前後讀 `init_cred.usage`（`kernel_read64(fd, data_addr(INIT_CRED))`）：
    - `ic0`（write 前）、`ic1`（寫 cred 後）、`ic2`（寫 real_cred 後）。
    - write#2 會把 `tree_pc=(target-8)|1` 存進 `init_cred.usage`，所以 `ic1 != ic0` 就
      **證明 rb_erase walk 真的落地**（write#1 與 write#2 在同一 rb_erase 呼叫，必同生）。
    - child uid 改印 `%u (0x%08x)` 以便看 signed/unsigned。
  - （前一任已加）`v9.50 DIAG`：FOPS installed 後讀 init_cred@0x00/@0x08/@0x1c。
- `src/util.c`：
  - `clone_child` 呼叫方（dummy 區塊）擷取第一個 clone 失敗的 errno。
  - `ks_increase` 擷取 pthread_create 失敗數與第一個 errno。
  - **v9.54**：reclaim gate_poll 從 `usleep(3000)` 改 `usleep(300)`（iteration 60→600、
    300→3000，維持 ~180ms/~900ms 覆蓋），把 leak slab discard 後的 SKB 搶頭竊取窗從 ~3ms
    縮到 ~300us。**尚未驗證**（run_r27 在 mm-leak 就先 EAGAIN，沒進 reclaim）。

### repair 的 write#2 風險（並行代理 run_r23_v952 的 panic 點）

repair（mode-4 寫 `init_cred+0x04`）的 write_value 必須「低 32 位元 = 0」才能把 uid 寫成 0。
核心裡低 32 位元 = 0 且落在 RAM 內的位址**只有** direct-map base `0xffffff8000000000`
（= phys 0x80000000，zoneinfo start_pfn=524288）。所以 repair 的 write#2 會寫進 **phys
0x80000000（RAM 第一頁）**，run_r23_v952 就在 repair punch 當下 panic（最可能第一頁 reserved
fault，也可能是資源 spike / 併發 fake_fops，未二分）。並行代理 v952b 把 value 換
`0xffffff8010000000`（phys 0x90000000）想避開第一頁，但那會把 uid 寫成 0x10000000（非 0）。

**重建/推檔**（Linux 下用 WSL 的 Windows NDK，bit-identical 已驗證）：
```bash
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root/source_code/ghostlock_rv101
NDKBIN="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin"
"$NDKBIN/clang.exe" --target=aarch64-linux-android31 -O2 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare -fno-stack-protector -fPIE -Isrc \
  -include target_rv101.h -o build/ghostlock_rv101 src/main.c src/util.c src/route.c \
  -pie -static-libgcc -pthread
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
"$ADB" -s <DEVICE_SERIAL> push build/ghostlock_rv101 /data/local/tmp/ghostlock_rv101
"$ADB" -s <DEVICE_SERIAL> shell "chmod 755 /data/local/tmp/ghostlock_rv101"
```

---

## 2. 下一輪待辦（按優先序）

1. **拿 DIAG 資料（唯一阻塞 blind-write 根因）**：在 mm-leak 成功的 boot（~396s uptime）跑
   ```
   GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 \
     /data/local/tmp/ghostlock_rv101
   ```
   看兩行關鍵輸出：
   - `DIAG init_cred=... usage=... @0x08=...`（應 @0x08=0，確認 init_cred->uid=0）。
   - `init_cred.usage before=... after_cred=... after_real=...`：
     - **若 `after_cred != before`（變成 `(task+0x778)|1` 形式）→ write#1/write#2 都落地**，
       那 child uid != 0 就是 **task 指標錯**（race/TASK_TASKS_OFF 錯），往 task 定位攻。
     - **若 `after_cred == before`（都是小 refcount）→ walk 沒走到 rb_erase**（reclaim 失敗
       或 mode-2 walk 系統性不走 rb_erase），往 reclaim/mode-2 攻。
2. **降低 mm-leak 資源需求 / 找替代**（見 §3 三個方向）。
3. **reclaim 穩定**（原本 Direction 1）仍要：`GL_PURGE_CNT=0 GL_TAIL_KILLS=16` 是對的，
   但必須先過 mm-leak 這關。

---

## 3. mm-leak EAGAIN 的三個可攻方向

1. **抓準 uptime**：實測 mm-leak 只在 ~396-397s 成功，405s 起就 EAGAIN。可能是 boot 後某
   background service（camera/gfx）在 ~400s 啟動，吃掉 thread/記憶體預算。做法：reboot 後
   立刻輪詢 uptime，到 ~380s 就開跑（run_r21 就是 396s 成功、10 輪全程 OK）。
2. **減少 mm-leak 的 thread 數**：`APPENDED_FUTEXES=4096`（=1024 bucket×4）。若降到 2048
   或 1024，碰撞 timing 訊號會變弱、mm-leak 可靠度下降，但可試。或把 flood(765)/dummy(450)
   往下調（它們是 reclaim 用的，mm-leak 本身不需要），讓 4096 threads 有更多資源——但會動到
   reclaim 參數，風險自負。
3. **定位到底是哪條上限**：在 fail 點 dump `getrlimit(RLIMIT_NPROC)`、`/proc/loadavg`
   （nr_threads）、`/proc/sys/kernel/threads-max`、以及 `cat /proc/self/status | grep Threads`
   的 pid 命名空間情形。目前靜態全排除（6608/13217/32768 都遠大於 1980、無 pids cgroup），
   唯一剩下嫌疑是 **VM overcommit（Committed_AS 28.5GB >> CommitLimit 2.48GB）**，但那通常
   回 ENOMEM 不是 EAGAIN——需要 kernel 側或 logcat/dmesg 抓 clone 失敗的 WARN 才能定案。

---

### A.3 重啟/panic 的 kernel 側線索（dmesg 抓到的 kernel cmdline）

從 dmesg 抓到的 boot cmdline 有幾條關鍵（之前 handoff 說「無串口、pstore 空」，實際是**有
ramoops 但 pstore 目錄空**，且**有 hypervisor watchdog**）：

```
console=ttyMSM0,115200n8        ← 其實有 serial console（ttyMSM0），只是沒接線
kernel.panic_on_rcu_stall=1     ← RCU stall 會 panic！
pstore.compress=none            ← ramoops 有設定
ramoops: using 0x200000@0xf8e00000, ecc: 0   ← ramoops 2MB @ 0xf8e00000
gh-watchdog hypervisor:qcom,gh-watchdog: QCOM Apps Watchdog Initialized   ← hypervisor watchdog
kiwi_v2: Loading driver ... +PANIC_ON_BUG   ← kiwi driver BUG() 即 panic
```

**含意**：exploit 的 reclaim 大殺（765 flood mm_struct 的 RCU lazy-free 波）極可能觸發
**RCU stall → panic_on_rcu_stall=1 → panic**；或觸發 **hypervisor watchdog 硬重啟**（此種重啟
不經 kernel panic 路徑 → ramoops 不寫 → pstore 空，解釋了為何抓不到 oops）。兩者都不是
`panic_on_warn` 可控。若要抓現場，**優先接 ttyMSM0 串口**（cmdline 有 console=ttyMSM0），
或設法讀 /sys/fs/pstore（目前 shell 在 log 群組但目錄空，可能因 watchdog 重啟不寫 ramoops）。

---

## 4. 已確認不變的狀態

| 項 | 狀態 |
|---|---|
| W1（--write1-text → getenforce=Permissive） | ✅ 已驗證（V10） |
| init_cred->uid = 0 | ✅ 本輪 dump 定案 |
| init_cred / init_task / selinux_state / ashmem_misc 偏移 | ✅ kallsyms 全對 |
| TASK_CRED_OFF=0x780 / TASK_REAL_CRED_OFF=0x778 | ✅ commit_creds disasm 對 |
| rb_erase write#1=target=write_value、write#2=*(write_value)=tree_pc | ✅ 反組譯定案 |
| mm-leak（kernelsnitch） | ❌ ~60% flaky（EAGAIN，~2000 task 處耗盡） |
| W2 blind write | ❌ child uid=-128（DIAG 待跑） |
| FOPS install（mode-5）| ❌ 併發 panic（global fops overwrite，V10 已知） |

---

## 附錄 A：本輪關鍵 log 摘錄

### A.1 panic 點會漂移（本輪新增觀測）

第十一任共 6 次 boot，device 在**多個不同階段** panic 重啟（無串口、pstore 空，拿不到 oops）：

| boot | uptime | 結果 |
|---|---|---|
| run_r21（前一任）| 396s | mm-leak 11× OK、FOPS LANDED、10 輪 blind write uid=-128、round9/10 panic |
| selftest1 | 522s | mm-leak EAGAIN（Threads:1037）無 panic |
| selftest2 | 454s | mm-leak 進行中（Threads:4097）collision 階段 panic |
| run_r22 | 397s | mm-leak OK、FOPS punch 後 panic |
| run_r24/25 | 405/548s | mm-leak EAGAIN（Threads:1043~1057）無 panic |
| **run_r26** | **396s** | **mm-leak OK、reclaim settle 階段 panic（punch 前）** |

> **結論**：mm-leak 成功（Threads:4097）不代表能走完——reclaim 的 phase-1 大殺（765 flood
> SIGKILL + RCU callback 波 + vmap stack free）也會 panic。這是 1.8GB 低記憶體裝置上、
> 765+450+4096 thread fork 風暴的**資源穩定性問題**，不是單一 bug。run_r21 能跑 10 輪
> 純屬該 boot 運氣好。**要穩定拿 DIAG，需縮小 fork 風暴或換 reclaim 載體**（見 §3）。

### A.0 SKB 搶頭 flake 已定案 = FOPS install 的「直接」阻塞（並行代理 run_r22_v950diag_110131）

並行代理在 11:01:31（uptime 395s，mm-leak 6/6 全成功、`Threads:4097`）跑出完整證據：

```
FOPS install attempt 1: dip=1 → write#1 MISSED (vehicle -1, llseek=0x0 exp=0xffffffe2733bcbc0)
FOPS install attempt 2: dip=1 → write#1 MISSED
FOPS install attempt 3: dip=0 → MISSED
FOPS install attempt 4: dip=0 → MISSED
FOPS install attempt 5: dip=0 → MISSED
FOPS install attempt 6: dip=1 → MISSED
```

每輪 `pselect returned ret=1 calls=5 success=5`（5 次 sched_setattr 全成功），但 DIAG 的
`llseek` 槽仍是 pristine 的 `CFI_NOOP_LLSEEK`（0x...1ab88），**不是 fops_field**。這代表：

- **sched_setattr 成功 ≠ walk 走到 rb_erase 的 write#1**（reclaim 失敗時 fake page 是 garbage，
  walk 沒走到 rb_erase 或寫到 freed slab，不在 SKB 裡）。
- mode-5 write#1 寫的是 **fake_fops->llseek（SKB 頁內）**，所以 DIAG MISSED 直接 = **SKB 沒吃下
  leak slab（reclaim 失敗）**。`dip=1`（leak slab 已 discard）但 SKB 搶不到頭，就是
  handoff App.H/J 的「SKB 搶頭」flake，**GL_PURGE_CNT=0 + GL_TAIL_KILLS=16 不足以解**。

**這對 W2 的含意**：mode-2（blind write）的 write#1 目標是 `task->cred`（heap，非 SKB 頁），
但 **walk 仍需要 fake page 有效才走得到 rb_erase**。所以 reclaim 失敗 → walk 沒走到 rb_erase →
write#1 沒落地 → child cred 沒被改。這與 run_r21 的 child uid=-128（且每輪 `cleanup: 0 diffs`）
一致——**blind write 的 -128 最可能就是 reclaim 失敗（walk 沒落地），不是 task 指標錯**。
要 100% 定案仍需 init_cred.usage DIAG 資料（見 §2），但「先解 SKB 搶頭」已確定是最高優先。

### A.2 mm-leak 失敗 boot（errno 擷取後）


mm-leak 失敗 boot（errno 擷取後）：
```
v9.13 diag: dummy clones ok=125 fail=325 (first errno=11 Try again)
kernelsnitch: ks_increase 4096 threads, 3040 failed (first err=11)
Threads:	1057
kernelsnitch: hashsize=1024 sz=0x3c0 single_matches=0 mm=-
kernelsnitch mm leak failed (mm=0000000000000000 r=8)
```

成功 boot（run_r21 / run_r22）：
```
v9.13 diag: dummy clones ok=450 fail=0
Threads:	4097
kernelsnitch: hashsize=1024 sz=0x3c0 single_matches=9571 mm=FOUND
leaked mm=ffffff8061a8da40 slab base=ffffff8061a8c000
```
