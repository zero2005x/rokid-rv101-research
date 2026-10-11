# Current Status Snapshot — 2026-08-22

> 中文版：[CURRENT_STATUS_20260822.md](CURRENT_STATUS_20260822.md)

> ## ⚠ 2026-09-13 Addendum (changes after this snapshot)
> 
> This file was the authoritative snapshot on 2026-08-22, but the following changes have
> since occurred; **for the current state, defer to `docs/deploy.md` and
> `reports/su_escalation_roi_assessment.md` §8**:
> 1. Engine selection changed from compile-time `USE_ENGINE` to **runtime auto-probing** (`AutoGhostlockEngine` + helper heartbeat file).
> 2. A privilege-escalation operations layer P0–P3 was added: root daemon keep-alive (automatic recovery within 9–18 seconds), `oom_score_adj=-1000`,
>    the App-side watchdog `RootDaemonKeeper`, and phone-side root status/metrics display.
> 3. The phone-side UI was fully redone (four tabs, collapsible settings, light/dark theme), and i18n expanded to 193 keys × 14 locales.
> 4. Ring and ghostlock messages are now sent to the phone as **structured codes** (no more Chinese sentences).
> 5. `adb over TCP` was measured to be **unsupported on this build** (`persist.adb.tcp.port` has no effect).


> This document is the authoritative "current status" snapshot and supersedes the outdated
> "root not yet acquired" descriptions in the historical handoff documents.
> The historical handoffs (`GHOSTLOCK_HANDOFF_V2~V13`, `FINAL_STATUS_20260202.md`, etc.) are
> retained as raw records of their respective points in time and are not rewritten retroactively.

## One-Sentence Conclusion

**Root achieved (2026-08-22)**: from an adb shell (uid=2000, SELinux Enforcing), CVE-2026-43499
(GhostLock) was used to obtain a root shell with `uid=0 gid=0`, SELinux Permissive, and full
capabilities. The focus has now shifted to preparing for **coordinated vulnerability disclosure**.

## Device / Firmware

| Item | Value |
|------|-----|
| Model | Rokid RV101 (`RG-glasses` / `glasses`, codename `neo`) |
| Serial number | `<DEVICE_SERIAL>` |
| Firmware / build | `1.22.009-20260710-150201` |
| Fingerprint | `Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys` |
| Android | 12 (SDK 32, user build) |
| Kernel | `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` |
| Security patch | `2024-07-05` |
| Bootloader | Unlocked (`orange` / `unlocked`) — **an environmental condition, not a prerequisite for privilege escalation (to be confirmed)** |

## Confirmed Results (evidence)

| Phase | Observation | Evidence |
|------|------|------|
| Before | `uid=2000(shell) … context=u:r:shell:s0`, `getenforce=Enforcing` | `assets/RV101_Validation/…/17_selinux_runtime.txt`, run log |
| After | `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` | `run_w2_v960_143215.log` (frozen: `RV101_Disclosure/confidential/raw_evidence/`) |
| After | `getenforce=Permissive`, `CapPrm/CapEff/CapBnd=000001ffffffffff` | same as above |
| Non-destructive demonstration | Created `/data/root_proof` (root:root), read `/data/adb` (0700 root) | same as above |
| Partition state | `system/system_ext/vendor` still `ro` (dm-verity not bypassed) | mount output |

> Primary evidence SHA-256 (frozen snapshot): `1b1953ee07c5f45222900a0fe4773ec27e263aecfbe5aafe879d2f352c4ef89e`
> Corresponds to the 14:49 frozen copy of `reports/ghostlock_runlogs/run_w2_v960_143215.log`.

## Current Focus (coordinated disclosure)

The disclosure package has been built under `RV101_Disclosure/`:

- `RV101_Vulnerability_Report.md` — primary vendor report (sensitive details moved into a confidential appendix)
- `11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md` — initial notification (minimum necessary information) + 6 follow-ups
- `CVE_Relationship_Assessment.md` — assessment of the existing CVE-2026-43499 vs independently fixable weaknesses (RS-1…RS-7)
- `Evidence_Checklist.md` / `Evidence_Index.md` / `SHA256SUMS.txt` — evidence and hashes
- `confidential/` — exploit source / offsets / raw evidence (restricted access)

## Evidence Still to Be Collected (affects CVSS and prerequisite conclusions)

1. **Who** unlocked the bootloader (researcher vs factory) — determines whether it is a product observation or a test artifact.
2. The **provenance** of the uid-2000 shell (researcher-supplied ADB key vs preloaded key).
3. App-scenario feasibility (whether `untrusted_app` can reach the same primitive).
4. A pre-exploit `Cap*` snapshot (symmetric to the post-exploit one).
5. A stable, re-frozen final `run_w2_v960_143215.log` (the original file is still being appended to).

## Next Steps (in priority order)

1. Fill in the missing evidence listed above.
2. Send the initial notification to security@rokid.com.
3. Follow the coordinated disclosure timeline (90 days, negotiable) according to the vendor's response.
