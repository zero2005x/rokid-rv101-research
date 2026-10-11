# Static RE: GateServiced error codes + rescued-binary classification

**Date**: 2026-09-13
**Scope**: (A) instruction-level resolution of the `load_app 12292` / `run_load_ta 12298` codes in
`/vendor/bin/GateServiced`; (B) classification of the 12 files in `binaries/device_rescue/`.
**Method**: static only — capstone 5.0 (aarch64) + pyelftools on the pulled binary; no device execution.

## Artifact provenance

| Item | Value |
| --- | --- |
| Device | `<DEVICE_SERIAL>` (Rokid RV101) |
| Source path | `/vendor/bin/GateServiced` (20,304 B, mode 750 root:root) |
| Exfil route | root daemon `/data/local/tmp/root_daemon.sock` → `cp` to `/data/local/tmp/GateServiced.root` (`uid=0(root) context=u:r:kernel:s0`) → `adb pull` |
| Local copy | `build-cache/GateServiced` |
| **sha256** | **`ecf72a60303aed8ecf7a21f99136572a1949862f7aeb1ee9ffbecbfb09e9fe75`** |
| Origin verification | `sha256sum /vendor/bin/GateServiced` **==** `sha256sum build-cache/GateServiced` (identical) |
| ELF | `ELF64`, little-endian, `AArch64`, `Type: DYN` (PIE), entry `0x2000`, `BIND_NOW`/`FLAGS_1: NOW` |
| `.text` | file off `0x2000`, vaddr `0x2000`, size `0x167C` (vaddr == file offset throughout `.text`) |
| Symbols | `.gnu_debugdata` (XZ, section [25] @ file `0x44C3`, size `0x408`) decompresses to a 4,360 B mini-ELF with a full `.symtab` — 14 `FUNC` + 3 `OBJECT` symbols |

Recovered symbol table (authoritative function map):

```
0x2000 _start(8)   0x2008 _start_main(64)  0x2048 get_milliseconds(120) 0x20c0 handle_client(816)
0x23f0 handle_sigint(80)  0x2440 main(508)  0x263c cmd_dispatch(460)  0x2808 get_file_size(332)
0x2954 read_file(284) 0x2a70 run_exec_cmd_ta(988)  0x2e4c run_load_ta(1288)
0x3354 run_unload_ta(280)  0x346c dma_alloc(356)  0x35d0 dma_unalloc(172)
```

Corrected PLT map (see *Corrections* §2 — the earlier report's PLT table is shifted by +0x20):

```
0x36a0 __libc_init      0x36b0 gettimeofday   0x36c0 __stack_chk_fail  0x36d0 memset
0x36e0 __android_log_print  0x36f0 recvfrom   0x3700 __sendto_chk     0x3710 close      0x3720 exit
0x37e0 fopen            0x37f0 fseek          0x3800 __errno          0x3810 strerror
0x3820 ftell            0x3830 fclose         0x3840 fread_unlocked
0x3850 CreateDmabufHeapBufferAllocator  0x3860 memcpy  0x3870 dup
0x3880 TZCom_getFdObject  0x3890 TZCom_getClientEnvObject  0x38a0 calloc  0x38b0 free
0x38c0 DmabufHeapAlloc   0x38d0 mmap   0x38e0 DmabufHeapCpuSyncStart  0x38f0 munmap
0x3900 FreeDmabufHeapBufferAllocator  0x3910 DmabufHeapCpuSyncEnd
```

---

## A. GateServiced error codes

### A.1 Every site that materialises a `0x30xx` code

Raw `MOVZ wN, #imm16` scan over the whole `.text` (hw=0) — **exactly 11 sites**, no `MOVK`/`MOVN` needed,
so each code is a single instruction with no ambiguity:

| # | Address | Instruction | Carried in | Function (symbol) |
| --- | --- | --- | --- | --- |
| 1 | `0x26FC` | `mov w21, #0x3002` | return `w21` | `cmd_dispatch` |
| 2 | `0x2B88` | `mov w20, #0x300C` | return `w20` | `run_exec_cmd_ta` |
| 3 | `0x2BB8` | `mov w20, #0x300D` | return `w20` | `run_exec_cmd_ta` |
| 4 | `0x2BE8` | `mov w20, #0x3005` | return `w20` | `run_exec_cmd_ta` |
| 5 | `0x2D2C` | `mov w20, #0x300E` | return `w20` | `run_exec_cmd_ta` |
| 6 | `0x2F48` | `mov w20, #0x300B` | return `w20` | `run_load_ta` |
| 7 | `0x30B4` | `mov w20, #0x3001` | return `w20` | `run_load_ta` |
| 8 | `0x30E4` | `mov w20, #0x3004` | return `w20` | `run_load_ta` |
| 9 | `0x30F4` | `mov w20, #0x3003` | return `w20` | `run_load_ta` |
| 10 | `0x3178` | `mov w6, #0x300A` | **log argument only** | `run_load_ta` |
| 11 | `0x3180` | `mov w20, #0x300A` | **return `w20`** | `run_load_ta` |

Code hex ↔ decimal: `0x3001`=12289, `0x3002`=12290, `0x3003`=12291, `0x3004`=12292, `0x3005`=12293,
`0x300A`=12298, `0x300B`=12299, `0x300C`=12300, `0x300D`=12301, `0x300E`=12302.

Note there are **two** instructions for `0x300A` and they are adjacent (`0x3178`, `0x3180`): one loads the
value into the *log* argument register `w6`, the next loads it into the *return* register `w20`. That
adjacency is the whole story of the bug.

### A.2 What each code actually means (instruction-level triggers)

| Code | Site | Reached from | Real trigger | Log tag / line in binary |
| --- | --- | --- | --- | --- |
| `0x3001` | `0x30B4` | `0x2FCC cbz w0, #0x30b4` | `get_file_size()` returned **exactly 0** → zero-length (empty) file | `load_app`:69 (shared tail) |
| `0x3002` | `0x26FC` | `0x26CC cmp x21,#0x80` / `0x26D0 b.lo #0x2778` | LOAD_TA payload length `>= 0x80` (≥128 B) | `cmd_dispatch`:28 |
| `0x3003` | `0x30F4` | `0x2FFC tbnz w0,#0x1f,#0x30ec` → `0x30EC bl free` | `read_file()` returned **negative** (fopen failed inside `read_file`, or `fread_unlocked != size`) | `load_app`:69 |
| `0x3004` | `0x30E4` | `0x2FE4 cbz x0,#0x30bc` | **`calloc(size,1)` returned NULL** (`0x2FE0 bl #0x38a0`) | `load_app`:49 → then `load_app`:69 |
| `0x3005` | `0x2BE8` | `0x2B10 cbz x0,#0x2bc0` | **`CreateDmabufHeapBufferAllocator()` returned NULL** (`0x2B0C bl #0x3850`) | `run_shared_memory_example`:102 |
| `0x300A` | `0x3180` | falls through `0x3158` | **unconditional catch-all**; overwrites `w20` | `run_load_ta`:200 |
| `0x300B` | `0x2F48` | `0x2EBC cbz x0,#0x2f20` | `appPath == NULL` | `run_load_ta`:180 |
| `0x300C` | `0x2B88` | `0x2AE8 ldrb w8,[0x53d8]` / `0x2AF0 tbz w8,#0,#0x2b60` | TA-loaded flag @`0x53D8` bit 0 == 0 | `run_exec_cmd_ta`:234 |
| `0x300D` | `0x2BB8` | `0x2AF8 ldr x8,[0x5410]` / `0x2AFC cbz x8,#0x2b90` | appObj @`0x5410` == NULL | `run_exec_cmd_ta`:239 |
| `0x300E` | `0x2D2C` | `0x2CFC ldr x8,[x19]` / `0x2D00 cbnz x8,#0x2d30` | TA response length == 0 | `run_exec_cmd_ta`:260 |

**Notable:** a genuine `DmabufHeapAlloc` / `dma_alloc` failure does **not** produce `0x3005`.
`0x2B28 bl #0x346c` (`dma_alloc`) → `0x2B2C cbz w0,#0x2bf0`; on failure it logs
`"dma_alloc failed with return value =%d"` and then propagates the **`dma_unalloc` return value**
(`0x2C78 bl #0x35d0` → `0x2C7C mov w20, w0`), not `0x3005`.

### A.3 THE ROOT CAUSE: why a missing file logs `0x3004` and returns `0x300A`

The decisive instruction sequence. `w20` is the return register for `run_load_ta`; `w6` is the
`__android_log_print` first variadic argument.

```asm
; ---- get_file_size() returns -1 (NOT 0) when the file does not exist -------------
0x02828:  bl   #0x37e0              ; fopen(path, "r")
0x0282c:  cbz  x0, #0x2894          ; NULL -> missing file
0x02894:  bl   #0x3800              ; __errno
0x028a0:  bl   #0x3810              ; strerror(errno)
0x028b8:  add  x2, x2, #0x876       ; -> 0x1876 "Failed to open file %s: %s (%d)"
0x028dc:  mov  w21, #-1             ; <== RETURN VALUE IS -1, not 0
0x02900:  mov  w0, w21              ; return -1

; ---- run_load_ta: the -1 defeats the 0x3001 check and becomes a giant calloc -------
0x02fc8:  bl   #0x2808              ; get_file_size(appPath)  -> w0 = -1
0x02fcc:  cbz  w0, #0x30b4          ; w0 == -1  => NOT taken; 0x3001 is skipped
0x02fd0:  sxtw x23, w0              ; x23 = 0xFFFFFFFFFFFFFFFF
0x02fd4:  mov  w1, #1
0x02fd8:  mov  x0, x23
0x02fe0:  bl   #0x38a0              ; calloc(0xFFFFFFFFFFFFFFFF, 1) -> NULL
0x02fe4:  cbz  x0, #0x30bc          ; taken
0x030c8:  add  x2, x2, #0x51d       ; -> 0x151d "Malloc failed while allocating memory to buffer"
0x030d0:  add  x4, x4, #0x979       ; -> 0x1979 "load_app"   <-- log tag
0x030d8:  mov  w5, #0x31            ; source line 49
0x030e0:  bl   #0x36e0              ; __android_log_print(...)
0x030e4:  mov  w20, #0x3004         ; <== w20 = 12292
0x030e8:  b    #0x30f8              ; join the shared failure tail

; ---- shared failure tail: logs 12292 under tag "load_app" -------------------------
0x03104:  add  x2, x2, #0x4df       ; -> 0x14df "Load app %s failed: %d"
0x0310c:  add  x4, x4, #0x979       ; -> 0x1979 "load_app"
0x03114:  mov  w5, #0x45            ; source line 69
0x0311c:  mov  x6, x21              ; %s = appPath
0x03120:  mov  w7, w20              ; %d = 12292   <-- THE "load_app 12292" LINE
0x03124:  bl   #0x36e0

; ---- the catch-all: 0x300A is HARDCODED and DESTROYS w20 -------------------------
0x0315c:  add  x3, x3, #0x10a0      ; (0x10a0 "run_load_ta")
0x03164:  add  x2, x2, #0x1da       ; -> 0x11da "Loading the application failed with %d!"
0x0316c:  add  x4, x4, #0xa0        ; -> 0x10a0 "run_load_ta"  <-- log tag
0x03174:  mov  w5, #0xc8            ; source line 200
0x03178:  mov  w6, #0x300a          ; <== %d argument is the CONSTANT 12298
0x03180:  mov  w20, #0x300a         ; <== return value OVERWRITTEN with 12298
0x03184:  bl   #0x36e0              ; "run_load_ta 12298"
0x03190:  stp  xzr, xzr, [x8]       ; clear appController @0x5400
0x03244:  ... 0x03254 mov w0, w20   ; return 12298
```

**Chain for a missing file**, in order:

1. `cmd_dispatch` `0x2798 bl #0x2e4c` → `run_load_ta(path)`.
2. `run_load_ta` `0x2FC8` → `get_file_size()` → `fopen` fails → **returns `-1`**.
3. `0x2FCC cbz w0,#0x30b4` tests for **0**, so `-1` takes the *wrong* branch — **`0x3001` is unreachable for a missing file**.
4. `sxtw x23, w0` sign-extends `-1` to `0xFFFFFFFFFFFFFFFF`; `calloc(2^64-1, 1)` overflows its size check and returns NULL.
5. NULL → `0x30BC` → `"Malloc failed…"` + tag `load_app` + line 49 → `w20 = 0x3004`.
6. `0x30F8` shared tail → `"Load app <path> failed: 12292"` + tag `load_app` + line 69. **← the "load_app 12292" observed in logcat.**
7. Fall through `0x3128`; apploader object was obtained at `0x2F50`, so `0x312C cbnz x8,#0x3158` skips the `appController is NULL!` line (that line *does* fire for the `/system/bin/sh` case).
8. `0x3178`/`0x3180`: `"Loading the application failed with 12298!"` + tag `run_load_ta` + line 200, and **`w20` is overwritten with `0x300A`**.
9. Returns `0x300A`.

So the two observed log lines are the *same* failure logged twice with two different stale tags, and the
value the caller finally sees (`0x300A`) is a **hardcoded constant that carries no information about the
actual fault**. `0x3004` is the only line that reflects reality, and it is a *calloc* failure caused by the
`-1` file size — it is a **second-order symptom of the missing file, not an allocation shortage**.

### A.4 "0x300A at the wire" is a misnomer — nothing is sent

`handle_client` (`0x21E0`–`0x2280`):

```asm
0x021e0:  bl   #0x263c              ; cmd_dispatch(...)
0x021e4:  cbz  w0, #0x22bc          ; ret == 0 -> build+send reply
0x021f0:  mov  w6, w0
0x02200:  add  x2, x2, #0x7c6       ; -> 0x17c6 "cmd_dispatch failed, ret:%x"
0x0220c:  bl   #0x36e0              ; log only
0x02220:  ...                       ; "wait for client data, client_fd:%d" (line 0x27=39)
                                    ; loops straight back to recvfrom
```

`sendto`/`__sendto_chk` is only reachable on the `cbz w0,#0x22bc` **success** path (`0x2350 bl #0x3700`).
On **any** non-zero `cmd_dispatch` return the service logs and returns to `recvfrom` **without writing a
single byte**. This exactly matches the live probe: `response: (EMPTY)`, `bytes: size=0`,
`HELD-OPEN (ESTABLISHED)`. The string `ret:300a` in
`reports/gateserviced_probe_2026-09-13.txt` is the *logcat* rendering of
`[GATE CA][ERRO][handle_client][59]:"cmd_dispatch failed, ret:300a"` — **it is never transmitted.**

### A.5 `load_app` is an inlined callee, not a function in this binary

This is the key structural fact that the earlier analysis missed:

- `.gnu_debugdata` contains **no `load_app` symbol**. `run_load_ta` is `0x2E4C`–`0x3354` (1288 B) and that
  single range *contains* all three `"load_app"`-tagged log sites.
- The literal `"load_app"` is at rodata `0x1979` and is used as the `%s` function-name argument of
  `__android_log_print` at **exactly three** sites, all inside `run_load_ta`: `0x30C4` (line 49),
  `0x3100` (line 69), `0x3138` (line 71).
- The literal `"run_load_ta"` (rodata `0x10A0`) is used at **seven** sites in the *same* function:
  `0x2E90`, `0x2EF4`, `0x2F28`, `0x2F94`, `0x3088`, `0x3160`, `0x3288`.
- The embedded source line numbers are mutually consistent and non-overlapping
  (`load_app`: 49/69/71 vs `run_load_ta`: 175/180/186/193/200/207/211), so `load_app()` was a genuine
  source function that the compiler **inlined into its only caller**, keeping `__func__`-derived string
  literals while emitting no out-of-line symbol.

Consequence: `load_app` is **not** a separate code unit to hook, patch, or call. Any plan that treats
"the `load_app` path" as distinct from `run_load_ta` in the binary is operating on a fiction.

### A.6 Independent confirmation from the live probe

`reports/gateserviced_probe_2026-09-13.txt` (probe P5, `LOAD_TA /does/not/exist.mbn`) — the recorded
logcat matches the static trace **line-for-line**, including the source line numbers decoded from the
`mov w5, #imm` operands:

```
[GATE CA][ERRO][get_file_size][18]:"Failed to open file /does/not/exist.mbn: No such file or directory (2)"
[GATE CA][ERRO][load_app][49]:"Malloc failed while allocating memory to buffer"       <- 0x30D8 mov w5,#0x31
[GATE CA][ERRO][load_app][69]:"Load app /does/not/exist.mbn failed: 12292"            <- 0x3114 mov w5,#0x45
[GATE CA][ERRO][load_app][71]:"appController is NULL!"                                <- 0x314C mov w5,#0x47
[GATE CA][ERRO][run_load_ta][200]:"Loading the application failed with 12298!"        <- 0x3174 mov w5,#0xC8
[GATE CA][ERRO][handle_client][59]:"cmd_dispatch failed, ret:300a"
```

P6 (`/system/bin/sh`, exists, non-TA) is also fully explained:
`get_file_size` succeeds → `calloc` succeeds → `read_file` succeeds → the apploader `loadApp` vtable call
(`0x3028 blr x8`) returns `12` → `0x3034 mov w20, w0` → `0x304C cbnz w20,#0x30f8` →
`"Load app /system/bin/sh failed: 12"` (tag `load_app`, line 69) → appController @`0x5400` is NULL
(`0x3054 cbz x8,#0x3278`) so line 71 `"appController is NULL!"` fires → `0x3180` overwrites with `0x300A`
→ returns `0x300A`. Both probes converge on `0x300A` through **different** internal codes — which is
precisely why the wire/log code is useless as a discriminator.

---

## B. Rescued binaries

All 12 files in `binaries/device_rescue/`. Sizes/hashes computed locally; `not an ELF` verified by magic.
"bionic" = Android `libc.so`/`liblog.so`/`libc++.so`; vendor SoC libs named explicitly.

### B.1 File-type table

| File | Size | ELF type / arch | Dynamic? | Linked libs (DT_NEEDED) |
| --- | ---: | --- | --- | --- |
| `ashmem_blob_test` | 7,400 | ELF64 AArch64 **DYN** (PIE) | dynamic | `libdl.so`, `libc.so` |
| `dsp_audit_blind` | 1,226,928 | ELF64 AArch64 **DYN** (PIE) | dynamic | `liblog.so`, `libm.so`, `libdl.so`, `libc.so` |
| `dsp_hidl_base_diag` | 10,776 | ELF64 AArch64 **DYN** (PIE) | dynamic | `libdl.so`, `libm.so`, `libc.so` |
| `dsp_hidl_session_diag` | 7,872 | ELF64 AArch64 **DYN** (PIE) | dynamic | `libdl.so`, `liblog.so`, `libm.so`, `libc.so` |
| `dsp_poc_standalone` | 1,288,720 | ELF64 AArch64 **DYN** (PIE) | dynamic | `liblog.so`, `libm.so`, `libdl.so`, `libc.so` |
| `dsp_raw_v2` | 8,160 | ELF64 AArch64 **DYN** (PIE) | dynamic | `libdl.so`, `libc.so` |
| `fakepayload` | 4,096 | **not an ELF** — raw data, 100% zero bytes | n/a | n/a |
| `kaslr_leak` | 8,336 | ELF64 AArch64 **DYN** (PIE) | dynamic | `libdl.so`, `libc.so` |
| `min_payload` | 24 | **not an ELF** — raw data, `CrAU` OTA header stub | n/a | n/a |
| `test_0582` | 2,145,128 | ELF64 AArch64 **EXEC** | **STATIC** (no `.dynamic`, no DT_NEEDED) | — (bionic libc linked in) |
| `test_21479` | 16,608 | ELF64 AArch64 **DYN** (PIE) | dynamic | `libdl.so`, `libc.so` |
| `test_36971` | 10,016 | ELF64 AArch64 **DYN** (PIE) | dynamic | `libdl.so`, `libc.so` |

All 10 ELF probes are clang/LLD 18.0.4 Android NDK builds (`Android (13691557, based on r522817d) clang
version 18.0.4`, `Linker: LLD 18.0.4`) and each still carries its original **source filename** in
`.comment`/symbol strings — that is the single most useful provenance artifact:

| File | Source file baked into the binary |
| --- | --- |
| `ashmem_blob_test` | `ashmem_blob_test.c` |
| `dsp_audit_blind` | `dsp_audit_blind.cpp` |
| `dsp_hidl_base_diag` | `dsp_hidl_base_diag.cpp` |
| `dsp_hidl_session_diag` | `dsp_hidl_session_diag.cpp` |
| `dsp_poc_standalone` | `dsp_poc_standalone.cpp` |
| `dsp_raw_v2` | `dsp_raw_binder_v2.c` |
| `kaslr_leak` | `bpf_sandbox_kaslr_leak.c` |
| `test_0582` | `cve_2024_0582_uaf_check.c` |
| `test_21479` | `cve_2025_21479_probe.c` |
| `test_36971` | `cve_2024_36971_test.c` |

### B.2 Per-file detail: sha256, notable strings, functional guess

**`ashmem_blob_test`** — `2e3551c5eaf37735e2a6c28dee55d82cfeee66cd3a9738b5a07f76662e75598a`
- Strings: `/dev/ashmem`, `ashmem_blob_test.c`, `ndk_version`
- Guess: **ashmem blob-allocation probe** exercising the `/dev/ashmem` driver path (GhostLock-era surface). Evidence: only distinctive device path in the file is `/dev/ashmem`; source name matches.

**`dsp_audit_blind`** — `353cb61f500f5f60b69578a57e50fd7c2b58c32e17acd77765fb537f8ab65711`
- Strings: `/dev/adsprpc-smd`, `[*] Rokid RV101 DSP Audit Tool (Blind Test)`, `[+] Successfully got DSP FD: `, `[+] Waking up DSP...`, `[-] Failed to open /dev/adsprpc-smd directly: `, `_Z8wake_dspv` (`wake_dsp()`), `dsp_audit_blind.cpp`
- Guess: **blind (no-response-parsing) audit probe of the Qualcomm adsprpc/fastrpc DSP channel** — opens `/dev/adsprpc-smd` directly and has a `wake_dsp()` helper; "Blind Test" means it fires requests without validating replies. 1.2 MB because libc++abi/libunwind (demangler) is statically embedded.

**`dsp_hidl_base_diag`** — `01d378ff8ec467f45b67681eabb1d3681315c2bed79db894aebe2def77606af2`
- Strings: `/system/lib64/libhidlbase.so`, `BpHwDspService::getNewDSPSession`, `vendor.qti.hardware.dsp@1.0::IDspService` (`_ZN6vendor3qti8hardware3dsp4V1_011IDspService19interfaceDescriptorE`), `Mode: read-only diagnostics; no session or ioctl calls.`, `dsp_hidl_base_diag.cpp`, `logcat -d -t 120 … grep -iE 'avc: denied|hwservicemanager|dspservice|adsprpcd|hwbinder'`
- Guess: **read-only HIDL reachability/diagnostic client** for `vendor.qti.hardware.dsp@1.0::IDspService` — resolves the service interface descriptor and greps logcat for SELinux denials; self-declares as *no session, no ioctl*.

**`dsp_hidl_session_diag`** — `7af1a13d052771de82bfa0baeb9ca176d27dc2ed774bff0bb15aedc6009a1c8e`
- Strings: `[*] Attempting getNewDSPSession(0)...`, `[!] getNewDSPSession symbol found - ready for full call`, `[-] getNewDSPSession symbol not resolved`, `BpHwDspService::getNewDSPSession`, `aplay -q /dev/zero 2>/dev/null &`, `cmd media volume 5 2>/dev/null`, `dsp_hidl_session_diag.cpp`
- Guess: **HIDL DSP *session* acquisition probe** — dlsym-resolves `getNewDSPSession` and attempts to open session 0, with shell side-effects (`aplay`, `cmd media volume 5`) used to keep the DSP powered/awake.

**`dsp_poc_standalone`** — `242f7b33b01dd087c1ed3c33360f1cee4e190259d38ea37ee302ed3d6f04ccc4`
- Strings: `/dev/adsprpc-smd`, **`/dev/adsprpc-smd-secure`**, `/dev/urandom`, `[*] Rokid RV101 Standalone Aggressive DSP POC`, `[+] Waking DSP...`, `tinymix 'DSP Power' 1 2>/dev/null || true`, `cmd media volume 5 2>/dev/null`, `aplay -q /dev/zero 2>/dev/null &`, `dsp_poc_standalone.cpp`
- Guess: **self-contained aggressive DSP PoC** — drives both the normal and the **secure** adsprpc node, force-powers the DSP via `tinymix 'DSP Power' 1`, and feeds `/dev/urandom` as fuzz input. Uses `std::thread`/`std::mutex`/`std::random` (present in its statically-embedded libc++ TU list, absent from `dsp_audit_blind`), consistent with a concurrent/multi-threaded fuzzing harness.

**`dsp_raw_v2`** — `6c83605655541fa5e5089fe91454b65913508a152076adb9cf60c714e972ea7e`
- Strings: `/dev/hwbinder`, `[+] /dev/hwbinder opened successfully`, `[-] open /dev/hwbinder`, `[*] Sending transaction to %s (method=%u)...`, `[+] Transaction completed. Reply size: %lu`, `dsp_raw_binder_v2.c`
- Guess: **raw hwbinder transaction sender** — bypasses the generated HIDL proxy stubs and writes HIDL transactions directly to `/dev/hwbinder`, printing method id and reply size. This is the "raw channel" approach the README names.

**`kaslr_leak`** — `64f330fc0143322761728ad5962a20eff94858e0a11653a9fa670b183e890e67`
- Strings: `[*] Compiling BPF OOB Leak payload inside Sandbox...`, `[+] BPF Program loaded successfully! Verifier bypassed.`, `[-] Verifier Rejected the Program:`, `[+] OOB Leak Success! Offset: 0x%lx, Leaked Value: 0x%016lx`, `] Found Kernel Pointer! KASLR Base might be deducible.`, `[-] RINGBUF map failed`, `[-] ARRAY map failed`, `bpf_sandbox_kaslr_leak.c`
- Guess: **eBPF verifier-bypass OOB read used to leak a kernel pointer and derive the KASLR base** — loads a malicious BPF program from inside a sandbox, reads out of bounds via ringbuf/array maps, and scans the leaked value for a kernel pointer. **Note:** this early version is *BPF-based*, not `printk_formats`-based; it uses `socketpair`/`setsockopt` (unprivileged-BPF enablement) and links only `libdl`/`libc`.

**`test_0582`** — `dbf6372a5fddfbb82a1ed6ece00f61e19179a6bf4319b33c452f91c5e7c9a1c1`
- Strings: `CVE-2024-0582 UAF Verifier`, `[!!!] Device IS VULNERABLE to CVE-2024-0582 syscall sequence!`, `[Phase 1] Initializing io_uring...`, `io_uring_setup`, `[+] io_uring_setup success: fd=%d`, `[-] Failed to initialize io_uring`, `[-] io_uring_enter failed: %s (errno=%d)`, `/dev/urandom`
- Guess: **CVE-2024-0582 (io_uring `io_uring`/`IORING_OP_FIXED_FILE` UAF) vulnerability oracle** — pure syscall-sequence detector. **Statically linked** (2.1 MB, `EXEC`, bionic libc compiled in, source `cve_2024_0582_uaf_check.c`), so it has no runtime library dependencies and can run on any aarch64 Android.

**`test_21479`** — `32fce439cbdaaa8fcdad3c121754264780ee4a132b49225463ed02994986164c`
- Strings: `CVE-2025-21479 GPU Micronode Authorization Bypass Probe`, `/dev/kgsl-3d0`, `[+] Opened /dev/kgsl-3d0 (fd=%d)`, `[-] Failed to open /dev/kgsl-3d0: %s`, `kgsl_fd`, `Iteration %d: %s`, `dmesg | grep -iE 'kgsl|adreno|gpu|fault'`, `cve_2025_21479_probe.c`
- Guess: **KGSL (Adreno GPU) micronode authorization-bypass probe for CVE-2025-21479** — opens `/dev/kgsl-3d0` and iterates GPU ioctl attempts then checks dmesg for KGSL faults.

**`test_36971`** — `d5de35f6a9fa1eda95eabf15c641fd1b28c7bd6d202c2e13e424c9bc2bb04054`
- Strings: `CVE ID: CVE-2024-36971`, ` < 2024-08-05 (CVE-2024-36971`, `getprop ro.build.version.security_patch 2>/dev/null`, `parse_kernel_version`, `cve_2024_36971_test.c`
- Guess: **CVE-2024-36971 (netfilter `nf_tables`/`nft_set` UAF) applicability test** — pairs a kernel-version parse (`parse_kernel_version`) with a security-patch-level check (`ro.build.version.security_patch`) against the 2024-08-05 fix date rather than blindly firing the exploit.

### B.3 Duplicates?

**No two files are byte-identical.** Full-sha256 grouping over all 12 yields **zero** duplicate hashes;
all 12 sizes are distinct except by coincidence of content (they are not).

Near-identical candidates, checked explicitly:

| Pair | Verdict | Evidence |
| --- | --- | --- |
| `dsp_audit_blind` (1,226,928) vs `dsp_poc_standalone` (1,288,720) | **Near-identical twins, NOT duplicates** | 4,261 of ~4,300 unique `strings ≥10` are shared (the entire statically-embedded libc++abi/libunwind/demangler block). They differ in the application TU: `dsp_audit_blind.cpp` vs `dsp_poc_standalone.cpp`. `dsp_poc_standalone` has ~269 extra strings including `condition_variable`/`mutex`/`thread`/`future`/`random` TUs → it is the *superset* build (adds threading + the `/dev/adsprpc-smd-secure` node and `tinymix`/`aplay` power-up commands). First-4096-byte hashes differ. |
| `test_21479` (16,608) vs `test_36971` (10,016) | **Not duplicates** | Different CVE targets (KGSL vs netfilter), different sizes, different source filenames, disjoint distinguishing strings. Both merely share the same NDK toolchain banner + `crtbegin.c`. |
| `dsp_hidl_base_diag` (10,776) vs `dsp_hidl_session_diag` (7,872) | **Not duplicates** | Same HIDL service (`vendor.qti.hardware.dsp@1.0::IDspService`) but different roles: base = read-only interface-descriptor diagnostics; session = `getNewDSPSession(0)` call. Different lib sets (`base` lacks `liblog`). |

The ~1.2 MB sizes of the two DSP files are **not** static linking — both are `DYN` with 4 DT_NEEDED; the
bulk is the statically-embedded libc++abi/libunwind (C++ exception + demangler) runtime pulled in by the
C++ TUs. This matters for re-running them: they still need the device's `liblog/libm/libdl/libc`.

### B.4 `fakepayload` and `min_payload` — neither is an executable

**`fakepayload`** — 4,096 B, sha256 `ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7`.

First 64 bytes:

```
00000000: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000010: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000020: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000030: 0000 0000 0000 0000 0000 0000 0000 0000  ................
```

It is **4,096 zero bytes — nothing else**. Distinct byte values: **1** (`0x00`, count 4096). The sha256 is
the canonical digest of a 4096-zero-byte buffer (verified by recomputation in Python). It is **not an ELF**
(no `\x7fELF`), **not an archive** (no magic), and **not even a text stub** — it is an all-zero page.
Most likely a **scratch/spacer file**: e.g. a `dd`-created or `ftruncate`/`fallocate`-created file used to
reserve exactly one page, or the residue of a test that wrote a payload and then truncated it. Its name
plus one-page size suggests it was a placeholder supplied where a real payload was expected. It carries
**zero information** — it should be treated as noise, not evidence.

**`min_payload`** — 24 B, sha256 `cafabf901a1d79fdf7db5da4a43436218d28eecd72822b410f9f7b639cf69f63`.

All 24 bytes:

```
00000000: 4372 4155 0200 0000 0000 0000 0000 0000  CrAU............
00000010: 0000 0000 0000 0000                      ........
```

`43 72 41 55` = ASCII **`CrAU`** — the magic of an Android A/B **OTA `payload.bin`** (the
`update_engine` delta-payload container). Critically, **24 bytes is exactly the size of the
`payload.bin` header** and nothing more:

| Offset | Size | Field | Value here |
| --- | --- | --- | --- |
| `0x00` | 4 | magic | `CrAU` |
| `0x04` | 8 | version | `02 00 00 00 00 00 00 00` (= 2 read little-endian) |
| `0x0C` | 8 | manifest size | all zero |
| `0x14` | 4 | metadata-signature size | all zero |

So it is a **header-only, zero-body `payload.bin` stub**: valid magic + version 2, manifest size 0,
signature size 0, and **no manifest and no data blocks at all**. Two observations mark it as
hand-crafted rather than extracted from a real OTA: (a) the version field is stored **little-endian**,
whereas the real `update_engine` payload format reads it big-endian (`00 00 00 00 00 00 00 02`); and
(b) the file terminates precisely at the header boundary. This is exactly the artifact you would write to
exercise the `update_engine` header-parsing gate documented in
`reports/SESSION_16_UPDATE_ENGINE_PAYLOAD_GATES.md` (`ParseHeaders` @ `0xcc550`, `METADATA_SIZE` gate)
with the smallest possible well-formed input. **It is not an executable and not an archive.**

Neither file is referenced anywhere in the repo (only their own inventory rows) — confirmed by a
recursive grep; `CrAU`/`DeltaMagic` appears nowhere else in the tree. `min_payload` is therefore the one
genuinely new piece of evidence here: it ties these two stray files to the **OTA / update_engine** attack
surface rather than to any of the kernel-CVE probes, and its "no manifest, no data" shape makes it a
*parser gate test vector*.

### B.5 Probe → CVE / subsystem mapping

| File | Subsystem | CVE | Evidence (decisive strings) | Confidence |
| --- | --- | --- | --- | --- |
| `test_0582` | **io_uring** | CVE-2024-0582 | `CVE-2024-0582 UAF Verifier`, `io_uring_setup`, `io_uring_enter failed`, `[Phase 1] Initializing io_uring...` | **Certain** (self-labelled) |
| `test_21479` | **KGSL / Adreno GPU** | CVE-2025-21479 | `CVE-2025-21479 GPU Micronode Authorization Bypass Probe`, `/dev/kgsl-3d0`, `kgsl_fd`, `dmesg \| grep -iE 'kgsl\|adreno\|gpu\|fault'` | **Certain** (self-labelled) |
| `test_36971` | **netfilter** | CVE-2024-36971 | `CVE ID: CVE-2024-36971`, `< 2024-08-05 (CVE-2024-36971`, `parse_kernel_version`, `getprop ro.build.version.security_patch` | **Certain** (self-labelled) |
| `dsp_poc_standalone` | **DSP / adsprpc (fastrpc)** | — | `/dev/adsprpc-smd`, `/dev/adsprpc-smd-secure`, `Rokid RV101 Standalone Aggressive DSP POC`, `wake_dsp` | **Certain** |
| `dsp_audit_blind` | **DSP / adsprpc (fastrpc)** | — | `/dev/adsprpc-smd`, `Rokid RV101 DSP Audit Tool (Blind Test)`, `wake_dsp` | **Certain** |
| `dsp_hidl_base_diag` | **DSP HIDL** (`vendor.qti.hardware.dsp@1.0::IDspService`) | — | `BpHwDspService::getNewDSPSession`, interface-descriptor symbol, `/system/lib64/libhidlbase.so` | **Certain** |
| `dsp_hidl_session_diag` | **DSP HIDL** (session acquisition) | — | `[*] Attempting getNewDSPSession(0)...`, `BpHwDspService::getNewDSPSession` | **Certain** |
| `dsp_raw_v2` | **DSP via raw hwbinder** | — | `/dev/hwbinder`, `[*] Sending transaction to %s (method=%u)...` | **Certain** |
| `kaslr_leak` | **KASLR leak — via eBPF verifier bypass** (not printk_formats) | — | `BPF Program loaded successfully! Verifier bypassed.`, `OOB Leak Success!`, `Found Kernel Pointer! KASLR Base might be deducible.` | **Certain** |
| `ashmem_blob_test` | **ashmem** (`/dev/ashmem`, GhostLock-related) | — (GhostLock = CVE-2026-43499) | `/dev/ashmem` is the only distinctive device path present | **High** for ashmem; **not** evidenced for a specific CVE in this binary |

Subsystem tally: KGSL ×1, io_uring ×1, netfilter ×1, DSP/adsprpc ×5 (2 direct adsprpc + 2 HIDL + 1 raw
hwbinder), ashmem ×1, KASLR ×1. The DSP family dominates — more than half the rescued probes target
Qualcomm's DSP channel, which is the least-documented surface in this set.

Two provenance caveats carried into the mapping: `ashmem_blob_test` links **only** `libdl`/`libc` and its
name suggests a blob-allocation *test* rather than an exploit, so its link to GhostLock is by naming and
device path only; and none of the 10 probes contains an embedded CVE string except `test_0582`,
`test_21479` and `test_36971`, so all other mappings rest on device paths and self-describing banners
rather than self-labelling. `fakepayload` and `min_payload` map to **no** kernel CVE — `min_payload`
belongs to the **OTA/update_engine** surface.

---

## Corrections to earlier reports

Target: `reports/GateServiced_Phase1_RE_Report.md` (§6 error table, §7 import table), cross-checked
against `scripts/analyze_gate*.py`, plus
`reports/gateserviced_probe_2026-09-13.txt` and `binaries/device_rescue/README.md`.

### 1. §6 error table — semantic errors

| Code | Earlier claim | Binary truth |
| --- | --- | --- |
| `0x3001` | `GENERAL_ERROR`, "File size = 0 (**file not found** or empty)" | **Wrong for missing files.** `get_file_size` returns **`-1`** on `fopen` failure (`0x28DC mov w21,#-1`), not 0. `0x3001` fires only when the path **exists and is exactly 0 bytes** (`0x2FCC cbz w0,#0x30b4`). A missing file never reaches `0x3001` — it reaches `0x3004`. |
| `0x3003` | "read_file failed (**mmap error path**)" | Address `0x30F4` is right, **cause is wrong**. Reached from `0x2FFC tbnz w0,#0x1f,#0x30ec` — i.e. `read_file()` returned **negative** (its own `fopen` failure, or a short `fread_unlocked` read). **There is no mmap anywhere on this path.** |
| `0x3004` | "ALLOC_FAILED — **DmabufHeapAlloc** returned NULL" | Function body is right, **API is wrong**. It is **`calloc`** (`0x2FE0 bl #0x38a0` → `0x2FE4 cbz x0,#0x30bc`), not `DmabufHeapAlloc`. `DmabufHeapAlloc` is only ever called from `dma_alloc`, used by `run_exec_cmd_ta`, never by `run_load_ta`. The logged tag on this path is `load_app`, not `run_load_ta`. |
| `0x3005` | "DMA_ALLOC_FAILED — **dma_alloc failed**" | Address `0x2BE8` right, **cause is wrong**. It is **`CreateDmabufHeapBufferAllocator()` returning NULL** (`0x2B0C bl #0x3850` → `0x2B10 cbz x0,#0x2bc0`), logged as `"Failed to create DMA buffer allocator!"` with tag `run_shared_memory_example` line 102. A real `dma_alloc`/`DmabufHeapAlloc` failure logs `"dma_alloc failed with return value =%d"` and returns the **`dma_unalloc`** code — it does **not** produce `0x3005`. |
| `0x300A` | "UNKNOWN — Internal error (**2 locations**)" | **Wrong model.** There is one logical site, `0x3178`/`0x3180`, and it is an **unconditional hardcoded overwrite** at the tail of the shared cleanup block entered at `0x30F8` by *every* load failure (`0x3001`, `0x3003`, `0x3004`, `loadApp` error, `appController == NULL`). `0x3178` only feeds the log `%d`; `0x3180` clobbers the return register. It is not "2 locations" and carries **no** information about the underlying fault. |
| `0x3002` | "ta_name **> 128** bytes" | Off by one: `0x26CC cmp x21,#0x80` / `0x26D0 b.lo` rejects payload length **`>= 128`**, so 128 itself is rejected. Payload must be `<= 127` bytes plus the NUL written at `0x2794`. |
| `0x300B` | `INVALID_APP`, "ta_path is NULL" | ✅ **Correct** (`0x2EBC cbz x0,#0x2f20` → `0x2F48`). |
| `0x300C` | `TA_NOT_LOADED`, "Loaded flag = 0" | ✅ **Correct** (`0x2AE8 ldrb w8,[0x53d8]` → `0x2AF0 tbz w8,#0,#0x2b60` → `0x2B88`). |
| `0x300D` | `TA_DEAD`, "appObj is NULL" | ✅ **Correct** (`0x2AF8 ldr x8,[0x5410]` → `0x2AFC cbz x8,#0x2b90` → `0x2BB8`). |
| `0x300E` | `RESP_INVALID`, "Response data length = 0" | ✅ **Correct** (`0x2CFC ldr x8,[x19]` → `0x2D00 cbnz x8,#0x2d30` → `0x2D2C`). |

Every `Code`/`Location`/`Function` triple is right except `0x3004` (API), `0x3005` (cause) and `0x300A`
(model). Only the *names* (`GENERAL_ERROR`, `ALLOC_FAILED`, …) are unsupported — the binary contains no
such name strings; they are inferred labels and should be marked as such.

### 2. §7.2/§7.3 PLT addresses are systematically off by +0x20

The earlier table appears to have counted PLT0 as 16 bytes instead of 32
(`.plt` is `0x3680`–`0x3920`; PLT0 occupies `0x3680`–`0x369F`; the 40 stubs run `0x36A0`–`0x3910`).
Every file-I/O and stack-guard entry is shifted:

| Symbol | Earlier | Actual |
| --- | --- | --- |
| `fopen` | `0x37C0` | **`0x37E0`** |
| `fseek` | `0x37D0` | **`0x37F0`** |
| `ftell` | `0x3800` | **`0x3820`** |
| `fclose` | `0x3810` | **`0x3830`** |
| `fread_unlocked` | `0x3820` | **`0x3840`** |
| `__stack_chk_fail` | `0x36A0` | **`0x36C0`** |

The earlier row semantics are also mixed up: `0x3800` is **`__errno`** and `0x3810` is **`strerror`**
(used at `0x2844`/`0x2850` to build the `%s (%d)` errno messages) — `ftell`/`fclose` are a further
0x20 bytes along. Note §2.2's *call-site* references (`recvfrom()` at `0x2280`, `sendto()` at `0x2350`)
are **correct** — only the PLT *address table* is shifted.

### 3. `scripts/analyze_gate*.py` — the origin of the `load_app` misconception, plus a code-name table that contradicts the report

**(a) `analyze_gate2.py` lines 262–270 mislabel the disassembly regions.** This is the single most
consequential script error, because it is where the phantom "`load_app`" unit comes from:

```python
regions = [
    ("handle_client (cmd_dispatch area)", 0x2200, 0x23f0),
    ("main (socket setup)",               0x2440, 0x26c0),
    ("run_load_ta / cmd processing",      0x2640, 0x2810),   # <- actually cmd_dispatch (0x263c-0x2808)
    ("read_file / get_file_size",         0x2808, 0x2a70),   # <- correct
    ("load_app / TrustZone interaction",  0x2a70, 0x2d90),   # <- actually run_exec_cmd_ta (0x2a70-0x2e4c)
    ("QSEE command dispatch",             0x2d90, 0x2f80),
    ("magic number / validation",         0x2dc0, 0x2e60),
]
```

`0x2640–0x2810` is `cmd_dispatch`, not `run_load_ta` (which is `0x2E4C–0x3354`); and
`0x2A70–0x2D90` is `run_exec_cmd_ta`, not `load_app`. The script therefore *printed `run_exec_cmd_ta`'s
body under the heading "load_app"*, which is the most plausible origin of the downstream belief that
`load_app` is a distinct function handling "TrustZone interaction". By contrast `analyze_gate3.py` lines
24–35 carry the **correct** map (identical to `.gnu_debugdata`), so the two scripts disagree with each
other.

**(b) `analyze_gate3.py` lines 394–405 — the `known` code-name table is wrong in four ways:**

```python
known = {
    0x3001: "GENERAL_ERROR",
    0x3002: "INVALID_PARAMETER",
    0x3003: "NOT_SUPPORTED",
    0x3005: "ALLOC_FAILED",        # report says 0x3004 = ALLOC_FAILED, 0x3005 = DMA_ALLOC_FAILED
    0x300b: "INVALID_APP",
    0x300c: "TA_NOT_LOADED",
    0x300d: "TA_DEAD",
    0x300e: "RESP_INVALID",
    0x300f: "CMD_NOT_SUPPORTED",   # 0x300f DOES NOT EXIST in the binary
}.get(val, "UNKNOWN")
```

1. **`0x3004` is absent** → the script prints it as `UNKNOWN`, even though it is a real and distinct code
   and is exactly the `12292` seen in logcat.
2. **`0x3005` is labelled `ALLOC_FAILED`**, which **directly contradicts** `GateServiced_Phase1_RE_Report.md`
   §6 (0x3004 = `ALLOC_FAILED`, 0x3005 = `DMA_ALLOC_FAILED`). The script and the report are mutually
   inconsistent; neither is right (see §1 above).
3. **`0x300a` is absent** → the **most frequently returned** code (`12298`) prints as `UNKNOWN`.
4. **`0x300f` is fabricated.** A raw `MOVZ/MOVK/MOVN`, hw=0 scan with `0x3000 <= imm16 <= 0x30ff` over the
   entire `.text` finds **exactly 11 sites** carrying `0x3001`–`0x3005` and `0x300a`–`0x300e`. There is
   **no `0x300f`** anywhere in the binary, and no `CMD_NOT_SUPPORTED` string either. Note also that the
   script's own scan loop (lines 378–392) is correct and *would* have found `0x3004` — only the name
   lookup is wrong.

**(c) The scripts' PLT arithmetic is right and the report's is wrong.** `analyze_gate.py` (lines 279,
531), `analyze_gate2.py` (line 258) and `analyze_gate3.py` (line 21) **all** use
`plt_base + 32 + i * 16` — i.e. they correctly model the 32-byte PLT0 header. I verified this directly:
PLT0 at `0x3680` is `stp x16,x30,[sp,#-0x10]!` / `adrp x16,#0x4000` / `ldr x17,[x16,#0x278]` /
`add x16,x16,#0x278` / `br x17` followed by **three `nop`s at `0x3694`/`0x3698`/`0x369C`**, with the first
real stub at `0x36A0`. **So §7.2/§7.3 of the Phase-1 report contradicts the project's own
scripts** — the scripts are right and the report's `+0x20`-shifted table is the error, which pins the
mistake to report transcription rather than to the analysis tooling.

### 4. Other §-level corrections

- **`load_app` is not a function.** §1's function map is otherwise exactly right (all 14 symbol sizes
  match `.gnu_debugdata`), but there is no `load_app` entry — because there is no `load_app` symbol. It is
  an **inlined callee** inside `run_load_ta` (see §A.5). §1 also names `0x2048` `timing_helper`; the
  symbol is **`get_milliseconds`**.
- **§4.3 quoted string is inexact.** The binary's literal at rodata `0x105D` is
  `"received magicNumTA failed,magicNumTA= %x"` — the report renders it with a space after the comma and
  no space before `%x`. The `cmp w6, #0xaa` @ `0x2E00` and the failure log @ `0x2E30` are **correct**.
- **§4.1/§4.2 DMA layout is correct** and verified independently: request magic `0x55` (`0x2C04`),
  cmd_id u32 @ `+1` (`0x2C0C`), input_len u64 @ `+5` (`0x2C14`), payload @ `+0xD` (`0x2C20 memcpy`);
  response magic `0xAA` (`0x2E00`), length u64 @ `+1` (`0x2E08 ldur x2,[x8,#1]`), data @ `+9`
  (`0x2E18`). §4.4's `"qcom,qseecom-ta"` @ rodata `0xFD2`, referenced `0x3494` — **correct**.
- **§5 global-state layout is correct** (`0x3C0` timestamp @ `0x21DC`, `0x3D0` DMA ptr, `0x3D8` loaded
  flag, `0x3E0` clientenv, `0x400` app-controller vtable, `0x410` app object). It omits one live slot:
  **`0x3F0`** is a second TZCom object used at `0x2FDC`/`0x31FC`.
- **§7.3 "PIE: ASLR applies" is misleading** — the binary is `ET_DYN` **`BIND_NOW`/full RELRO**, so GOT
  overwrite is not available; that is a *defence*, and it strengthens rather than weakens the report's
  "no command injection" conclusion.
- **§8 row 2 ("File Existence Oracle 🟢 HIGH — differentiate error codes: `0x3001` vs QSEE rejection") is
  WRONG and must be withdrawn.** `handle_client` (`0x21E4 cbz w0,#0x22bc`) returns to `recvfrom` on any
  non-zero `cmd_dispatch` result **without ever calling `sendto`**. The client receives **0 bytes** and the
  logged code is invisible off-box. There is **no** error-code oracle over the socket. The same
  correction voids the probe report's shorthand "`ret:300a` (at the wire)" — `0x300a` exists only in
  logcat.
- **Probe report §"analysis" item 3** ("why LOAD_TA fails at 12292/12298 is unconfirmed") is now
  **resolved** — see §A.3. The cause is the `-1` file-size → `calloc(2^64-1)` → NULL → `0x3004` →
  hardcoded `0x300A`, not a QSEE/TA rejection. The report's observation that "the playbook's error table
  assigns `0x3001` to a missing file but a missing file yields `0x300a`" was the correct symptom; the
  mechanism is the two-instruction `0x3178`/`0x3180` overwrite, not an alternative error path.
- **`binaries/device_rescue/README.md` corrections.**
  (a) The claim *"它們是**執行檔**"* ("they are executables") is **false for 2 of 12**: `fakepayload` is
  4,096 zero bytes and `min_payload` is a 24-byte `CrAU` OTA header stub — neither is an ELF.
  (b) `kaslr_leak` is described as *"現行洩漏走 printk_formats"* ("current leak uses printk_formats") —
  accurate as a contrast, but the binary itself must be labelled a **BPF verifier-bypass OOB leak**
  (`BPF Program loaded successfully! Verifier bypassed.`), not a printk-format-string leak.
  (c) The inventory's "12 executables" phrasing in `reports/missing_binaries_inventory_2026-09-13.md`
  §4.1/§4.3 inherits the same error.
- **`reports/FACTS.md` §5 item 5** ("the 12 rescued binaries are executable but not decompiled,
  especially `fakepayload`/`min_payload` whose origin is unknown") can now be narrowed: 10 are NDK
  aarch64 ELF probes with their **source filenames still embedded**, and the remaining 2 are
  **non-executable payload stubs**, one of which (`min_payload`) is identified as an OTA
  `payload.bin` header test vector.

---

## What remains unknown

1. **Why the `0x300A` constant is there.** Static analysis proves *what* happens (hardcoded overwrite at
   `0x3180`) but not *why* the source has it. The most likely story is a shared "load failed" tail that
   originally returned a fixed legacy code, later refactored to log a real value (`%d` at `0x3114`) while
   the tail kept its constant — but this is inference, not evidence. No source is available to confirm.
2. **The exact `load_app()` boundary.** It is inlined, so the split between `load_app` body and
   `run_load_ta` body inside `0x2E4C`–`0x3354` is only *approximated* by the embedded line numbers
   (49/69/71 vs 175–211). The true first/last instruction of the inlined region cannot be recovered from
   the binary alone.
3. **Whether `get_file_size` returning `-1` is intentional.** `-1` is a defensible error sentinel; the
   bug is the caller's failure to test for it (`cbz` instead of `cmp ... , #0` + sign check). Whether the
   original source intended `0` and the `-1` is a regression is unknown.
4. **A genuine empty-file test was not performed.** `0x3001` is predicted (not observed) to fire for an
   existing zero-byte path. Only P5 (missing) and P6 (non-TA) were exercised live. This is a one-probe gap
   and is cheap to close.
5. **`run_exec_cmd_ta`'s `0x3005`/`dma_alloc`-failure return values.** A `dma_alloc` failure propagates the
   `dma_unalloc` (`0x35D0`) return code, whose exact value space was not traced to ground truth.
6. **`fakepayload`'s purpose is unrecoverable.** It is 4,096 zero bytes with no embedded metadata and no
   reference anywhere in the repo; no further static analysis can add information. Its name is the only
   clue and naming is not evidence.
7. **`min_payload`'s authorship and endianness.** The `CrAU` magic and exact 24-byte header length identify
   the format, but the **little-endian** version field is non-standard for `payload.bin`, so it was
   hand-written by *some* local tool that is not present in the repo. Which script wrote it, and whether it
   was ever fed to `update_engine`, is unknown.
8. **Provenance/age of the 10 probes.** All are NDK clang 18.0.4 builds (2026-05…08 per the README) and
   none is reproducible from this repo — the README states the sources are gone. Embedded source
   filenames give the original TU names but not the code.
9. **`device_rescue` sha256 baseline.** These hashes are now recorded; no independent (e.g. build-server or
   OTA) copy exists to cross-check that the pulled bytes are unmodified.
