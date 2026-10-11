# Rokid RV101 — Coordinated Vulnerability Disclosure Package

**Purpose:** Evidence preparation for responsible disclosure to Rokid of a demonstrated
local privilege-escalation (LPE) chain on a personally owned Rokid Glasses RV101.

**Status:** DRAFT — not yet submitted to the vendor.

**Confidentiality:** This entire directory is **RESTRICTED** until coordinated public
disclosure. The `confidential/` subtree additionally contains exploit source, sensitive
offsets, and raw evidence with kernel addresses and must not leave the research team
except under explicit vendor coordination for reproduction.

**Primary vulnerability exercised:** [CVE-2026-43499](https://access.redhat.com/security/vulnerabilities/RHSB-2026-010)
("GhostLock") — Linux kernel rtmutex futex-PI stack use-after-free → local privilege
escalation. This is an **existing, upstream-assigned CVE**; this package documents its
**exploitability on the RV101**, not a new CVE.

**Secondary finding (privacy-LED trust boundary):** with the root gained by the chain, the
camera-in-use privacy LED control chain — sysfs `0640` (DAC-overridable), the `lights_ctrl`
Binder service (txn#8 sendEvent / #9 cancelEvent), and the `vendor.rkd.camera.session_open`
property driving `init`'s "camera opened/closed" action — can be suppressed or forged while
the camera subsystem stays active. Documented in
`reports/RV101_PRIVACY_LED_TRUST_BOUNDARY_AUDIT.md` (control paths A/B/C dynamically
validated 2026-08-24, `reports/led_validation/led_test.log`) and reported as **RS-8** in
`RV101_Vulnerability_Report.md`. This is a Rokid-specific hardening issue, **not** a new CVE.

## How to use this package

| Read order | File | Purpose |
|---|---|---|
| 1 | `RV101_Vulnerability_Report.md` | Main vendor-facing report (sensitive details deferred to the confidential appendix). |
| 2 | `CVE_Relationship_Assessment.md` | Existing-CVE vs. independently-fixable-vendor-issue analysis. |
| 3 | `11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md` | Concise first-contact email + follow-up templates. |
| 4 | `Evidence_Checklist.md` / `Evidence_Index.md` | What evidence exists, where it is, and what is still missing. |
| 5 | `Disclosure_Timeline.md` | Dated record of events (unknown dates marked "To be confirmed"). |
| 6 | `confidential/Confidential_Technical_Appendix.md` | Sensitive reproduction detail for vendor (restricted). |
| 7 | `Redaction_Checklist.md` | What must stay confidential / be redacted before any public release. |

## Evidence-status vocabulary (used throughout)

- **CONFIRMED** — directly demonstrated by a captured log/artifact.
- **STRONGLY SUPPORTED** — supported by multiple independent observations.
- **INFERRED** — technically plausible, not directly demonstrated.
- **UNKNOWN** — insufficient evidence.

## Key files and hashes

Authoritative hashes: `12_Hashes/SHA256SUMS.txt` (mirrored at `./SHA256SUMS.txt`).

> **Integrity note:** the live device log that produced the before/after evidence was
> still being appended when this package was assembled. The **frozen snapshot** in
> `confidential/raw_evidence/run_w2_v960_143215.log` (SHA-256 `1b1953ee07c5…`) is the
> authoritative copy referenced throughout. Re-freeze the original before final send.

Primary before/after evidence (single file, raw):
`confidential/raw_evidence/run_w2_v960_143215.log` — contains `getenforce before: Enforcing`,
`uid=2000` startup, then `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0`,
`Permissive`, full capability mask, and a root-owned `/data/root_proof`.

Redacted disclosure copy (addresses/exploit internals removed):
`05_Post_Exploit_State/post_exploit_state_REDACTED.txt`.

## Important scope boundaries (do not overclaim)

- UID 0 / full capabilities / SELinux Permissive: **CONFIRMED**.
- Persistent root across reboot: **NOT claimed** (UNKNOWN / not demonstrated).
- AVB / dm-verity / boot-chain bypass: **NOT claimed** (system/vendor remained read-only).
- "Works on every RV101 firmware": **NOT claimed** (tested on one device, one firmware).
- Bootloader unlocked: **observed environmental condition**, NOT a demonstrated
  precondition of CVE-2026-43499 (see Attack Preconditions in the report).
