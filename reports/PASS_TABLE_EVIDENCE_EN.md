# PASS table (traceable-evidence edition)

> Purpose: rewrite "status/conclusion" into a list of **verifiable, retainable, traceable** evidence.
>
> Design principles:
>
> - **Every row** must be reproducible as retainable stdout/dmesg/system state using the "corresponding tool (repo file)".
> - **Every row** defines a Minimum Evidence Set (MES), to avoid relying on subjective description alone.
>
> Source references (where the original conclusions are scattered):
>
> - `HANDOVER_SESSION_20260131.md`, the final section "Kernel Exploit Path Final Conclusion"
> - The Attack Vector table of `Medium_Rokid_Root_Article_EN.md`
> - The "file ↔ detection tool" index of `VULN_VERIFICATION_INDEX.md`
>
> ✅ **2026-08-21 update**: the MES (Minimum Evidence Set) defined by this table has actually been collected in full.
> Per-row results are in `reports/5.2_VERIFICATION_LIVE_ADB_2026.md` (adb on the real device) and
> `reports/5.2_VERIFICATION_2026.md` (firmware binary layer); all 10 rows of this table hold (the **15 rows are in `5.2_VERIFICATION_LIVE_ADB_2026.md` §1.1–1.15; count corrected 2026-09-13**).

---

## Common evidence fields (required for every row)

Paste the following outputs into each row's Evidence section (or save them as an attached file):

- Device version: `uname -a`, `uname -r`
- Android patch level: `getprop ro.build.version.security_patch`
- Kernel hardening/switches:
  - `cat /proc/cmdline`
  - `cat /proc/sys/kernel/kptr_restrict`
  - `cat /proc/sys/kernel/unprivileged_bpf_disabled`
  - `cat /proc/sys/kernel/dmesg_restrict`
- Capabilities/permissions: `id`, `cat /proc/self/status | grep Cap`
- SELinux: `getenforce`

> Suggested naming: `evidence/common_<YYYYMMDD>_<device>.txt`

---

## Traceable evidence table (row by row)

> Field description:
>
> - **Repo file**: your detection/PoC source file (and the corresponding binary if already compiled).
> - **Output fields to capture (MES)**: collect only these, and they will suffice to support or refute that row's conclusion.
> - **PASS definition**: turn "PASS" from a verbal reason into a condition that the MES can verify.

| Vector / CVE                   | Current conclusion (compiled from handoffs/summaries)                             | Repo file (main)                                                                                     | Output fields to capture (MES)                                                                                                                        | PASS definition (verifiable)                                                                                                                                         |
| ---------------------------- | ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| io_uring / CVE-2024-0582     | PBUF_RING does not exist (N/A); also, the provide/remove path needs separate assessment | `io_uring_probe.c`, `cve_2024_0582_mmap_uaf.c`, `cve_2024_0582_uaf_check.c`, `io_uring_uaf_verify*.c` | 1) errno returned by `REGISTER_PBUF_RING` (expected `EINVAL`) 2) ops/features list 3) whether `IORING_OP_REMOVE_BUFFERS` is `EINVAL`                         | If `REGISTER_PBUF_RING == EINVAL` and the program explicitly confirms "functionality unsupported" → **CVE-2024-0582 (PBUF_RING+mmap) does not hold**; provide/remove related tests must be split into their own row           |
| BPF Verifier / CVE-2023-2163 | Patched                                                  | `cve_2023_2163_probe.c` (bin: `cve_2023_2163_probe`)                                                 | 1) each probe's result (Patched/Vulnerable) 2) the key lines of the verifier reject/accept                                                                | If all probes show Patched (or the verifier rejects, preventing construction of an unsafe path) → **PASS=patched/not exploitable**                                                       |
| Netfilter / CVE-2021-22555   | Not exploitable (missing SysV IPC / User NS / insufficient privileges)            | `cve_2021_22555_check.c`, `cve_2021_22555_test.c`, (reference) `cve-2021-22555_exploit.c`                 | 1) `CONFIG_USER_NS`/`CONFIG_SYSVIPC` check results 2) `setsockopt(IPT_SO_SET_REPLACE)` errno (commonly `EPERM`) 3) `lsmod`/x_tables module state (if available) | If `CONFIG_USER_NS` is off or the required capabilities cannot be obtained and the `IPT_SO_SET_REPLACE` path is unreachable → **PASS=not exploitable (under the current privilege model)**                              |
| Dirty Pipe / CVE-2022-0847   | Patched                                                  | `dirtypipe_test.c` (bin: `dirtypipe_test`)                                                           | 1) test file path and hash (before/after) 2) test program stdout (whether the overwrite succeeded)                                                                           | If the test confirms "cannot overwrite a read-only file" (hash unchanged + program reports not vulnerable) → **PASS=patched**                                                                        |
| Route cache / CVE-2024-36971 | PoC failed to trigger; limited by the environment (loopback/dummy0)             | `cve_2024_36971_test.c`, `cve_2024_36971_v*.c`, `cve_2024_36971_bpf_spray.c`                          | 1) active NICs (`ip link`/`ip addr`) 2) PoC attempt count/strategy version 3) whether a crash/dmesg anomaly appears 4) spray success statistics                                   | If the environment evidence shows the conditions needed to "trigger obsolete" cannot be produced and multiple PoC versions fail to produce UAF indicators over a long run → **PASS=not triggerable in this environment (not equal to patched)**                              |
| KGSL / CVE-2024-38401        | Failed to trigger; suspected patched/affected by hardening                  | `kgsl_race.c`, `kgsl_bpf_spray*.c`, `CVE-2024-38401_Test_Report.md`                                   | 1) whether `/dev/kgsl-3d0` can be opened 2) ioctl race execution results 3) whether dmesg shows a GPU fault / kernel oops 4) the test version/round in the report                     | If multiple race strategies + spray all show no crash/anomaly and the driver/patch messages support patched → **PASS=not proven exploitable (must be explicitly marked as "not triggered" rather than "patched")**                             |
| Binder (UAF/race class)        | Extensive testing with no crash; DoS/txn errors present but no privilege escalation            | `binder_probe.c`, `binder_uaf_v5.c` (**v2–v4 not retained**), `binder_bpf_exploit.c`                                    | 1) `/dev/binder` reachability 2) attempts and results per version 3) dmesg binder error summary 4) whether a kernel oops appears                                             | If no UAF indicator appears even under heavy racing (no oops/no crash/no privilege change) → **PASS=not proven exploitable (and must note: not targeting a single CVE)**                                           |
| Ashmem (overflow/size)      | Needs on-device behavior as supporting evidence (the code itself is a harness)                | `ashmem_overflow_exploit.c`, `ashmem_probe.c`                                                         | 1) whether `/dev/ashmem` exists and its permissions 2) errno of the key test cases (e.g. a huge set_size value) 3) whether dmesg is anomalous                                           | If all dangerous size cases are rejected (with explicit errno) and there is no kernel oops → **PASS=no exploitable behavior observed (a case→errno mapping must be attached)**                                           |
| Adsprpc                      | Mainly reachability/permission surface (SELinux/node permissions)                 | `adsprpc_test.c`                                                                                      | 1) `/dev/adsprpc-smd` open result and errno 2) node permissions/SELinux context (`ls -lZ`) 3) ioctl return errno                                          | If the device node is unreachable or the ioctl is blocked by permissions/SELinux and the evidence is complete → **PASS=attack surface controlled (not that the driver has no bugs)**                                                            |
| TIPC                         | Must first clarify whether it is enabled/reachable                                   | `tipc_probe.c`, `tipc_exploit.c`                                                                      | 1) `socket(AF_TIPC, ...)` result/errno 2) whether tipc is loaded (/proc/modules) 3) whether the generic netlink family id lookup succeeds                              | If AF_TIPC directly gives `EAFNOSUPPORT`, or tipc is not loaded and cannot be auto-loaded → **PASS=attack surface does not exist**; if the socket can be created then PASS cannot say "not enabled" and must become "enabled but no exploitable point found" |

---

## Suggested evidence file naming per row (for delivery/audit convenience)

- io*uring: `evidence/io_uring_probe*<date>.txt`, `evidence/cve*2024_0582_mmap_uaf*<date>.txt`
- bpf: `evidence/cve_2023_2163_probe_<date>.txt`
- netfilter: `evidence/cve_2021_22555_check_<date>.txt`, `evidence/cve_2021_22555_test_<date>.txt`
- dirtypipe: `evidence/dirtypipe_test_<date>.txt` + `evidence/dirtypipe_targetfile_hash_<date>.txt`
- route: `evidence/cve_2024_36971_v9_<date>.txt` + `evidence/net_ifaces_<date>.txt`
- kgsl: `evidence/kgsl_race_<date>.txt` + `evidence/kgsl_dmesg_<date>.txt`
- binder: `evidence/binder_uaf_v5_<date>.txt` + `evidence/binder_dmesg_<date>.txt`
- ashmem: `evidence/ashmem_overflow_<date>.txt` + `evidence/ashmem_dmesg_<date>.txt`
- adsprpc: `evidence/adsprpc_test_<date>.txt` + `evidence/adsprpc_ls_lZ_<date>.txt`
- tipc: `evidence/tipc_probe_<date>.txt` + `evidence/tipc_modules_<date>.txt`

---

## Note: how to avoid inconsistency between the "PASS reason" and the "implementation path"

- **Same CVE name, different paths must be split into separate rows**: for example io_uring's PBUF_RING+mmap (the CVE-2024-0582 description) vs provide/remove buffers (another attack surface).
- **"Not triggered" ≠ "patched"**: for race/heap-spray class PoCs (route cache, KGSL, binder), without version/patch evidence you can only write "not triggered/not proven" for now.
- **"Attack surface controlled" must be accompanied by permission evidence**: for adsprpc and TIPC, at minimum node permissions/SELinux context or a socket errno is needed to support the conclusion.
