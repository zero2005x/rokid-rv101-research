# Vulnerability-Detection Implementation Audit (CVE Description ↔ Repo Code Cross-Check)

> 中文版：[VULN_IMPLEMENTATION_AUDIT.md](VULN_IMPLEMENTATION_AUDIT.md)

> Goal: confirm whether each "detection/PoC" in the repo is **implemented according to the prerequisites and trigger chain in the authoritative CVE description**, and whether it can support the "PASS" conclusions in your table.
>
> Source of truth: the CVE Record **Description** from NVD / cve.org (prerequisites, typical trigger chain, affected version ranges).
>
> Note: this audit only checks "whether the code path matches the CVE description"; it is **not equivalent to whether the issue can actually be triggered on the device** (that requires execution output, dmesg, and version/patch evidence).

## Quick Conclusions (high-priority discrepancies)

1. **CVE-2024-0582 (io_uring)**
   - `cve_2024_0582_mmap_uaf.c`: the trigger chain matches the CVE description closely (PBUF_RING + mmap + unregister/free).
   - `cve_2024_0582_uaf_check.c`: it calls itself a verifier, but its main path goes through `PROVIDE_BUFFERS/REMOVE_BUFFERS`, which is **not equivalent to the PBUF_RING trigger point in the CVE description and needs a separately proven link**.

2. **TIPC (attack surface)**
   - If your table says "AF_TIPC is not enabled/not compiled", note that the repo also contains `tipc_probe.c` (capability probing) and `tipc_exploit.c` (explicitly targeting TIPC subsystem vulnerabilities).
   - This means the "conclusion" most likely rests on **actual execution results** (e.g. socket creation, /proc/config.gz, module load state); repo code alone cannot support "not enabled".

3. **Ashmem (integer overflow/size handling)**
   - `ashmem_overflow_exploit.c` is more of a "robustness/invalid-value testing" harness; the code alone does not yield "strictly validated, therefore PASS".
   - A PASS requires you to add: the errno / behavior observed when run on the device, plus version or commit/patch evidence for the corresponding kernel/driver.

---

## 1) io_uring — CVE-2024-0582

**Authoritative description core (NVD/cve.org summary)**

- Key trigger chain: register with `IORING_REGISTER_PBUF_RING` → `mmap` the corresponding region → free/unregister (or close/unregister) causing a UAF/memory issue.

**Repo cross-check**

- ✅ Closely matches:
  - `cve_2024_0582_mmap_uaf.c`
    - It specifically uses `IORING_REGISTER_PBUF_RING` and performs an `IORING_OFF_PBUF_RING` mmap.
    - It then performs unregister/close and similar operations, matching the "mmap still active + free/unregister" test direction.
- ⚠️ Possibly does not match (rename it or document its purpose):
  - `cve_2024_0582_uaf_check.c`
    - The main path uses `IORING_OP_PROVIDE_BUFFERS` / `IORING_OP_REMOVE_BUFFERS` together with heap spray/race.
    - This may be a UAF/race detection for a different path, but it is **not the PBUF_RING + mmap trigger chain described for CVE-2024-0582**.

**Audit conclusion**

- `cve_2024_0582_mmap_uaf.c`: can be regarded as "implemented per the description".
- `cve_2024_0582_uaf_check.c`: recommended to split it out in the report/table as "io_uring provide/remove buffers race" rather than treating it directly as a CVE-2024-0582 verifier.

---

## 2) BPF Verifier — CVE-2023-2163

**Authoritative description core (NVD/cve.org summary)**

- Type: a pruning/state-derivation error in the BPF verifier that makes an unsafe path be treated as safe, potentially leading to out-of-bounds/arbitrary read-write.

**Repo cross-check**

- ✅ Closely matches:
  - `cve_2023_2163_probe.c`
    - It combines multiple probes to test whether the verifier wrongly accepts suspicious bounds/branch states.
    - The output logic follows the typical probing pattern of "if the verifier accepts → judge it vulnerable".

**Audit conclusion**

- A high degree of implementation fidelity to the description; the credibility of PASS/FAIL mainly depends on:
  - whether the target kernel reports verifier reject/accept behavior (execution output).

---

## 3) Netfilter x_tables — CVE-2021-22555

**Authoritative description core (NVD/cve.org summary)**

- Type: heap out-of-bounds write in `net/netfilter/x_tables.c`.
- Common exploitation prerequisites: the ability to operate the iptables/setsockopt path; many scenarios go through a user namespace + capabilities (depending on the distro/configuration).

**Repo cross-check**

- ✅ Reasonable "reachability/prerequisite checks":
  - `cve_2021_22555_check.c`: checks `CONFIG_USER_NS` and netfilter-related config/conditions, matching the "judge exploitability first" approach.
  - `cve_2021_22555_test.c`: uses `setsockopt(IPT_SO_SET_REPLACE)` to observe `EPERM/ENOPROTOOPT/EINVAL`, which can be used to judge whether the interface is reachable.
- ⚠️ Reference exploit (not necessarily applicable to Android/ARM64):
  - `cve-2021-22555_exploit.c`: complete exploit source (the comments lean toward a specific kernel/distro); it does not mean it can be used directly on a Rokid device.

**Audit conclusion**

- The detection/prerequisite-confirmation part is consistent with the CVE description.
- If your table's PASS reason is "CAP/USER_NS is disabled, therefore not exploitable", you need to attach:
  - on-device evidence of the `CONFIG_USER_NS` state and the capabilities state (execution output).

---

## 4) Dirty Pipe — CVE-2022-0847

**Authoritative description core (NVD/cve.org summary)**

- Type: pipe buffer flags are not initialized correctly, allowing the page cache to be rewritten (usually making read-only files overwritable).

**Repo cross-check**

- ✅ Closely matches:
  - `dirtypipe_test.c`: the typical flow (prepare pipe → splice → write) attempts to overwrite a read-only file.

**Audit conclusion**

- A high degree of implementation fidelity to the description.
- PASS/FAIL depends on whether the file was actually modified on the device (and whether the kernel version is in the affected range).

---

## 5) Net/Route (dst cache / UDP) — CVE-2024-36971

**Authoritative description core (NVD/cve.org summary)**

- Type: an RCU cleanup/free-ordering problem in `__dst_negative_advice` causing a UAF; the description mentions that UDP sockets make the old bug reachable again.

**Repo cross-check**

- ✅ Partially matches (the direction is reasonable, but it is a highly uncertain race PoC):
  - `cve_2024_36971_v13.c` (**v9 and others were not retained; corrected 2026-09-13**): the overall direction of UDP + ICMP error + spray/race matches the description.
  - `cve_2024_36971_test.c`: a checking tool oriented toward version/patch predicates.

**Audit conclusion**

- The direction matches the description, but whether it is "already fixed" or "not affected" should usually rest on:
  - the device's actual kernel commit/Android patch level, plus corroboration from the observed results of running the PoC.

---

## 6) KGSL GPU — CVE-2024-38401

**Authoritative description core (NVD/cve.org summary)**

- Type: concurrent IOCTL calls to the Qualcomm KGSL driver cause memory corruption.

**Repo cross-check**

- ✅ Reasonably matches (centred on race + ioctl):
  - `kgsl_race.c`: concurrent ioctl testing.
  - `kgsl_bpf_spray*.c`: adds heap spray to improve observability/hit rate.
  - `CVE-2024-38401_Test_Report.md`: aggregates the test conclusions (not triggered).

**Audit conclusion**

- The "implemented per the description" direction is correct; but PASS/FAIL still mainly depends on whether the race can be triggered on the device, plus the driver version/patch.

---

## 7) Binder (multiple UAF/race generations)

**Authoritative description core**

- This column is usually not a single CVE but "the UAF/race attack surface commonly seen historically in the binder subsystem".

**Repo cross-check**

- ✅ Reasonable as "stress testing/race attempts":
  - `binder_probe.c`: capability probing.
  - `binder_uaf_v5.c` (**v2–v4 were not retained; corrected 2026-09-13**): multiple race trigger modes + spray.

**Audit conclusion**

- These programs are more like "exploratory PoCs/harnesses" and are not suitable for directly stating that "a specific CVE has been patched".
- If your table's PASS targets a specific binder CVE, it is recommended to:
  - state the CVE number explicitly and check whether the program really covers that CVE's trigger chain.

---

## 8) Ashmem

**Authoritative description core**

- If your table describes "overflow is infeasible because ashmem mmap/size is validated", that is a "behavior/mitigation conclusion".

**Repo cross-check**

- ⚠️ The current code alone cannot directly prove a PASS:
  - `ashmem_overflow_exploit.c`: it tends to run through a large number of edge cases and tells the user to inspect the results themselves (errno/dmesg).

**Audit conclusion**

- To support a PASS: you need to attach the output of running `ashmem_overflow_exploit.c` on the device, plus the corresponding driver behavior.

---

## 9) Adsprpc

**Authoritative description core**

- adsprpc security is usually related to the device node permissions, the SELinux policy, and the specific ioctl/driver bug.

**Repo cross-check**

- ⚠️ `adsprpc_test.c` is mainly an "open + ioctl reachability" probe.

**Audit conclusion**

- If the table's PASS reason is "not reachable because permissions/SELinux are controlled", it needs execution output/permission evidence; the code itself will not draw the conclusion for you.

---

## 10) TIPC

**Authoritative description core**

- TIPC's attack surface depends on: whether it is compiled in/loaded/allows AF_TIPC sockets, and whether the netlink control interface is reachable.

**Repo cross-check**

- ✅ Probing tool: `tipc_probe.c`
- ✅ Exploit framework targeting TIPC vulnerabilities: `tipc_exploit.c`

**Audit conclusion**

- If your table says "TIPC has no attack surface", you need direct corroboration from `tipc_probe`'s on-device output or the kernel config.

---

## Next Steps (the "traceable evidence" you are advised to fill in)

- For each PASS field, add at least 1 of:
  - the target device's `uname -a` / `uname -r` and Android security patch level.
  - the stdout (key lines) of the corresponding probe/test program, plus a dmesg summary where necessary.
- For items where "the code path does not match" (such as `cve_2024_0582_uaf_check.c`):
  - rename/split the column in the table so that different attack surfaces are not conflated into the verification of one CVE.
