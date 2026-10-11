# Session 15 Research Summary - Rokid AR Glasses RV101

## Date: 2026-08-14

## Target Device
- **Model**: Rokid AR Glasses RV101 (serial <DEVICE_SERIAL>)
- **Kernel**: 5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201
- **Android**: 12, SELinux Enforcing, shell uid=2000, CapEff=0

## Breakthrough: Runtime BPF Verification Unlocked

Root cause of all earlier `SO_ATTACH_BPF: Invalid argument` failures: the
kernel (`sock_setsockopt` in net/core/sock.c) requires `optlen == sizeof(u32)`
for SO_ATTACH_BPF. Passing `sizeof(long)` (8) returns EINVAL.

**Fix**: pass an `int bpf_fd = (int)pfd; setsockopt(fd, SOL_SOCKET, SO_ATTACH_BPF, &bpf_fd, sizeof(bpf_fd));`

This unlocks runtime execution of SOCKET_FILTER programs on the device. Attach
works on AF_UNIX DGRAM, AF_INET UDP and AF_INET TCP sockets. Trigger via UDP
loopback (127.0.0.1) or TCP localhost connection.

## CONFIRMED: CVE-2024-50164-class bug, runtime write into readonly map

### Verifier behavior (5.10.209)
- `check_mem_size_reg` equivalent in `check_func_arg` (verifier.c): for a
  **non-constant** size (`!tnum_is_const(reg->var_off)`) it sets `meta = NULL`
  (kernel/bpf/verifier.c, "For unprivileged variable accesses, disable raw mode").
- With `meta == NULL`, `check_helper_mem_access()` for `PTR_TO_MAP_VALUE`
  evaluates `meta && meta->raw_mode ? BPF_WRITE : BPF_READ` => **BPF_READ**,
  so the `BPF_F_RDONLY_PROG` "write into map forbidden" check in
  `check_map_access_type` is **bypassed**.
- The bounds check `check_map_access(..., reg->umax_value, ...)` is **independent
  of meta** and still enforced (verified: vs=4, len range [1,8] rejected with
  `invalid access to map value, value_size=4 off=0 size=8`).
- Stack targets still go through `check_stack_access_within_bounds` (meta-independent)
  => no stack OOB write.

### Runtime proof (`test_memuninit.c`)
Non-privileged SOCKET_FILTER loads, attaches to a UDP socket, and on packet
arrival calls `bpf_skb_load_bytes(skb, 0, &m1_value, len)` where:
- m1 = ARRAY map with `BPF_F_RDONLY_PROG` (0x80), value_size 8
- len = variable value loaded from map m2, converged to [1,8] via
  `if w5<1 exit; if w5>8 exit`

Result (repeatable across runs):
```
LOOKUP(before) value=0x1111111111111111
sendto -> 8
LOOKUP(after)  value=0x23fe1000d5d76eac   (skb/IP header bytes written into readonly map)
```
=> **The kernel-side write into a BPF_F_RDONLY_PROG map actually happens at runtime
from an unprivileged program.** This is a confirmed, reproducible, unprivileged
kernel write primitive (bounded, in-map).

## JIT folding test (`test_jitfold.c`)
- Program reads m1 (0x80 readonly) value, stores it into normal map m3.
- First trigger: m3 = 0x1111...; UPDATE m1 = 0xabcd...; second trigger: m3 = 0xabcd...
- => **No JIT/verifier constant folding** of readonly-map loads on this kernel
  (`R7_w=invP(id=0)`), the program reads real memory each run.
- => No cross-program verifier-vs-memory desync primitive (6.x-style rodata
  folding exploit does not apply on 5.10.209).
- Note: userspace UPDATE_ELEM on BPF_F_RDONLY_PROG maps succeeds (only prog-side
  writes are restricted by that flag); so the write-to-readonly-map has no
  confidentiality impact by itself.

## Conclusion for CVE-2024-50164 on this device
- **Confirmed present and unprivileged-reachable** (load + runtime execution).
- **Impact limited to**: bounded in-map write into a "prog-readonly" map,
  content = skb bytes (partially attacker-controlled). Range checks fully intact.
- **Not directly exploitable for LPE** on 5.10 (no verifier folding). Worth
  reporting as a kernel integrity defect (write into read-only data), and it is
  the CVE-2024-50164 root cause on a 5.10.x baseline (<= 5.10.230 affected).

## CVE-2024-50063 reviewed (tail-call)
- PoC requires two BPF LSM programs (file_alloc_security + audit_rule_known),
  bpf_tail_call and a PROG_ARRAY.
- LSM prog type + attach requires CAP_BPF/root on Android. **Not reachable from
  unprivileged shell.** Discarded.

## Tail-call unprivileged testing (test_tailcall.c)
- PROG_ARRAY + bpf_tail_call fully work unprivileged: P1 tail-calls P2
  (SOCKET_FILTER), P2 writes marker map. Confirmed end-to-end.
- 5.10 verifier tail_call checks are only: map type == PROG_ARRAY, and
  subprog/no-btf restrictions. index (arg3 = ARG_ANYTHING) is runtime-checked
  (index >= max_entries -> no jump); no verifier bypass surface.
- CVE-2024-50063 class (tail call between incompatible hooks/ctx) unreachable:
  requires multiple prog types (LSM), which unprivileged load rejects. The
  prog-array compatibility check (bpf_prog_array_compatible: owner_prog_type /
  jited / attach_btf_id) is trivially satisfied for the only loadable type.
- No unprivileged-reachable tail-call bug found. Noted as working primitive
  only.

## Verifier root-cause note (confirmed)
- kernel/bpf/verifier.c check_func_arg, non-const mem-size arg:
  `if (!tnum_is_const(reg->var_off)) meta = NULL;` (comment: disable raw mode
  for unprivileged variable accesses). Consequence: check_helper_mem_access()
  sees meta==NULL and for PTR_TO_MAP_VALUE chooses BPF_READ instead of
  BPF_WRITE, bypassing the BPF_F_RDONLY_PROG "write into map forbidden" check.
  This is the CVE-2024-50164 root cause, present on 5.10.209. Only observed
  impact is the bounded readonly-map write; no stack/range bypass (those checks
  are meta-independent).

## Files
- `source_code/test_memuninit.c` — confirmed 50164-class load + runtime write
  (offset=0 skb header content; offset=28 yields zeros — socket-filter skb data
  start is not the IP header as assumed).
- `source_code/test_jitfold.c` — JIT folding test (no folding).
- `source_code/test_attach.c` — minimal attach test (optlen fix validation).
- `source_code/test_tailcall.c` — unprivileged PROG_ARRAY tail call works.

## Next Steps
1. Unprivileged BPF surface (SOCKET_FILTER, PROG_ARRAY, tail_call, all common
   map types) is now fully mapped: only confirmed defect is CVE-2024-50164
   (bounded write into readonly map), which does not directly yield LPE on
   5.10. Recent 5.10 verifier bugs (CVE-2024-42313, CVE-2024-50063 etc.) are
   either out of scope for 5.10 or privileged-only. Known 2021-2023 verifier
   bugs are almost certainly backported in this 2025 build.
2. Recommend pivoting: userspace (am/run-as, framework components), or re-assess
   GPU/KGSL and other device surfaces. Optionally strengthen the 50164 finding
   (controlled-value write) for the report first.
