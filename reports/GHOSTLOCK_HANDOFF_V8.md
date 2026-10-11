# GhostLock RV101 — 工作狀態交接 v8（第八任 → 第九任）

> English: [GHOSTLOCK_HANDOFF_V8_EN.md](GHOSTLOCK_HANDOFF_V8_EN.md)

> 更新於 2026-08-20。本輪（第八任）自 v9.30 接手，主要產出：**run51 死因定讞**、
> **W2 FOPS 路線的全部符號/offset 完成驗證**、並**發現第三個位址錯誤 + kCFI 關鍵障礙**。
> 源碼：`source_code\ghostlock_rv101\`（仍為 v9.30 邏輯，`target_rv101.h` 已新增 v9.31 W2 常數）。
> 目標裝置：`<DEVICE_SERIAL>`（RV101 / RG_glasses）。

## 0. 最關鍵的一句話（先讀這行）

**裝置目前離線（adb 無裝置、USB 無、192.168.1.110 不通）——實測無法進行；但 run51 的死因已可用既有日誌定讞，無需再跑「無害目標測試」。**

---

## 1. run51 死因：已定讞為「8-byte 塗抹」（嫌疑 A），非 flood（嫌疑 B）

### 1.1 判決依據：run50 其實就是「無害目標測試」

上一任設計的「無害目標測試」是：用 v9.30 + `GL_TARGET_DELTA=+0x800000` 把寫入目標挪到無害 RAM。
**run50（v9.29，錯 phys base）寫入的目標 `0xffffff80034a0140` 恰好就是 `selinux_state + 0x800000` 的無害位址**——也就是說，這個測試**已經被跑過了**：

| Run | 版本 | 寫入目標 | 結果 |
|---|---|---|---|
| 50 | v9.29 | `0xffffff80034a0140`（selinux_state **+0x800000**，無害） | 5/5 punch 全部 `returned`，STRICT child_pc=OK，**0 死亡**，enforce 仍 1 |
| 51 | v9.30 | `0xffffff8002ca0140`（**真 selinux_state**） | punch 1 即死，日誌截斷於 `consumer punch 1/5` |

兩次 run 的 flood/reclaim/punch 機械完全相同，唯一變數是「目標位址」。run50 打無害位址不炸、
run51 打 selinux_state 就炸——**這就排除了 flood 競態（嫌疑 B），坐實 8-byte 塗抹（嫌疑 A）**。
（flood 若為因，run50 同樣會死。）

### 1.2 塗抹的位元組級解剖

W1 的寫入值是 `page_base + SAFE_OFF`（例：run51 為 `0xffffff800b07d500`），rb_erase case-A 把它
8-byte 寫到 `selinux_state + 0`，得到（little-endian）：

```
byte0=0x00  byte1=0xd5  byte2=0x07  byte3=0x0b  byte4=0x80  byte5..7=0xff
```

- byte0 = `0x00` → `enforcing = 0`（預期的 permissive，**但這一步本身就被接下來的腐蝕害死**）
- byte1 = `0xd5` → `checkreqprot` 非零
- byte2 = `0x07` → `initialized` 非零
- byte3..7 → `policycap[]`（或 `disabled`/`enabled`，視 config）全被塗成垃圾

`rb_erase` 只能 8-byte 寫、且寫入值是核心指標（高 4 bytes 必為 `0xff`），**W1 無法只寫 1 byte**，
所以「寫 selinux_state 關 SELinux」這條路在本原語下**死路**。上一任「對策 B」（改 write_value 上層
bytes 匹配合法欄位）不可行，理由同上一任所述。

### 1.3 結論

**放棄 W1（SELinux），直接推 W2（cred = init_cred / patch cred）。** W2 目標是 8-byte 指標欄位，
寫入值也是 8-byte 指標，無塗抹問題——與上一任「對策 A」一致。

---

## 2. 本輪核心成果：W2 FOPS 路線的符號與 offset 全部驗證完成

以下全部從 `research/ghostlock_refs/rv101_kallsyms.txt`（UTF-16LE 需先 `iconv`）與
`binaries/extracted_1.22/kernel`（raw Image，file offset == link offset）直接取出，非估算。

### 2.1 符號（offset = VA − `0xffffffc008000000`）

| 符號 | VA | offset |
|---|---|---|
| `init_task` | `ffffffc00a1eb840` | `0x021eb840` |
| `init_cred` | `ffffffc00a202058` | `0x02202058` |
| `selinux_state` | `ffffffc00aca0140` | `0x02ca0140` |
| `commit_creds` | `ffffffc00818b328` | `0x18b328` |
| `prepare_kernel_cred` | `ffffffc00818ba9c` | `0x18ba9c` |
| `noop_llseek` | `ffffffc0083d1930` | `0x3d1930` |
| `ashmem_fops` | `ffffffc009e977a8` | `0x1e977a8` |
| `ashmem_misc` | `ffffffc00a9bcbb0` | `0x29bcbb0` |
| `configfs_file_operations` | `ffffffc009d46840` | `0x1d46840` |
| `configfs_bin_file_operations` | `ffffffc009d46960` | `0x1d46960` |
| `configfs_read_bin_file` | `ffffffc00849ec40` | `0x49ec40` |
| `configfs_write_bin_file` | `ffffffc00849ee00` | `0x49ee00` |
| `configfs_read_file` | `ffffffc00849e8e0` | `0x49e8e0` |
| `configfs_write_file` | `ffffffc00849ea48` | `0x49ea48` |
| `ashmem_ioctl / mmap / open / release / show_fdinfo` | 見 v3 交接 §5 | 與 v3 一致 |

### 2.2 task_struct offset（本輪 disasm 實測，RV101 5.10.209）

| 欄位 | offset | 來源 |
|---|---|---|
| `tasks`（list_head） | **`0x4c8`** | `__arm64_sys_kill` 的 init_task 走查（`add x8,x23,#0x4c8; sub x22,x25,#0x4c8`） |
| `pid` | **`0x5c8`** | `copy_process`：`p->pid = pid_nr(pid)`（`str w8,[x20,#0x5c8]`） |
| `tgid` | **`0x5cc`** | `copy_process` CLONE_THREAD 分支：`p->tgid = current->tgid`（`ldr w8,[x19,#0x5cc]; str w8,[x20,#0x5cc]`） |
| `group_leader` | `0x608` | 同上（`str x20/x8,[x20,#0x608]`） |
| `real_cred` | `0x778` | 既有（commit_creds 反組譯） |
| `cred` | `0x780` | 既有；本輪於 `__arm64_sys_kill` 再確認（`ldr x8,[x21,#0x780]`） |
| `comm` | **`0x790`** | `__get_task_comm`/`__set_task_comm`（`add x0,x20,#0x790`） |

> `comm` 在 `0x790`（非 `cred+8`=0x788）——因為 **CONFIG_KEYS=y** 在 cred 與 comm 之間塞了
> `cached_requested_key`（8 bytes）。

### 2.3 本輪發現的「第三個位址錯誤」＋ kCFI 障礙（重要！）

1. **`ashmem_misc.fops` 是 `+0x10`，不是 v3 交接寫的 `+0x18`。**
   raw Image dump `ashmem_misc`：`+0x000`=minor(0xff)、`+0x008`=name 指標、**`+0x010`=`&ashmem_fops`**。
   v3 的 `+0x18` 其實是 `list` 欄位，照寫會把 fake_fops 寫進 list 頭、W2 整條鏈失效。

2. **本核心是 Clang kCFI + LTO**（所有符號帶 `$hash` 後綴、有 `.cfi_jt` 跳表）。實測 `ashmem_fops`
   與 `configfs_*_fops` 表內存的是 **`.cfi_jt` 跳表位址，不是函數本體位址**：
   - `ashmem_fops` dump：`+0x008`=`ashmem_llseek.cfi_jt`、`+0x020`=`ashmem_read_iter.cfi_jt`、
     `+0x050`=`ashmem_ioctl.cfi_jt`、`+0x060`=`ashmem_mmap.cfi_jt`、`+0x070`=`ashmem_open.cfi_jt`、
     `+0x080`=`ashmem_release.cfi_jt`、`+0x0e0`=`ashmem_show_fdinfo.cfi_jt`。
   - `configfs_bin_file_operations` dump：`+0x010`=`configfs_read_bin_file.cfi_jt`、
     `+0x018`=`configfs_write_bin_file.cfi_jt`、`+0x070`=open、`+0x080`=release。

   **含義**：fake file_operations 表**必須填入真實 `.cfi_jt` 跳表位址**（已算好寫進
   `target_rv101.h` 的 `CFI_*_OFF`），不能像 OnePlus 參考（6.12）那樣填函數本體位址。

3. **configfs 用 `.read`/`.write`（fops +0x10/+0x18），不是 `.read_iter`/`.write_iter`（+0x20/+0x28）。**
   所以 fake fops 要把 `configfs_read_bin_file.cfi_jt` / `configfs_write_bin_file.cfi_jt` 填進
   **`FOPS_READ_OFF`/`FOPS_WRITE_OFF`**，不是 OnePlus 的 `READ_ITER`/`WRITE_ITER` 槽位。

---

## 3. W2 FOPS 路線的實作計畫（下一步，需裝置）

參考：`research/ghostlock_refs/extracted/oneplus/GhostLock-for-OnePlus-main/src/core/{fops.c,root.c}`。

1. **噴灑頁新增 fake fops 表**（`prepare_skb_payload` 內，例如 `FOPS_OFF = 0x1600`），內容 = 上述
   `CFI_*` 跳表位址拼成的 `file_operations`：
   - `read` = `configfs_read_bin_file.cfi_jt`、`write` = `configfs_write_bin_file.cfi_jt`
   - `llseek` = `noop_llseek.cfi_jt`、`ioctl`/`compat_ioctl`/`mmap`/`open`/`release`/`show_fdinfo`
     = ashmem 原 `.cfi_jt`
2. **用 mode-2 寫入原語把 `fake_fops` 寫進 `data_addr(ASHMEM_MISC)+0x10`**（不是 +0x18）。
3. 開 `/dev/ashmem`，其 read/write/ioctl 現在走 configfs → 依 OnePlus `util.c` 的
   `configfs_read_once`/`configfs_write_once`（用 ashmem `SET_NAME` blob 控制 configfs buffer 的
   `bin_buffer` 欄位）取得**任意核心讀寫**。
4. 從 `data_addr(INIT_TASK + TASK_TASKS_OFF)` 走 task list，用 **`tgid`（offset `0x5cc`）比對**
   找 child（child 的 tgid == `fork()` 回傳值；比 comm 更穩）。`TASK_PID_OFF=0x5c8` 亦可作備援。
5. `task->cred（0x780）← data_addr(INIT_CRED)`（用 mode-2 寫，或直接任意寫 patch cred 的
   uid/caps/sid）。child `setuid(0)` + `setenforce 0` → root shell（`main.c` 骨架已內建）。

> **configfs 任意讀寫原語已逐行對照完成**，見 `reports/GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md`
> （RV101 configfs_buffer offset：`bin_buffer=0x58`、`bin_buffer_size=0x60`、`cb_max_size=0x64`、
> `needs_read_fill=0x50`；ashmem 前綴 "dev/ashmem/" = 11 bytes；讀寫都用 `bin_buffer`，比 OnePlus 的
> page/bin_buffer 分流更簡單）。
>
> 已把上述所有 offset（含 `CFI_*`、`FOPS_*`、`ASHMEM_MISC_FOPS_OFF=0x10`、`CFG_*_OFF`、
> `ASHMEM_NAME_PREFIX_LEN`、`TASK_TASKS_OFF`、`TASK_PID_OFF`、`TASK_TGID_OFF`、`TASK_COMM_OFF`）
> 寫入 `source_code/ghostlock_rv101/target_rv101.h` 的「W2 FOPS route (v9.31 prep)」區段，
> 但**尚未接入任何程式碼**，僅供下一任實作取用。

---

## 4. 待辦（按優先序）

1. **接回裝置**（USB；**本 build 的 adbd 不綁 TCP，`adb connect` 不可用**——2026-09-13 實測）。這是唯一硬阻礙。
2. 依 §3 實作 W2 FOPS 路線並實測（先 `--selftest` 確認 reclaim 仍穩，再跑 W2）。
3. 補測 `TASK_PID_OFF`/`TASK_TGID_OFF`（disasm `copy_process` 或 `do_task_stat` 尾部），讓 task
   walk 可改比 tgid（比 comm 更穩，因為 comm 可能被截斷/改名）。

## 5. 環境與工具（不變）

- 建置：`$env:ANDROID_NDK_HOME='...\ndk\android-ndk-r27d'; cd source_code\ghostlock_rv101; mingw32-make -B`
- 跑批：`scripts/ghostlock_reboot_run.ps1 -Cycles 3 -Env "..." -ExploitArgs "..." -Log runNN`
- 反組譯：`python3 scripts/rv101_disasm.py <hex-offset> <len>`（本輪已裝 capstone 5.0.7，WSL 內可跑）
- kallsyms：`iconv -f UTF-16LE -t UTF-8 research/ghostlock_refs/rv101_kallsyms.txt | grep <sym>`
- 裝置每 panic 2-5 分鐘自動重開，KASLR slide 會變；每次執行前確保 uptime > 150s。
