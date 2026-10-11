# [ARCHIVE] NVD / OSV Record Digest — CVE-2026-43499

> English: [advisory_nvd_osv_record_20260824_EN.md](advisory_nvd_osv_record_20260824_EN.md)

> Archived: 2026-08-24 by RV101 research team.
> Sources:
> - NVD: https://nvd.nist.gov/vuln/detail/CVE-2026-43499 (Published 2026-05-21, last modified 2026-07-24, source CNA: kernel.org)
> - OSV: https://osv.dev/vulnerability/CVE-2026-43499 (Published 2026-05-21T12:17:49Z)
> - oss-security: http://www.openwall.com/lists/oss-security/2026/07/08/12
> - Red Hat: https://access.redhat.com/security/cve/cve-2026-43499

---

## Official description (verbatim, kernel.org)

> In the Linux kernel, the following vulnerability has been resolved:
>
> **rtmutex: Use waiter::task instead of current in remove_waiter()**
>
> remove_waiter() is used by the slowlock paths, but it is also used for
> proxy-lock rollback in rt_mutex_start_proxy_lock() when invoked from
> futex_requeue(). In the latter case waiter::task is not current, but
> remove_waiter() operates on current for the dequeue operation. That results
> in several problems:
>
> 1) the rbtree dequeue happens without waiter::task::pi_lock being held
> 2) the waiter task's pi_blocked_on state is not cleared, which leaves a
>    dangling pointer primed for UAF around.
> 3) rt_mutex_adjust_prio_chain() operates on the wrong top priority waiter task
>
> Use waiter::task instead of current in all related operations in
> remove_waiter() to cure those problems.
>
> [ tglx: Fixup rt_mutex_adjust_prio_chain(), add a comment and amend the changelog ]

## Scoring

| Source | Score | Vector |
|---|---|---|
| kernel.org (CNA) | **7.8 HIGH** | `CVSS:3.1/AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H` |
| NVD assessment | not yet provided | — |

## Git facts (kernel stable tree)

| 項目 | Commit |
|---|---|
| **Introducing commit** | `8161239a8bcce9ad6b537c04a1fa3b5c68bae693`（NVD git range 起點；對應 2.6.39 年代） |
| **Mainline fix** | `3bfdc63936dd4773109b7b8c280c0f3b5ae7d349`（Linux 7.1） |
| Stable backports (fixed) | `f3fa3424bceb…`, `838ce5cb5d93…`, `d8cce4773c2b…`, `8a1fc8d698ac…`, `6d52dfcb2a5d…`, `3fb7394a8377…`, `88614876370a…` |

### Ecosystem affected → fixed ranges (OSV)

| Branch | Introduced | Fixed in |
|---|---|---|
| 5.10 LTS | 2.6.39 起（本支線） | **5.10.261** ← RV101 為 5.10.209 ⇒ 受影響且低於修補版 |
| 5.15 LTS | 5.11.0 | 5.15.212 |
| 6.1 LTS | 6.1 branch | 6.1.175 |
| 6.6 LTS | 6.6 branch | 6.6.140 |
| 6.12 LTS | 6.12 branch | 6.12.86 |
| 6.18 | 6.18 branch | 6.18.27 |
| 7.0 | 7.0 branch | 7.0.4 |
| mainline | — | 7.1+ |

## Downstream advisory coverage (OSV downstream list, 摘錄)

RHSA-2026:37728/38491/38492/39082/39083/39983/39984/40068/40082/40425/40760/41062/41063/41234/41235/41920、RLSA-2026:38491/38492、ALSA-2026:38491/38492、SUSE-SU-2026 多件、USN-8488~8669 多件、DEBIAN-CVE-2026-43499、MGASA-2026-0174/0177、openSUSE-SU-2026 等 —— 生態系影響面極廣。

## Reporter / attribution

- Reported to security@kernel.org by **Nebula Security**（自動化分析工具 **VEGA** 發現）
- Public write-up: Nebusec "IonStack part II: GhostLock, a stack-UAF that has existed in ALL Linux distributions for 15 years"（https://nebusec.ai/research/ionstack-part-2/）
- Fix credit: tglx（fixup adjust_prio_chain）

---

## RV101 對照結論（研究團隊註記，2026-08-24）

| 主張 | 證據狀態 |
|---|---|
| RV101 kernel 5.10.209-perf < stable fixed 5.10.261 | ✅ 本檔 OSV 官方區間 |
| 反組譯確認 remove_waiter 未含修正語意 | ✅ research/ghostlock_refs/remove_waiter.disasm.txt |
| 實機利用成功（shell→root） | ✅ run_w2_v960_143215.log 凍結證據 |
| CONFIG_FUTEX_PI 依賴成立於 Android kernel | ✅ futex PI 為 Android 12 必備 |
