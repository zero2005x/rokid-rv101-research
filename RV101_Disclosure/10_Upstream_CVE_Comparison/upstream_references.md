# Upstream CVE-2026-43499 References

Collected public sources for the "existing CVE vs. new CVE" comparison.

- Red Hat — [RHSB-2026-010 Locking Subsystem Privilege Escalation (CVE-2026-43499, CVE-2026-53166) — "GhostLock"](https://access.redhat.com/security/vulnerabilities/RHSB-2026-010)
- CloudLinux — [GhostLock (CVE-2026-43499) Local Root Exploit: Kernel Update](https://blog.cloudlinux.com/ghostlock-cve-2026-43499-local-root-exploit-kernel-update-for-cloudlinux/)
- AlmaLinux — [GhostLock (CVE-2026-43499) kernel privilege escalation: patch released](https://almalinux.org/sv/blog/2026-07-09-ghostlock/)
- Nebusec — [IonStack part II: GhostLock, a stack-UAF that has existed in ALL Linux distributions for 15 years](https://nebusec.ai/research/ionstack-part-2/)
- Huawei Cloud — [rtmutex UAF local privilege escalation / container escape (CVE-2026-43499)](https://support.huaweicloud.com/intl/en-us/bulletin-cce/cce_bulletin_0134.html)

## Fix / affected-range facts (from project notes)

- Fix commit (mainline): `3bfdc63936dd`.
- Affected range: Linux 2.6.39 through 7.1.
- RV101 kernel `5.10.209-perf` falls inside the range and lacks the fix
  (disassembly evidence: `research/ghostlock_refs/remove_waiter.disasm.txt`).

## Comparison result

| Question | Answer |
|---|---|
| Already assigned? | Yes |
| Same underlying bug? | Yes (rt-mutex `remove_waiter()` stack UAF) |
| Separate fix beyond backport required? | Not demonstrated |
| Independently fixable vendor weaknesses? | Candidates RS-1…RS-7 — require validation |
