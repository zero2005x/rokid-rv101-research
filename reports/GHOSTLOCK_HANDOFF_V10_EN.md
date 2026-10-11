# GhostLock RV101 — Work Status Handoff v10 (Tenth Holder: reclaim Fix + W1 Data-Path Assault)

> 中文版：[GHOSTLOCK_HANDOFF_V10.md](GHOSTLOCK_HANDOFF_V10.md)

> 📌 **Basis of fact**: whenever this document touches facts such as device generation, kernel config, CVE status, GhostLock offset, and product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; in case of conflict, that table adjudicates).


> Updated 2026-08-21. This round (the tenth holder) completed the following on the live device (<DEVICE_SERIAL>):
> 1. **Root-caused and fixed reclaim's "leak slab is never discarded" root cause**, and for the first time stably observed a STRICT hit.
> 2. **Settled by disassembly that the W1 target = `selinux_state + 0`** (the enforcing byte).
> 3. Found that `P0_KERNEL_PHYS_LOAD = 0x80000000` has **no effect** on the W1 write (enforce does not flip),
>    and the physical load base of the data path (data_addr) still requires empirical proof.

> **【round 5-6 major conclusion — W1 is fully verified, and write#1 is a deterministic global write unrelated to reclaim】**
> - After `data_addr()` was changed back to `text_addr()` (the RV101 kernel image is at a high physical address):
>   **both `--write1` (standard) and `--write1-text` have been validated on the live device → `getenforce` = Permissive**.
> - Key insight (confirmed by measurement): **write#1** of rb_erase writes a GLOBAL target
>   (`parent=tree_pc&~3=target-8`, writing `target` itself), and is **unrelated to reclaim** — even when
>   `cleanup: 0 diffs` (reclaim missed completely, STRICT does not pass), enforce still flips to 0.
>   So **W1 is "a deterministic privilege escalation that hits on every punch"**; reclaim only affects the observation
>   of write#2 (STRICT verification) and does not affect the actual write. This directly achieves the "verify privilege escalation"
>   part of the objective.
> - W2 (cred=init_cred → uid=0) still needs FOPS/configfs **arbitrary read** (to find the child task_struct),
>   and the fake_fops table lives in a reclaimed SKB page → reclaim success is still required. reclaim has been root-caused
>   (flush fixed with stride, see appendix G; SKB head-grab/accumulation is still racy, see below).

---

## 0. Most Critical Conclusions (read first)

### 0.1 reclaim fix (this round's biggest breakthrough)

The root cause of `--selftest` (mode 3) previously showing "0 diffs / dip=0": **the leak slab is frozen on the per-CPU partial
chain and is never discarded**. In `__slab_free` of SLUB 5.10, the first free of the leak slab triggers
`put_cpu_partial(drain=1)`, which freezes it; the following 16 frees take the `FREE_FROZEN` fastpath, and after the slab becomes empty
it is still frozen on the chain. It is only discarded when "some later free makes the chain's `pobjects > cpu_partial(13)` → `unfreeze_partials`".
The old design put the dummy kills **before** pre+leak (FIFO), so the flush happened before the leak slab became empty
→ the leak slab was never discarded (dip=0).

**Fix (new `GL_TAIL_KILLS`)**: after `kill_child(leak_child)`, kill N more dummy child processes,
so that their frees (queued after the leak slab in FIFO order) overflow the per-CPU partial chain → `unfreeze_partials` →
discard the frozen empty leak slab. Measured: `GL_TAIL_KILLS=34` **hits STRICT on attempt 1** on a fresh device
(vehicle 618).

### 0.2 W1 target = `selinux_state + 0` (settled by disassembly)

The chain of `sel_write_enforce@0x638644`:
```
x8 = *(file + 0x20); x8 = *(x8+0x28); x8 = *(x8+0x3c8); x21 = *(x8+0x48);
strb w8, [x21]            ; enforcing byte @ *x21
...
selinux_status_update_setenforce(x21, ...)   ; read *(x21+0x10)=status_page
```
`x21` is treated as `struct selinux_state *` (`status_page@+0x10`, `status_lock@+0x18`),
and what `strb [x21]` writes is `selinux_state[0]` (enforcing). The RV101 kernel (5.10 GKI) has **no**
separate `selinux_enforcing` global (kallsyms only has `selinux_enforcing_boot@0xffffffc00a196788`),
unlike OnePlus (6.12, with the separate symbol `SELINUX_ENFORCING_OFF=0x026894d0`). So
`SELINUX_STATE_OFF=0x02ca0140; enforcing@+0` in `target_rv101.h` **is correct**.

### 0.3 ~~The data path (data_addr) still awaits empirical proof~~ **✅ Proven empirically in appendix E (data_addr→text_addr, W1 Permissive)**

After actually measuring W1 (`GL_TAIL_KILLS=34`, reclaim STRICT hit), `getenforce` still returned `1`. Since
the W1 target is correct (0.2) and write#1 should land at `data_addr(SELINUX_STATE)=0xffffff8002ca0140`,
the conclusion is that **`P0_KERNEL_PHYS_LOAD=0x80000000` is wrong**. Evidence:
- `/proc/zoneinfo` shows that the first free-memory range of node 0 is `0x80000000-0x808f3fff` (9MB),
  i.e. the kernel is **not** at the RAM base (otherwise that range should be reserved).
- Honor (Pixel GKI) documentation says `P0_KERNEL_PHYS_DELTA=0` is correct, but the RV101 is a QCM4290-specific
  `5.10.209-perf` kernel, so the physical layout may differ.
- Historical values: `0x80800000` (original, no panic), `0x80080000` (previously panicked), `0x80000000` (v9.30).

---

## 1. Verified Results

| Item | Status |
|---|---|
| reclaim: `GL_TAIL_KILLS` triggers leak slab discard | ✅ `--selftest` STRICT hit (fresh, attempt 1) |
| Restoration of V5 reclaim parameters (dummy=450, kills=0) | ✅ `V9_DUMMY_CNT 200→450`, `GL_DUMMY_KILLS default 450→0` |
| W1 target = selinux_state+0 | ✅ settled by disassembly (see 0.2) |
| `SELINUX_STATE_OFF=0x02ca0140` | ✅ matches kallsyms `selinux_state@0xffffffc00aca0140` |
| W1 (--write1 → Permissive) | ~~❌ STRICT OK but enforce does not flip (data_addr wrong)~~ **✅ verified in appendix E that it does flip (2026-09-13 correction)** |
| W2 (FOPS → cred) | ~~❌ not achieved~~ **✅ verified in appendix AA (child uid=0)** |

---

## 2. Code Changes This Round (`source_code/ghostlock_rv101/`)

- `src/util.c`:
  - `V9_DUMMY_CNT 200 → 450` (restore the V5 known-good; a comment explains the nr_partial starvation root cause).
  - `GL_DUMMY_KILLS` default `V9_DUMMY_CNT → 0`.
  - New `GL_TAIL_KILLS` (0~272, default 0): kill dummies after the leak child to trigger discard.
  - `p0_data_alias()` gains a `GL_KERNEL_PHYS_LOAD` override (`set_kernel_phys_load()`).
  - The "v9.10 gate" diagnostic print was moved to **after** the first send (the original print reopened the steal window).
- `src/main.c`:
  - `SELFTEST_RUNS` cap 10 → 100, and prints hit statistics.
  - Parses the `GL_KERNEL_PHYS_LOAD` environment variable.
- `src/common.h`: declares `set_kernel_phys_load()`.

---

## 3. Build/Run Quick Reference (unchanged)

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

# reclaim verification (successful configuration)
"$ADB" -s <DEVICE_SERIAL> shell "GL_SKB_PAD=0 GL_DUMMY_KILLS=0 GL_TAIL_KILLS=34 \
  GL_SETUP_ATTEMPTS=4 /data/local/tmp/ghostlock_rv101 --selftest"
```

---

## 4. Next Round's TODO (**2026-09-13 note: all three items below were completed in later appendices; do not redo them**) (by priority)

1. **Settle `P0_KERNEL_PHYS_LOAD`** (the only blocker for W1/W2):
   - Run `--write1` for each `GL_KERNEL_PHYS_LOAD=<candidate>` in turn (with `GL_TAIL_KILLS=34`) and watch `getenforce`.
   - Candidates: `0x80800000`, `0x80080000`, `0x8ab00000` (after node 0), near `0xe8000000`.
   - More reliable approach: disassemble `kimage_voffset = kimage_vaddr - x0` in `__primary_switched@0x20502b4`
     and `__create_page_tables@0x2050040`, and recover the x0 passed in by the bootloader (=PA(_text));
     or find the kernel load address from DTBO/xbl.
2. **Verify W1**: `--write1` → `getenforce` returns Permissive.
3. **Verify W2**: FOPS route → `task->cred = init_cred` → `id` returns uid=0.
4. Further strengthen reclaim stability: `GL_TAIL_KILLS` must be >13 slabs (>221 child processes) to "deterministically" overflow the chain;
   `SKB_RECLAIM_SENDS` 768 → 1536 can add vehicle coverage margin (the leak slab once landed on vehicle 618).

---

## Appendix B: Tenth Holder round 2 — additional disassembly findings

1. **Full disassembly of `rb_erase@0x774b38`** (case 1, node has only a right child):
   ```
   0x774b3c: ldp x8,x10,[x0,#8]     ; x8=left(0), x10=right(write_value)
   0x774b44: cbz x8, 0x774bb8       ; left==0 → case 1
   0x774bb8: ldr x9,[x0]            ; x9 = node->pc = (target-8)|1
   0x774bbc: ands x8,x9,#-4         ; x8 = parent = target-8
   0x774bc0: str x9,[x10]           ; WRITE#2: *(write_value) = (target-8)|1  ← STRICT check
   0x774bc8: ldr x9,[x8,#0x10]      ; x9 = parent->rb_left = *(target+8)
   0x774bcc: cmp x9,x0              ; rb_left == node?
   0x774bd0: b.eq 0x774d90          ; yes → write rb_left(target+8)
   0x774bd4: str x10,[x8,#8]        ; WRITE#1: parent->rb_right(target+0) = write_value
   ```
   Confirms that W1's WRITE#1 lands at `target+0 = selinux_state[0]` (`*(target+8)!=node` is always true,
   because selinux_state[8]=ss_initialized≈1 and node is a stack address). write_value=page_base+0x1500
   has low byte=0x00 → it should zero the enforcing byte.

2. **`__primary_switched@0x20502b4`**: `kimage_voffset = kimage_vaddr - x0`, and the caller
   (`__relocate_kernel@0x141c408/0x141c448`) passes `x0 = adrp #0xffffffc008000000` =
   **KIMAGE_TEXT_BASE (link VA), not the standard arm64 __PHYS_OFFSET**. So
   `kimage_voffset = slide` (KASLR slide). This is Qualcomm's non-standard practice; but whether one uses the
   standard `__kimg_to_phys(x)=x-kimage_voffset` or adds `-KIMAGE_TEXT_BASE+PHYS_OFFSET`,
   `__pa_symbol(_text)` equals **0x80000000** (PHYS_OFFSET).

3. **`sel_write_enforce@0x638644`**: `strb w8,[x21]` (x21=chain result) writes the enforcing byte,
   and `selinux_status_update_setenforce(x21,...)` reads `*(x21+0x10)=status_page`,
   `*(x21+0x18)=status_lock`. So x21=&selinux_state, enforcing@**selinux_state+0**.

**Conclusion (contradiction still unresolved)**: the three facts — data_addr(SELINUX_STATE)=0xffffff8002ca0140 is correct, WRITE#1 lands in
selinux_state[0], and enforcing@selinux_state[0] — are all settled by disassembly, yet measured W1 with
`GL_KERNEL_PHYS_LOAD = 0x80000000 / 0x80800000 / 0x80080000` is all STRICT OK while
enforce does not flip. To investigate: a) whether Qualcomm has physical KASLR (PA(_text) randomized); b) whether
the BSS physical address of `selinux_state` differs from the Image (physical relocation); c) whether enforcing is actually
read from a status-page mirror rather than from selinux_state[0].

## Appendix C: Tenth Holder round 3 — additional findings

1. **GKI `struct file` layout (critical)**: `do_dentry_open@0x3cfd60` does `str inode,[f+0x20]`,
   `ldr x8,[f+0x18]` → in this kernel **`f_inode@0x20`, `f_op@0x18` (swapped relative to mainline)**.
   Therefore the chain in `sel_read_enforce`, `*(file+0x20)+0x28 → +0x3c8 → +0x48`, is
   `f_inode → i_sb → s_fs_info → selinux_fs_info.state = &selinux_state`, and finally
   `ldarb [&selinux_state]` reads `selinux_state[0]`. **Confirms enforcing@selinux_state+0**.

2. **Full disassembly of `rb_erase` case-1** (`0x774b3c~0x774bdc`) confirms WRITE#1 (`str x10,[x8,#8]`)
   lands at `parent->rb_right = target+0` (`parent=target-8`, `*(target+8)!=node` always true),
   and WRITE#2 lands at `*(write_value)`. That is, W1's `write_value=page_base+0x1500` (low byte=0)
   will be written into `selinux_state[0]`.

3. **Node memory quantification**: the 18 `node 0:[mem ...]` ranges sum to 2026496 KB = `Memory: 2026496K total`,
   so it is System RAM (including the kernel), the kernel really is at PA=0x80000000, and `data_addr` is correct.

**Still unresolved (the only blocker for W1/W2)**: after all three points above were settled, `--write1` (GL_KERNEL_PHYS_LOAD=
0x80000000/0x80800000/0x80080000) is still STRICT OK while `getenforce` does not return Permissive.
The most suspicious new direction: Qualcomm's `__primary_switched@0x20502b4` uses `kimage_voffset =
kimage_vaddr - KIMAGE_TEXT_BASE = slide` (non-standard `VA-PA`), which may imply **physical relocation**;
this needs to be measured with `GL_KERNEL_PHYS_LOAD` sweeping 0x80000000..0xfc000000, or by disassembling
`__relocate_kernel@0x141c3e8` to confirm whether it performs a physical memcpy.

## Appendix D: Tenth Holder round 4 findings (PA(_text)=0x80000000 now firmly established, W1 still does not flip)

1. **No physical relocation**: `__relocate_kernel@0x141c3e8` calls `0x141c330` (`.rela.dyn` patching:
   `add x14,x14,x23; str x14,[x12,x23]`) — it only adds the slide to VA references and does **no physical memcpy**.
   `0x141c29c` only does `msr ttbr0/ttbr1_el1` (page-table switch). → PA(_text) is fixed.

2. **`kimage_voffset = slide` (non-standard)**: `__primary_switched` receives `x0=KIMAGE_TEXT_BASE`
   (caller `adrp x0,#0xffffffc008000000`), so `kimage_voffset = kimage_vaddr - KIMAGE_TEXT_BASE
   = slide`. But whether one uses the standard `__kimg_to_phys(x)=x-kimage_voffset` or adds
   `-KIMAGE_TEXT_BASE+PHYS_OFFSET`, both yield `__pa_symbol(_text)=0x80000000`.

3. **Measured sweep** (`scripts/scan_kernel_phys_load.sh`):
   - 0x80080000: 0/6 reclaim hits (inconclusive).
   - 0xa8000000: STRICT hit (vehicle 523, attempt 1) → enforce still Enforcing.
   - Together with the earlier 0x80000000 and 0x80800000 (STRICT OK but no flip) → **none of the 4 PA candidates flips**.

**Conclusion**: the four facts — PA(_text)=0x80000000, data_addr correct, WRITE#1 lands in selinux_state[0], enforcing@
selinux_state[0] — are all settled, yet measured W1 still does not flip enforcing. The most likely residual directions:
(a) the kernel-image .data/.bss alias in the linear map is made read-only by `set_memory_ro` (Qualcomm hardening), so
WRITE#1 lands in a read-only page and is silently absorbed or goes to the wrong page; (b) enforcing is actually read from the status-page
mirror (the u32 at `page_to_virt(status_page)+8`) rather than from selinux_state[0]. For the next round it is suggested to first disassemble
the full `sel_read_enforce` chain + `mark_rodata_ro`/`set_memory_ro` to confirm (a), or switch to the W2 configfs
read primitive (first read back selinux_state.status_page, then locate the real enforcing field).

## Appendix E: Tenth Holder round 5 major breakthrough — W1 verified (data_addr → text_addr)

**Root cause (final finding)**: the kernel image of the RV101 (Qualcomm QCM4290) kernel is at a **high physical address**,
and `__primary_switched@0x20502b4` uses `kimage_voffset = kimage_vaddr - KIMAGE_TEXT_BASE = slide`
(caller `adrp x0,#0xffffffc008000000`, confirmed by manually decoding the ADRP), i.e. `__pa_symbol(_text) =
KIMAGE_TEXT_BASE = 0xffffffc008000000` (not 0x80000000). So the exploit's direct-map alias
`data_addr()` (which assumes PA(_text)=0x80000000) **maps to the wrong physical page**.

**Fix**: `data_addr()` returns `text_addr()` again (runtime kernel VA = KIMAGE_TEXT_BASE + slide).
rb_erase is an ordinary kernel VA write, so writing `text_addr(symbol)` hits the correct physical page.

**Verification (live device)**:
```
--write1-text (writes text_addr(SELINUX_STATE)=0xffffffe31c0a0140):
  [+] W1-text: selinux_state (VA) verified: 1 vehicle STRICT signature (attempt 1)
  [*] enforce file = '0' (raw 30 00 00 00)
  [+] SELinux DISABLED (text_addr) attempt 1
  → getenforce = Permissive  ✅✅ W1 complete
```

**Contrast**: `--write1` (writing data_addr=0xffffff8002ca0140) is STRICT OK but enforce does not flip;
`--write-dmesg` (writing data_addr(DMESG_RESTRICT)=0xffffff800aa72d50) → writes into the wrong page and panics.
All three consistently prove that the direct-map alias is wrong and the kernel VA is right.

**Remaining (W2)**: the root cause of W2's FOPS-route ENOTTY was clarified in the second half of this round (see appendix F):
1. **The `ASHMEM_SET_NAME` ioctl works perfectly on a fresh boot** (minimal test `r=0`); the ENOTTY is neither
   a wrong ioctl number nor SELinux.
2. **mode-5's rb_erase write#2 (`str x9,[x8]` storing tree_pc=fake_fops into fops_field)
   always lands** (it writes the GLOBAL `ashmem_misc.fops`, unrelated to reclaim). When reclaim
   fails (write#1 MISSED/0 diffs = the SKB did not take in the leak slab), `fake_fops=page_base+0x1600`
   is a stale pointer to the freed slab, and write#2 writes it into fops anyway → subsequent opens go through garbage
   fops → unlocked_ioctl=0 → ENOTTY. **In other words: reclaim is the only variable, and write#2
   pollutes fops every time; only when reclaim succeeds is fake_fops valid and does configfs_read work.** Countermeasure: a fresh
   settled device + GL_TAIL_KILLS=34 + multiple FOPS_ATTEMPTS (each write#2 overwrites fops,
   so the retry mechanism itself is right; the failures were just 6 consecutive reclaim failures with the device already degraded).

## Appendix T: round 14 — copy_to_user's CONFIG_HARDENED_USERCOPY check is the key suspect

**Disassembly of the helper called by simple_read_from_buffer@0x411634**:
- `0x3cca84` = `check_copy_size` (CONFIG_HARDENED_USERCOPY): it validates the source address **before**
  `raw_copy_to_user`. The flow includes: a stack-object check (0x3ccc94), a heap-object vmemmap walk (0x3ccb40),
  and the kernel `_text.._end` whitelist (0x3ccba8); if the "usercopy-abort" flag is hit it does `bl 0x3b7c7c` (possibly
  panic/BUG_ON).
- `0xdd28c` = `raw_copy_to_user` (the actual `ldr from; str to`).

**Implications**:
- **Concurrent process (read_file, page@0x10=NULL)**: `check_copy_size(from=NULL)` may BUG_ON by itself, or
  the subsequent `raw_copy_to_user` may `ldr` from NULL → data abort → **panic** (this is read_file's concurrent panic source,
  clear-cut).
- **exploit read (from=head=init_task.tasks, kernel .data)**: in theory the `_text.._end` whitelist should pass,
  but the RV101's KASLR slide is as large as ~140GB (the kernel image is at a high VA), which may cause `check_copy_size`'s
  whitelist/vmemmap walk to misjudge `head` → take usercopy-abort or -EFAULT (the bash-17 bin_file panic
  is not settled, but this is the most suspicious).

**Next round**: the actual path `check_copy_size` takes for head needs to be compared with a GL_READ_TARGET using an address
"known to be inside `_text.._end` and not slab" (such as selinux_state/asmhem_fops itself), or by reading a direct-map address
to see whether it returns 8; and the concurrent panic should be fixed with priority (read_file's NULL page is clear-cut, and bin_file's concurrent bin_buffer_size=0
is safe, so bin_file should be chosen + find out why the exploit read panics).

## Appendix S: round 13 — GL_READ_TARGET bisection caught LANDED, panic occurs before pread (= concurrent process)

**Measurement (bash-21 run3, v9.43 read_file + GL_READ_TARGET=0xffffff8040000000 direct-map)**:
```
install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 449)
(then the log cuts off directly, no "blob OK" → device panics and goes offline)
```
**Key**: the panic happens **before** `blob OK` (= open/first SET_NAME stage), **not** in configfs's
pread/copy_to_user. So:
- The copy_to_user(head) hypothesis is excluded (the panic is before pread).
- What remains is the **concurrent process** (or open/SET_NAME itself, but fake_fops's open=ashmem_open and
  ioctl=ashmem_ioctl are both correct slots, and open's CFI check also passes).

**Finding**: mode-5 overwriting the **GLOBAL `ashmem_misc.fops` with fake_fops** is the root cause of the concurrent panic —
background processes (surfaceflinger/camera using ashmem) perform open/read/mmap/ioctl through fake_fops after it is installed,
and fake_fops's read is configfs (read_file against a concurrent ashmem fd = NULL page@0x10 → copy_to_user(NULL)
panic; bin_file's concurrent bin_buffer_size=0 is safe, but bash-17's panic should be **another** NULL
slot of the concurrent process, not the exploit's pread).

**Conclusion**: W2 is stuck on the **inherent contradiction** of "GLOBAL fops overwrite → concurrent panic". Solving it requires (a) restoring fops
immediately after installation (the restore's write#1 will pollute the original fops->llseek, an acceptable side effect but it must be implemented), or
(b) using per-file fops rather than GLOBAL fops (no such primitive currently), or (c) capturing the oops over the serial port to pinpoint exactly which slot the concurrent process
stepped on and then fill it in.

## Appendix R: round 12 — panic sources narrowed: not the chain, but copy_to_user(head) or a concurrent process

**bash-16 (bin_file, v9.41) reached mutex_lock and only hung without faulting** → proving that bin_file's
`file+0x18 → +0x78 → +0x50` chain **does not fault** (the garbage is merely readable). So bash-17 (v9.42,
after mutex zeroing) does not panic in the chain, but in what **the two read functions share**: `copy_to_user(buf, head, 8)`
or a **concurrent process**. read_file (v9.43) additionally has an independent panic from "concurrent NULL page@0x10" (measured: bash-20
run1 went offline right after dip=1 during FOPS install = concurrent process).

**Added `GL_READ_TARGET` env (v9.44) + `env_hex()`**: this can retarget the configfs read to an arbitrary address,
bisecting "kernel-image VA vs direct-map VA". But mode-5 reclaim has recently been flaky (bash-20/21 several
0/6; attempt1 dip=1 but 0 diffs = SKB lost the head-grab again), so the bisection has not yet caught LANDED + `configfs_read target`.

**Additional disassembly of ashmem_open@0xc68a04**: the RV101 ashmem_area = name[0..0xFF], unpinned_list@0x110,
prot_mask@0x130=7, and the SET_NAME check field at 0x120 (zalloc=0).

**Next round**: first stabilize reclaim (the SKB head-grab with GL_PURGE_CNT=0 is still ~50% flaky, see appendix J), or grab
dmesg/serial concurrently, otherwise the GL_READ_TARGET bisection will keep stalling without LANDED.

## Appendix Q: round 11 addendum — the panic has two independent sources (read_bin's chain vs read_file's concurrent NULL page)

**read_bin_file (v9.42) and read_file (v9.43) both panic, but from different sources**:
- **read_bin_file's prologue chain (file+0x18 → +0x78 → +0x50) executes unconditionally**, and for an ashmem file
  it reads a garbage pointer; `ldr x25,[x8,#0x50]` may fault on an unmapped address — a **KASLR-related random panic**
  (in bash-16 the garbage happened to be readable; in bash-17 it faulted).
- **read_file has no unconditional chain** (only in the needs_read_fill!=0 branch), but **a concurrent ashmem fd's
  page@0x10=0 → copy_to_user(NULL) → panic** (the old round 3-6 problem). Measured: the v9.43 panic can occur
  **during FOPS install** (bash-20 run1: offline after dip=1 and before DIAG = concurrent process), or at the exploit's
  pread (bash-18 run2: after blob OK).

**Disassembly of asmhem_open@0xc68a04**: the RV101's ashmem_area layout = name[0..0xFF], unpinned_list@
0x110 (INIT_LIST_HEAD), **prot_mask@0x130=7**; the SET_NAME check field is at **0x120** (zalloc=0 →
SET_NAME is fine, ruling out prot_mask blocking SET_NAME).

**CATCH-22 finding**: read_file has the cleanest read path for the exploit (no chain) but the concurrent NULL page necessarily
panics; read_bin_file's concurrent bin_buffer_size=0 is safe but the exploit's chain randomly faults. The next round must
**complete the GL_READ_TARGET bisection** (the `GL_READ_TARGET` env has been added to read a direct-map address instead; but three retries
were all 0/6 with no LANDED) or **capture the oops over the serial port** to confirm whether copy_to_user faults on a kernel-image VA.

## Appendix P: round 11 — the pread panic is unrelated to the prologue chain (read_file also panics)

**Measurement (v9.43, switched back to configfs_read_file, page@0x10)**:
```
install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 224)
configfs_read: blob OK, about to pread
→ device panics and goes offline (no configfs_read target=... r=8)
```
Identical to the v9.42 (configfs_read_bin_file) panic. **configfs_read_file's read path does not execute the
file+0x18 prologue chain at all** (only in the needs_read_fill!=0 branch), yet it still panics.

**Conclusion**: the panic is **not** the prologue chain, not the mutex (already zeroed), not needs_read_fill (already 0), and
not the bin_buffer/page target (=head=init_task.tasks, a valid .data VA). All that the two read functions share is:
1. `mutex_lock(configfs_buffer+0x20)` (already zeroed; the fastpath should succeed);
2. `simple_read_from_buffer → copy_to_user(buf, head, 8)` (head is valid; copy_to_user should not fault);
3. **a concurrent process** (after fake_fops is installed, a background process panics on some NULL/garbage f_op slot or NULL page@0x10
   [read_file's concurrent ashmem fd]).

**Additional disassembly of ashmem_open@0xc68a04**: the RV101's ashmem_area layout differs from mainline —
`prot_mask @ 0x130` (=7), `unpinned_list @ 0x110`, and the SET_NAME check field at `0x120` (zalloc=0,
so SET_NAME is fine). This rules out "SET_NAME returning -EINVAL because prot_mask is non-zero".

**Next round (capture the panic scene, priority)**: pstore is empty (no ramoops), so a **serial console** or
**background `dmesg -w` capture** is needed to leave a kernel oops at the moment of the panic; or first verify copy_to_user by changing the configfs
read target to "a known-readable address in the direct map" (not a kernel-image VA), bisecting between "copy_to_user faults reading a
kernel-image VA" and "a concurrent process".

## Appendix O: round 10 — static comparison of the pread panic (read_file vs read_bin_file prologue)

**Disassembly comparison (finding)**:
- `configfs_read_file@0x49e8e0`: the `file+0x18 → +0x78 → +0x50` prologue chain is executed **only in the fill path
  where needs_read_fill!=0** (after 0x49e928); the read path (needs_read_fill=0) only
  does `mutex_lock → simple_read_from_buffer(page@0x10, count@0x00)`.
- `configfs_read_bin_file@0x49ec40`: the same `file+0x18 → +0x78 → +0x50` chain is **unconditional**
  (0x49ec60-0x49ec80, before mutex_lock), but x25 is only used in the fill path (useless when needs_read_fill=0).

**The read helper 0x411634 = simple_read_from_buffer has been confirmed by disassembly**: `pos=*ppos; if(pos>=available)
return 0; ret=min(count,available-pos); copy_to_user(to, from+pos, ret)`. head=init_task.tasks
(a valid .data VA, verified many times) → copy_to_user should not fault in theory.

**So bash-17 run2's pread panic is statically contradictory** (mutex=0, byte@0x55=0, needs_read_fill=0,
bin_buffer=head valid, copy_to_user should not fault). The most suspicious candidates are now: **bin_file's unconditional file+0x18
chain reading a garbage pointer for an ashmem file** (bash-16 did not fault because the garbage happened to be readable, but that is not
guaranteed every time), or **a concurrent process**. The next round can switch back to `configfs_read_file` (page@0x10, prologue
chain is conditional) + mutex/needs_read_fill zeroed as before, avoiding the unconditional chain to re-verify; the cost is that a concurrent process's
NULL page@0x10 will panic again (the old round 3-6 problem).

## Appendix N: round 9 — configfs_read_bin_file's mutex must be 0 (v9.42 fixes the pread hang)

**Measurement (bash-16, read_iter=ashmem, `--write2` finally LANDED + reached configfs read)**:
```
install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 159)
configfs_read: blob OK, about to pread
(then pread hangs, no further configfs_read target=... is printed, but the device does not panic and is still Enforcing)
```

**Root cause**: the first thing `configfs_read_bin_file@0x49ec40` does when disassembled is
`add x19,x26,#0x20; bl mutex_lock` (locking `configfs_buffer+0x20` = asma->name[32..39]).
The old blob only zeroed needs_read_fill/0x55-byte/bin_buffer and **did not zero the mutex**, so mutex owner =
0x41 (fill garbage) → `mutex_lock` slow path spins/sleeps on a garbage mutex → **pread hangs forever** (no panic).

**Fix (v9.42)**: the full blob in `ashmem_set_name_bin_blob()` also fills
`name[21..28]` (=configfs mutex owner, 8 bytes, asma->name[32..39]) with 0x01, and then in the descending
zero sequence **adds `{28,27,26,25,24,23,22,21}`** (13 zeros in total) to clear the mutex owner entirely to 0. This way
`mutex_lock` takes the fastpath (owner==0 → cmpxchg succeeds) → pread can enter `simple_read_from_buffer`.

**Residual (round 9 measurement, v9.42, 4 retries)**:
- run1: LANDED → panic at open/first SET_NAME (no "blob OK").
- run2: LANDED + **"blob OK"** → panic during pread (no "configfs_read target=...", device offline).
- run3/run4: 0/6 (reclaim miss).
So the mutex fix made SET_NAME pass entirely (blob OK), but **pread now panics** (instead of hanging).
pread = configfs_read_bin_file: its prologue (the three-read chain `file+0x18 → +0x78 → +0x50`) was already proven
not to fault in bash-16 (v9.41) (it reached mutex_lock); the mutex is zeroed; needs_read_fill=0, bin_buffer=head
(init_task.tasks, a valid .data VA). So the panic is most likely **the copy_to_user from head step** or
**a concurrent process** (pstore is empty, so the panic reason cannot be obtained). The next round needs to capture the panic scene (serial console or
last_kmsg), or replace head with a known-readable address (such as selinux_state) to verify whether copy_to_user is the culprit.

## Appendix M: round 8 — quantifying the mode 5 reclaim gap + read_iter side effects + W2 catch-22

**v9.40 DIAG (measured, `--write2`)**: `n=15872 llseek=noop_llseek read=configfs_read_bin_file
scratch=0 safe=0` — the SKB is the **pristine payload** (MSG_PEEK did not truncate, write#1 did not write into the SKB). So
mode 5's "0 diffs" = **reclaim really missed** (the SKB did not take in the leak slab), not a direction/DIAG problem.

**Quantification (all round 5-8 data)**:
- mode 3 (selftest) read_iter=NULL: **4/6 (67%)** (bash-4/bash-10).
- mode 3 read_iter=ashmem: 1/3 (33%, bash-14).
- mode 5 (FOPS) read_iter=NULL: **2/15 (13%)** (bash-6 1/2, bash-12 1/1, bash-13 0/6,
  bash-15 0/6).
- mode 5 read_iter=ashmem: **0/18** (bash-7/8/9/11).

So there are two layers of gap, and both are in "things set only after reclaim" (the static contradiction is still unresolved):
1. **mode 5 is ~3x worse than mode 3** (22% vs 67% with read_iter NULL) — the most suspicious cause is mode-5 write#2
   writing GLOBAL fops every time, which after pollution makes background processes (surfaceflinger/camera using ashmem) behave abnormally,
   which in turn stirs up SLUB/timing.
2. **read_iter=ashmem drives it further to zero** (mode5 22%→0%) — mechanism unknown (read_iter@0x20 participates in neither
   rb_erase nor reclaim), possibly just a small-sample coincidence (mode5's true value ~10-15%).

**W2 catch-22**:
- `read_iter=NULL`: reclaim hits (~22%), but **LANDED immediately panics** (bash-6/bash-12 both show
  "write#1 LANDED → device not found"; bash-13 0/6 did not hit so there was no panic). The panic is a concurrent
  process taking read_iter and stepping on a NULL f_op, or the pread of the configfs bin_file blob (6×SET_NAME not empirically proven).
- `read_iter=ashmem_read_iter`: no panic, but reclaim ~0% (never installs successfully).

**Next round's directions (by priority)**:
1. Capture one "LANDED + `configfs_read: blob OK, about to pread`" to determine whether the panic is read_iter NULL
   (concurrent) or the bin_file blob (pread) — v9.41 already added that print, so just retry repeatedly with read_iter=NULL.
2. If the blob is the panic source: fix bin_file's zero_at (or switch back to read_file + accept the concurrent panic).
3. If read_iter is the panic source: find a "safe but not ashmem_read_iter" read_iter value (to avoid it driving
   reclaim to zero), or restore fops immediately after installation.
4. Attack mode 5's 3x reclaim gap: change the reclaim carrier (sendmsg/keyctl big_key/BPF map), or
   run a mode-3 selftest before mode-5 write#2 to confirm reclaim health.

## Appendix L: the reclaim gap between mode 5 (FOPS) and mode 3 (selftest) — the key unresolved issue from round 7

**Measured comparison (round 7, same build, fresh boot, GL_PURGE_CNT=0 + stride16)**:
- `--selftest` (mode 3): **2/3 hit** (bash-4 and bash-10 both 2/3; run1/run2 always hit, run3 accumulated degradation).
- `--write2` (mode 5, FOPS install): **0/6 × 4 times** (bash-6 only ever LANDED once, on attempt2).

**Ruled out**: build changes (bin_file/read_iter) did not affect selftest (bash-10 still 2/3 with the new build);
slab_drain/spawn_child (still 0/6 after removing them in v9.39). `cleanup`'s recv(consume) also reported 0 diffs, so it is
not a MSG_PEEK false negative; write#1 truly did not write into the SKB = **mode 5's reclaim (SKB head-grab) fails systematically**.

**Statically contradictory**: reclaim (prepare_good_kernel_page → send SKB) happens **before** `set_pselect_write_mode`
and should in theory be mode-independent. Both rb_erase calls are the "left-NULL" case (rb_left=NULL, rb_right≠0), and
write#1 writes the fake page in both. The only difference is that mode 5's write#2 writes the **GLOBAL `ashmem_misc.fops`** (and lands
every time, polluting fops), whereas mode 3's write#2 writes inside the fake page.

**Most suspicious (to be verified next round)**: after mode 5's write#2 pollutes the global fops, **device background processes** (camera/gfx/
audio opening /dev/ashmem) step on the dangling fake_fops and crash/slow down, changing the SLUB/scheduling
timing of subsequent attempts → reclaim degrades. But this does not explain why "attempt 1 (before pollution) also misses"; perhaps attempt 1 was a ~34%
pure flake that happened to occur 4 times in a row (probability ~1.3%). **The next round should directly capture the "attempt 1 LANDED" instance to verify whether configfs read
(bin_file 6×SET_NAME) returns 8 → task walk → cred write.**

## Appendix K: FOPS install has LANDED, concurrent panic root cause + configfs_read_bin_file switch (v9.38)

**Measurement (round 7, `GL_PURGE_CNT=0` + stride16 + before bin_file)**: the W2 route's
`install_fake_fops_v2 DIAG: write#1 LANDED (vehicle 231)` — **mode-5 FOPS install landed successfully for the first time**
(the combination of data_addr=text_addr + the reclaim fix all came together). But the device then immediately panicked and went
offline (concurrent ashmem read).

**Root cause**: fake_fops->read originally pointed at `configfs_read_file` (page@0x10). It treats
file->private_data as a configfs_buffer, with `count@0x00`="dev/ashm" (huge) + `page@0x10`=0
(a concurrent fd is zalloc'd) → `simple_read_from_buffer` does copy_to_user to address 0 → **any concurrent
process reading /dev/ashmem panics**. This is the inherent risk of mode-5 overwriting the global fops.

**Fix (v9.38)**: point it at `configfs_read_bin_file` instead (CFI_CONFIGFS_READ_BIN_OFF 0x1238e20).
Disassembly of `configfs_read_bin_file@0x49ec40` settles it: it reads `bin_buffer@0x58` + `bin_buffer_size@0x60`,
gated on `needs_read_fill@0x50`==0 and `byte@0x55`==0. A concurrent fd (zalloc) has bin_buffer_size=0
→ returns 0 harmlessly, no panic.

**configfs_read_once rewrite**: `ashmem_set_name_bin_blob()` uses 6 SET_NAME calls (the zero_at trick):
1 full blob (`name[77..84]=target`, `name[85]=len`, `name[86]=NUL`, placeholder 0x01),
then 5 descending single-NUL re-SET_NAMEs zeroing `[74],[72],[71],[70],[69]` in order
(=byte@0x55 and the 4 bytes of needs_read_fill), so that strscpy truncates before target and does not touch `name[77..85]`.

**v9.38b**: fake_fops additionally gets `.read_iter@0x20 = ashmem_read_iter.cfi_jt` (originally 0), to prevent
concurrent processes taking the read_iter path (sendfile/aio/splice/io_uring) from stepping on a NULL f_op.

**Round 7 measured residual**: both W2 route runs were stuck at reclaim's "dip=1 but 0 diffs" (SKB head-grab flake)
— GL_PURGE_CNT=0 raised the hit rate from ~0/5 to selftest 2/3, but it is still a ~66%/attempt probability and degrades
cumulatively with each run (dip=0 from run3 on). W2 needs to hit on the 1st-2nd attempt on a fresh boot. **Not yet**
seen: configfs read returning 8 (FOPS install LANDED twice but both times panicked concurrently before open/configfs, or reclaim missed).
Next round: fresh boot + GL_PURGE_CNT=0 + stride16 to retry W2 repeatedly, or keep suppressing the
flood/dummy kernel stack's order-2 burst (change the reclaim carrier).

## Appendix J: reclaim's third root cause — the purge squad's kernel stack burst buries the leak slab (solved by GL_PURGE_CNT=0)

**Measurement (round 7, `--selftest` SELFTEST_RUNS=3, `GL_PURGE_CNT=0` + stride16)**:
**2/3 hit** (run1 vehicle[157] STRICT OK, run2 vehicle[26] STRICT OK, run3 accumulated degradation dip=0).
Compare with ~0/5 when GL_PURGE_CNT=700. **This is a decisive breakthrough for reclaim stability.**

**Root cause**: the purge squad (V9_PURGE_CNT=700) child processes' kernel stacks (16KB=order-2) burst-release through the vmap
lazy list during the gate window (o2 +1200~+3200), burying the leak slab (mm_struct order-2) deep in the buddy
LIFO → the first SKB cannot grab it (0 diffs).

**Fix (v9.37)**: `V9_PURGE_CNT` becomes the env `GL_PURGE_CNT` (default 700). Setting it to **0** leaves the
flood+dummy kernel stacks (~20MB) sitting in the vmap lazy list (**below the ~30MB purge threshold, so they do not enter
buddy**), and the only order-2 free in buddy is the leak slab → the SKB is guaranteed to grab it.

**Residual**: the per-run memfd pin accumulation (~80 slabs/run) makes dip=0 from the 3rd run on. So in practice
**FOPS install should succeed on the 1st-2nd attempt on a fresh boot**; if it fails, reboot and start over.

## Appendix I: the nature of the write primitive — "known value → known address", reclaim-independent

rb_erase's two writes are **coupled** (case-1, `node->rb_left==NULL`):
- **WRITE#1** `str x8,[x11]`: `node->rb_right`(=write_value) → `parent->rb_left/right`(=target).
  parent=`tree_pc & ~3` = target-8 (mode 1/4) or fake_fops (mode 5).
- **WRITE#2** `str x9,[x8]`: `tree_pc` → `write_value` (**write_value must be a legal address**,
  otherwise panic).

So write_value can only be a "legal kernel address":
- mode 1: `page_base+SAFE_OFF` (inside the fake page, low byte 0x00) → it can only write **this address value** to target.
  W1 relies on the low byte 0x00 to flip enforcing, and is **unrelated to reclaim, hitting on every punch** (Permissive already verified).
- mode 2: `text_addr(INIT_CRED)` → it can write the init_cred address to any target, but the target must be
  **known** (task->cred is heap and must be read first → blocked on FOPS/reclaim).
- mode 5: write_value=fops_field (global) → WRITE#2 writing fops **necessarily lands** (appendix F).

**Conclusion**: W1's privilege escalation is **deterministic** (global target + legal address value) and has been achieved; W2 (uid=0) requires
**arbitrary read** (to find the child task_struct→cred), which can only go through FOPS/configfs → still blocked on reclaim. The modprobe_path/
core_pattern route is **infeasible** because write_value must be a legal address (writing a string would make WRITE#2 write to an illegal address and panic).
If the next round goes the global-write route, it can consider mode 2 to write init_cred to some **known global** (e.g. globalize some
process's cred pointer), or keep attacking reclaim to unlock arbitrary read.

## Appendix H: SKB head-grab failure — settle too short, the purge burst buries the leak slab (v9.36 lengthens settle)

**Measurement**: even with the flush fixed (dip=1), `cleanup: 0 diffs` is still common (the SKB did not take in the leak slab).
Log evidence: `settle[settle]: 13 rounds, o2=1449` followed by `o2 1 -> 2706` (gate window +1257
order-2 slabs). These 1257 are not caused by phase-3's ~33 kills, but by phase-1's 700 purge +
765 flood child-process kernel stacks **burst-purging** through the vmap lazy list (lasting 10s+) only during
phase-3, burying the leak slab deep in the buddy LIFO.

**Root cause**: settle's `quiet=6` (3 seconds of double stability) passed during a gap in the purge burst; purge is
bursty (3s idle then another burst), so 3s quiet is false stability.

**Fix (v9.36)**: `settle_wait("settle", 12, 20, 60)` — requires **20 quiet rounds (10s)**
with o2 and objs simultaneously stable, letting purge drain completely before entering phase 3. The cap went 40→60 rounds (20s→30s).

**Measured result (v9.36)**: **unresolved**. settle often hits the 61-round cap (quiet only reaching 5/8/18, o2 fluctuating
for >30s); even when quiet does reach 20 (24 rounds), the gate window is still `o2 74 -> 3313` (+3239). That is,
phase-3's +1000~+3000 order-2 burst is **not entirely phase-1 purge residue**; phase-3's own kills
(or a new purge triggered by kernel stack vfree) flood buddy again at the gate moment. The SKB head-grab is still the main flake.

**Next round's directions (W2 unlock priority)**:
1. First `--selftest` to confirm reclaim health, then `--write2` (single shot, fresh, GL_TAIL_KILLS=16).
2. Change the reclaim carrier (sendmsg/keyctl big_key/BPF map) to avoid the SKB's order-2 head-grab contention.
3. Or switch to "deterministic write#1 + modprobe_path/core_pattern" global-write privilege escalation (see appendix I below).

## Appendix G: reclaim's second root cause — GL_TAIL_KILLS killing consecutively by mistake (fixed to stride)

**Measurement (second half of round 5, fresh device, `--selftest` SELFTEST_RUNS=5, GL_TAIL_KILLS=34)**:
- run 1: `dip=1` → vehicle[325] STRICT OK (**first hit**).
- run 2-5: `dip=0 calm=364` (leak slab never discarded) → all miss.
- Each run accumulates `objs` by ~+130 (memfd pin mm leak), and reclaim degrades accordingly.

**Root cause (settled from slub_5.10.209.c source)**: `put_cpu_partial`'s flush condition is
`drain && old_pobjects > slub_cpu_partial(s)` (mm_struct size≥256 → **cpu_partial=13**).
`pobjects` is the **cumulative** total of the whole per-CPU partial chain; a slab only enters
`put_cpu_partial` on its "first free" (the rest take the FREE_FROZEN fastpath without adding pobjects), so each dummy slab contributes only **+1**.
When the leak slab is frozen, pobjects=1. To overflow (>13) requires another +13 → **the first-free of 14 different dummy slabs**.

**BUG**: the old loop `for(i=dk; i<dk+tail; i++) kill_child(dummy[i])` killed **consecutively**, so
`GL_TAIL_KILLS=34` spanned only ~2 dummy slabs (only +2 pobjects), nowhere near 13. The reason 34
"sometimes hit" was entirely a matter of luck with phase-1 residual pobjects happening to fall in the narrow window 10<residual≤13.

**Fix (v9.35)**: change to **stride** (kill the first child of each dummy slab):
```c
for (int j = 0; j < tail; j++) {
  int i = dk + j * MM_OBJS_PER_SLAB;   // the first child of each slab
  if (i >= V9_DUMMY_CNT) break;
  kill_child(dummy[i]);
}
```
The semantics of `GL_TAIL_KILLS` changed to "how many dummy slabs to span"; **14 = the deterministic minimum** (clean chain:
leak 1 + 14 first-frees → old_pobjects 14 > 13). `GL_TAIL_KILLS=16` is recommended (for margin).

## Appendix F: W2 ENOTTY root cause settled (mode-5 write#2 necessarily pollutes fops)

**Minimal ioctl test (live device)**: `ashmem_ioctl_test` (`src/ashmem_ioctl_test.c`):
- fresh boot: `SET_NAME→r=0 GET_NAME→r=0` (ioctls 0x41007701/0x81007702 work normally).
  `GET_SIZE` with the wrong number (num=3=0x80087703) returns ENOTTY; the correct one is num=4=0x80087704.
- After running W2 (same boot): all three return ENOTTY → **proving W2 has polluted `ashmem_misc.fops`**.
- Normal again after reboot → the pollution is runtime-only and cleared by reboot.

**Static findings**:
- `ashmem_misc@0x29bcbb0`: minor=0x7f, fops@+0x10=ashmem_fops@0x1e977a8. ✓
- `ashmem_fops` raw dump: `unlocked_ioctl@0x50=ashmem_ioctl.cfi_jt@0x123e118` (`bti c;b ashmem_ioctl`). ✓
- Disassembly of `ashmem_ioctl@0xc67e2c`: `0xc67ff4` compares against `0x41007701`==SET_NAME → copy_from_user→
  strscpy, returns 0; default returns -ENOTTY. **So as long as the ioctl reaches ashmem_ioctl it always succeeds.**
- Disassembly of `do_vfs_ioctl@0x3ee250` + `misc_open@0x86bd18`: `file->f_op@0x28`, and ioctl falls through
  to `f_op->unlocked_ioctl@0x50`; NULL or -ENOIOCTLCMD → ENOTTY.

**Conclusion**: ENOTTY = the fd's f_op is no longer ashmem_fops, but the **stale
fake_fops** written by mode-5 write#2 (when reclaim fails, page_base is a freed slab whose +0x50 unlocked_ioctl slot is 0).
mode-5's rb_erase write#2 writes the GLOBAL `fops_field=text_addr(ASHMEM_MISC+0x10)`, which is **unrelated to reclaim
and lands every time**; only write#1 (writing the fake page's llseek) is affected by reclaim. So W2's success condition =
reclaim success (write#1 LANDED). This round's 6 consecutive MISSED are device degradation (memfd accumulated pin mm).

**Countermeasure (next round)**: fresh boot + settle ~380–400s + `GL_TAIL_KILLS=34` + higher `FOPS_ATTEMPTS`;
or `--selftest` first to confirm reclaim health before `--write2`. A failure pollutes fops, and the retry's write#2 overwrites it,
but if a concurrent process opens /dev/ashmem between the two attempts it steps on stale fops → panic (known from UPDATE 2), so after a failure it is better to
reboot and start over.

## Appendix A: excerpt of this round's measured logs

reclaim hit (`--selftest`, GL_TAIL_KILLS=34, fresh, attempt 1):
```
[*] v9.10 gate: mm slabs 127 -> 126 objs 847 -> 823 (dip=1 calm=0)
[*] vehicle[618] diffs ...
[*] vehicle[618]: STRICT child_pc=OK scratch=OK (exp child_pc=ffffff80566652f9 ...)
[+] SELFTEST: write into sprayed page verified: 1 vehicle(s) carried the STRICT walk signature (attempt 1)
```

W1 (GL_TAIL_KILLS=34, STRICT hit but enforce does not flip):
```
[*] vehicle[515]: STRICT child_pc=OK scratch=OK (exp child_pc=ffffff8002ca0139 ...)
[+] W1: selinux_state.enforcing verified: 1 vehicle(s) carried the STRICT walk signature (attempt 1)
[*] enforce file = '1' (raw 31 00 00 00)   ← data_addr(0x80000000) did not write the real selinux_state
```

## Appendix U: v9.46 — root cause of the configfs pread panic settled (read_bin_file's unconditional chain)

**Background**: bash-21/22 all three times panicked immediately after `configfs_read: blob OK, about to pread`
(`device not found`), and `dmesg` after reboot had only boot messages with no panic trace — a 3/3
**deterministic** panic, not random concurrency.

**Disassembly findings** (capstone, kernel@`binaries/extracted_1.22/kernel`, BASE=`0xffffffc008000000`):
- `CFI_CONFIGFS_READ_BIN` jt@`0x1238e20` → `b configfs_read_bin_file@0x49ec40`.
- `CFI_CONFIGFS_READ_FILE` jt@`0x1239168` → `b configfs_read_file@0x49e8e0`.

The two read functions have **different structures**:

| | `configfs_read_bin_file` (0x49ec40) | `configfs_read_file` (0x49e8e0) |
|---|---|---|
| `file+0x18→+0x78→+0x50` chain | **unconditional** (`ldr x8,[x0,#0x18]`→`[x8,#0x78]`→`[x8,#0x50]` executes before the branch) | **conditional** (only inside the `needs_read_fill@0x50 != 0` fill branch) |
| Read source | `bin_buffer@0x58` + `bin_buffer_size@0x60` | `page@0x10` + `count@0x00` |
| Our read (needs_read_fill=0) | still dereferences the three-level chain first → the `inode+0x78`/`+0x50` of the devtmpfs /dev/ashmem inode hits NULL/garbage → **panic** | skips the chain → straight to `simple_read_from_buffer(buf,count,ppos,page,count@0x00)` → safe |

**Root cause**: `file+0x18` = `f_inode` (devtmpfs inode); `configfs_read_bin_file` treats ashmem's
`/dev/ashmem` (devtmpfs) file as a configfs file, unconditionally walking the configfs-specific
`f_inode→i_sb→…` chain and dereferencing NULL/garbage → our own pread panics every time. **This is a deterministic panic, not a concurrent process.**

**v9.46 fix**: `.read` now points at `CFI_CONFIGFS_READ_FILE_OFF` (`configfs_read_file`) by default, and adds
`ashmem_set_name_read_blob()`:
- `page@0x10 = name[5..12]` (`memcpy(name+5,&target,8)`).
- `name[13]=0` terminates → `strlcpy` writes only 13 bytes and **never touches** the mutex (name[21..28]) or
  needs_read_fill (name[69..72]) → they are 0 from the fresh kzalloc onward and stay 0 on every read → **no need
  for bin_file's 13 per-bit zeroings**.
- `count@0x00` = the fixed prefix `"dev/ashm"` = `0x6d6873612f766564` (huge), so
  `simple_read_from_buffer`'s available ≥ len always holds → it returns 8 bytes normally.

`GL_BIN_FILE=1` switches back to the bin_file route for comparison.

**Residual risk**: read_file's **concurrent** path (a background process opening /dev/ashmem and then `read()`) will
`page@0x10=0` → `simple_read_from_buffer(from=NULL)` → `copy_to_user(NULL)` → panic (this is the
real concurrent NULL-page panic observed in v9.43). But background processes rarely read() ashmem (usually mmap+
ioctl), far less likely than bin_file's 100% deterministic panic. If it still needs to be eliminated, the directions are: restore fops immediately after the read
(a mode-5 reverse write, which dirties the real ashmem_fops->llseek) or locate it via the serial console.

## Appendix V: v9.47 — read_file passes on the live device + reverse chunk walk + slab_drain

**v9.46 measurement (bash-23 run 2)**: the read_file route's probe **succeeded**:
```
[*] configfs_read: blob OK, about to pread
[*] configfs_read target=0xffffffe873febd08 len=8 -> r=8 errno=0     ← first pread returned r=8!
[*] configfs probe: init_task.tasks.next = 0xffffff8001032948
[+] fake_fops installed into ashmem_misc.fops
```
→ **The deterministic panic (bin_file's unconditional chain) is confirmed fixed**. The old forward walk panicked on the 3rd read
(`task+TASK_TGID_OFF=0xffffff8001032a4c`, a direct-map heap address) — a concurrent process
stepping on the global fake_fops (or a direct-map read panic, pending the GL_READ_TARGET bisection).

**Two v9.47 fixes**:
1. **Reverse chunk walk** (`find_task_by_tgid` rewritten): start from `init_task.tasks.prev` (the most recently
   forked task; the child lands near the tail right after forking) and walk **backward**, one 0x110-byte
   chunk read per step (covering tasks.next@0/tasks.prev@8/pid@0x100/tgid@0x104 in one read). This compresses the walk from
   ~1000 reads to ~10, shrinking the window from seconds to milliseconds and greatly reducing the chance of a concurrent panic.
2. **Add `slab_drain()` before FOPS install**: the selftest/write1 paths all have `slab_drain()` (3 waves of
   fork/kill churn to warm the per-CPU partial chain), but the W2 FOPS route alone missed it — this is the most likely
   cause of the "contradiction" between mode-5 write#1 landing ~25% of the time vs selftest's 67%.

**Residual issues**:
- reclaim is unstable (write#1 LANDED ~25-50%); on MISSED, mode-5 write#2 still writes a stale
  fake_fops → the device is unstable (background processes may panic at any time). slab_drain should improve this, pending test.
- During the cred write stage (do_cred_write × W2_ATTEMPTS) fake_fops is still globally installed (a ~1-20s window);
  if concurrent panics persist, fops must be restored immediately after the walk (mode-5 reverse write, at the cost of the real
  ashmem_fops->llseek being written with fops_target=a data address → a background lseek will panic, but read/mmap/
  ioctl/release all go through the real fops and are safe, which is better than leaving fake_fops installed).

## Appendix W: v9.48 — walk hardening (forward fallback + pointer validation)

`find_task_by_tgid` was refactored into `walk_tasks(fd, head, tgid, backward)`:
- **Backward** first (`init_task.tasks.prev`; the child was just forked near the tail, ~10 steps).
- On backward failure, a **forward** fallback (`init_task.tasks.next`, ~1000 steps, slow but a safety net).
- Before each step, validate that `entry` is a canonical kernel address (`& 0xffff000000000000`), rejecting
  the 0x41-fill/half-zero garbage pointers produced by strscpy's 0x00 truncation, avoiding a walk that goes to -EFAULT and fails outright.

**Not implemented (deferred until the device is back for measurement)**: fops restore during the cred-write stage (mode-5 reverse write
`tree_pc=ashmem_fops` → restoring `ashmem_misc.fops`). Cost: write#1 writes the real
`ashmem_fops->llseek` with `fops_target` (a data address) → a background lseek panics; and restore's
`prepare_good_kernel_page()` resets `skb_sv` (the fake_fops SKB fd of the install is lost, but the
SKB still survives in the kernel socket queue and is only freed at process exit, so it is harmless). Because it is unmeasurable, it is deferred for now — first verify whether the
backward walk + slab_drain have already suppressed concurrent panics to an acceptable level.

## Appendix X: v9.49 — W2 settled: configfs read cannot read heap/direct-map (GL_READ_TARGET empirical proof)

**Decisive experiment (bash-28, read_file build + GL_READ_TARGET override)**:
- `GL_READ_TARGET=0xffffff8000100000` (direct-map, phys 1MB) → reclaim LANDED →
  `configfs_read: blob OK, about to pread` → **panic** (no `r=`).
- Contrast: kernel-image addresses (head=0xffffffde…) always returned `r=8`/`r=16`.
- Together with bash-23 run2 (forward walk reading the heap address derived from `init_task.tasks.next`,
  `0xffffff8001032a4c`, also panicked) and bash-27 run1/2 (backward walk reading
  `init_task.tasks.prev` also panicked) → **consistent: any direct-map (0xffffff80…) read panics,
  kernel-image (0xffffffde…) reads work normally.**

**panic_on_warn ruled out**: `/proc/sys/kernel/panic_on_warn = 0` (measured). So this is not a check_copy_size
WARN→panic. Disassembly confirms that `usercopy_abort@0x3cc9ec`/`check_heap_object
@0x3b7c7c` only `bl` to `__warn`/`usercopy_warn` and **do not panic directly**. The panic comes from
`check_heap_object`'s **vmemmap walk** (`pfn_to_page` resolving a garbage
`struct page` for a direct-map address and then dereferencing it), or from `raw_copy_to_user` faulting on an unmapped address — neither is
controllable by panic_on_warn.

**Root cause**: the local configfs read's `copy_to_user` can only pass the `_text.._end` whitelist
(kernel-image 0xffffffde…); heap/direct-map (0xffffff80…) goes through check_heap_object's
vmemmap walk → resolves garbage for the RV101's non-standard physical memory configuration (kernel at HIGH physical) →
panic. **configfs arbitrary read is limited to the kernel image and cannot read task_struct (heap).**

**Impact on W2**: W2 = FOPS read → task walk (the init_task.tasks list is in the heap) → find the child
task_struct → cred=init_cred. Every step of the task walk must read a heap task_struct → panic. **W2
(uid=0) is blocked dead on the existing FOPS-read route by this kernel limitation.**

**Achieved**: W1 (--write1 → getenforce=Permissive) is confirmed usable and is a live-device-verified privilege escalation
(SELinux disabled). W2's read primitive (read_file) itself has been verified to return `r=8`, but it can only read
the kernel image and cannot locate the child cred in the heap.

**Potential ways out (untried; requires the device to recover)**:
1. Test the **exact value** of `GL_READ_TARGET=init_task.tasks.next` once more (the bash-28 run had reclaim
   MISSED and did not test it), to determine whether "mapped heap also panics" or "only the 1MB reserved address panics".
2. kptr_restrict=0 (W1 writing KPTR_RESTRICT_OFF) → /proc leaks the child task_struct address, then use
   mode-2 to write init_cred (bypassing the heap read).
3. Change the read carrier (non-configfs).

## Appendix Y: v9.50/v9.51 — prev-read breakthrough + cred-write lands but uid=-128

**Breakthrough**: the task walk no longer needs a heap read. `init_task.tasks` is STATIC (readable from the kernel image),
and fork inserts the new task at the TAIL of the list, so `init_task.tasks.prev` (head+8, a single kernel-image read)
yields the tasks address of the newest task → `task = prev - 0x4c8` is the just-forked child task_struct.
**Measurement (bash-2 run2)**: `prev=0xffffff802b854dc8 prev2=... stable=1` (two reads agree).

**Evidence that the cred write landed**: after `do_cred_write` (mode-2),
`init_cred.usage before=0x4 after_cred=0xffffff802b855079 after_real=0xffffff802b855071`.
`after_cred = (task+0x778)|1 = tree_pc` → **rb_erase's write#2 really landed** (writing init_cred.usage),
and write#1 of the same rb_erase (writing the init_cred address to task+0x780 cred) should have landed at the same time.

**init_cred verification** (kernel-image read, DIAG):
`init_cred=0xffffffd676402058 usage=0x4 @0x08=0 @0x1c=0` → **uid=gid=0 (root)**,
data_addr(INIT_CRED) is correct.

**Contradiction (unresolved)**: write#2 landed + init_cred->uid=0, but the child reports
`child uid = 4294967168 (0xffffff80)` (=-128), neither 0 nor 2000. Neither write#1's nor write#2's
value is 0xffffff80 (write#1=0xffffffd676402058, write#2=0xffffff802b855079),
so -128 is not a directly written value. Most likely: write#1's landing point direction (the parent->rb_left==node branch)
or pselect's fd_set encoding of the kernel-image address (0xffffffd6…) is wrong, causing the write to the wrong field.

**Residual issues**:
1. **child uid=-128** (wrong cred write result, main blocker).
2. **fake_fops stale** (after the cred write's reclaim resets `skb_sv`, the next round's SET_NAME returns
   ENOTTY) — run2 round2+ are all ENOTTY. fake_fops must be reinstalled before each read, or skb_sv saved/restored.
3. Concurrent panic (background processes stepping on the global fake_fops; DIAG's MSG_PEEK scan of ~768 vehicles lengthens the window).

**Next steps**: investigate write#1's landing point (direction branch) and the source of -128; fix fake_fops staleness (save skb_sv before
the cred write / or restore fops immediately after the read).

## Appendix Z: v9.52 — child uid=-128 root cause settled + repair

**Root cause (settled by disassembling `__arm64_sys_getuid@0x1735b8`)**:
```
mrs x8, sp_el0          ; current
ldr x8, [x8, #0x780]    ; current->cred  (cred POINTER @ 0x780 ✓)
ldr w8, [x8, #4]        ; cred->uid @ **0x04**  ← uid is at offset 4, not 8!
```
In this kernel `struct cred`'s `usage` is a **4-byte `atomic_t`** (not an 8-byte `atomic_long_t`),
so uid is @ 0x04. mode-2's write#2 (`str tree_pc, [init_cred+0]`, 8-byte) overwrites
usage(0x00-0x04) + **uid(0x04-0x08)** → uid = tree_pc>>32 = `0xffffff80` (=-128).
This explains `child uid = 4294967168 (0xffffff80)`.

**Evidence**: `init_cred.usage before=0x4 after_cred=0xffffff802b855079` (write#2 landed),
DIAG `init_cred @0x08=0` (that is gid, not uid! uid is at 0x04). So init_cred itself has uid=0,
but write#2 wrote uid@0x04 to 0xffffff80.

**v9.52 repair**: after the cred write, use mode-4 to write a value whose low 4 bytes=0 into `init_cred+0x04`
(uid), restoring uid to 0. write#2 will land at `*value`.
- `value=0xffffff8000000000` (direct-map base=phys memstart) → **panic** (bash-4 run1).
- `value=0xffffff8010000000` (phys memstart+256MB) → pending test (bash-5), but reclaim was 0/6 MISSED.

**Residual**: the repair's write#2 landing point must be a "writable and non-critical" kernel address, but values with "low 4 bytes=0"
all land in low physical memory. A safer option is pending (for example choosing a .bss symbol aligned to 4GB, or using the
sprayed page but aligning its base to a 4GB boundary — infeasible).

## Appendix AA: v9.58 — W2 achieved! child uid=0 (mode-5 cred write)

**Fixing -128**: `do_cred_write` switched from mode-2 to **mode-5**. mode-2's write#2 writes
init_cred.usage (offset 0), and because cred.usage is a 4-byte atomic_t, uid@0x04 was written as
tree_pc>>32=0xffffff80. mode-5's write#2 writes `*write_value`=the cred slot (=init_cred, correct), and
write#1 writes write_target=init_cred+8 (gid, not touching uid@0x04). → getuid()=init_cred->uid=0.

**Measurement (bash-7 run1)**:
```
configfs probe: init_task.tasks.next = 0xffffff8001037248
round 1..4: child uid = 2000        ← the cred write's reclaim did not land (uid unchanged)
round 5:    child uid = 0 (0x00000000)
[+] child is root!
[+] GHOSTLOCK ROOT: uid=0 gid=483095800
uid=0(root) gid=483095800(u4830_i5800) groups=... context=u:r:kernel:s0
[+] dropping to root shell
[T+551184ms] exploit complete — root shell spawned via .ghostlock_root.sh
```
Follow-up verification: `/data/local/tmp/su_gl` was created by **root (uid 0)** (`-rwxr-xr-x 1 root ...`),
proving the child really executed the root script's `cp` as uid=0.

**All three W2 stages pass**: FOPS install → prev-read (no heap read; reads the STATIC
init_task.tasks.prev) → mode-5 cred write → **id uid=0**.

**Achievement summary**:
1. reclaim stability: slab_drain + GL_TAIL_KILLS/GL_PURGE_CNT parameter tuning + mode-5 + prev-read.
2. W1 (--write1 → getenforce=Permissive): **verified**.
3. W2 (FOPS route → cred=init_cred → id uid=0): **verified (child uid=0)**.
