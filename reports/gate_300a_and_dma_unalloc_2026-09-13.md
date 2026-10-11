# GateServiced — `0x300A` hardcoding + `dma_unalloc` return-value space

**Date**: 2026-09-13
**Artifact**: `/home/kali/rokid_root/build-cache/GateServiced`
(`sha256 ecf72a60303aed8ecf7a21f99136572a1949862f7aeb1ee9ffbecbfb09e9fe75`, 20,304 B, ELF64 aarch64 `ET_DYN` PIE,
`.text` vaddr == file offset `0x2000`–`0x367C`)
**Method**: static only — capstone 5.0 (aarch64) + pyelftools, full linear-disassembly CFG with predecessor
closure per sink address. Scripts written for this pass: `build-cache/gate_re/lin.py` (linear CFG + backward
reachability + register-writer scan), `build-cache/gate_re/q1.py`, reusing `build-cache/gate_re/gdis.py`.
Upstream/public-source corroboration via AOSP `libdmabufheap` (quoted below) and a web hunt for the QTI
SMCInvoke gate sample (result: no public source found — see §Q1.5).
> **權威來源**：本檔的結論已折入 `reports/FACTS.md` §5.2（S18–S20）；若兩者不一致，以 **FACTS.md 為準**，
> 並請回頭修正本檔（引用規則見 FACTS §10）。

**Scope**: answers exactly two questions. Prior reports `reports/static_re_gate_codes_and_rescue_2026-09-13.md`
and `reports/GateServiced_Phase1_RE_Report.md` are treated as inputs and are **not** re-derived.

> ⚠ **Rev. 2 (2026-09-13 15:09) — one side-finding retracted.** §6 items 5–6 and the two matching rows
> of the Confidence table originally claimed that `dma_alloc`'s heap-name argument is a stack pointer
> (`0x5790`) and that this is why `dma_alloc` fails, and that an uninitialised fd slot lets
> `dma_unalloc` launder a failed allocation into success. **Both are wrong.** A live probe plus a
> re-read of `dma_alloc`'s prologue show the heap name is the internal rodata `"qcom,qseecom-ta"`
> (`0xfd2`) and the caller's `x1` is `len`; and the fd slot is initialised to `-1` before the
> allocation. See **`reports/gate_dma_alloc_heapname_probe_2026-09-13.txt`**. The two assigned
> questions (Q1 `0x300A`, Q2 `dma_unalloc` value space) are **unaffected**; only the
> "why does `dma_alloc` fail" side claim and the "indeterminate fd" sub-claim were retracted.

---

## Q1 `0x300A` hardcoding

### Q1.1 `run_load_ta` control-flow graph (0x2E4C–0x3354)

Full linear-leader CFG (18 basic blocks; `lin.py 0x2e4c 0x3354`):

| Block | Extent | Last insn | Successors |
| --- | --- | --- | --- |
| B0 | `0x2e4c`–`0x2eac` | `bl __android_log_print` | `0x2eb0` |
| B1 | `0x2eb0`–`0x2eb4` | `b #0x3244` | epilogue |
| B2 | `0x2eb8`–`0x2f1c` | `b #0x3194` | `0x3194` |
| B3 | `0x2f20`–`0x2f4c` | `b #0x3244` | epilogue |
| B4 | `0x2f50`–`0x2fbc` | `b #0x3194` | `0x3194` |
| B5 | `0x2fc0`–`0x30b0` | `b #0x31b4` | `0x31b4` |
| B6 | `0x30b4`–`0x30b8` | `b #0x30f8` | `0x30f8` |
| B7 | `0x30bc`–`0x30e8` | `b #0x30f8` | `0x30f8` |
| B8 | `0x30ec`–`0x30f4` | `mov w20, #0x3003` (falls through) | `0x30f8` |
| **B9** | **`0x30f8`–`0x3154`** | `bl __android_log_print` (after `0x312c cbnz x8,#0x3158`) | **`0x3158`**, `0x3130` |
| B10 | `0x3130`–`0x3154` | `bl __android_log_print` | `0x3158` |
| **B11** | **`0x3158`–`0x3190`** | `stp xzr,xzr,[x8]` | `0x3194` |
| B12 | `0x3194`–`0x31b0` | `blr x8` | `0x31b4` |
| B13 | `0x31b4`–`0x31f4` | `blr x8` | `0x31f8` |
| B14 | `0x31f8`–`0x31f8` | `stp xzr,xzr,[x21]` | `0x31fc` |
| B15 | `0x31fc`–`0x3218` | `blr x8` | `0x321c` |
| B16 | `0x321c`–`0x323c` | `blr x8` | `0x3240` |
| B17 | `0x3240`–`0x3240` | `stp xzr,xzr,[x19]` | `0x3244` |
| B18 | `0x3244`–`0x3274` | `ret` (`0x3254 mov w0, w20`) | — |
| B19 | `0x3278`–`0x327c` | `b #0x30f8` | `0x30f8` |
| B20 | `0x3280`–`0x331c` | `b #0x2eb0` | `0x2eb0` |
| B21 | `0x3320`–`0x334c` | `b #0x2eb0` | `0x2eb0` |
| B22 | `0x3350` | `bl __stack_chk_fail` | — |

Predecessor closure of **B11** (the block that holds both `0x3178` and `0x3180`) is exactly:

```
rev[B11] = { B9 (0x30f8), B10 (0x3130) }
```

`0x3194` is a **successor** of B11, never a predecessor — the post-failure cleanup sequence at
`0x3194`→`0x31b4`→`0x3240`→`0x3244` is reached *after* `0x3180` has run and never returns to it.
All three instructions in B11 (`0x3178 mov w6,#0x300a`, `0x3180 mov w20,#0x300a`, `0x3184 bl log`)
execute unconditionally in program order: **every execution of `0x3178` is immediately followed by
`0x3180`.**

### Q1.2 What `w20` holds on every path reaching `0x3178`

Backward-reachable blocks to `0x3180` (and to `0x3178` — identical set, since B11 contains both):

```
{ B0 0x2e4c, B2 0x2eb8, B4 0x2f50, B5 0x2fc0, B6 0x30b4, B7 0x30bc,
  B8 0x30ec, B9 0x30f8, B10 0x3130, B11 0x3158, B19 0x3278 }
```

Every write to `w20`/`x20` anywhere in that closure, in program order:

| # | Address | Instruction | Value written | Where it matters |
| --- | --- | --- | --- | --- |
| 1 | `0x30b4` | `mov w20, #0x3001` | 12289 (`GENERIC`/empty-file) | arg `w7` of the log at `0x3124` **only** |
| 2 | `0x30e4` | `mov w20, #0x3004` | 12292 (calloc NULL) | arg `w7` of the log at `0x3124` **only** |
| 3 | `0x30f4` | `mov w20, #0x3003` | 12291 (`read_file` < 0) | arg `w7` of the log at `0x3124` **only** |
| 4 | `0x3278` | `mov w20, wzr` | 0 | *itself overwritten*: falls to B9 → B11 |
| 5 | `0x3180` | `mov w20, #0x300a` | 12298 | **read at `0x3254 mov w0, w20` → the function's return value** |

The single read of the return slot is `0x3254 mov w0, w20` (B18). Between B11 and B18 there is no
write to `w20` on any path: B11→B12→…→B17→B18 are all writes to `x8`/`x0`/`w1`/`x21`/`x19`/`x26`
and the `stp xzr,xzr` pairs.

**Therefore: on 100 % of paths that reach `0x3178`, `w20 == 0x300A` at `0x3180`.** Values 1–3 are
live only as the `%d` argument of the *first* failure log line (`0x3120 mov w7, w20` /
`0x3124 bl __android_log_print`, tag `load_app`, source line `0x45`=69). After `0x3180` they are
dead: from `0x3180` onward `w20` is 12298, unreadably so, and 12298 is what the caller gets.

Corollary: `run_load_ta` has exactly **two** observable return classes —

* `0` — only from `0x2eb0 mov w20, wzr` (already-loaded short-circuit `0x2e84 b.ne`, *and* both
  success exits `0x331c`/`0x334c` which `b #0x2eb0`);
* `0x300b` — only from `0x2f48` (`appPath == NULL`, returns directly at `0x3244`);
* **`0x300a` — everything else.**

### Q1.3 Is the earlier code "truly dead once overwritten"?

Two distinct answers, and the distinction is the whole finding:

* As a **return value**: yes, dead. `0x3001`/`0x3003`/`0x3004` (and the `apploader` `loadApp`
  failure code copied at `0x3034 mov w20, w0`, and the `appController` code) can never reach
  `0x3254`. The caller (`cmd_dispatch` `0x2798 bl #0x2e4c` → `0x279c mov w21, w0`) sees only
  `0`/`0x300a`/`0x300b`.
* As a **log argument**: no, very much alive. `0x3120 mov w7, w20` consumes it, which is why the
  code exists at all and why the compiler did *not* delete stores 1–3.

### Q1.4 Compiler-optimization evidence (why this is source-level, not an artefact)

This is a decisive negative test rather than a speculation:

1. **A collapsed switch/if-chain would leave one store, not four.** We observe four separate
   materialisations (`0x30b4`, `0x30e4`, `0x30f4`, `0x3278`) plus the overwrite. The compiler kept
   each because each feeds a *different* log line.
2. **`0x3178` is a redundant reload only under the "source used a constant" hypothesis.** If the
   source had been `LOGE("Loading the application failed with %d!", w20);` with `w20` already in
   a general-purpose register, the compiler would have copied `w20` into `w6`
   (`mov w6, w20`, exactly as at `0x2f10`, `0x2fb0`, `0x30a4`, `0x2ca8`, `0x2cf4`) and needed no
   `movz`. Instead it emits `mov w6, #0x300a` — **the literal is materialised into the argument
   register as an immediate.** A `w20`-valued log cannot produce `mov w6, #imm`.
3. **No `#define`/enum can be recovered from the binary, but the constant is not a compiler
   sentinel.** The compiler never invents an error code; `0x300A` exists in the source token
   stream as a literal or as a macro/enum that expands to it.
4. **The `goto fail` shape is real and structural.** `0x30f8` is a single shared tail entered by
   four distinct error branches (`0x30b8`, `0x30e8`, fall-through from `0x30f4`, `0x327c`) and by
   the `loadApp`-error test `0x304c cbnz w20,#0x30f8`; the tail logs once with the true code
   (`0x3124`), conditionally logs "appController is NULL!" (`0x3130`), then enters the
   *unconditional* cleanup block `0x3158` that logs a fixed code and returns it. Classic
   "`fail:`-label used both as 'log the real reason' and as 'return one generic status'" — the
   second half was written against a constant and never updated.

**Why the binary's other paths keep their distinct codes.** Every other `0x30xx` site is on a
*return* path that does not traverse `0x3158`: `0x300c`→`0x2d30`, `0x300d`→`0x2d30`,
`0x300e`→`0x2d30` (all `run_exec_cmd_ta`), `0x3005`→`0x2cb0`, `0x300b`→`0x3244`, `0x3002`→`0x27d8`
(`cmd_dispatch`), `0x3001`/`0x3003`/`0x3004`→`0x30f8`. They are distinct because they never pass
through the one block that clobbers `w20`. `0x300A` is the *only* code on the cleanup/teardown
path, and it is a fixed value because the source wrote a fixed value there.

### Q1.5 Public upstream source

A dedicated hunt was run (GitHub/searchcode/grep.app/Wayback; exact tokens `ISMCIGate_sharedMemoryExample`,
`dtxGetAppletInfo`, `smcinvoke_gate_ta`, `qseeproxydaemon`, `run_load_ta`, `"Loading the application
failed with %d!"`, `run_shared_memory_example`, `TZCom_getClientEnvObject`). Findings:

* Qualcomm does publish an SMCInvoke concept page — [Qualcomm Linux Security Guide, "SMCInvoke"](https://docs.qualcomm.com/doc/80-70014-11/topic/smcinvoke.html)
  — but the fetched page contains **no** gate-sample source, no error-code table, and no
  `dtxGetAppletInfo`/`ISMCIGate_sharedMemoryExample` identifier (the page body is a JS shell).
* No public copy of `run_load_ta` / `run_shared_memory_example` / the `0x30xx` enum was located.
  The only confident public statement that can be made is that the *DMA/dmabuf* half of the binary
  is stock AOSP `libdmabufheap` (see Q2.4), and the `0x30xx` codes are vendor-private.

**Verdict: NOT FOUND — and now a *supported* negative.** A second, deeper OSINT pass was run
(Sourcegraph public-code API, GitHub REST repo search, CodeLinaro GitLab project search, full-tarball
grep of the public Qualcomm **`mink`** repo, GitHub `qualcomm/quic-teec`) and produced:

* `ISMCIGate_sharedMemoryExample`, `ISMCIGate`, `dtxGetAppletInfo`, `run_load_ta`,
  `run_shared_memory_example`, `TZCom_getClientEnvObject`, `TZCom_getFdObject`,
  `"Loading the application failed with %d!"`, `"dma_alloc failed with return value =%d"`,
  `"appController is NULL!"` — **0 hits each**, including across all 768 files of the public `mink`
  tree; that tree contains literally **zero** `0x3xxx` literals.
  *Control:* `DmabufHeapCpuSyncStart` → 12 hits, so the zero results are meaningful, not an
  artefact of a broken search.
* **The public framework DOES exist and DOES contain GateServiced's callees** — this is the useful
  positive: [`TZCom.cpp` `TZCom_getClientEnvObject`/`TZCom_getFdObject`](https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/mink/-/blob/main/libmink/Transport/TZCom/src/TZCom.cpp),
  [`libmink/SDK/QTVM/sampleCA/src/main.c`](https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/mink/-/blob/main/libmink/SDK/QTVM/sampleCA/src/main.c)
  (`CreateDmabufHeapBufferAllocator` → `DmabufHeapAlloc` → `T_CHECK(munmap…)` →
  `FreeDmabufHeapBufferAllocator`, plus `static Object gAppController` and
  `IAppLoader_loadFromBuffer(appLoader, …, &gAppController)`), and the TA interface
  `ISMCIExampleApp` with method `sharedMemoryExample` — i.e. the binary's
  `ISMCIGate_sharedMemoryExample` is the same `ISMCI<X>_sharedMemoryExample` naming scheme with
  `X = Gate` instead of `X = ExampleApp`.
* **The upstream *sibling* daemon logs the real code where GateServiced logs a constant.** Public
  [`mink_platform/HLOS/minkdaemon/src/TaLoader.cpp:104-113`](https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/mink/-/blob/main/mink_platform/HLOS/minkdaemon/src/TaLoader.cpp):
  ```c
  ret = IAppLoader_loadFromBuffer(appLoader, buffer, size, appController);
  if (ret) {
      LOG_ERR("Retry count %d: Load app %s failed: %d (%s)", retry, path.c_str(), ret,
              app_loader_strerror(ret));
  ```
  That is `"Load app %s failed: %d (%s)"` — the binary's string is `"Load app %s failed: %d"`
  (`0x14f6`; the `(%s)` variant is **absent** from the binary, verified by substring search), the
  same family with the symbolising suffix dropped. So GateServiced is a **sibling/older
  revision of a public sample that passes the raw loader return**, which is exactly the
  behaviour GateServiced's `0x3120 mov w7, w20` reproduces at `0x3124`.
* **`0x3001`–`0x300e` cannot be an upstream framework code**, because every public code space on
  this path is documented and *none* uses that range: the kernel apploader
  [`IQSEEComCompatAppLoader.h`](https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/securemsm-kernel/-/blob/sec-kernel.lnx.13.1.r5-rel/smcinvoke/IQSEEComCompatAppLoader.h)
  defines `IAppLoader_ERROR_*` `INT32_C(10)`…`INT32_C(26)`; the userspace stringifier
  `mink_platform/HLOS/minkdaemon/src/TaLoader.cpp:26-53 app_loader_strerror()` spans base 10..21;
  `libqcomtee/include/qcomtee_errno.h` uses `0..10` and `-90..-103`; `Object_ERROR_*` uses `1..5`.
  A load failure surfacing as `0x300A` is therefore **the sample's own private enum/constant**.

**Verdict restated: NOT FOUND, by design.** The `0x30xx` names live in a private header that was
never published; the "generic load-failed" reading rests on binary evidence (Q1.2–Q1.4) plus the
sibling-source contrast above, not on a quoted declaration. Treat any specific name
(`GATE_LOAD_FAILED`, `SMCINVOKE_GATE_ERROR_LOAD`, ...) as **unverified**.

### Q1.6 Conclusion — Q1

> **FACT (evidence-backed).** `0x300A` is an unconditional, source-level hardcoded constant emitted
> into `w20` at `0x3180` on every path that reaches the shared failure/cleanup tail at `0x30f8`
> (`rev[0x3158] = {0x30f8, 0x3130}`, and `0x3178`/`0x3180` are consecutive instructions in one
> basic block). It overwrites the true, per-fault codes (`0x3001`/`0x3003`/`0x3004`, the `loadApp`
> return, the `appController` code) **after** those codes have been consumed by the `load_app`
> log line at `0x3124`, and the overwritten value is the one returned at `0x3254`. Consequently
> `run_load_ta` reports `0x300A` for every internal failure except `appPath == NULL` (`0x300B`),
> while the informative code survives only in `logcat`.
>
> **INFERENCE (most probable explanation).** The vendor's `run_load_ta` has a single shared
> clean-up/label tail whose source contains a *literal* generic "application load failed" status
> (the `0x300A` token, spelled literally or via a private macro/enum) which is passed both to the
> `"Loading the application failed with %d!"` log call and to the function's return. The author
> logged the *real* code earlier in the tail (`"Load app %s failed: %d"`, which does receive
> `w20` via `0x3120 mov w7, w20`) but left the tail's own status constant — i.e. an
> intentional-looking generic status that in practice destroyed the diagnostic value of the
> return path. It is **not** a compiler collapse, **not** a `goto fail` mis-merge bug, and **not**
> an uninitialised register: `movz` of a fresh immediate cannot be produced by any of those.
> **Corroborated by the public sibling source** (Q1.5): the published
> `mink_platform/HLOS/minkdaemon/src/TaLoader.cpp` passes the **raw** `IAppLoader_loadFromBuffer`
> return into its `"Load app %s failed: %d (%s)"` log and then returns it; GateServiced's
> `0x3124` reproduces the first half (`0x3120 mov w7, w20`) and then replaces the value with
> `0x300a` before returning. The upstream code space contains no `0x30xx` code at all
> (`IAppLoader_ERROR_*` = 10..26, `QCOMTEE_*` = 0..10 / -90..-103, `Object_ERROR_*` = 1..5), so
> `0x300a` is the Gate sample's own private constant, not a framework status it could have
> mistakenly substituted.
> Confidence **High** (≥95 % after the sibling-source contrast); the residual uncertainty is only
> whether the source spelled the token as a macro/enum vs. a raw literal — irrelevant to behaviour.

---

## Q2 `dma_unalloc` return space

### Q2.1 Call sites — there is exactly one, and it is a direct call

| Kind | Address | Instruction | Note |
| --- | --- | --- | --- |
| **PLT stub for `dma_unalloc`** | — | **does not exist** | `dma_unalloc` is a *local* function (`nm -D` lists only its callees). Its address `0x35d0` is not in `.rela.plt` / `.dynsym` |
| **Call site (only one in `.text`)** | `0x2c78` | `bl #0x35d0` | inside `run_exec_cmd_ta` (`0x2a70`–`0x2e4c`), in the inlined `run_shared_memory_example` cleanup region |
| Consumers of its result | `0x2c7c mov w20, w0` | | clobbers the caller's own error code |
| | `0x2c80 cbz w0, #0x2cb0` | | 0 → skip the error log |
| | `0x2ca8 mov w6, w20` / `0x2cac bl __android_log_print` | | logs `"dma_unalloc failed with return value =%d"` (rodata `0x131a`, tag `run_shared_memory_example` `0x18b4`, source line `0x97`=151) |
| | `0x2ccc cbz w20, #0x2d64` | | 0 → **success** branch of `run_exec_cmd_ta` |
| | `0x2cf4 mov w6, w20` / `0x2cf8 bl __android_log_print` | | logs `"run_shared_memory_example input_ptr failed with error: %d"` (rodata `0x1926`) |
| | `0x2d64`–`0x2d88` (logs `"…succeeded…"`, line `0x100`) → `0x2d30 ldr x8,[x26,#0x28]` (canary) → **`0x2d40 mov w0, w20`** → `ret` | | **becomes `run_exec_cmd_ta`'s return value** |
| Whole-binary scan | `bl #0x35d0` occurs **once**; `DmabufHeapCpuSyncEnd` (`0x3910`) is called **once**, at `0x3604` inside `dma_unalloc` | | no second path into either |

Callers reach `0x2c78` from **three** places, all of which then read the result:

| Path | Arrives at `0x2c78` via | Error code held in `w20` *before* the call | Overwritten? |
| --- | --- | --- | --- |
| `dma_alloc` failed | `0x2b28 bl #0x346c` → `0x2b2c cbz w0,#0x2bf0` **not taken** → `0x2b30`–`0x2b58` log `"dma_alloc failed with return value =%d"` (rodata `0x1624`) → `0x2b5c b #0x2c64` → `0x2c78` | `dma_alloc`'s return | **yes** |
| `TZCom_getFdObject` failed | `0x2c34 cbz w0,#0x2d98` not taken → `0x2c38`–`0x2c5c` log `"Mapping sharedMemObj failed!"` (rodata `0x17fb`) → falls into `0x2c64` → `0x2c78` | `TZCom_getFdObject`'s return | **yes** — not even stored first: the log at `0x2c5c` drops it and `0x2c7c` writes `w0` |
| success so far | `0x2d98`…`0x2dc4` TA call → `0x2d74`/`0x2de4` → falls into `0x2c64` → `0x2c78` | actual TA result | **yes** |

### Q2.2 Values `dma_unalloc` can return

Exhaustive register trace of `dma_unalloc` (`0x35d0`–`0x3678`): the only writes to the return
register are

```
0x3608:  mov  w21, w0        ; w21 = DmabufHeapCpuSyncEnd(...) result
0x3664:  mov  w0, w21        ; return w21
```

`w21` is never reassigned between them; the two other cleanup calls discard their results
(`0x3638 bl munmap` → result unused; `0x3660 bl FreeDmabufHeapBufferAllocator` → `void`).
Therefore:

> **`dma_unalloc` returns exactly what `DmabufHeapCpuSyncEnd()` returns: `0` on success, `-1` on
> failure. Its value space is `{0, -1}` — it is *not* an `-errno` channel and it never returns a
> `0x30xx` vendor code.**

This is confirmed against the AOSP implementation the vendor linked against
([`BufferAllocatorWrapper.cpp`](https://android.googlesource.com/platform/system/memory/libdmabufheap/+/refs/heads/main/BufferAllocatorWrapper.cpp),
[`BufferAllocator.cpp`](https://android.googlesource.com/platform/system/memory/libdmabufheap/+/refs/heads/main/BufferAllocator.cpp)):
`int DmabufHeapCpuSyncEnd(...)` funnels into `BufferAllocator::CpuSyncEnd` → `DoSync`, whose body is
`return TEMP_FAILURE_RETRY(ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync));` — the raw `ioctl` return,
i.e. `0` or `-1` with `errno` set. The `-EINVAL` early-outs in the wrapper are unreachable here
because `buffer_allocator` was already NULL-checked at `0x2b10`/`0x2bc0`.

The binary's own check at `0x3630 cmn x0,#1` / `0x3634 b.eq` (skip `munmap` when the mapped pointer
is still `-1`) matches the `-1`-sentinel convention, and `dma_alloc`'s `0x34d0 cmn x0,#1` does the
same for `DmabufHeapAlloc` (which returns a **fd** on success, `-1` on failure — confirmed in
`BufferAllocator::DmabufAlloc`, `return heap_data.fd` / `return ret`).

### Q2.3 What the caller does with each value

```
0x2c78:  bl   #0x35d0          ; dma_unalloc(allocator, heapname, &bufid, &mapped)
0x2c7c:  mov  w20, w0          ; <== caller's error code is DESTROYED here
0x2c80:  cbz  w0, #0x2cb0      ; ret == 0  -> skip the log
0x2c84..0x2cac  log "dma_unalloc failed with return value =%d"  (only when ret != 0)
0x2cb0:  ldr  x8, [sp,#0x10]   ; appController vtable (TA-controller destroy)
0x2cb4:  cbz  x8, #0x2ccc
0x2cc8:  blr  x8               ; called with w1 = 0xffff regardless of ret
0x2ccc:  cbz  w20, #0x2d64     ; ret == 0  -> TAKE THE SUCCESS BRANCH
0x2cd0..0x2cf8  log "run_shared_memory_example input_ptr failed with error: %d"
...      then either 0x2cfc (len==0 -> 0x300E) or 0x2d64 (success log) / 0x2d30 epilogue
0x2d40:  mov  w0, w20      ; <== run_exec_cmd_ta return  -> cmd_dispatch 0x2770 mov w21, w0
```

| `dma_unalloc` ret | Real-world trigger | Caller behaviour | Client-visible? |
| --- | --- | --- | --- |
| `0` (ioctl OK) | sync succeeded | `0x2c80` skips log; `0x2ccc` jumps to `0x2d64` → **returns 0 from `run_exec_cmd_ta`** | **NO error at all** — `cmd_dispatch` returns 0, `handle_client` takes the success path `0x21e4 cbz w0,#0x22bc` and `__sendto_chk` (`0x2350`) sends the (possibly empty/stale) buffer. The *pre-existing* `dma_alloc`/`TZCom` failure is **completely swallowed for the client**, and the `run_exec_cmd_ta` code is overwritten to 0. Only the earlier `"dma_alloc failed…"` / `"Mapping sharedMemObj failed!"` lines remain, in logcat |
| `-1` (ioctl failed) | e.g. `EBADF`/`EINVAL` | `0x2c80` falls through → log at `0x2cac`; `0x2ccc` falls through → log at `0x2cf8`; returns `0xffffffff` (`-1`) → `cmd_dispatch` `0x2770 mov w21,w0` → `handle_client` `0x21e4` non-zero → logs `"cmd_dispatch failed, ret:%x"` = `ret:ffffffff` and **sends 0 bytes** | **NO** — `handle_client`'s non-zero path never calls `sendto` (prior report §A.4 confirmed). logcat only |

**Answer to "logcat only, or swallowed":** for the *client*, a real `dma_unalloc` **failure** is
indistinguishable from a service-side failure — `handle_client` closes the exchange with **zero
bytes sent** (`0x21e4 cbz w0,#0x22bc`; all `sendto` traffic is on the zero-return path only).
A `dma_unalloc` **success after an earlier failure** is worse than swallowed: `0x2ccc cbz w20,#0x2d64`
converts the operation into a **reported success**, because the only tested value is
`dma_unalloc`'s return, not the original fault. The two `__android_log_print` lines at `0x2cac`
and `0x2cf8` (plus `0x2b58`/`0x2c5c` for the original fault) are the **only** record anywhere.

A concrete, instruction-provable instance of the "success after failure" case: after a **successful**
`dma_alloc`, any later failure falls into `0x2c64` → `0x2c78` with a **valid** fd, so
`DmabufHeapCpuSyncEnd` succeeds, `dma_unalloc` returns `0`, and `0x2ccc` reports **success** over the
earlier fault. Both reaching edges are visible in the CFG: `0x2c34 cbz w0,#0x2d98` → (`0x2d9c`
`cbz x8,#0x2c38` → `0x2e20 b #0x2c64`) and the `TZCom_getFdObject` failure at `0x2c38` itself.
**Correction (2026-09-13 15:09):** this case is *not* reached via an uninitialised fd — the
out-slot is set to `-1` at `0x2b00`/`0x2b04` before `dma_alloc` and written only on success
(`0x34f0`), so a failed allocation yields `dma_unalloc` = `-1` and the error branch. The earlier
"indeterminate `[x29-0x1c]`" reading is retracted; see
`reports/gate_dma_alloc_heapname_probe_2026-09-13.txt`. Likewise the companion claim that
`dma_alloc`'s heap-name argument is a stack pointer — and that this is why it fails — is retracted:
`0x3494` loads the internal rodata heap name `0xfd2` and the caller's value becomes `len` (`0x3498`),
and two live `EXEC_CMD` runs succeeded with no `dma_alloc failed` line. (Flagged as a finding; not one
of the two assigned questions.)

**Public scaffolding now positively identified (Q2.4a).** The OSINT pass located the published
Qualcomm `mink` sources that GateServiced's DMA half is built from, which locks down the API
contracts used above:

* [`libmink/SDK/QTVM/libs/libdmabufheap/include/BufferAllocatorWrapper.h`](https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/mink/-/blob/main/libmink/SDK/QTVM/libs/libdmabufheap/include/BufferAllocatorWrapper.h)
  declares exactly the five imported symbols, including
  `int32_t DmabufHeapAlloc(BufferAllocator*, const char* heap_name, size_t len, unsigned int heap_flags, size_t legacy_align)`
  — a correction to the earlier reading (see §6 item 6): the binary satisfies this order and does
  **not** pass the caller's value as `heap_name` (`gate_dma_alloc_heapname_probe_2026-09-13.txt`) —
  and `int32_t DmabufHeapCpuSyncStart/End(BufferAllocator*, unsigned int dmabuf_fd, SyncType, void*)`.
* [`mink_platform/HLOS/minkdaemon/taAutoLoad/DmaMemPool.cpp`](https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/mink/-/blob/main/mink_platform/HLOS/minkdaemon/taAutoLoad/DmaMemPool.cpp)
  is the published closest analog of the binary's `dma_alloc`/`dma_unalloc` pair; it uses the
  **same heap name** `static const char *DMA_BUF_HEAP("qcom,qseecom-ta")` that the binary hardcodes
  at rodata `0xfd2` (referenced `0x3494`) and the same `DmabufHeapCpuSyncStart`/`CpuSyncEnd` +
  `munmap` + `DmabufHeapAlloc(…, DMA_BUF_HEAP, len, 0, 0)` sequence.
* [`libmink/SDK/QTVM/sampleCA/src/main.c`](https://git.codelinaro.org/clo/la/platform/vendor/qcom/opensource/mink/-/blob/main/libmink/SDK/QTVM/sampleCA/src/main.c)
  shows the same object model (`static Object gAppController`,
  `IAppLoader_loadFromBuffer(appLoader, …, &gAppController)`) and the same
  `CreateDmabufHeapBufferAllocator` → `DmabufHeapAlloc` → `munmap` ×2 →
  `FreeDmabufHeapBufferAllocator` teardown shape as the binary's `0x346c`/`0x35d0`.

One divergence worth recording (does not affect Q2's answer): the binary passes
`sync_type = 3` (`kSyncReadWrite`) to **both** wrappers (`0x34dc mov w2,#3` before
`DmabufHeapCpuSyncStart` at `0x34f4`, and `0x35f4 mov w2,#3` before `DmabufHeapCpuSyncEnd` at
`0x3604`), whereas the public sibling's release path uses `kSyncWrite`. That only widens the sync
class; it changes neither the `{0,-1}` value space nor the `0x3630 cmn x0,#1` `-1` fd guard.

This is scaffolding, not the sample: `run_load_ta`/`run_exec_cmd_ta`/`run_shared_memory_example`,
`dma_alloc`, `dma_unalloc` and the `0x30xx` enum appear **nowhere** publicly (see Q1.5), so the
return-value semantics proven in Q2.2 still rest on the binary plus the AOSP/QTI library sources,
not on a published copy of these two wrappers.

### Q2.4 Relation to the already-known codes and strings

* **`0x3005` is *not* on this path.** `0x3005` is written at `0x2be8`, reached from
  `0x2b10 cbz x0,#0x2bc0` — i.e. `CreateDmabufHeapBufferAllocator()` returned NULL. That branch
  jumps to `0x2cb0` (`0x2bec b #0x2cb0`), **skipping `dma_unalloc` entirely** (no allocator object
  exists to sync). So `0x3005` *does* reach the caller intact — it is the one DMA-path error code
  that survives. This corrects the implication in
  `reports/GateServiced_Phase1_RE_Report.md` §6 (per the prior report's §1) only in emphasis: the
  *code* attribution is right, but the *contrast* matters — `0x3005` survives precisely because it
  bypasses `0x2c78`.
* **`"dma_alloc failed with return value =%d"`** — rodata vaddr `0x1624`, referenced from exactly
  one site, `0x2b40 add x2, x2, #0x624`, logged at `0x2b58`; `w6` (`0x2b3c mov w6, w0`) carries
  `dma_alloc`'s `{0, -1}`-style failure return (a **negative fd/`-1`**, printed as a large unsigned
  `%d`, e.g. `-1` → `-1`). The log is *immediately* superseded: `0x2b5c b #0x2c64` → `0x2c78` →
  `0x2c7c`, where `w20` is overwritten. **The value printed at `0x2b58` is never the value
  returned** unless `dma_unalloc` happens to fail with the same code.
* **`"dma_unalloc failed with return value =%d"`** — rodata `0x131a`, single site `0x2c90`.
* **`"Error: DMA_BUF_IOCTL_SYNC start failed, ret = %d"`** (rodata `0x18ce`) is a **one-string,
  two-site** message reused by both sync wrappers: `0x3504` (inside `dma_alloc`, after
  `DmabufHeapCpuSyncStart` at `0x34f4`) and `0x3614` (inside `dma_unalloc`, after
  `DmabufHeapCpuSyncEnd` at `0x3604`). Neither site sets the `x4` tag register — the last `x4`
  write before each is its own NULL argument (`0x34ec mov x4, xzr` and `0x35fc mov x4, xzr`
  respectively), with the intervening BLs writing nothing after it — so **both lines print with a
  NULL/empty tag**, and only the `"start"` wording plus the surrounding lines attribute one.
  Both wrappers also collapse every ioctl failure to `-1` (`0x34f8 mov w23, w0` → `0x3510` and
  `0x3608 mov w21, w0` → `0x3620`), so a `ret = -1` in logcat carries **no** errno and does not by
  itself say which direction failed.
* **`0x300A` vs `dma_unalloc`**: fully orthogonal. `run_load_ta`'s `0x300A` overwrite is at
  `0x3180` and never touches DMA; `run_exec_cmd_ta`'s DMA path never reaches `0x3180`. The two
  independent "the code you see is not the code that failed" defects are in different functions.

### Q2.5 Conclusion — Q2

> **FACT.** `dma_unalloc` (`0x35d0`) has **one** call site (`0x2c78`) and **no** PLT stub; it
> returns **exactly** `DmabufHeapCpuSyncEnd`'s result (`0x3608`/`0x3664`), which per the linked
> AOSP `libdmabufheap` is a raw `ioctl(DMA_BUF_IOCTL_SYNC)` return: **`0` or `-1`**, never
> `-errno`, never a vendor `0x30xx`. On any non-zero value the caller logs
> `"dma_unalloc failed with return value =%d"` (`0x2cac`) and returns the value itself
> (`0x2d40 mov w0, w20`), destroying its own richer error code at `0x2c7c`. On zero (`0x2ccc`) it takes the
> **success** branch — so an earlier `dma_alloc`/`TZCom_getFdObject` failure can be reported to
> the caller as success. Either way **nothing reaches the client**; `handle_client` only ever
> `sendto`s on a zero return, and its non-zero path sends zero bytes.
>
> **INFERENCE.** The source coupled cleanup status to return status (`ret = dma_unalloc(...);
> if (ret) LOG(...); return ret;`) with no separate `status` variable — a coding pattern that is
> only correct when cleanup cannot fail independently of the operation. It is a *reporting*
> defect (error-code laundering), not itself a memory-safety bug; its security relevance is that
> the DMA-failure surface is invisible both on the wire and, for the success case, in the
> returned code. Confidence **Very High** for the mechanical claims; **Medium** for how often the
> indeterminate-fd success path actually triggers on the device (needs one live probe).

---

## Evidence (disassembly excerpts)

Byte columns are the raw instruction encodings; source lines shown in `;` comments are the
`mov w5,#imm` line numbers decoded from the binary.

### E1 — Q1, the decisive excerpt: `0x3158`–`0x3190` (block B11)

```asm
  0x03158:  e2ffffd0   adrp     x2, #0x1000
  0x0315c:  e3ffffd0   adrp     x3, #0x1000
  0x03160:  e4ffffd0   adrp     x4, #0x1000
  0x03164:  42680791   add      x2, x2, #0x1da      ; -> 0x11da "Loading the application failed with %d!"
  0x03168:  63bc2291   add      x3, x3, #0x8af      ; -> 0x18af "ERRO"
  0x0316c:  84800291   add      x4, x4, #0xa0       ; -> 0x10a0 "run_load_ta"  (tag)
  0x03170:  c0008052   mov      w0, #6
  0x03174:  05198052   mov      w5, #0xc8           ; source line 200
  0x03178:  46018652   mov      w6, #0x300a         ; <== LOG ARGUMENT IS A FRESH IMMEDIATE, not "mov w6, w20"
  0x0317c:  e1031faa   mov      x1, xzr
  0x03180:  54018652   mov      w20, #0x300a        ; <== RETURN SLOT DESTROYED
  0x03184:  57010094   bl       #0x36e0             ; __android_log_print
  0x03188:  080000d0   adrp     x8, #0x5000
  0x0318c:  08011091   add      x8, x8, #0x400
  0x03190:  1f7d00a9   stp      xzr, xzr, [x8]      ; appController = NULL
```

Predecessors of this block, proven by predecessor closure, are **only** `0x30f8` and `0x3130` —
and `0x30f8` is itself reachable only from `0x30b4` (`0x3001`), `0x30bc` (`0x3004`),
`0x30ec`→`0x30f4` (`0x3003`), and `0x3278` (`w20 = 0`):

```asm
  0x02fc8:  10feff97   bl       #0x2808             ; get_file_size(appPath)
  0x02fcc:  40070034   cbz      w0, #0x30b4         ; == 0  -> w20 = 0x3001 (empty file)
  0x02fe4:  c00600b4   cbz      x0, #0x30bc         ; calloc NULL -> w20 = 0x3004
  0x02ffc:  8007f837   tbnz     w0, #0x1f, #0x30ec  ; read_file < 0 -> w20 = 0x3003
  0x0304c:  74050035   cbnz     w20, #0x30f8        ; loadApp() failed -> shared tail
  0x03054:  281100b4   cbz      x8, #0x3278         ; appController == NULL -> w20 = 0 -> shared tail
  0x030b4:  34008652   mov      w20, #0x3001
  0x030b8:  10000014   b        #0x30f8
  0x030e4:  94008652   mov      w20, #0x3004
  0x030e8:  04000014   b        #0x30f8
  0x030f4:  74008652   mov      w20, #0x3003
  0x030f8:  e2ffffd0   adrp     x2, #0x1000         ; --- shared failure tail ---
  0x0311c:  e60315aa   mov      x6, x21             ; %s = appPath
  0x03120:  e703142a   mov      w7, w20             ; %d = the REAL code, read here …
  0x03124:  6f010094   bl       #0x36e0             ; … "Load app %s failed: %d"  (tag load_app, line 0x45)
  0x03128:  680342f9   ldr      x8, [x27, #0x400]
  0x0312c:  680100b5   cbnz     x8, #0x3158         ; non-NULL appController -> straight to B11
  0x03130:  ...                                    ; else log "appController is NULL!" (line 0x47) -> B11
```

`0x3120 mov w7, w20` is the *only* consumer of the true code; `0x3180` is the *only* producer of
the returned code.

### E2 — Q2, the decisive excerpt: `dma_unalloc` body + its single call site

```asm
===== dma_unalloc @ 0x35d0 (172 bytes) — returns DmabufHeapCpuSyncEnd's result and nothing else =====
  0x035d0:  fd7bbca9   stp      x29, x30, [sp, #-0x40]!
  0x035e4:  f70301aa   mov      x23, x1             ; heap name (2nd arg)
  0x035e8:  410040b9   ldr      w1, [x2]            ; dmabuf_fd := *p_dmabuf_fd   <-- the fd under test
  0x035ec:  f60303aa   mov      x22, x3             ; &map_ptr
  0x035f4:  62008052   mov      w2, #3              ; sync_type = kSyncReadWrite
  0x035f8:  e3031faa   mov      x3, xzr
  0x035fc:  e4031faa   mov      x4, xzr
  0x03600:  f30300aa   mov      x19, x0             ; allocator
  0x03604:  c3000094   bl       #0x3910            ; DmabufHeapCpuSyncEnd(alloc, *p_fd, 3, NULL, NULL)
  0x03608:  f503002a   mov      w21, w0             ; <== SOLE source of the return value
  0x0360c:  e0000034   cbz      w0, #0x3628         ; 0 -> skip log
  0x03610:  e2ffffd0   adrp     x2, #0x1000
  0x03614:  42382391   add      x2, x2, #0x8ce      ; -> 0x18ce "Error: DMA_BUF_IOCTL_SYNC start failed, ret = %d"
  0x03618:  c0008052   mov      w0, #6
  0x03620:  e303152a   mov      w3, w21
  0x03624:  2f000094   bl       #0x36e0
  0x03628:  c00240f9   ldr      x0, [x22]           ; map_ptr
  0x0362c:  1f0400b1   cmn      x0, #1              ; == -1 ?
  0x03630:  a0000054   b.eq     #0x3644            ; yes -> skip munmap
  0x03634:  e10317aa   mov      x1, x23
  0x03638:  ae000094   bl       #0x38f0            ; munmap(...)   <-- RESULT DISCARDED
  0x0363c:  08008092   mov      x8, #-1
  0x03640:  c80200f9   str      x8, [x22]
  0x03644:  800240b9   ldr      w0, [x20]           ; *p_fd
  0x03648:  8000f837   tbnz     w0, #0x1f, #0x3658  ; fd < 0 -> skip close
  0x0364c:  31000094   bl       #0x3710            ; close(fd)     <-- RESULT DISCARDED
  0x03650:  08008012   mov      w8, #-1
  0x03654:  880200b9   str      w8, [x20]
  0x03658:  730000b4   cbz      x19, #0x3664
  0x0365c:  e00313aa   mov      x0, x19
  0x03660:  a8000094   bl       #0x3900            ; FreeDmabufHeapBufferAllocator (void)
  0x03664:  e003152a   mov      w0, w21             ; <== return w21  == { 0, -1 }
  0x03678:  c0035fd6   ret
```

```asm
===== run_exec_cmd_ta: the ONLY call site, and every use of the result =====
  0x02b28:  51020094   bl       #0x346c            ; dma_alloc(alloc, heapname, &bufid, &map)
  0x02b2c:  20060034   cbz      w0, #0x2bf0         ; 0 -> success
  0x02b3c:  e603002a   mov      w6, w0
  0x02b40:  42901891   add      x2, x2, #0x624      ; -> 0x1624 "dma_alloc failed with return value =%d"
  0x02b50:  a50d8052   mov      w5, #0x6d           ; line 109
  0x02b58:  57010094   bl       #0x36e0             ; LOG (logcat only)
  0x02b5c:  42000014   b        #0x2c64             ; -> cleanup, converges on 0x2c78
  ...
  0x02c78:  56020094   bl       #0x35d0            ; <== dma_unalloc(...)
  0x02c7c:  f403002a   mov      w20, w0             ; <== the caller's error code is DESTROYED
  0x02c80:  80010034   cbz      w0, #0x2cb0         ; 0 -> no log
  0x02c90:  42680c91   add      x2, x2, #0x31a      ; -> 0x131a "dma_unalloc failed with return value =%d"
  0x02ca0:  e5128052   mov      w5, #0x97           ; line 151
  0x02ca8:  e603142a   mov      w6, w20
  0x02cac:  8d020094   bl       #0x36e0             ; LOG (logcat only)
  0x02ccc:  d4040034   cbz      w20, #0x2d64        ; 0 -> SUCCESS branch (returns 0)
  0x02cf4:  e603142a   mov      w6, w20
  0x02cf8:  7a020094   bl       #0x36e0             ; "run_shared_memory_example input_ptr failed with error: %d"
  0x02d40:  e003142a   mov      w0, w20             ; return -> cmd_dispatch 0x2770 mov w21, w0
```

Wire consequence (unchanged from the prior report, re-confirmed here):

```asm
  0x021e0:  17010094   bl       #0x263c            ; cmd_dispatch(...)
  0x021e4:  c0060034   cbz      w0, #0x22bc         ; only ret == 0 reaches the reply builder
  0x021f0:  e603002a   mov      w6, w0
  0x02200:  42181f91   add      x2, x2, #0x7c6      ; -> 0x17c6 "cmd_dispatch failed, ret:%x"
  0x0220c:  35050094   bl       #0x36e0             ; log only, then back to recvfrom
  ...
  0x02350:  bl #0x3700                                ; __sendto_chk — reachable only via 0x22bc (ret == 0)
```

### E3 — the exact log-string sites (single-site proof)

| String (vaddr) | Referencing `add x2` site(s) | Log call |
| --- | --- | --- |
| `"dma_alloc failed with return value =%d"` `0x1624` | `0x2b40` (one site) | `0x2b58` |
| `"dma_unalloc failed with return value =%d"` `0x131a` | `0x2c90` (one site) | `0x2cac` |
| `"Loading the application failed with %d!"` `0x11f1` | `0x3164` (one site) | `0x3184` |
| `"Failed to create DMA buffer allocator!"` `0x15fb` | `0x2bcc` | `0x2be4` → `0x2be8 mov w20,#0x3005` |
| `"Error: DMA_BUF_IOCTL_SYNC start failed, ret = %d"` `0x18ce` | `0x3504` (in `dma_alloc`) **and** `0x3614` (in `dma_unalloc`) | `0x3514` / `0x3624`; `x4` = NULL at both |
| `"CreateDmabufHeapBufferAllocator() failed."` `0x1444` | `0x353c` (inside `dma_alloc`'s own NULL check) | `0x3548` |
| `"run_shared_memory_example input_ptr failed with error: %d"` `0x1926` | `0x2cdc` | `0x2cf8` |

### E4 — the `0x30xx` sites relevant to Q2 (contrast)

```asm
  0x02b0c:  51030094   bl       #0x3850            ; CreateDmabufHeapBufferAllocator()
  0x02b10:  800500b4   cbz      x0, #0x2bc0
  0x02be8:  b4008652   mov      w20, #0x3005       ; <== survives to the caller
  0x02bec:  31000014   b        #0x2cb0            ;     ... because it BYPASSES 0x2c78
  0x02b88:  94018652   mov      w20, #0x300c       ; TA-not-loaded, -> 0x2d30 (before dma_alloc)
  0x02bb8:  b4018652   mov      w20, #0x300d       ; appObj NULL,     -> 0x2d30
  0x02d2c:  d4018652   mov      w20, #0x300e       ; response len 0,  -> 0x2d30
```

---

## Corrections to earlier reports

Corrections to `reports/static_re_gate_codes_and_rescue_2026-09-13.md` (SR) and
`reports/GateServiced_Phase1_RE_Report.md` (P1):

1. **SR §"What remains unknown" item 5 is now closed** ("`dma_unalloc`'s exact value space was not
   traced to ground truth"): it is **`{0, -1}`**, sourced solely from `DmabufHeapCpuSyncEnd`, per
   `0x3608`/`0x3664` plus the AOSP implementation quoted in Q2.2. It is **not** an `-errno`
   channel, so a failing `dma_unalloc` will print `-1`, never `-22`/`-9`/etc.
2. **SR §A.2 / P1 §6, `0x3005` emphasis**: the prior statement "a genuine `DmabufHeapAlloc`/
   `dma_alloc` failure does **not** produce `0x3005`" is correct, but the reason `0x3005`
   nonetheless reaches the caller is structural and should be stated: `0x2be8`'s branch target
   `0x2bec b #0x2cb0` **routes around `0x2c78`**, i.e. `0x3005` is the only DMA-family code that
   escapes the `dma_unalloc` overwrite. Conversely `0x300C`/`0x300D`/`0x300E` escape because they
   are returned from `0x2d30`, before `dma_alloc` is ever called.
3. **SR §A.2 last table row (`0x300A` "unconditional catch-all")** — confirmed and sharpened:
   predecessor closure of the containing block is exactly `{0x30f8, 0x3130}`, so "catch-all" should
   read "the *only* return-producing block on the failure path, entered unconditionally, with the
   value materialised as a fresh immediate at `0x3178`/`0x3180`". The prior report's "two
   instructions, one logical site" model is right; the added fact is that **`0x3178` is
   `mov w6, #imm`, which excludes the "log the variable" reading of the source** (Q1.4 item 2).
4. **P1 §1 function map / SR symbol table**: `dma_unalloc`'s **only** caller is `0x2c78`; there is
   **no PLT stub** for it (it is file-local, absent from `.dynsym`). Any earlier text implying
   `dma_unalloc` is an imported/external symbol, or is called from `run_unload_ta`, is wrong —
   `run_unload_ta` (`0x3354`–`0x346c`) contains no call to it (`0x3354`–`0x346c` only performs the
   appController/appObj `blr` teardown, structurally the same shape as `0x3194`–`0x3240`).
5. **SR §A.2 row for `0x3004`** is fully consistent with this pass; no change. The "`-1` file size
   → `calloc(2^64-1)`" chain is not re-examined here.
6. ~~**New (not in either report), affects P1 §4.2's DMA layout description**: the heap-name argument
   handed to `DmabufHeapAlloc` is **not** a rodata string …~~ → ❌ **RETRACTED 2026-09-13 15:09,
   falsified by device measurement + re-disassembly; see
   `reports/gate_dma_alloc_heapname_probe_2026-09-13.txt`.** The claim rested on following the
   *caller's* argument into `x1` without reading `dma_alloc`'s own prologue. Reading it:
   `0x3488 mov x21,x1` saves the caller's value, then `0x348c`/`0x3494` **overwrites `x1` with
   rodata `0xfd2`** (`"qcom,qseecom-ta"`) and `0x3498 mov x2,x21` moves the caller's value into
   **`x2` = `len`** → `0x34a8 bl DmabufHeapAlloc(allocator, "qcom,qseecom-ta", len, 0, 0)`.
   So the effective signature is **`dma_alloc(allocator, len, &fd_out, &map_out)`** — a local
   wrapper that supplies the heap name itself, exactly like the public sibling `DmaMemPool.cpp`.
   The public `BufferAllocatorWrapper.h` argument order is therefore **satisfied, not violated**
   (the earlier reading was the error). The call-site value `x20` at `0x2b20` is `output_len`
   (`0x2a98 mov x20,x4`), which the live log confirms: `output_len = 20480` / `1024` matches the
   buffer that was actually allocated and synced. The `pthread_create`→`handle_client`→
   `cmd_dispatch` trail (`0x2510`→`0x2548`→`0x21e0`→`0x2764`) is **real but carries no heap name**.
   Live corroboration: two `EXEC_CMD` runs (14:42 and 15:09) **both succeeded**
   (`run_shared_memory_example succeeded`) with **no** `"dma_alloc failed…"` line in logcat —
   i.e. `dma_alloc` does **not** fail on this path and there is no defect here.
7. ~~**"Concrete, instruction-provable instance of the success-after-failure case"** (Q2.3): the fd
   out-slot `[x29-0x1c]` is indeterminate on the `dma_alloc`-failure path …~~ → ❌ **RETRACTED**,
   same source. The slot is explicitly initialised before the allocation:
   `0x2b00 mov w8,#-1` / `0x2b04 stur w8,[x29,#-0x1c]`, and `dma_alloc` writes the real fd only on
   success (`0x34f0 str w23,[x19]`, `x19 == &[x29-0x1c]`). On the `dma_alloc`-failure exits the slot
   therefore still holds `-1`, `dma_unalloc`'s `ldr w1,[x2]` (`0x35e8`) loads `-1`,
   `ioctl(-1, DMA_BUF_IOCTL_SYNC, …)` cannot return `0`, and the caller takes the error branch. The
   indeterminate-fd laundering path **does not exist**.
   **The laundering itself is still real, with a different (and reachable) trigger**: after a
   *successful* `dma_alloc`, a later failure — `0x2c34 cbz w0,#0x2d98` … `0x2e20 b #0x2c64`, or the
   `TZCom_getFdObject` failure at `0x2c38` — falls into `0x2c64`→`0x2c78` with a valid fd, so
   `dma_unalloc` returns `0`, `0x2ccc cbz w20,#0x2d64` takes the success branch, `run_exec_cmd_ta`
   returns `0`, and **no error line is printed at all**. Still a reporting defect only: the client
   sees zero bytes on the non-zero path and a (possibly stale) buffer on the zero path.

---

## Confidence

| Claim | Label | Confidence | Basis |
| --- | --- | --- | --- |
| Predecessor closure of the `0x3178`/`0x3180` block is exactly `{0x30f8, 0x3130}` | FACT | **Very High** | full linear-leader CFG + backward closure; cross-checked twice with two independent walkers (`cfg.py`, `lin.py`) |
| `0x3178` and `0x3180` execute consecutively on every path reaching either | FACT | **Very High** | same basic block, straight-line, no branch between |
| `w20 == 0x300A` at the return read `0x3254` on every `0x30f8`-rooted failure path | FACT | **Very High** | exhaustive writer scan over the reachable closure (5 writers, one read) |
| `0x3001`/`0x3003`/`0x3004` are live only as the `0x3124` log argument and dead for the return | FACT | **Very High** | single read of `w20` before `0x3180` is `0x3120 mov w7, w20` |
| `0x300A` is a source-level constant (not a compiler collapse / not `goto fail` mis-merge / not uninitialised) | FACT | **High** | `mov w6,#0x300a` is an immediate materialisation; a variable log would emit `mov w6, w20` as it does at 5 other sites |
| The constant is a generic "load failed" vendor status, written in the shared cleanup tail | INFERENCE | **High (≈95 %)** | tail structure + `%d`-with-constant log + the public sibling `TaLoader.cpp` passing the **raw** code where this binary passes `0x300a`; no source for the sample itself |
| No public upstream source for the sample/`0x30xx` enum | FACT (negative) | **Very High** | two independent hunts; Sourcegraph/GitHub/CodeLinaro searches **with a passing control** (`DmabufHeapCpuSyncStart`=12) and a 768-file tarball grep of the public `mink` repo (zero `0x3xxx` literals) |
| The public `mink` sources are the correct scaffolding for GateServiced's callees (`TZCom_*`, `DmabufHeap*`, `gAppController`, `IAppLoader_loadFromBuffer`, `ISMCI<X>_sharedMemoryExample`) | FACT | **Very High** | quoted declarations + `sampleCA/src/main.c` + `ISMCIExampleApp.idl`; naming scheme matches with `X = Gate` |
| `dma_unalloc` value space is exactly `{0, -1}` | FACT | **Very High** | `0x3608`/`0x3664` register trace + AOSP `BufferAllocatorWrapper.cpp` / `BufferAllocator.cpp` quoted |
| `dma_unalloc` has one call site and no PLT stub | FACT | **Very High** | full-`.text` `bl`-target scan; `nm -D` shows it is not imported |
| Client receives **0 bytes** on any non-zero path (including a real `dma_unalloc` failure) | FACT | **Very High** | `0x21e4 cbz` / `0x2350 bl __sendto_chk`; matches the recorded live probe |
| A `dma_unalloc` ret of 0 after a `dma_alloc` failure reports **success** to the caller | FACT | **High** | `0x2c80 cbz` + `0x2ccc cbz w20,#0x2d64`; the only guard is `dma_unalloc`'s own result |
| That success case actually occurs on-device | FACT (mechanism reachable in the CFG) / ~~Medium~~ | **High** | `0x2c34`→`0x2d98`→`0x2e20 b #0x2c64` and `0x2c38`→`0x2c64` both reach `0x2c78` with a valid fd after a successful `dma_alloc`; the earlier "indeterminate fd" trigger is **retracted** — `0x2b00`/`0x2b04` initialise the slot to `-1` (`gate_dma_alloc_heapname_probe_2026-09-13.txt`) |
| ~~`dma_alloc`'s heap name is `0x5790` (a stack pointer)~~ → **heap name is the internal rodata `"qcom,qseecom-ta"` (`0xfd2`); the caller's `x1` is `len`** | ~~FACT/INFERENCE~~ **FACT** | **Very High** | `0x3488 mov x21,x1` → `0x3494 add x1,x1,#0xfd2` → `0x3498 mov x2,x21` → `0x34a8 bl DmabufHeapAlloc`; call site `0x2b20 mov x1,x20` with `x20 = output_len` (`0x2a98`); live `output_len = 20480`/`1024` runs succeed with no `dma_alloc failed` line |

### Cheapest experiments that would falsify the remaining unknowns

1. **Q1**: send `LOAD_TA` with (a) a missing path, (b) an existing 0-byte file, (c) an existing
   non-TA file, and diff the `load_app`-tagged lines. Predictions: (a) `get_file_size` → `load_app[49]`
   malloc-fail → `load_app[69] failed: 12292`; (b) `load_app[69] failed: 12289`; both then
   `run_load_ta[200] … 12298` and `handle_client[59] ret:300a`. If (b) shows 12289 at line 69 but
   the caller still sees `300a`, Q1.2 is confirmed end-to-end on the wire.
2. **Q2**: one `EXEC_CMD` with any payload; watch logcat for
   `dma_alloc failed with return value =%d`, then `dma_unalloc failed with return value =%d`
   (or its absence), then `run_shared_memory_example input_ptr failed with error: %d`, then
   `cmd_dispatch failed, ret:%x`. The `ret:` value should equal the `dma_unalloc` value (or `0`
   with no other error line, which would prove the success-after-failure path).
   → ✅ **RUN 2026-09-13 15:09** (`reports/gate_dma_alloc_heapname_probe_2026-09-13.txt`):
   `LOAD_TA` + `EXEC_CMD 0x00010002` at `output_len` 1024 and 20480 both returned success
   (`run_shared_memory_example succeeded`, `Send: 25.05.06.01`) with **zero** `dma_alloc` /
   `dma_unalloc` / `DMA_BUF_IOCTL_SYNC` failure lines → `dma_alloc` **does not** fail, and the
   success-after-failure path was **not** taken (no error line to launder).
