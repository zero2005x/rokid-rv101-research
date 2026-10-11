# GhostLock RV101 — 工作狀態交接 v13（第十三任：Clean Root（uid=0 gid=0）達成）

> 更新於 2026-08-22。本輪（第十三任）在實機（<DEVICE_SERIAL>）上完成：
> **1. Clean Root 達成：child `uid=0` 且 `gid=0`，`id` 回 `uid=0(root) gid=0(root) groups=0(root)`。**
> **2. 定案 kernel-side gid repair（第 4 次 reclaim）會 panic，改用 user-space `setgid(0)` 修復。**
> **3. 把 boot 健康 gate + 自動 retry 寫成 `scripts/boot_run_loop.sh`（Branch C）。**

---

## 0. 最關鍵結論（先讀）

> **【Clean Root 已達成】**
> 證據（`reports/ghostlock_runlogs/run_w2_clean_v959_134613.log`，v959，boot 後 ~110s）：
> ```
> [+] KASLR: slide=0x0000002480400000 base=0xffffffe488400000 (2 anchors)
> [*] install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 1, ...)
> [*] round 1/1: child=8967 prev=0xffffff801dac04c8 prev2=0xffffff801dac04c8 stable=1 task=0xffffff801dac0000
> [*] child uid = 0 (0x00000000) gid = 0 (0x00000000)
> [+] child is root!
> [+] GHOSTLOCK ROOT: uid=0 gid=0
> uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
> Permissive
> [*] [T+110439ms] exploit complete — root shell spawned via /data/local/tmp/.ghostlock_root.sh
> ```
> 裝置側確認：`getenforce=Permissive`、`/data/local/tmp/su_gl` 為 `-rwxr-xr-x 1 root …`。
> **滿足 handoff #13 的 Full success**（uid=0 且 gid=0、SELinux Permissive、功能完整的 root shell、無 heap read）。

> **【gid 修復的正解 = user-space `setgid(0)`，不是 kernel-side repair】**
> - v958（kernel-side `do_gid_repair()`，第 4 次 reclaim 週期）：**會 panic**。證據
>   `run_w2_clean_v958_133632.log`：FOPS LANDED + 兩次 cred write 成功後，第 4 次
>   `prepare_good_kernel_page()` 的 `dip=0 calm=3604`（leak slab 永不 discard，因前三輪的
>   memfd pin 累積餓死 per-CPU partial 鏈），pselect 走 garbage fake page → 裝置重啟
>   （log 斷在 `consumer punch 1/5`，其後 `device not found`）。
> - v959（user-space fix）：child 拿到 `init_cred`（uid=0、CAP_FULL_SET）後，在回報前呼叫
>   `setgroups(0,NULL); setgid(0);` —— CAP_SETGID 使 `setgid(0)` 走 `prepare_creds`+`commit_creds`
>   換成乾淨 cred（uid=0 gid=0 usage=1）。**零額外 reclaim、零 panic 風險**。實測 gid=0。

---

## 1. 本輪開場時的真實狀態（What was true at the start）

- 裝置：上一任（V12）結束後已 up 16 min、`getenforce=Permissive`（V12 的殘留 root 狀態）。
- source：`do_cred_write()` 仍是 mode-5（`data_addr(INIT_CRED)+8, target, 5`），無 `do_gid_repair`。
- binary sha1 = `2850496c18dc3ca5fa9340896d2110ef216e030e`（本任開始前一致）。
- 無並行代理活動（開場後無新的非本任 runlog）。

## 2. 本輪改了什麼（What changed in source/build）

- `src/main.c`：
  1. `child_main` 的 `'C'` handler 增加 **user-space gid fix**：`setgroups(0,NULL); setgid(0);`，
     並把回報從單一 `uid` 改成 `uint32_t ids[2]`（uid+gid）。
  2. W2 round loop：`child uid = %u … gid = %u` 同時印 uid/gid；`if (ids[0]==0)` 判 root。
  3. **移除** kernel-side `do_gid_repair()`（v958 嘗試），並留註解（v9.59 NOTE）說明第 4 次
     reclaim 會 dip=0 → panic，故改 user-space。
- `src/common.h`：加 `#include <grp.h>`（`setgroups` 需要）。
- `scripts/boot_run_loop.sh`（新增）：reboot → gate `nr_threads<2500` → `timeout 240` 跑 W2 →
   grep 成功 → 自動 retry。**注意：Windows adb.exe 輸出 CRLF，`get-state` 必須 `tr -d '\r'`**
  （否則 `[ "$ST" = "device" ]` 永遠 false，boot 偵測失效——本任實踩並修復）。

## 3. 跑了哪些測試（What tests ran）

| # | binary | boot | 結果 |
|---|---|---|---|
| 1 | v958 (kernel repair) | boot1 @213s | FOPS install 6/6 MISSED（reclaim flake，mm-leak 全成功） |
| 2 | v958 | boot2 @240s | FOPS install 6/6 MISSED（同上） |
| 3 | v958 | boot3 @~110s | FOPS **LANDED veh9** + cred write 成功 → **do_gid_repair 第4次 reclaim dip=0 → panic** |
| 4 | v959 (user-space fix) | boot4 | FOPS install MISSED（reclaim flake） |
| 5 | v959 | boot5 @~110s | **SUCCESS：uid=0 gid=0**，`uid=0(root) gid=0(root)`，Permissive |

> reclaim（mode-5 FOPS install）仍是 ~50%/boot 的 flake：5 個 boot 中 2 個 LANDED（其中 1 個
> 走完 clean root）。mm-leak 在健康 boot 上 100% 成功（`Threads:4097`）。與 V12 一致。

## 4. gid repair 是否成功（Whether the gid repair succeeded）

**是，但走的是 user-space 路徑，不是 handoff #13 提議的 kernel-side 路徑。**

- kernel-side `do_gid_repair()`（第 4 次 reclaim）**失敗且 panic**（見 §0 與 §3 #3）。根因：
  前三輪 reclaim 的 memfd pin 累積使第 4 輪 `dip=0`，leak slab 永不 discard → pselect 走
  garbage fake page → 裝置重啟。
- user-space `setgid(0)` **成功**：`child uid = 0 (0x00000000) gid = 0 (0x00000000)`、
  `id` 回 `uid=0(root) gid=0(root) groups=0(root)`。這證實 mode-5 的 uid 路徑不變，gid 由
  child 以 CAP_SETGID 自行清零。

## 5. 下一任的精確命令（Exact next commands）

命令 1 — 重現 Clean Root（**先確認 nr_threads<3000，否則 reboot**）：
```bash
ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"; S="<DEVICE_SERIAL>"
"$ADB" -s $S shell cat /proc/loadavg      # 第4欄 total 必須 < 3000
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root
./scripts/ghostlock_run.sh w2_clean v959 \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1 /data/local/tmp/ghostlock_rv101"
# 成功判據：log 內出現 "gid = 0 (0x00000000)" 與 "uid=0(root) gid=0(root)"
```
命令 2 — 自動 retry（Branch C，已實作；會自動 reboot 直到成功，最多 N 個 boot）：
```bash
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root
./scripts/boot_run_loop.sh 6 w2_clean v959 "gid = 0 (" \
  "GL_PURGE_CNT=0 GL_TAIL_KILLS=16 GL_DUMMY_KILLS=0 W2_ATTEMPTS=1" /data/local/tmp/ghostlock_rv101
```
命令 3 — 重建/推檔（如需再改 source）：
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

## 6. 仍待解 / 建議（Remaining / next refinements）

1. **reclaim（mode-5 FOPS install）仍是唯一 flake**（~50%/boot）。已用 boot_run_loop.sh 自動化
   掩蓋，但根因（SKB 搶頭 + o2 churn 埋 leak slab）未解。若要再壓：縮小 flood/dummy 的 kernel
   stack 量、或換 reclaim 載體（V10 附錄 H/J）。
2. **root shell 的 gid 修復已乾淨**，但 `context=u:r:kernel:s0`（cred=init_cred 的 kernel context）
   非典型 `u:r:su:s0`。功能無礙（能寫 /sys/fs/selinux/enforce、寫 /data/local/tmp）。如需更「正常」
   的 app context，需換 non-init_cred 的 root cred（超出本任範圍）。
3. 並行代理協調：本任開場無並行代理；若下任觀測到非本任 runlog，先確認單一執行者。

## 附錄 A：最終 binary 與產出

- binary sha1（local=device）：`ec342a5aaf999d0a41ccd7ccc24d028e8b7f637e`（v959，user-space gid fix）。
- 成功 log：`reports/ghostlock_runlogs/run_w2_clean_v959_134613.log`。
- 關鍵對照（kernel-side repair panic）：`reports/ghostlock_runlogs/run_w2_clean_v958_133632.log`。
- 新腳本：`scripts/boot_run_loop.sh`。
