# GhostLock RV101 — 工作狀態交接 v10（第十任：reclaim 修復 + W1 資料路徑攻堅）
> English: [GHOSTLOCK_HANDOFF_V10_EN.md](GHOSTLOCK_HANDOFF_V10_EN.md)
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> 更新於 2026-08-21。本輪（第十任）在實機（<DEVICE_SERIAL>）上完成：
> 1. **定位並修復 reclaim 的「leak slab 永不 discard」根因**，首次穩定觀測到 STRICT 命中。
> 2. **反組譯定案 W1 目標 = `selinux_state + 0`**（enforcing byte）。
> 3. 發現 `P0_KERNEL_PHYS_LOAD = 0x80000000` 對 W1 寫入**不生效**（enforce 不翻），
>    資料路徑（data_addr）的實體載入基址仍需實證。

> **【round 5-6 重大結論 — W1 已完整驗證，且 write#1 是 reclaim-無關的確定性全域寫】**
> - `data_addr()` 改回 `text_addr()`（RV101 kernel image 在高位實體位址）後：
>   **`--write1`（標準）與 `--write1-text` 都已在實機驗證 → `getenforce` = Permissive**。
> - 關鍵洞察（實測證實）：rb_erase 的 **write#1** 寫的是 GLOBAL 目標
>   （`parent=tree_pc&~3=target-8`，寫 `target` 本身），**與 reclaim 無關**——即使
>   `cleanup: 0 diffs`（reclaim 完全沒中、STRICT 不過），enforce 照樣翻 0。
>   所以 **W1 是「每次 punch 都命中的確定性提權」**，reclaim 只影響 write#2 的觀測
>   （STRICT 驗證），不影響實際寫入。這直接達成目標的「驗證提權」部分。
> - W2（cred=init_cred → uid=0）仍需 FOPS/configfs **任意讀**（找 child task_struct），
>   而 fake_fops 表住在 reclaimed SKB 頁 → 仍需 reclaim 成功。reclaim 已 root-cause
>   （flush 用 stride 修正，見附錄 G；SKB 搶頭/累積仍 racy，見下）。

---

## 0. 最關鍵結論（先讀）

### 0.1 reclaim 修復（本輪最大突破）

`--selftest`（mode 3）此前「0 diffs / dip=0」的根本原因：**leak slab 被凍結在 per-CPU partial
鏈上、永不 discard**。SLUB 5.10 的 `__slab_free` 裡，leak slab 的第一個 free 觸發
`put_cpu_partial(drain=1)` 將其凍結；後續 16 個 free 走 `FREE_FROZEN` fastpath，slab 變空後
仍凍結在鏈上。它只會在「後續某次 free 使鏈的 `pobjects > cpu_partial(13)` → `unfreeze_partials`」
時才被 discard。舊設計把 dummy kills 放在 pre+leak **之前**（FIFO），flush 在 leak slab 變空
之前就發生 → leak slab 永不 discard（dip=0）。

**修復（新增 `GL_TAIL_KILLS`）**：在 `kill_child(leak_child)` **之後** 再殺 N 個 dummy 子進程，
讓它們的 free（FIFO 排在 leak slab 之後）把 per-CPU partial 鏈灌爆 → `unfreeze_partials` →
discard 凍結空的 leak slab。實測 `GL_TAIL_KILLS=34` 在 fresh device 上 **attempt 1 即 STRICT 命中**
（vehicle 618）。

### 0.2 W1 目標 = `selinux_state + 0`（反組譯定案）

`sel_write_enforce@0x638644` 的鏈：
```
x8 = *(file + 0x20); x8 = *(x8+0x28); x8 = *(x8+0x3c8); x21 = *(x8+0x48);
strb w8, [x21]            ; enforcing byte @ *x21
...
selinux_status_update_setenforce(x21, ...)   ; 讀 *(x21+0x10)=status_page
```
`x21` 被當成 `struct selinux_state *`（`status_page@+0x10`、`status_lock@+0x18`），
`strb [x21]` 寫的就是 `selinux_state[0]`（enforcing）。RV101 kernel（5.10 GKI）**沒有**
獨立的 `selinux_enforcing` 全域（kallsyms 僅有 `selinux_enforcing_boot@0xffffffc00a196788`），
與 OnePlus（6.12，`SELINUX_ENFORCING_OFF=0x026894d0` 獨立符號）不同。所以
`target_rv101.h` 的 `SELINUX_STATE_OFF=0x02ca0140; enforcing@+0` **是對的**。

### 0.3 ~~資料路徑（data_addr）仍待實證~~ **✅ 已於附錄 E 實證（data_addr→text_addr，W1 Permissive）**

實測 W1（`GL_TAIL_KILLS=34`，reclaim STRICT 命中）後 `getenforce` 仍回 `1`。既然
W1 目標正確（0.2）、write#1 應落在 `data_addr(SELINUX_STATE)=0xffffff8002ca0140`，
則結論是 **`P0_KERNEL_PHYS_LOAD=0x80000000` 是錯的**。證據：
- `/proc/zoneinfo` 顯示 node 0 第一段 free memory 就是 `0x80000000-0x808f3fff`（9MB），
  即 kernel **不在** RAM 基址（否則該段應 reserved）。
- Honor（Pixel GKI）文件稱 `P0_KERNEL_PHYS_DELTA=0` 是對的，但 RV101 是 QCM4290 專屬
  `5.10.209-perf` 核心，實體佈局可能不同。
- 歷史值：`0x80800000`（原始，不 panic）、`0x80080000`（曾 panic）、`0x80000000`（v9.30）。

---

## 1. 已驗證成果

| 項 | 狀態 |
|---|---|
| reclaim：`GL_TAIL_KILLS` 觸發 leak slab discard | ✅ `--selftest` STRICT 命中（fresh, attempt 1） |
| V5 回收參數還原（dummy=450、kills=0） | ✅ `V9_DUMMY_CNT 200→450`、`GL_DUMMY_KILLS 預設 450→0` |
| W1 目標 = selinux_state+0 | ✅ 反組譯定案（見 0.2） |
| `SELINUX_STATE_OFF=0x02ca0140` | ✅ kallsyms `selinux_state@0xffffffc00aca0140` 吻合 |
| W1（--write1 → Permissive） | ~~❌ STRICT OK 但 enforce 不翻（data_addr 錯）~~ **✅ 已於附錄 E 驗證會翻（2026-09-13 更正）** |
| W2（FOPS → cred） | ~~❌ 未達~~ **✅ 已於附錄 AA 驗證（child uid=0）** |

---

## 2. 本輪程式碼變更（`source_code/ghostlock_rv101/`）

- `src/util.c`：
  - `V9_DUMMY_CNT 200 → 450`（還原 V5 known-good，註解說明 nr_partial 飢餓根因）。
  - `GL_DUMMY_KILLS` 預設 `V9_DUMMY_CNT → 0`。
  - 新增 `GL_TAIL_KILLS`（0~272，預設 0）：leak child 之後再殺 dummy，觸發 discard。
  - `p0_data_alias()` 增加 `GL_KERNEL_PHYS_LOAD` 覆寫（`set_kernel_phys_load()`）。
  - 「v9.10 gate」診斷 print 移到 first send **之後**（原 print 重開 steal window）。
- `src/main.c`：
  - `SELFTEST_RUNS` 上限 10 → 100，並列印命中統計。
  - 解析 `GL_KERNEL_PHYS_LOAD` 環境變數。
- `src/common.h`：宣告 `set_kernel_phys_load()`。

---

## 3. 建置/執行速查（不變）

```bash
NDKBIN="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d/toolchains/llvm/prebuilt/windows-x86_64/bin"
cd /mnt/c/Users/<USER>/Documents/codebase/rokid_root/source_code/ghostlock_rv101
"$NDKBIN/clang.exe" --target=aarch64-linux-android31 -O2 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare -fno-stack-protector -fPIE -Isrc \
  -include target_rv101.h -o build/ghostlock_rv101 src/main.c src/util.c src/route.c \
  -pie -static-libgcc -pthread

ADB="/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe"
"$ADB" -s <DEVICE_SERIAL> push "$(wslpath -w .../build/ghostlock_rv101)" /data/local/tmp/ghostlock_rv101
"$ADB" -s <DEVICE_SERIAL> shell "chmod 755 /data/local/tmp/ghostlock_rv101"

# reclaim 驗證（成功配置）
"$ADB" -s <DEVICE_SERIAL> shell "GL_SKB_PAD=0 GL_DUMMY_KILLS=0 GL_TAIL_KILLS=34 \
  GL_SETUP_ATTEMPTS=4 /data/local/tmp/ghostlock_rv101 --selftest"
```

---

## 4. 下輪待辦（**2026-09-13 註：以下三項均已在後續附錄完成，勿重做**）（按優先序）

1. **定案 `P0_KERNEL_PHYS_LOAD`**（唯一阻塞 W1/W2）：
   - 用 `GL_KERNEL_PHYS_LOAD=<候選>` 逐個跑 `--write1`（配 `GL_TAIL_KILLS=34`），看 `getenforce`。
   - 候選：`0x80800000`、`0x80080000`、`0x8ab00000`（node 0 之後）、`0xe8000000` 附近。
   - 更可靠做法：反組譯 `__primary_switched@0x20502b4` 的 `kimage_voffset = kimage_vaddr - x0`
     與 `__create_page_tables@0x2050040`，還原 bootloader 傳入的 x0（=PA(_text)）；
     或從 DTBO/xbl 找 kernel load address。
2. **驗證 W1**：`--write1` → `getenforce` 回 Permissive。
3. **驗證 W2**：FOPS 路線 → `task->cred = init_cred` → `id` 回 uid=0。
4. reclaim 穩定度再加強：`GL_TAIL_KILLS` 需 >13 slab（>221 子進程）才「確定性」灌爆鏈；
   `SKB_RECLAIM_SENDS` 768 → 1536 可增加 vehicle 覆蓋餘裕（leak slab 曾落在 vehicle 618）。

---

## 附錄 B：第十任 round 2 新增反組譯定案

1. **`rb_erase@0x774b38` 全反組譯**（case 1，node 只有 right child）：
   ```
   0x774b3c: ldp x8,x10,[x0,#8]     ; x8=left(0), x10=right(write_value)
   0x774b44: cbz x8, 0x774bb8       ; left==0 → case 1
   0x774bb8: ldr x9,[x0]            ; x9 = node->pc = (target-8)|1
   0x774bbc: ands x8,x9,#-4         ; x8 = parent = target-8
   0x774bc0: str x9,[x10]           ; WRITE#2: *(write_value) = (target-8)|1  ← STRICT 檢查
   0x774bc8: ldr x9,[x8,#0x10]      ; x9 = parent->rb_left = *(target+8)
   0x774bcc: cmp x9,x0              ; rb_left == node?
   0x774bd0: b.eq 0x774d90          ; 是 → 寫 rb_left(target+8)
   0x774bd4: str x10,[x8,#8]        ; WRITE#1: parent->rb_right(target+0) = write_value
   ```
   確認 W1 的 WRITE#1 落在 `target+0 = selinux_state[0]`（`*(target+8)!=node` 恆真，
   因 selinux_state[8]=ss_initialized≈1，node 是 stack 位址）。write_value=page_base+0x1500
   的 low byte=0x00 → 理應零化 enforcing。

2. **`__primary_switched@0x20502b4`**：`kimage_voffset = kimage_vaddr - x0`，且 caller
   （`__relocate_kernel@0x141c408/0x141c448`）傳 `x0 = adrp #0xffffffc008000000` =
   **KIMAGE_TEXT_BASE（link VA），不是標準 arm64 的 __PHYS_OFFSET**。所以
   `kimage_voffset = slide`（KASLR slide）。這是 Qualcomm 的非標準做法；但無論用
   標準 `__kimg_to_phys(x)=x-kimage_voffset` 或加上 `-KIMAGE_TEXT_BASE+PHYS_OFFSET`，
   `__pa_symbol(_text)` 都等於 **0x80000000**（PHYS_OFFSET）。

3. **`sel_write_enforce@0x638644`**：`strb w8,[x21]`（x21=chain 結果）寫 enforcing byte，
   且 `selinux_status_update_setenforce(x21,...)` 讀 `*(x21+0x10)=status_page`、
   `*(x21+0x18)=status_lock`。所以 x21=&selinux_state，enforcing@**selinux_state+0**。

**結論（仍未解的矛盾）**：data_addr(SELINUX_STATE)=0xffffff8002ca0140 正確、WRITE#1 落在
selinux_state[0]、enforcing@selinux_state[0] 三件事都經反組譯定案，但實測
`GL_KERNEL_PHYS_LOAD = 0x80000000 / 0x80800000 / 0x80080000` 的 W1 全都 STRICT OK 卻
enforce 不翻。待查：a) 是否 Qualcomm 有實體 KASLR（PA(_text) 隨機）；b) 是否
`selinux_state` 的 BSS 實體位址與 Image 不同（實體重定位）；c) 是否 enforcing 其實由
status page 鏡像讀取而非 selinux_state[0]。

## 附錄 C：第十任 round 3 新增定案

1. **GKI `struct file` 佈局（關鍵）**：`do_dentry_open@0x3cfd60` 做 `str inode,[f+0x20]`、
   `ldr x8,[f+0x18]` → 本核心的 **`f_inode@0x20`、`f_op@0x18`（與 mainline 互換）**。
   因此 `sel_read_enforce` 的鏈 `*(file+0x20)+0x28 → +0x3c8 → +0x48` 是
   `f_inode → i_sb → s_fs_info → selinux_fs_info.state = &selinux_state`，最後
   `ldarb [&selinux_state]` 讀 `selinux_state[0]`。**確認 enforcing@selinux_state+0**。

2. **`rb_erase` case-1 全反組譯**（`0x774b3c~0x774bdc`）確認 WRITE#1（`str x10,[x8,#8]`）
   落在 `parent->rb_right = target+0`（`parent=target-8`，`*(target+8)!=node` 恆真），
   WRITE#2 落在 `*(write_value)`。即 W1 的 `write_value=page_base+0x1500`（low byte=0）
   會寫進 `selinux_state[0]`。

3. **節點記憶體定量**：`node 0:[mem ...]` 18 段總和 = 2026496 KB = `Memory: 2026496K total`，
   所以它是 System RAM（含 kernel），kernel 確實在 PA=0x80000000，`data_addr` 正確。

**仍未解（W1/W2 的唯一阻塞）**：以上三點全定案後，`--write1`（GL_KERNEL_PHYS_LOAD=
0x80000000/0x80800000/0x80080000）仍 STRICT OK 卻 `getenforce` 不回 Permissive。
最可疑的新方向：Qualcomm 的 `__primary_switched@0x20502b4` 用 `kimage_voffset =
kimage_vaddr - KIMAGE_TEXT_BASE = slide`（非標準 `VA-PA`），可能隱含**實體重定位**；
需用 `GL_KERNEL_PHYS_LOAD` 掃 0x80000000..0xfc000000 實測，或反組譯
`__relocate_kernel@0x141c3e8` 確認是否做實體 memcpy。

## 附錄 D：第十任 round 4 定案（PA(_text)=0x80000000 已確證、W1 仍不翻）

1. **無實體重定位**：`__relocate_kernel@0x141c3e8` 呼叫 `0x141c330`（`.rela.dyn` 修補：
   `add x14,x14,x23; str x14,[x12,x23]`）——只把 VA 參考加 slide，**沒有實體 memcpy**。
   `0x141c29c` 只做 `msr ttbr0/ttbr1_el1`（頁表切換）。→ PA(_text) 固定。

2. **`kimage_voffset = slide`（非標準）**：`__primary_switched` 收到 `x0=KIMAGE_TEXT_BASE`
   （caller `adrp x0,#0xffffffc008000000`），所以 `kimage_voffset = kimage_vaddr - KIMAGE_TEXT_BASE
   = slide`。但無論用標準 `__kimg_to_phys(x)=x-kimage_voffset` 或加
   `-KIMAGE_TEXT_BASE+PHYS_OFFSET`，都推出 `__pa_symbol(_text)=0x80000000`。

3. **實測掃描**（`scripts/scan_kernel_phys_load.sh`）：
   - 0x80080000：0/6 reclaim 命中（inconclusive）。
   - 0xa8000000：STRICT 命中（vehicle 523, attempt 1）→ enforce 仍 Enforcing。
   - 加上先前 0x80000000、0x80800000（STRICT OK 但不翻）→ **4 個 PA 候選都不翻**。

**結論**：PA(_text)=0x80000000、data_addr 正確、WRITE#1 落 selinux_state[0]、enforcing@
selinux_state[0] 四件事全定案，但 W1 實測仍不翻 enforcing。最可能的殘餘方向：
(a) 線性映射的 kernel-image .data/.bss 別名被 `set_memory_ro`（Qualcomm 硬化）設成唯讀，
WRITE#1 落進唯讀頁被靜默吸收或跳到錯誤頁；(b) enforcing 其實由 status page 鏡像
（`page_to_virt(status_page)+8` 的 u32）讀取而非 selinux_state[0]。下輪建議先反組譯
`sel_read_enforce` 完整鏈 + `mark_rodata_ro`/`set_memory_ro` 確認 (a)，或改走 W2 configfs
讀原語（先讀回 selinux_state.status_page 再定位真正的 enforcing 欄位）。

## 附錄 E：第十任 round 5 重大突破 — W1 已驗證（data_addr → text_addr）

**根因（最終定案）**：RV101（Qualcomm QCM4290）核心的 kernel image 位在**高位實體位址**，
`__primary_switched@0x20502b4` 用 `kimage_voffset = kimage_vaddr - KIMAGE_TEXT_BASE = slide`
（caller `adrp x0,#0xffffffc008000000`，手動解碼 ADRP 證實），即 `__pa_symbol(_text) =
KIMAGE_TEXT_BASE = 0xffffffc008000000`（非 0x80000000）。所以 exploit 的 direct-map alias
`data_addr()`（假設 PA(_text)=0x80000000）**對映到錯誤的實體頁**。

**修復**：`data_addr()` 改回傳 `text_addr()`（runtime kernel VA = KIMAGE_TEXT_BASE + slide）。
rb_erase 是普通 kernel VA 寫入，寫 `text_addr(symbol)` 即可命中正確實體頁。

**驗證（實機）**：
```
--write1-text（寫 text_addr(SELINUX_STATE)=0xffffffe31c0a0140）:
  [+] W1-text: selinux_state (VA) verified: 1 vehicle STRICT signature (attempt 1)
  [*] enforce file = '0' (raw 30 00 00 00)
  [+] SELinux DISABLED (text_addr) attempt 1
  → getenforce = Permissive  ✅✅ W1 完成
```

**對照**：`--write1`（寫 data_addr=0xffffff8002ca0140）STRICT OK 卻 enforce 不翻；
`--write-dmesg`（寫 data_addr(DMESG_RESTRICT)=0xffffff800aa72d50）→ 寫進錯誤頁 panic。
三者一致證明 direct-map alias 錯、kernel VA 對。

**剩餘（W2）**：W2 FOPS 路線的 ENOTTY 根因已在本輪後半定位清楚（見附錄 F）：
1. **`ASHMEM_SET_NAME` ioctl 在 fresh boot 上完全正常**（最小測試 `r=0`），ENOTTY 非
   ioctl 編號錯也非 SELinux。
2. **mode-5 的 rb_erase write#2（`str x9,[x8]` 把 tree_pc=fake_fops 存進 fops_field）
   是「必然落地」的**（它寫 GLOBAL `ashmem_misc.fops`，與 reclaim 無關）。當 reclaim
   fail（write#1 MISSED/0 diffs = SKB 沒吃下 leak slab），`fake_fops=page_base+0x1600`
   是指向已釋放 slab 的 stale 指標，write#2 照樣把它寫進 fops → 之後 open 走 garbage
   fops → unlocked_ioctl=0 → ENOTTY。**也就是說：reclaim 是唯一變數，write#2 每次都
   污染 fops；reclaim 成功時 fake_fops 才有效、configfs_read 才會通。** 對策：fresh
   settled device + GL_TAIL_KILLS=34 + 多次 FOPS_ATTEMPTS（每次 write#2 會覆蓋 fops，
   retry 機制本身是對的；失敗只是連續 6 次 reclaim 都 fail，且裝置已退化）。

## 附錄 T：round 14 — copy_to_user 的 CONFIG_HARDENED_USERCOPY check 是關鍵嫌疑

**反組譯 simple_read_from_buffer@0x411634 呼叫的 helper**：
- `0x3cca84` = `check_copy_size`（CONFIG_HARDENED_USERCOPY）：在 `raw_copy_to_user` **之前**先
  驗證來源位址。流程含：stack-object 檢查（0x3ccc94）、heap-object vmemmap 走訪（0x3ccb40）、
  kernel `_text.._end` 白名單（0x3ccba8）；若命中「usercopy-abort」旗標會 `bl 0x3b7c7c`（可能
  panic/BUG_ON）。
- `0xdd28c` = `raw_copy_to_user`（真正的 `ldr from; str to`）。

**含意**：
- **並發進程（read_file, page@0x10=NULL）**：`check_copy_size(from=NULL)` 可能自己 BUG_ON，或
  之後 `raw_copy_to_user` 對 NULL `ldr` → data abort → **panic**（這是 read_file 的並發 panic 源，
  明確）。
- **exploit 讀（from=head=init_task.tasks，kernel .data）**：理論上 `_text.._end` 白名單應 pass，
  但 RV101 的 KASLR slide 高達 ~140GB（kernel image 在高位 VA），可能讓 `check_copy_size` 的
  白名單/vmemmap 走訪對 `head` 誤判 → 走 usercopy-abort 或 -EFAULT（bash-17 bin_file 的 panic
  未定案，但此為最可疑）。

**下一輪**：`check_copy_size` 對 head 的實際走向需要用一個「已知在 `_text.._end` 內且非 slab」
的位址（如 selinux_state/asmhem_fops 本身）做 GL_READ_TARGET 對照，或直接讀 direct-map 位址
看是否回 8；並優先修並發 panic（read_file 的 NULL page 已明確，bin_file 並發 bin_buffer_size=0
安全，故應選 bin_file + 查清 exploit 讀為何 panic）。

## 附錄 S：round 13 — GL_READ_TARGET 二分抓到 LANDED，panic 在 pread 之前（=並發進程）

**實測（bash-21 run3，v9.43 read_file + GL_READ_TARGET=0xffffff8040000000 direct-map）**：
```
install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 449)
（然後 log 直接斷掉，無 "blob OK" → 裝置 panic 離線）
```
**關鍵**：panic 發生在 `blob OK` **之前**（= open/首個 SET_NAME 階段），**不是** configfs 的
pread/copy_to_user。所以：
- copy_to_user(head) 假說被排除（panic 在 pread 之前）。
- 剩下的就是 **並發進程**（或 open/SET_NAME 本身，但 fake_fops 的 open=ashmem_open、
  ioctl=ashmem_ioctl 都是正確 slot，open 的 CFI check 也 pass）。

**定案**：mode-5 把 **GLOBAL `ashmem_misc.fops` 覆寫成 fake_fops** 是並發 panic 的根因——
背景進程（surfaceflinger/camera 用 ashmem）在裝上之後 open/read/mmap/ioctl 任一操作走 fake_fops，
而 fake_fops 的 read 是 configfs（read_file 對並發 ashmem fd = NULL page@0x10 → copy_to_user(NULL)
panic；bin_file 的並發 bin_buffer_size=0 安全，但 bash-17 的 panic 應是並發進程的**其它** NULL
slot，非 exploit pread）。

**結論**：W2 卡在「GLOBAL fops 覆寫 → 並發 panic」這個**固有矛盾**。要解需（a）裝上後立刻
restore fops（restore 的 write#1 會汙染 original fops->llseek，副作用可接受但需實作），或
（b）用 per-file fops 而非 GLOBAL fops（目前無此原語），或（c）串口抓 oops 精確定位並發進程
踩了哪個 slot 再補齊。

## 附錄 R：round 12 — panic 源收斂：不是 chain、是 copy_to_user(head) 或並發進程

**bash-16（bin_file, v9.41）已到 mutex_lock 且只 hang 不 fault** → 證明 bin_file 的
`file+0x18 → +0x78 → +0x50` chain **不 fault**（garbage 只是可讀）。所以 bash-17（v9.42,
mutex zero 後）的 panic 不在 chain，而在**兩 read 函式共通**的：`copy_to_user(buf, head, 8)`
或 **並發進程**。read_file（v9.43）則多一個「並發 NULL page@0x10」的獨立 panic（實測 bash-20
run1 在 FOPS install 中途 dip=1 後就離線 = 並發進程）。

**已加 `GL_READ_TARGET` env（v9.44）+ `env_hex()`**：可把 configfs 讀目標改成任意位址，
做「kernel-image VA vs direct-map VA」二分。但 mode-5 reclaim 近期 flaky（bash-20/21 多次
0/6，attempt1 dip=1 但 0 diffs = SKB 搶頭又輸），二分尚未抓到 LANDED + `configfs_read target`。

**反組譯 ashmem_open@0xc68a04 追加**：RV101 ashmem_area = name[0..0xFF]、unpinned_list@0x110、
prot_mask@0x130=7、SET_NAME 檢查欄位 0x120(zalloc=0)。

**下一輪**：把 reclaim 先穩回（GL_PURGE_CNT=0 的 SKB 搶頭仍 ~50% flaky，見附錄 J），或直接
並發抓 dmesg/串口，否則 GL_READ_TARGET 二分會一直卡在沒 LANDED。

## 附錄 Q：round 11 追加 — panic 有兩個獨立源（read_bin 的 chain vs read_file 的並發 NULL page）

**read_bin_file（v9.42）與 read_file（v9.43）都 panic，但源不同**：
- **read_bin_file 的 prologue chain（file+0x18 → +0x78 → +0x50）是無條件執行的**，對 ashmem file
  讀到 garbage 指標，`ldr x25,[x8,#0x50]` 可能對未映射位址 fault——**KASLR 相關的隨機 panic**
  （bash-16 那次 garbage 恰好可讀、bash-17 就 fault）。
- **read_file 沒有無條件 chain**（只在 needs_read_fill!=0 分支），但**並發 ashmem fd 的
  page@0x10=0 → copy_to_user(NULL) → panic**（round 3-6 老問題）。實測 v9.43 的 panic 可發生
  在 **FOPS install 中途**（bash-20 run1：dip=1 後、DIAG 前就離線 = 並發進程），也可在 exploit
  pread（bash-18 run2：blob OK 後）。

**asmhem_open@0xc68a04 反組譯**：RV101 的 ashmem_area 佈局 = name[0..0xFF]、unpinned_list@
0x110（INIT_LIST_HEAD）、**prot_mask@0x130=7**；SET_NAME 檢查欄位在 **0x120**（zalloc=0 →
SET_NAME 正常，排除了 prot_mask 擋 SET_NAME）。

**CATCH-22 定案**：read_file 對 exploit 的 read 路徑最乾淨（無 chain）但並發 NULL page 必
panic；read_bin_file 並發 bin_buffer_size=0 安全但 exploit 的 chain 隨機 fault。下一輪必須
**完成 GL_READ_TARGET 二分**（已加 `GL_READ_TARGET` env，改讀 direct-map 位址；但三次 retry
都 0/6 沒 LANDED）或**用串口抓 oops**，確認 copy_to_user 對 kernel-image VA 是否 fault。

## 附錄 P：round 11 — pread panic 與 prologue chain 無關（read_file 也 panic）

**實測（v9.43，改回 configfs_read_file，page@0x10）**：
```
install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 224)
configfs_read: blob OK, about to pread
→ 裝置 panic 離線（無 configfs_read target=... r=8）
```
與 v9.42（configfs_read_bin_file）的 panic 一模一樣。**configfs_read_file 的 read 路徑根本
不執行 file+0x18 那條 prologue chain**（只在 needs_read_fill!=0 分支），卻照樣 panic。

**結論**：panic **不是** prologue chain、不是 mutex（已 zero）、不是 needs_read_fill（已 0）、
不是 bin_buffer/page 目標（=head=init_task.tasks 有效 .data VA）。兩條 read 函式共通的只剩：
1. `mutex_lock(configfs_buffer+0x20)`（已 zero，fastpath 應成功）；
2. `simple_read_from_buffer → copy_to_user(buf, head, 8)`（head 有效，copy_to_user 不該 fault）；
3. **並發進程**（fake_fops 裝上後，背景進程用某個 NULL/garbage f_op slot 或 NULL page@0x10
   [read_file 的並發 ashmem fd] 而 panic）。

**反組譯 ashmem_open@0xc68a04 追加**：RV101 的 ashmem_area 佈局與 mainline 不同——
`prot_mask @ 0x130`（=7）、`unpinned_list @ 0x110`、SET_NAME 的檢查欄位在 `0x120`（zalloc=0，
所以 SET_NAME 正常）。這排除了「SET_NAME 因 prot_mask 非零而 -EINVAL」。

**下一輪（抓 panic 現場，優先）**：pstore 是空的（無 ramoops），需用 **串口 console** 或
**`dmesg -w` 背景抓取** 在 panic 瞬間留下 kernel oops；或先驗證 copy_to_user：把 configfs
讀目標換成一個「直接映射區已知可讀位址」（非 kernel-image VA），二分是「copy_to_user 對
kernel-image VA 讀取 fault」還是「並發進程」。

## 附錄 O：round 10 — pread panic 的靜態對照（read_file vs read_bin_file 的 prologue）

**反組譯對照（定案）**：
- `configfs_read_file@0x49e8e0`：`file+0x18 → +0x78 → +0x50` 這條 prologue chain **只在
  needs_read_fill!=0 的 fill 路徑**（0x49e928 之後）才執行；read 路徑（needs_read_fill=0）只
  做 `mutex_lock → simple_read_from_buffer(page@0x10, count@0x00)`。
- `configfs_read_bin_file@0x49ec40`：同樣的 `file+0x18 → +0x78 → +0x50` chain 是**無條件**的
  （0x49ec60-0x49ec80，在 mutex_lock 之前），結果 x25 只在 fill 路徑用（needs_read_fill=0 時
  無用）。

**讀 helper 0x411634 = simple_read_from_buffer 已反組譯確認**：`pos=*ppos; if(pos>=available)
return 0; ret=min(count,available-pos); copy_to_user(to, from+pos, ret)`。head=init_task.tasks
（有效 .data VA，已多次驗證）→ copy_to_user 理論上不該 fault。

**所以 bash-17 run2 的 pread panic 靜態上矛盾**（mutex=0、byte@0x55=0、needs_read_fill=0、
bin_buffer=head 有效、copy_to_user 不該 fault）。最可疑改為：**bin_file 的無條件 file+0x18
chain 對 ashmem file 讀到 garbage 指標**（bash-16 沒 fault 是因為 garbage 恰好可讀，但不保證
每次都不 fault），或**並發進程**。下輪可直接改回 `configfs_read_file`（page@0x10，prologue
chain 是條件式）+ mutex/needs_read_fill 照 zero，避開無條件 chain 再驗證；代價是並發進程的
NULL page@0x10 又會 panic（round 3-6 老問題）。

## 附錄 N：round 9 — configfs_read_bin_file 的 mutex 必須為 0（v9.42 修 pread hang）

**實測（bash-16，read_iter=ashmem，`--write2` 終於 LANDED + 進到 configfs read）**：
```
install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 159)
configfs_read: blob OK, about to pread
（然後 pread 卡住，不再印 configfs_read target=...，但裝置沒 panic、仍 Enforcing）
```

**根因**：`configfs_read_bin_file@0x49ec40` 反組譯第一件事就是
`add x19,x26,#0x20; bl mutex_lock`（鎖 `configfs_buffer+0x20` = asma->name[32..39]）。
舊 blob 只 zero 了 needs_read_fill/0x55-byte/bin_buffer，**沒 zero mutex**，所以 mutex owner =
0x41（fill 垃圾）→ `mutex_lock` 慢路徑 spin/睡在垃圾 mutex 上 → **pread 永久 hang**（不 panic）。

**修復（v9.42）**：`ashmem_set_name_bin_blob()` 的 full blob 把
`name[21..28]`（=configfs mutex owner 8 bytes，asma->name[32..39]）也填 0x01，再在 descending
zero 序列**新增 `{28,27,26,25,24,23,22,21}`**（共 13 個 zero）把 mutex owner 全清 0。如此
`mutex_lock` 走 fastpath（owner==0 → cmpxchg 成功）→ pread 才能進 `simple_read_from_buffer`。

**殘留（round 9 實測，v9.42 4 次 retry）**：
- run1：LANDED → 在 open/首個 SET_NAME 就 panic（無 "blob OK"）。
- run2：LANDED + **"blob OK"** → pread 中 panic（無 "configfs_read target=..."，裝置離線）。
- run3/run4：0/6（reclaim miss）。
所以 mutex 修復讓 SET_NAME 全過（blob OK），但 **pread 現在會 panic**（不是 hang）。
pread = configfs_read_bin_file：其 prologue（`file+0x18 → +0x78 → +0x50` 三連讀）在 bash-16
(v9.41) 已證明不 fault（有走到 mutex_lock）；mutex 已 zero；needs_read_fill=0、bin_buffer=head
（init_task.tasks，有效 .data VA）。所以 panic 最可能是 **copy_to_user from head 這一步**或
**並發進程**（pstore 是空的，拿不到 panic 原因）。下輪需抓 panic 現場（serial console 或
last_kmsg），或用一個已知可讀位址（如 selinux_state）換掉 head 驗證 copy_to_user 是否為元兇。

## 附錄 M：round 8 — mode 5 reclaim 落差的量化 + read_iter 副作用 + W2 catch-22

**v9.40 DIAG（實測，`--write2`）**：`n=15872 llseek=noop_llseek read=configfs_read_bin_file
scratch=0 safe=0` — SKB 是 **pristine payload**（MSG_PEEK 沒截斷、write#1 沒寫進 SKB）。所以
mode 5 的「0 diffs」= **reclaim 真的沒中**（SKB 沒吃下 leak slab），非方向/DIAG 問題。

**量化（round 5-8 全數據）**：
- mode 3（selftest）read_iter=NULL：**4/6（67%）**（bash-4/bash-10）。
- mode 3 read_iter=ashmem：1/3（33%，bash-14）。
- mode 5（FOPS）read_iter=NULL：**2/15（13%）**（bash-6 1/2、bash-12 1/1、bash-13 0/6、
  bash-15 0/6）。
- mode 5 read_iter=ashmem：**0/18**（bash-7/8/9/11）。

所以有兩層落差，且都在「reclaim 之後才設定的東西」上（靜態矛盾仍無解）：
1. **mode 5 比 mode 3 差 ~3x**（22% vs 67%，read_iter NULL 時）——最可疑是 mode-5 write#2
   每次都寫 GLOBAL fops、污染後讓背景進程（surfaceflinger/camera 用 ashmem）行為異常，
   反過來攪動 SLUB/timing。
2. **read_iter=ashmem 進一步歸零**（mode5 22%→0%）——機制不明（read_iter@0x20 不參與
   rb_erase 也不參與 reclaim），可能只是小樣本巧合（mode5 真值 ~10-15%）。

**W2 catch-22**：
- `read_iter=NULL`：reclaim 會中（~22%），但 **LANDED 就 panic**（bash-6/bash-12 都
  「write#1 LANDED → device not found」；bash-13 0/6 沒中所以沒 panic）。panic 是並發
  進程走 read_iter 踩 NULL f_op，或 configfs bin_file blob 的 pread（6×SET_NAME 未實證）。
- `read_iter=ashmem_read_iter`：不 panic，但 reclaim ~0%（永遠裝不上）。

**下一輪方向（依優先）**：
1. 抓一次「LANDED + `configfs_read: blob OK, about to pread`」判斷 panic 是 read_iter NULL
   （並發）還是 bin_file blob（pread）——現有 v9.41 已加該 print，用 read_iter=NULL 反覆
   retry 即可。
2. 若 blob 是 panic 源：修 bin_file 的 zero_at（或換回 read_file + 接受並發 panic）。
3. 若 read_iter 是 panic 源：找「安全但非 ashmem_read_iter」的 read_iter 值（避免它把
   reclaim 歸零），或裝上後立刻 restore fops。
4. 攻 mode 5 reclaim 的 3x 落差：換 reclaim 載體（sendmsg/keyctl big_key/BPF map），或
   在 mode-5 write#2 之前先跑一次 mode-3 selftest 確認 reclaim 健康。

## 附錄 L：mode 5（FOPS）與 mode 3（selftest）的 reclaim 落差 — round 7 未解關鍵

**實測對照（round 7，同 build、fresh boot、GL_PURGE_CNT=0 + stride16）**：
- `--selftest`（mode 3）：**2/3 hit**（bash-4 與 bash-10 都 2/3，run1/run2 必中、run3 累積退化）。
- `--write2`（mode 5，FOPS install）：**0/6 × 4 次**（bash-6 只有 attempt2 曾 LANDED 一次）。

**已排除**：build 改動（bin_file/read_iter）沒影響 selftest（bash-10 用新 build 仍 2/3）；
slab_drain/spawn_child（v9.39 移除後仍 0/6）。`cleanup` 的 recv(consume) 也報 0 diffs，所以
不是 MSG_PEEK 假陰性；write#1 真的沒寫進 SKB = **mode 5 的 reclaim（SKB 搶頭）系統性失敗**。

**靜態上矛盾**：reclaim（prepare_good_kernel_page → send SKB）在 `set_pselect_write_mode`
**之前**，理論上與 mode 無關。兩者 rb_erase 都是「left-NULL」case（rb_left=NULL, rb_right≠0），
write#1 都寫 fake 頁。唯一差異是 mode 5 的 write#2 寫 **GLOBAL `ashmem_misc.fops`**（且每次都
落地、污染 fops），而 mode 3 的 write#2 寫 fake 頁內。

**最可疑（待下輪驗證）**：mode 5 的 write#2 污染全域 fops 後，**裝置背景進程**（camera/gfx/
audio 開 /dev/ashmem）踩到 dangling fake_fops 而 crash/slow，改變了後續 attempt 的 SLUB/排程
timing → reclaim 退化。但這解釋不了「attempt 1（污染前）也 miss」；可能 attempt 1 是 ~34% 的
純 flake、恰好連 4 次（機率 ~1.3%）。**下一輪直接抓「attempt 1 LANDED」那次驗證 configfs read
（bin_file 6×SET_NAME）是否回 8 → task walk → cred 寫入。**

## 附錄 K：FOPS install 已 LANDED、並發 panic 根因 + configfs_read_bin_file 切換（v9.38）

**實測（round 7，`GL_PURGE_CNT=0` + stride16 + bin_file 前）**：W2 route 的
`install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 231)` — **mode-5 FOPS install 首次
成功落地**（data_addr=text_addr + reclaim 修復的組合全部打通）。但隨後裝置立刻 panic
離線（concurrent ashmem read）。

**根因**：fake_fops->read 原本指 `configfs_read_file`（page@0x10）。它把
file->private_data 當 configfs_buffer，`count@0x00`="dev/ashm"（巨大）+ `page@0x10`=0
（並發 fd 是 zalloc）→ `simple_read_from_buffer` 對 0 位址 copy_to_user → **任何並發
進程一讀 /dev/ashmem 就 panic**。這是 mode-5 覆寫全域 fops 的固有風險。

**修復（v9.38）**：改指 `configfs_read_bin_file`（CFI_CONFIGFS_READ_BIN_OFF 0x1238e20）。
反組譯 `configfs_read_bin_file@0x49ec40` 定案：讀 `bin_buffer@0x58` + `bin_buffer_size@0x60`，
gate 在 `needs_read_fill@0x50`==0 且 `byte@0x55`==0。並發 fd（zalloc）的 bin_buffer_size=0
→ 回 0 無害，不 panic。

**configfs_read_once 改寫**：`ashmem_set_name_bin_blob()` 用 6 次 SET_NAME（zero_at 技巧）：
1 次 full blob（`name[77..84]=target`、`name[85]=len`、`name[86]=NUL`，placeholder 0x01），
再 5 次 descending 單-NUL re-SET_NAME 依序 zero `[74],[72],[71],[70],[69]`（=byte@0x55 與
needs_read_fill 的 4 bytes），讓 strscpy 在 target 之前截斷、不碰 `name[77..85]`。

**v9.38b**：fake_fops 再補 `.read_iter@0x20 = ashmem_read_iter.cfi_jt`（原本是 0），避免
並發進程走 read_iter 路徑（sendfile/aio/splice/io_uring）踩 NULL f_op。

**round 7 實測殘留**：W2 route 兩次都卡在 reclaim 的「dip=1 但 0 diffs」（SKB 搶頭 flake）
——GL_PURGE_CNT=0 把命中率從 ~0/5 拉到 selftest 2/3，但仍是 ~66%/attempt 的機率、且每
run 累積退化（run3 起 dip=0）。W2 需在 fresh boot 上第 1-2 次 attempt 就命中。**尚未**
看到 configfs read 回 8（FOPS install 曾 LANDED 兩次但都在 open/configfs 前並發 panic 或
reclaim miss）。下一輪：fresh boot + GL_PURGE_CNT=0 + stride16 反覆 retry W2，或繼續壓
flood/dummy kernel stack 的 order-2 爆衝（換 reclaim 載體）。

## 附錄 J：reclaim 三次根因 — purge squad 的 kernel stack 爆衝埋掉 leak slab（GL_PURGE_CNT=0 解）

**實測（round 7，`--selftest` SELFTEST_RUNS=3，`GL_PURGE_CNT=0` + stride16）**：
**2/3 hit**（run1 vehicle[157] STRICT OK、run2 vehicle[26] STRICT OK、run3 累積退化 dip=0）。
對照 GL_PURGE_CNT=700 時 ~0/5。**這是 reclaim 穩定度的決定性突破。**

**根因**：purge squad（V9_PURGE_CNT=700）的子進程 kernel stack（16KB=order-2）經 vmap
lazy list 在 gate window 爆衝釋放（o2 +1200~+3200），把 leak slab（mm_struct order-2）
埋進 buddy LIFO 深處 → 第一個 SKB 搶不到（0 diffs）。

**修復（v9.37）**：`V9_PURGE_CNT` 改成 env `GL_PURGE_CNT`（預設 700）。設 **0** 後，
flood+dummy 的 kernel stack（~20MB）停在 vmap lazy list（**低於 ~30MB purge 門檻，不進
buddy**），buddy 裡唯一 order-2 free 就是 leak slab → SKB 必搶到。

**殘留**：每 run 的 memfd pin 累積（~80 slab/run）使第 3 run 起 dip=0。所以實務是
**fresh boot 上第 1-2 次 FOPS install 就該成功**；失敗就 reboot 重來。

## 附錄 I：write 原語的本質 — 「已知值 → 已知位址」、reclaim-無關

rb_erase 的兩筆寫入是**耦合**的（case-1，`node->rb_left==NULL`）：
- **WRITE#1** `str x8,[x11]`：`node->rb_right`(=write_value) → `parent->rb_left/right`(=target)。
  parent=`tree_pc & ~3` = target-8（mode 1/4）或 fake_fops（mode 5）。
- **WRITE#2** `str x9,[x8]`：`tree_pc` → `write_value`（**write_value 必須是合法位址**，
  否則 panic）。

所以 write_value 只能是「合法 kernel 位址」：
- mode 1：`page_base+SAFE_OFF`（fake 頁內，低 byte 0x00）→ 只能把**這個位址值**寫到 target。
  W1 靠低 byte 0x00 翻 enforcing，**與 reclaim 無關、每次 punch 必中**（已驗證 Permissive）。
- mode 2：`text_addr(INIT_CRED)` → 可把 init_cred 位址寫到任意 target，但 target 必須
  **已知**（task->cred 是 heap、需先讀 → 卡 FOPS/reclaim）。
- mode 5：write_value=fops_field（全域）→ WRITE#2 寫 fops 是**必然落地**的（附錄 F）。

**結論**：W1 的提權是**確定性**的（全域目標 + 合法位址值），已達成；W2（uid=0）需要
**任意讀**（找 child task_struct→cred），只能走 FOPS/configfs → 仍卡 reclaim。modprobe_path/
core_pattern 路線因 write_value 必須是合法位址（寫字串會讓 WRITE#2 寫到非法位址 panic）
而**不可行**。下一輪若走全域寫，可考慮 mode 2 把 init_cred 寫到某個**已知全域**（如把某
process 的 cred 指針全域化），或繼續攻 reclaim 解鎖任意讀。

## 附錄 H：SKB 搶頭失敗 — settle 太短、purge 爆衝埋掉 leak slab（v9.36 加長 settle）

**實測**：即便 flush 修好（dip=1），仍常見 `cleanup: 0 diffs`（SKB 沒吃下 leak slab）。
log 證據：`settle[settle]: 13 rounds, o2=1449` 之後 `o2 1 -> 2706`（gate window +1257 個
order-2 slab）。這 1257 不是 phase-3 的 ~33 個 kill 造成的，而是 phase-1 的 700 purge +
765 flood 子進程 kernel stack 經 vmap lazy list 的 **burst 式 purge**（持續 10s+）在
phase-3 期間才爆出來，把 leak slab 埋進 buddy LIFO 深處。

**根因**：settle 的 `quiet=6`（3 秒雙穩定）在 purge burst 的空檔就通過了；purge 是
bursty（空 3s 再爆），所以 3s quiet 是假穩定。

**修復（v9.36）**：`settle_wait("settle", 12, 20, 60)` — 要求 **20 個 quiet round（10s）**
的 o2 與 objs 同時穩定，讓 purge 徹底排空再進 phase 3。cap 40→60 round（20s→30s）。

**實測結果（v9.36）**：**未解**。settle 常撞 61-round cap（quiet 只到 5/8/18，o2 持續波動
>30s）；即便 quiet 真到 20（24 rounds），gate window 仍 `o2 74 -> 3313`（+3239）。也就是
phase-3 的 +1000~+3000 order-2 爆衝**不全是 phase-1 purge 殘留**，phase-3 自己的 kill
（或 kernel stack vfree 觸發的新 purge）在 gate 當下又灌爆 buddy。SKB 搶頭仍是主 flake。

**下一輪方向（W2 解鎖優先級）**：
1. 先 `--selftest` 確認 reclaim 健康再 `--write2`（單次、fresh、GL_TAIL_KILLS=16）。
2. 更換 reclaim 載體（sendmsg/keyctl big_key/BPF map）避免 SKB 的 order-2 搶頭競爭。
3. 或改走「deterministic write#1 + modprobe_path/core_pattern」全域寫提權（見下附錄 I）。

## 附錄 G：reclaim 二次根因 — GL_TAIL_KILLS 連續殺錯（已修成 stride）

**實測（round 5 後半，fresh device，`--selftest` SELFTEST_RUNS=5, GL_TAIL_KILLS=34）**：
- run 1：`dip=1` → vehicle[325] STRICT OK（**第一次中**）。
- run 2-5：`dip=0 calm=364`（leak slab 永不 discard）→ 全 miss。
- 每 run `objs` 累積 +~130（memfd pin mm leak），reclaim 隨之退化。

**根因（slub_5.10.209.c 源碼定案）**：`put_cpu_partial` 的 flush 條件是
`drain && old_pobjects > slub_cpu_partial(s)`（mm_struct size≥256 → **cpu_partial=13**）。
`pobjects` 是整條 per-CPU partial 鏈的**累計**；一個 slab 只有「第一個 free」會進
`put_cpu_partial`（其餘走 FREE_FROZEN fastpath 不加 pobjects），每個 dummy slab 只貢獻 **+1**。
leak slab 凍結時 pobjects=1。要 overflow（>13）需再 +13 → 需 **14 個不同 dummy slab 的
first-free**。

**BUG**：舊 loop `for(i=dk; i<dk+tail; i++) kill_child(dummy[i])` 是**連續**殺，
`GL_TAIL_KILLS=34` 只跨 ~2 個 dummy slab（只 +2 pobjects），根本到不了 13。34 之所以
「有時中」，全靠 phase-1 殘留 pobjects 剛好落在 10<residual≤13 的窄窗（撞運氣）。

**修復（v9.35）**：改成 **stride**（每個 dummy slab 殺第一個孩子）：
```c
for (int j = 0; j < tail; j++) {
  int i = dk + j * MM_OBJS_PER_SLAB;   // 每個 slab 的第一個孩子
  if (i >= V9_DUMMY_CNT) break;
  kill_child(dummy[i]);
}
```
`GL_TAIL_KILLS` 語義改為「跨幾個 dummy slab」；**14 = 決定性最小值**（clean chain：
leak 1 + 14 first-free → old_pobjects 14 > 13）。建議 `GL_TAIL_KILLS=16`（留裕度）。

## 附錄 F：W2 ENOTTY 根因定案（mode-5 write#2 必然污染 fops）

**最小 ioctl 測試（實機）**：`ashmem_ioctl_test`（`src/ashmem_ioctl_test.c`）：
- fresh boot：`SET_NAME→r=0 GET_NAME→r=0`（ioctl 0x41007701/0x81007702 正常）。
  `GET_SIZE` 用錯編號（num=3=0x80087703）回 ENOTTY，正確是 num=4=0x80087704。
- 跑過 W2 後（同 boot）：三項全 ENOTTY → **證明 W2 已污染 `ashmem_misc.fops`**。
- reboot 後恢復正常 → 污染是 runtime 的，reboot 清除。

**靜態定案**：
- `ashmem_misc@0x29bcbb0`：minor=0x7f, fops@+0x10=ashmem_fops@0x1e977a8。✓
- `ashmem_fops` 實體 dump：`unlocked_ioctl@0x50=ashmem_ioctl.cfi_jt@0x123e118`（`bti c;b ashmem_ioctl`）。✓
- `ashmem_ioctl@0xc67e2c` 反組譯：`0xc67ff4` 比對 `0x41007701`==SET_NAME → copy_from_user→
  strscpy，回 0；default 回 -ENOTTY。**所以 ioctl 只要走到 ashmem_ioctl 就一定成功。**
- `do_vfs_ioctl@0x3ee250` + `misc_open@0x86bd18` 反組譯：`file->f_op@0x28`，ioctl fall-through
  走 `f_op->unlocked_ioctl@0x50`；NULL 或 -ENOIOCTLCMD → ENOTTY。

**結論**：ENOTTY = fd 的 f_op 已非 ashmem_fops，而是 mode-5 write#2 寫進的 **stale
fake_fops**（reclaim fail 時 page_base 是已釋放 slab，其 +0x50 unlocked_ioctl 槽為 0）。
mode-5 的 rb_erase write#2 寫 GLOBAL `fops_field=text_addr(ASHMEM_MISC+0x10)`，**與 reclaim
無關、每次都落地**；只有 write#1（寫 fake 頁 llseek）受 reclaim 影響。故 W2 成功條件 =
reclaim 成功（write#1 LANDED）。本輪 6 次全 MISSED 是裝置退化（memfd 累積 pin mm）。

**對策（下輪）**：fresh boot + settle ~380–400s + `GL_TAIL_KILLS=34` + `FOPS_ATTEMPTS` 拉高；
或先 `--selftest` 確認 reclaim 健康再 `--write2`。失敗會污染 fops，retry 的 write#2 會覆蓋，
但兩次嘗試間若有並發進程 open /dev/ashmem 會踩 stale fops → panic（UPDATE 2 已知），故失敗後
寧可 reboot 重來。

## 附錄 A：本輪實測 log 摘錄

reclaim 命中（`--selftest`, GL_TAIL_KILLS=34, fresh, attempt 1）：
```
[*] v9.10 gate: mm slabs 127 -> 126 objs 847 -> 823 (dip=1 calm=0)
[*] vehicle[618] diffs ...
[*] vehicle[618]: STRICT child_pc=OK scratch=OK (exp child_pc=ffffff80566652f9 ...)
[+] SELFTEST: write into sprayed page verified: 1 vehicle(s) carried the STRICT walk signature (attempt 1)
```

W1（GL_TAIL_KILLS=34, STRICT 命中但 enforce 不翻）：
```
[*] vehicle[515]: STRICT child_pc=OK scratch=OK (exp child_pc=ffffff8002ca0139 ...)
[+] W1: selinux_state.enforcing verified: 1 vehicle(s) carried the STRICT walk signature (attempt 1)
[*] enforce file = '1' (raw 31 00 00 00)   ← data_addr(0x80000000) 沒寫到真的 selinux_state
```

## 附錄 U：v9.46 — configfs pread 恐慌的根因定案（read_bin_file 的無條件鏈）

**背景**：bash-21/22 三次都在 `configfs_read: blob OK, about to pread` 之後立刻 panic
（`device not found`），且 `dmesg` 在 reboot 後只剩 boot 訊息、看不到 panic trace —— 3/3
**決定性**恐慌，不是隨機並發。

**反組譯定案**（capstone，kernel@`binaries/extracted_1.22/kernel`，BASE=`0xffffffc008000000`）：
- `CFI_CONFIGFS_READ_BIN` jt@`0x1238e20` → `b configfs_read_bin_file@0x49ec40`。
- `CFI_CONFIGFS_READ_FILE` jt@`0x1239168` → `b configfs_read_file@0x49e8e0`。

兩個 read 函式**結構不同**：

| | `configfs_read_bin_file` (0x49ec40) | `configfs_read_file` (0x49e8e0) |
|---|---|---|
| `file+0x18→+0x78→+0x50` 鏈 | **無條件**（`ldr x8,[x0,#0x18]`→`[x8,#0x78]`→`[x8,#0x50]` 在分支前就執行） | **有條件**（只在 `needs_read_fill@0x50 != 0` 的 fill 分支內） |
| 讀取來源 | `bin_buffer@0x58` + `bin_buffer_size@0x60` | `page@0x10` + `count@0x00` |
| 我們 read（needs_read_fill=0） | 仍先 deref 三層鏈 → devtmpfs /dev/ashmem inode 的 `inode+0x78`/`+0x50` 命中 NULL/垃圾 → **panic** | 跳過鏈 → 直達 `simple_read_from_buffer(buf,count,ppos,page,count@0x00)` → 安全 |

**根因**：`file+0x18` = `f_inode`（devtmpfs inode）；`configfs_read_bin_file` 把 ashmem 的
`/dev/ashmem`（devtmpfs）file 當成 configfs file，無條件走 `f_inode→i_sb→…` 的 configfs 專屬
鏈，deref 到 NULL/垃圾 → 我們自己的 pread 每次都 panic。**這是決定性恐慌，不是並發進程。**

**v9.46 修正**：`.read` 預設改指 `CFI_CONFIGFS_READ_FILE_OFF`（`configfs_read_file`），並新增
`ashmem_set_name_read_blob()`：
- `page@0x10 = name[5..12]`（`memcpy(name+5,&target,8)`）。
- `name[13]=0` 終結 → `strlcpy` 只寫 13 bytes，**永遠不碰到** mutex(name[21..28]) 與
  needs_read_fill(name[69..72]) → 它們從 fresh kzalloc 起就是 0，每次 read 都維持 0 → **不需
  要 bin_file 那 13 次逐位元清零**。
- `count@0x00` = 固定 prefix `"dev/ashm"` = `0x6d6873612f766564`（巨大），
  `simple_read_from_buffer` 的 available ≥ len 恆成立 → 正常回 8 bytes。

`GL_BIN_FILE=1` 可切回 bin_file 路線做對照。

**殘餘風險**：read_file 的**並發**路徑（背景進程 open /dev/ashmem 後 `read()`）會
`page@0x10=0` → `simple_read_from_buffer(from=NULL)` → `copy_to_user(NULL)` → panic（這才是
真正的並發 NULL-page panic，v9.43 觀察到的）。但背景進程 read() ashmem 極罕見（通常 mmap+
ioctl），機率遠低於 bin_file 的 100% 決定性恐慌。若仍需消除，方向：讀完立刻 restore fops
（mode-5 反寫，會把真的 ashmem_fops->llseek 弄髒）或串列 console 定位。

## 附錄 V：v9.47 — read_file 實測通過 + 反向 chunk walk + slab_drain

**v9.46 實測（bash-23 run 2）**：read_file 路線的 probe **成功**：
```
[*] configfs_read: blob OK, about to pread
[*] configfs_read target=0xffffffe873febd08 len=8 -> r=8 errno=0     ← 第一次 pread 回來 r=8！
[*] configfs probe: init_task.tasks.next = 0xffffff8001032948
[+] fake_fops installed into ashmem_misc.fops
```
→ **決定性恐慌（bin_file 的無條件鏈）確認修復**。舊 forward walk 在第 3 次 read
（`task+TASK_TGID_OFF=0xffffff8001032a4c`，direct-map heap 位址）panic —— 並發進程
踩全局 fake_fops（或 direct-map read panic，待 GL_READ_TARGET 二分）。

**v9.47 兩項修正**：
1. **反向 chunk walk**（`find_task_by_tgid` 重寫）：從 `init_task.tasks.prev`（最新
   fork 的 task，child 剛 fork 完就落在 tail 附近）**反向**走，每步 1 次 0x110-byte
   chunk read（一次涵蓋 tasks.next@0/tasks.prev@8/pid@0x100/tgid@0x104）。把 walk 從
   ~1000 次 read 壓到 ~10 次，窗口從秒級降到毫秒級，大幅降低並發 panic 機率。
2. **FOPS install 前加 `slab_drain()`**：selftest/write1 路徑都有 `slab_drain()`（3 波
   fork/kill churn 暖 per-CPU partial chain），唯獨 W2 FOPS 路線漏掉 —— 這是 mode-5
   write#1 落地 ~25% vs selftest 67% 的「矛盾」最可能原因。

**殘餘問題**：
- reclaim 不穩（write#1 LANDED ~25-50%）；MISSED 時 mode-5 write#2 仍寫入 stale
  fake_fops → 裝置不穩（背景進程隨時 panic）。slab_drain 應可改善，待測。
- cred write 階段（do_cred_write × W2_ATTEMPTS）fake_fops 仍全局掛著（~1-20s 窗口），
  若並發 panic 仍在，需在 walk 後立刻 restore fops（mode-5 反寫，代價：真
  ashmem_fops->llseek 被寫成 fops_target=資料位址 → 背景 lseek 會 panic，但 read/mmap/
  ioctl/release 全走真 fops 安全，比掛 fake_fops 好）。

## 附錄 W：v9.48 — walk 加固（forward fallback + 指標校驗）

`find_task_by_tgid` 重構為 `walk_tasks(fd, head, tgid, backward)`：
- 先**反向**（`init_task.tasks.prev`，child 剛 fork 在 tail 附近，~10 步）。
- 反向失敗時**正向** fallback（`init_task.tasks.next`，~1000 步，慢但兜底）。
- 每步讀前校驗 `entry` 為 canonical kernel 位址（`& 0xffff000000000000`），擋掉
  strscpy 0x00 截斷產生的 0x41-fill/半零垃圾指標，避免走到 -EFAULT 直接失敗。

**未實作（待 device 回來實測再決定）**：cred-write 階段 fops restore（mode-5 反寫
`tree_pc=ashmem_fops` → `ashmem_misc.fops` 復原）。代價：write#1 把真
`ashmem_fops->llseek` 寫成 `fops_target`（資料位址）→ 背景 lseek panic；且 restore 的
`prepare_good_kernel_page()` 會 reset `skb_sv`（install 的 fake_fops SKB fd 丟失，但
SKB 仍在 kernel socket queue 存活、process exit 才釋放，無害）。因不可測，暫緩——先驗證
backward walk + slab_drain 是否已把並發 panic 壓到可接受。

## 附錄 X：v9.49 — W2 定案：configfs read 無法讀 heap/direct-map（GL_READ_TARGET 實證）

**決定性實驗（bash-28，read_file build + GL_READ_TARGET override）**：
- `GL_READ_TARGET=0xffffff8000100000`（direct-map, phys 1MB）→ reclaim LANDED →
  `configfs_read: blob OK, about to pread` → **panic**（無 `r=`）。
- 對照：kernel-image 位址（head=0xffffffde…）probe 一直回 `r=8`/`r=16`。
- 加上 bash-23 run2（forward walk 讀 `init_task.tasks.next` 推得的 heap 位址
  `0xffffff8001032a4c` 也 panic）、bash-27 run1/2（backward walk 讀
  `init_task.tasks.prev` 也 panic）→ **一致：任何 direct-map（0xffffff80…）讀取都 panic，
  kernel-image（0xffffffde…）讀取正常。**

**排除 panic_on_warn**：`/proc/sys/kernel/panic_on_warn = 0`（實測）。所以不是
check_copy_size 的 WARN→panic。反組譯證實 `usercopy_abort@0x3cc9ec`/`check_heap_object
@0x3b7c7c` 都只 `bl` 到 `__warn`/`usercopy_warn`，**不直接 panic**。panic 來自
`check_heap_object` 的 **vmemmap walk**（`pfn_to_page` 對 direct-map 位址解出垃圾
`struct page` 後 deref），或 `raw_copy_to_user` 對未映射位址的 fault —— 兩者都不是
panic_on_warn 可控。

**根因**：本機 configfs read 的 `copy_to_user` 只能過 `_text.._end` whitelist
（kernel-image 0xffffffde…），heap/direct-map（0xffffff80…）走 check_heap_object 的
vmemmap walk → 對 RV101 的非標準實體記憶體配置（kernel 在 HIGH physical）解出垃圾 →
panic。**configfs 任意讀被限制在 kernel-image，讀不到 task_struct（heap）。**

**對 W2 的影響**：W2 = FOPS read → task walk（init_task.tasks 串列在 heap）→ 找 child
task_struct → cred=init_cred。task walk 每一步都要讀 heap task_struct → panic。**W2
（uid=0）在現有 FOPS-read 路線上被此核心限制擋死。**

**已達成**：W1（--write1 → getenforce=Permissive）確定可用，是實機驗證過的提權
（SELinux 關閉）。W2 的 read primitive（read_file）本身已驗證 `r=8`，但只能讀
kernel-image，無法定位 heap 中的 child cred。

**潛在出路（未試，需 device 恢復）**：
1. `GL_READ_TARGET=init_task.tasks.next` 的**確切值**再測一次（bash-28 那次 reclaim
   MISSED 沒測到），確認是否「mapped heap 也 panic」還是「只有 1MB reserved 位址 panic」。
2. kptr_restrict=0（W1 寫 KPTR_RESTRICT_OFF）→ /proc 洩漏 child task_struct 位址，再用
   mode-2 寫 init_cred（繞過 heap read）。
3. 換 read 載體（非 configfs）。

## 附錄 Y：v9.50/v9.51 — prev-read 突破 + cred-write 落地但 uid=-128

**突破**：task walk 不再需要 heap read。`init_task.tasks` 是 STATIC（kernel-image 可讀），
fork 把新 task 插到串列 TAIL，所以 `init_task.tasks.prev`（head+8，單次 kernel-image read）
就拿到最新 task 的 tasks 位址 → `task = prev - 0x4c8` 就是剛 fork 的 child task_struct。
**實測（bash-2 run2）**：`prev=0xffffff802b854dc8 prev2=... stable=1`（兩次讀一致）。

**cred write 落地證據**：`do_cred_write`（mode-2）後
`init_cred.usage before=0x4 after_cred=0xffffff802b855079 after_real=0xffffff802b855071`。
`after_cred = (task+0x778)|1 = tree_pc` → **rb_erase 的 write#2 確實落地**（寫 init_cred.usage），
同一次 rb_erase 的 write#1（寫 init_cred 位址到 task+0x780 cred）理應同時落地。

**init_cred 驗證**（kernel-image read，DIAG）：
`init_cred=0xffffffd676402058 usage=0x4 @0x08=0 @0x1c=0` → **uid=gid=0（root）**，
data_addr(INIT_CRED) 正確。

**矛盾（未解）**：write#2 落地 + init_cred->uid=0，但 child 回報
`child uid = 4294967168 (0xffffff80)`（=-128），不是 0 也不是 2000。write#1/write#2 的
值都不是 0xffffff80（write#1=0xffffffd676402058, write#2=0xffffff802b855079），
所以 -128 不是直接寫進去的值。最可能：write#1 落點方向（parent->rb_left==node 分支）
或 pselect fd_set 對 kernel-image 位址（0xffffffd6…）的編碼有誤，導致寫到錯誤欄位。

**殘餘問題**：
1. **child uid=-128**（cred write 結果錯誤，主阻塞）。
2. **fake_fops stale**（cred write 的 reclaim reset `skb_sv` 後，下一 round 的 SET_NAME
   ENOTTY）—— run2 round2+ 全部 ENOTTY。需在每次 read 前重裝 fake_fops，或 save/restore
   skb_sv。
3. 並發 panic（背景進程踩 global fake_fops，DIAG 的 MSG_PEEK 掃描 ~768 vehicles 拖長窗口）。

**下一步**：查 write#1 落點（方向分支）與 -128 來源；修 fake_fops stale（cred write 前
save skb_sv / 或 read 完立刻 restore fops）。

## 附錄 Z：v9.52 — child uid=-128 根因定案 + repair

**根因（反組譯 `__arm64_sys_getuid@0x1735b8` 定案）**：
```
mrs x8, sp_el0          ; current
ldr x8, [x8, #0x780]    ; current->cred  (cred POINTER @ 0x780 ✓)
ldr w8, [x8, #4]        ; cred->uid @ **0x04**  ← uid 在 offset 4，不是 8！
```
本機 `struct cred` 的 `usage` 是 **4-byte `atomic_t`**（不是 8-byte `atomic_long_t`），
所以 uid @ 0x04。mode-2 的 write#2（`str tree_pc, [init_cred+0]`，8-byte）覆寫
usage(0x00-0x04) + **uid(0x04-0x08)** → uid = tree_pc>>32 = `0xffffff80`（=-128）。
這解釋了 `child uid = 4294967168 (0xffffff80)`。

**證據**：`init_cred.usage before=0x4 after_cred=0xffffff802b855079`（write#2 落地），
DIAG `init_cred @0x08=0`（那是 gid 不是 uid！uid 在 0x04）。所以 init_cred 本身 uid=0，
但 write#2 把 uid@0x04 寫成 0xffffff80。

**v9.52 repair**：cred write 後再 mode-4 寫一個低 4 bytes=0 的值進 `init_cred+0x04`
（uid），把 uid 修回 0。write#2 會落在 `*value`。
- `value=0xffffff8000000000`（direct-map base=phys memstart）→ **panic**（bash-4 run1）。
- `value=0xffffff8010000000`（phys memstart+256MB）→ 待測（bash-5），但 reclaim 0/6 MISSED。

**殘餘**：repair 的 write#2 落點必須是「可寫且非關鍵」的 kernel 位址，但「低 4 bytes=0」
的值都落在 low physical。更安全選項待定（例如選一個 .bss 對齊 4GB 的符號，或改用
sprayed page 但讓其 base 對齊到 4GB 邊界——不可行）。

## 附錄 AA：v9.58 — W2 達成！child uid=0 (mode-5 cred write)

**修復 -128**：`do_cred_write` 從 mode-2 改 **mode-5**。mode-2 的 write#2 寫
init_cred.usage（offset 0），因 cred.usage 是 4-byte atomic_t，uid@0x04 被寫成
tree_pc>>32=0xffffff80。mode-5 的 write#2 寫 `*write_value`=cred slot（=init_cred，正確），
write#1 寫 write_target=init_cred+8（gid，不碰 uid@0x04）。→ getuid()=init_cred->uid=0。

**實測（bash-7 run1）**：
```
configfs probe: init_task.tasks.next = 0xffffff8001037248
round 1..4: child uid = 2000        ← cred write 的 reclaim 沒落地（uid 沒變）
round 5:    child uid = 0 (0x00000000)
[+] child is root!
[+] GHOSTLOCK ROOT: uid=0 gid=483095800
uid=0(root) gid=483095800(u4830_i5800) groups=... context=u:r:kernel:s0
[+] dropping to root shell
[T+551184ms] exploit complete — root shell spawned via .ghostlock_root.sh
```
後續驗證：`/data/local/tmp/su_gl` 由 **root(uid 0)** 建立（`-rwxr-xr-x 1 root ...`），
證明 child 確實以 uid=0 執行了 root script 的 `cp`。

**W2 三階段全通**：FOPS install → prev-read（無 heap read，讀 init_task.tasks.prev
STATIC）→ mode-5 cred write → **id uid=0**。

**達成總結**：
1. reclaim 穩定度：slab_drain + GL_TAIL_KILLS/GL_PURGE_CNT 調參 + mode-5 + prev-read。
2. W1（--write1 → getenforce=Permissive）：**已驗證**。
3. W2（FOPS 路線 → cred=init_cred → id uid=0）：**已驗證（child uid=0）**。
