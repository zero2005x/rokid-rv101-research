# GhostLock W2 — configfs read blocker (round 3 findings)
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


Date: 2026-08-21  |  Device: Rokid RV101 (<DEVICE_SERIAL>), kernel 5.10.209 ab1.22.009

## Status: write primitive VERIFIED, fake_fops install VERIFIED (flaky), configfs read returns 0 (root cause NOT yet confirmed)

## What works (verified this round)

1. **Write primitive** — mode-3 selftest with `GL_SKB_PAD=0` gives
   `STRICT child_pc=OK scratch=OK` (vehicle[77]). The rb_erase case-A write
   lands at exactly `slab+OFF == recv[OFF]`. `GL_SKB_PAD=0` is correct.

2. **Fake fops table is correctly populated** — new `install_fake_fops DIAG`
   (MSG_PEEK of the reclaimed SKB) shows:
   ```
   llseek=0xffffffeae621ab88 (noop_llseek.cfi_jt, correct)
   read  =0xffffffeae6239168 (configfs_read_file.cfi_jt, correct = kaslr_base+0x1239168)
   ```
   So the payload fill, KASLR base, and cfi_jt offsets are all correct.

3. **Fake fops install lands (rarely)** — bash-3 attempt 1 had vehicle[143]
   diff `0x1600..0x1607 = 0xffffff80029bcbb9` = the write#2 child->pc fixup
   `(asmhem_misc.fops-8)|1`. That PROVES the rb_erase ran, so write#1
   (`fake_fops -> ashmem_misc.fops`) also landed. In that run the configfs
   read STILL returned r=0.

## The blocker: configfs read returns 0

`configfs_read_once(fd, target, &v, 8)` does SET_NAME(page=target) then
`pread(fd, &v, 8, 0)`. GET_NAME confirms the blob lands correctly
(`page@0x10 = target`, mutex/needs_read_fill = 0), but pread returns
**r=0 errno=0** every time — even reading from `0xdead000000000000` returns
r=0 (not -EFAULT), so the read bails at `simple_read_from_buffer`'s
`pos >= available` check.

`simple_read_from_buffer(buf, 8, ppos=&0, from=page, available=buffer->count)`
returns 0 iff `available == 0` (pos=0). `available = buffer->count @ 0x00 =
ashmem_area->name[0..7]`, which ashmem_open sets to the 11-byte prefix
`"dev/ashmem/"`, so name[0..7] should be `"dev/ashm"` = 0x6d6873612f766564
(a huge positive size_t), and the read should return 8.

**Static analysis (disasm, all cross-checked against rv101_kallsyms + Image)
says the read must return 8, but it returns 0.** This is the unresolved
contradiction. The two candidate explanations:

- (A) `buffer->count@0x00` is actually 0 at runtime (prefix not "dev/ashm").
- (B) the fake_fops install did not actually land (write#1 missed), so the
  read went through the ORIGINAL ashmem read_iter (returns 0 for an empty
  area) — indistinguishable from (A) by return value alone.

### Definitive test still needed

The current `install_fake_fops DIAG` (MSG_PEEK of ALL 768 SKB vehicles, not
just skb_sv[0]) now reports whether write#2 landed (`child_pc == (target-8)|1`).
If a run shows `write#2 LANDED` **and** the immediately-following
`configfs_read DIAG bad=0xdead...` returns **-EFAULT**, then the read path
reaches copy_to_user and the real bug is (A) count==0. If it returns **0**
with `LANDED`, then count@0x00 == 0 (name[0..7] != "dev/ashm") — need to dump
the ashmem_area prefix.

## The other big problem: reclaim/walk flakiness

The mode-4 write#2 lands only ~1-in-10 to 1-in-20 attempts. Two failure
modes observed:

1. `gate: ... dip=0 calm=364` — the mm_struct slab discard never detected.
2. `dip=1` but `cleanup: 0 vehicle(s) with diffs` — gate fires but the
   first send's kmalloc-16k does NOT take the leak slab (some other order-2
   allocation steals it). The walk then reads garbage at fake_lock and never
   reaches the rb_erase, so write#1 never lands either.

Because the walk reads `fake_lock = page_base + LOCK_OFF` (in the reclaimed
slab), **both** write#1 and write#2 depend on the reclaim succeeding. The
`first send` not always grabbing the buddy LIFO head is the dominant failure.

## Files changed this round (synced to Windows project)

- `src/util.c`:
  - `configfs_read_once` + invalid-address DIAG read.
  - `cleanup_page_prepare_state` + FAKE_FOPS dump.
  - `install_fake_fops` + MSG_PEEK all-vehicle write#2 probe (no page free).
- `target_rv101.h`, `src/main.c`, `src/route.c` — unchanged logic (carried over).

## Env / build / run (unchanged, correct)

- Build: NDK r27d `clang.exe --target=aarch64-linux-android31 -O2 ... -Isrc -include target_rv101.h -pie -static-libgcc -pthread`.
- Push: Windows adb.exe (alias `adb`), `wslpath -w` for paths.
- Run: `GL_SKB_PAD=0 GL_DUMMY_KILLS=180 W2_ATTEMPTS=1 ./ghostlock_rv101`.
- Fresh reboot + uptime>150s improves the reclaim odds. Reboot clears the
  corrupted `asmhem_misc.fops` left by a previous landed write#1.

## UPDATE 2 — mode-4 panics; reclaim needs a settled device

- **Reclaim needs uptime > ~400s.** On a fresh boot (uptime < ~300s) the
  selftest AND mode-4 both get `0 diffs`; on a settled device (uptime ~472s,
  ~920s) the mode-3 selftest is `STRICT OK` on attempt 1. The "uptime>150s"
  note is a minimum, not sufficient.
- **Mode-4 panics the device.** After the write#1 lands
  (`asmhem_misc.fops = fake_fops`), a concurrent process that opens
  `/dev/ashmem` runs `fops_get` on the corrupted fops (owner =
  `(target-8)|1` garbage) and faults → kernel panic. bash-13 (mode-4, settled
  device) panicked right after `gate: dip=1` in attempt 1, before the DIAG
  could run. This is the known "concurrent ashmem read flaky-panic" and is
  inherent to overwriting a GLOBAL `miscdevice.fops` that the whole system
  uses.

## UPDATE 3 (round 4) — root cause found: try_module_get(owner) fails

Static analysis of the misc_open path (disasm `misc_open@0x86bd18`,
`try_module_get@0x23f47c`) + the Image dump pinpoints the configfs-read
failure:

- The rb_erase write#2 (`child->__rb_parent_color = pc`) always lands at
  `write_value+0 = fake_fops+0` = the fops **owner** slot, setting
  `owner = (target-8)|1 = 0xffffff80029bcbb9` (target = ashmem_misc.fops).
- `try_module_get(owner)` reads `state@owner` (=0xc009ccf9, ok) and
  `refcnt@owner+0x3c8` (= `*(u32*)0xffffff80029bcf81`). The Image dump shows
  **that u32 is 0** (it is padding just past `sun6i_msgbox_driver`), so
  `try_module_get` returns **false** → `fops_get` fails → `misc_open`
  returns **-ENODEV** → the open FAILS.
- This is device-specific: the OnePlus' equivalent refcnt garbage is non-zero,
  so its open succeeds; RV101's is 0.

Consequence: when write#1 DOES land (`asmhem_misc.fops = fake_fops`), the
subsequent `/dev/ashmem` open fails and the read never reaches configfs. When
write#1 does NOT land (reclaim flake), the open uses the original ashmem fops
and `ashmem_read_iter` returns 0 for the empty area. Either way the configfs
read returns 0 — which matches every run observed.

The OnePlus reference (`research/ghostlock_refs/extracted/oneplus/.../util.c`,
`fops.c`) confirms the read primitive should be:
`pos = ASHMEM_PREFIX_COUNT(0x6d6873612f766564="dev/ashm") - len`,
`page = target - pos`, `pread(fd, data, len, pos)` — so that
`from+pos == target` and `available-pos == len`. My `pos=0, page=target`
variant is equivalent and is NOT the bug.

### Fixes to try (owner = NULL / valid so try_module_get returns true)

1. **Third rb_erase write** to poke `*(u64*)0xffffff80029bcf81` non-zero
   (write#1 = valid addr → refcnt low32 != 0). But its write#2 then re-clobbers
   `owner` with `(0x29bcf81-8)|1` — cascading, messy.
2. **Restore owner to NULL after the install** before open: needs a write of 0
   to `fake_fops+0`, but rb_erase write#2 to `value+0` faults when value==0.
3. **Use configfs_read_bin_file (bin_buffer@0x58)** instead of
   configfs_read_file: the concurrent process's `bin_buffer_size@0x60` stays 0
   (zalloc) → its read returns 0 harmlessly (no NULL-page copy → no panic).
   This also avoids the concurrent-ashmem panic. Requires writing
   `name[88..99]` past the strscpy NUL — the OnePlus "zero_at" multi-SET_NAME
   trick, which RV101's word-at-a-time strscpy complicates (needs careful
   byte-by-byte zeroing of the mutex while keeping bin_buffer/target intact).

## UPDATE 4 (round 5) — write#2 LANDED but read STILL returns 0

Disassembled `rb_erase@0x774b38` (called from `rt_mutex_adjust_prio_chain@0x1ceaf4`).
The inlined `__rb_change_child` is exactly:

```
ldr  x9, [x0]            ; pc = node->__rb_parent_color
ands x10, x9, #-4        ; parent = pc & ~3
ldr  x12, [x11, #0x10]!  ; x12 = parent->rb_left = *(target+8)
cmp  x12, x0             ; parent->rb_left == node ?
csel x11, x11, x13, eq   ; x11 = (==) ? target+8 : target+0
str  x8, [x11]           ; WRITE#1 = node->rb_right (write_value) -> target+0/8
```

So write#1 → **target+0 (asmhem_misc.fops)** iff `*(target+8) != node`.

New evidence (bash-17, settled device, WITH the refcnt-fix second route):
```
install_fake_fops DIAG: write#2 LANDED (vehicle 35, child_pc=0xffffff80029bcbb9
                        read=0xffffffed16e39168 exp_read=0xffffffed16e39168)   <- read slot correct
configfs_read DIAG bad=0xdead000000000000 -> r=0 errno=0                      <- NOT -EFAULT
```

So even with write#2 proven landed (rb_erase ran, fake_fops table + read slot
correct), the read returns **0, not -EFAULT**. `simple_read_from_buffer` returns
0 only on `pos >= available` (available==0) or `!count`. count is passed as 8,
so `available == count@0x00 == 0` — OR the read never reached configfs (write#1
went to target+8/list.next and the open used the ORIGINAL ashmem fops).

Two remaining hypotheses:
- (A) write#1 actually landed at `target+8 = ashmem_misc.list.next` (needs
  `list.next == node`, which static analysis says is impossible — list.next is
  a .data pointer, node is a kernel-stack address).
- (B) write#1 landed at `target+0 = fops`, the refcnt-fix made the open succeed,
  and `configfs_read_file` returned 0 because `buffer->count@0x00 == 0` — i.e.
  `asmhem_area->name[0..7] != "dev/ashm"` at runtime (contradicts ashmem_open).

`/proc/misc` corruption (list.next == fake_fops) would prove (A). The device
panics/reboots during repeated mode-4 runs (bash-17 ended with the device
gone), which makes the decisive observation hard.

## UPDATE 5 (round 6) — switched to configfs_read_BIN_file (bin_buffer), root cause likely FOUND

Disassembled `configfs_read_bin_file@0x49ec40` and `configfs_write_bin_file@0x49ee00`:

- `configfs_read_bin_file` ends with
  `simple_read_from_buffer(buf, count, ppos, bin_buffer@0x58, bin_buffer_size@0x60)`
  — its **available is `bin_buffer_size` (name[96..99])**, a field the SET_NAME
  blob CAN control, unlike `configfs_read_file` whose available is `count@0x00`
  = the fixed "dev/ashm" prefix. This is the likely root cause of the read==0:
  `count@0x00` was not usable, and the bin-buffer path sidesteps it entirely.
- `configfs_write_bin_file` does `simple_write_to_buffer(bin_buffer, ...)` with
  NO item-ops flush — so it is a clean arbitrary write (no panic), unlike
  `configfs_write_file` (which calls `flush_write_buffer` -> item ops -> crash).

Fix implemented (built, synced, untested — device is in a long reboot):
- fake fops `.read` now points at `configfs_read_bin_file.cfi_jt`
  (`CFI_CONFIGFS_READ_BIN_OFF 0x1238e20`) instead of configfs_read_file.
- `configfs_read_once` now sets `bin_buffer@0x58 = target` and
  `bin_buffer_size@0x60 = len` via the OnePlus "zero_at" multi-SET_NAME trick
  (`ashmem_set_name_blob`): one full SET_NAME (0x00->0x01) then a descending
  re-SET_NAME with a single NUL at each zero position, so mutex owner
  (name[32..39]) / needs_read_fill (name[80..83]) / bin_buffer_size low bytes
  end up 0 while bin_buffer keeps the target. Traced the strscpy word-at-a-time
  zeroing: it only clobbers inside the NUL's 8-byte word, and target/len live
  in words 9-11, so the trick survives on RV101.
- The refcnt-fix second route (try_module_get(owner) needs `refcnt@0x29bcf81 != 0`)
  is still present; 0x29bcf81 is in sun6i_msgbox_driver padding (next symbol
  hwspinlock_tree @ 0x29bd000), so the 8-byte poke is benign.

Next: on the device once it returns, run mode-4 and confirm `configfs_read` now
returns 8 (not 0) — then the task walk + cred write can proceed.

## Next steps (priority order)

1. Catch a `write#2 LANDED` run with the new all-vehicle DIAG and read the
   `configfs_read DIAG` line to disambiguate (A) vs (B).
2. If (A): dump `asmhem_area->name[0..10]` (the prefix) — e.g. leak the
   ashmem_area address (kmalloc-4k) or add a second overlay read.
3. Improve reclaim reliability — the `first send`/buddy-LIFO steal is the
   dominant flake; consider GL_GATE_DELAY_USEC tuning or an immediate double
   send to grab the head more reliably.

## UPDATE 6 (round 10) — write#1 -> list.next PROVEN; mode-5 write#2 workaround implemented

Decisive static evidence closes hypothesis (A) vs (B) in favor of **(A)**:

- Binary dump of `ashmem_fops @ 0x1e977a8`: **owner = NULL**, read = NULL,
  write = NULL (ashmem uses `.read_iter`, and `fops_get`/`try_module_get(NULL)`
  returns TRUE). So the ORIGINAL ashmem open always succeeds and its pread on
  an empty area returns 0.
- `misc_open@0x86bd18` disasm: `list_for_each_entry` (list @ +0x18), on minor
  match does `fops_get(c->fops)` = `if (!try_module_get(fops->owner)) skip`;
  first pass miss -> request_module("char-major-..") -> second pass ->
  `try_module_get` false -> `-ENODEV` (0x86be64). file->private_data is set to
  &miscdevice, then the device's own open (ashmem_open) replaces it with the
  ashmem_area.
- `try_module_get@0x23f47c` disasm: `state@module+0 != 2` (== `"ashm"` low word
  of the name pointer = 0x09ccf9ae, passes) AND `refcnt@module+0x3c8 != 0`.
  fake owner = `(target-8)|1 = 0xffffff80029bcbb9` -> refcnt @ 0x29bcf80 == 0
  (binary dump of the sun6i_msgbox_driver padding is all-zero) -> returns false.

Therefore, if write#1 had landed at **target+0 (fops)**, the open MUST return
-ENODEV. bash-3 (no refcnt fix) showed the open SUCCEEDED with the GET_NAME
dump, and bash-17 (refcnt fix) still read 0 (not -EFAULT). Both are only
consistent with **write#1 -> target+8 (list.next)**, i.e. the fake fops was
never installed and the read went through the ORIGINAL ashmem read_iter.

Why `*(target+8) == node` (list.next == a kernel-stack address) remains
unexplained statically, but the *consequence* is now proven.

### Workaround shipped (mode 5, v9.32): install via the rb_erase write#2

Since write#1's direction is unusable for the fops field, use the case-1
child->pc fixup instead: `str x9,[x8]` stores `tree_pc` at `write_value+0`.
Set `write_value = fops_field` (the real ashmem_misc.fops address) and
`tree_pc = fake_fops` with **COLOR=0** (aligned), so write#2 stores fake_fops
straight into ashmem_misc.fops. write#1 = fops_field lands in fake_fops->llseek
(benign; the read path never calls llseek). fake_fops->owner stays NULL, so
the open succeeds with NO refcnt-fix second route.

- route.c: new `pselect_custom_write == 5` branch (write_value = custom value)
  + `tree_pc = (write_target-8)` color-0 for mode 5.
- util.c: `install_fake_fops_v2()` calls `set_pselect_write_mode(fake_fops+8,
  fops_field, 5)` and verifies write#1 via the fake table's llseek slot.
- main.c: uses `install_fake_fops_v2` by default (`GL_FOPS_V2=0` falls back to
  the old mode-4 install).

Built + synced to Windows. Next device run: mode-5 should make the open succeed
with fake fops and `kernel_read64(init_task.tasks)` return 8 — then the task
walk + cred write can proceed.

## UPDATE 7 (round 11-12) — device recovered (Windows adb.exe), mode-5 write#2 does NOT land in kernel-image .data

Two hard findings on the live device (serial <DEVICE_SERIAL>):

1. **The Linux `adb` (/usr/bin/adb) cannot see the device** — it is attached to the
   Windows USB stack. Only `adb.exe` shows `<DEVICE_SERIAL> device`. All prior
   "device offline" checks used the wrong adb. Correct invocation:
   `"$WINADB" = /mnt/c/.../platform-tools/adb.exe`.

2. **The rb_erase write lands in the sprayed kmalloc page but NOT in kernel-image
   .data.** mode-5 (v9.32: write#2 = tree_pc -> write_value=fops_field) ran and
   `write#1 LANDED (vehicle 145, llseek=0xffffff80029bcbc0)` — proving the
   rb_erase executed and write#2 (fake_fops -> ashmem_misc.fops) was the very
   next store. Yet the configfs read still returned **r=0** (and the
   bad=0xdead probe returned r=0, not -EFAULT), i.e. the ORIGINAL ashmem
   read_iter handled it and fake_fops was never installed.

Static re-checks that ruled out everything else:
- name layout confirmed via ashmem_open@0xc68a04 disasm: `str x11,"dev/ashm"`
  at name[0..7] then `stur w10,"mem/"` at name[7..10] -> name[0..10]="dev/ashmem/",
  count@0x00 = "dev/ashm" (0x6d6873612f766564), needs_read_fill@0x50=0.
- configfs_read_file@0x49e8e0 -> simple_read_from_buffer(page@0x10=target,
  count@0x00="dev/ashm") must return 8.
- fake fops cfi_jt slots verified (read->configfs_read_file, open->ashmem_open).

Conclusion: **the direct-map alias data_addr() of kernel-image .data does not
take effect on write.** The write primitive only works against the reclaimed
kmalloc page (mode-3 selftest STRICT OK). Kernel config shows
`CONFIG_STRICT_KERNEL_RWX=y` + `CONFIG_RODATA_FULL_DEFAULT_ENABLED=y`; the
kernel-image .data direct-map alias is either the wrong PA (P0_KERNEL_PHYS_LOAD
0x80000000 is suspect) or read-only via the linear map. Either way, the fake
fops install (asmhem_misc.fops, .data) and thus the configfs read are blocked,
so init_task.tasks walk -> cred overwrite cannot proceed.

Open lead for a future round: determine the true kernel image physical load
(and thus the correct direct-map alias). `data_addr()` currently assumes
`P0_KERNEL_PHYS_LOAD=0x80000000` (RAM base, text_offset=0 from the Image header).
A wrong PA shifts every kernel-image write to an unmapped/foreign page while the
sprayed-page writes still work.
