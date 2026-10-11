# Disclosure Timeline — CVE-2026-43499 (GhostLock) on Rokid Glasses RV101

> 中文版：[Disclosure_Timeline.md](Disclosure_Timeline.md)

> **Purpose**: to record the coordinated disclosure timeline for this research. This is the
> reference of record prior to the public write-up.
> **Status**: continuously updated. Last updated: 2026-09-06.

---

## Summary

| Item | Content |
|---|---|
| Target device | Rokid Glasses RV101 (RG-glasses) |
| Vulnerability | CVE-2026-43499 "GhostLock" (rt-mutex futex-PI use-after-free) |
| Nature of report | **Affected-product confirmation / exploit validation** (not a new CVE) |
| Affected versions | `1.22.009-20260710-150201` and earlier |
| Fixed version | `1.25.012-20260901-150201` (pushed 2026-09-01) |
| Initial report | 2026-08-25 18:26 (UTC+8) |
| Proposed disclosure date | **2026-11-23** (90 days) or later, negotiable |
| Vendor contact | security@rokid.com → degang.xu@rokid.com |
| Bounty | None (the vendor has no bounty programme) |
| Credit | The vendor is willing to give public credit |

---

## Timeline

| Date | Event |
|---|---|
| 2026-08-25 18:26 (UTC+8) | Sent the initial disclosure e-mail to `security@rokid.com`, reporting the GhostLock LPE (`uid 2000 → uid 0`, SELinux Permissive) and listing the additional RS-1..RS-9 items. Proposed a 90-day coordinated-disclosure window. |
| 2026-08-27 20:23 | First follow-up: the 48-hour acknowledgement window had passed with no response; requested a case/ticket number. |
| Late 2026-08 – early 09 | Sought coordination channels: both HITCON ZeroDay and TWCERT replied **declining to assist** (`[TBD]` exact dates and reasons for declining). |
| 2026-09-01 | The device received an OTA notification: `1.22.009-20260710-150201` → `1.25.012-20260901-150201`. |
| 2026-09-03 23:06 | Disassembled `remove_waiter()`: determined that `1.25.012` has backported upstream `3bfdc63` (switching to `waiter->task`). Note that both versions have an SPL of `2024-07-05`; the SPL is unreliable. |
| 2026-09-04 23:52 | Second follow-up: reported the suspected fix, requested confirmation of the affected scope / the fix / an advisory / credit, proposed 11-23 as the disclosure date, and stated that no weaponized material would be provided. |
| 2026-09-06 evening | **Rokid replied** (许德刚 / degang.xu@rokid.com): confirmed receipt, confirmed the fix, no broad advisory, no bounty, willing to give public credit, requested that the redacted comparison be sent. |
| 2026-09-16 | **Disassembly of the vulnerable 1.22.009 kernel completed**: re-downloaded the OTA from the vendor CDN (981,231,124 bytes) → SHA-256 matched the retained list → `payload.bin` → `boot.img` → kernel Image (47,331,340 bytes) → restored symbols → disassembled `remove_waiter()`. **Decisive finding**: `remove_waiter()` is **instruction-for-instruction identical in both versions, with the only difference at `+0x040`** (`mrs x20, sp_el0` ↔ `ldr x20, [x23, #0x30]`). Also corrected one misjudgement: the earlier claim that "the vulnerable version has no `waiter->task` load" **does not hold** (`+0x160` has a `#0x30` load whose base is `x9`, unrelated to `waiter->task`) |
| 2026-09-16 | **Independent re-check of the patched 1.25.012 side completed**: downloaded the OTA (1,045,901,265 bytes, **MD5 matching the vendor OTA metadata**) → disassembled with the same toolchain. **Both versions' `remove_waiter()` are `0x28c` / 163 instructions, differing byte-for-byte in only the one instruction at `+0x040`** (`mrs x20, sp_el0` ↔ `ldr x20, [x23, #0x30]`). The 1.25.012 kernel string = `5.10.209-perf-g9a43350cc9ee-ab1.25.012-20260901-150201` (1.22.009 is `gd908d71a0404`) |
| 2026-09-16 | **Reply draft completed (not yet sent)**: `docs/reply-to-rokid-2026-09-16.md` (replacing the old 9/6 draft), with the regenerated `docs/ghostlock-before-after-redacted.pdf` attached. For the decisions, see "9/16 reply decisions" below. |

---

## Vendor position (summary of the 2026-09-06 e-mail)

- Confirmed receipt of the 8/25 original report and the 9/4 follow-up, and apologised for the delay.
- Confirmed that `1.25.012-20260901-150201` **intentionally** includes the CVE-2026-43499 fix.
- Confirmed that `1.22.009-20260710-150201` and earlier versions are **affected**.
- Does **not** plan to publish a broad public advisory (reason: not directly exposed to the internet, limited impact).
- Has **no** monetary reward (bounty) programme; willing to discuss other forms of recognition (recognition/appreciation).
- Requested that the **redacted technical comparison** and other non-weaponized material be sent to `degang.xu@rokid.com`.
- Is aware of and respects our position of not providing/publishing the device-specific exploit.
- Is aware of our proposed disclosure date of 2026-11-23.

---

## 9/16 reply decisions (final)

| Item | Decision |
|---|---|
| On "no broad advisory" | **Pragmatically accepted**, but an alternative is requested: release notes / a support KB that explicitly lists the **CVE identifier** and the **affected/fixed version mapping**, plus authorisation to quote the vendor's confirmation |
| Disclosure date | **Not postponed for lack of an advisory** (stated explicitly in the letter); the vendor is still welcome to provide a schedule for coordination |
| Draft pre-review | **10 business days** after submission; no reply by then counts as approval; the draft is scheduled to be sent on **2026-11-02** |
| Pre-review red lines | **Not removable**: the CVE identifier, the version mapping, the technical facts, and the de-identified before/after privilege evidence. **Negotiable**: schedule and wording |
| Public format | Referred to only as a "sanitized educational write-up"; the **video/platform is not raised proactively** |
| Case number | Still being pursued (not provided in either e-mail) |
| Distribution scope | Follow-up questions: whether `1.25.012` has been distributed to **all RV101 units / all regions**; and whether **other models sharing the same kernel generation** are affected |
| Source of 1.25.012 | Obtained via the device's **OTA download** (for independent verification; worded in the letter as "delivered to my test unit") |
| RS-1..RS-9 | **Elevated to a second formal item**: requesting separate tracking, a **named technical contact**, and a **secure channel**; details are not disclosed in plaintext |
| Encryption | **Insisting on PGP / a secure channel**; this time the de-identified redacted version is sent first so as not to block the process |
| Attachment | The redacted comparison goes out with this message; To `degang.xu`, **CC `security@rokid.com`**; annotated "for remediation verification only, do not redistribute" |
| Attachment completeness | ✅ No `[TBD]`: `1.22.009` OTA filename + SHA-256 (from `RV101_Disclosure/SHA256SUMS.txt`; MD5 not retained), `1.25.012` MD5 (vendor OTA metadata), kernel version string marked `not available`; §3.1 for the vulnerable version changed to a **semantic description**; the internal checklist has been removed; the PDF has been regenerated (`scripts/gen_redacted_pdf.py`) |
| Outstanding (optional) | ~~1.22.009 instruction-level listing~~ ✅ added; ~~1.25.012 kernel version string / patched-side re-check~~ ✅ completed (see above). The attachment no longer has any `not available` fields |
| Credit | **`Mazda Payrang` (pseudonym only; no other names will be made public)** |
| Written acknowledgement | Explicitly requesting a **signed letter of acknowledgement from a manager-level representative** (usable in a portfolio), preferably on paper, with **printing and postage costs borne by the researcher**; a PDF is also acceptable |
| 9/6→9/16 period | **No further e-mails or material were sent** |
| Test unit | Still on `1.22.009` (**deliberately kept on the old version** to continue the research) |
| Tone | Cooperative and friendly |
| Identity | **Pseudonym `Mazda Payrang`** (consistent between credit and signature; only the pseudonym is used throughout; the previous signature used in this thread is not made public) |

## Open items (do not close together with GhostLock)

1. **RS-1..RS-9** (the Rokid-specific weaknesses listed under item 6 of the original report), of which the most significant are:
   - `GateServiced` listening on `0.0.0.0:8341` with root privileges.
   - The privacy LED while the camera is in use is fully controllable by software/REE (**fix not yet confirmed; still under embargo**).
   - `tracefs` KASLR leak, `dmesg_restrict=0`, `/dev/kgsl-3d0` and `/dev/i2c-1` = 0666, forgeable wearing/proximity state.
2. Whether the vendor will mention the fix in the release notes (even without a broad advisory).

---

## Next steps

1. ~~Finalise the credit name~~ ✅ `Mazda Payrang` (including the signature).
2. ~~Fill in the `[TBD]` entries and regenerate the PDF~~ ✅, and the **1.22.009 instruction-level listing has been filled in from the real disassembly** (2026-09-16).
3. **Send** (`docs/reply-to-rokid-2026-09-16.md`; attachment: `docs/ghostlock-before-after-redacted.pdf`; To `degang.xu`, CC `security@rokid.com`), with the redacted comparison attached.
4. Follow up on: the case number, release notes/KB, distribution scope and other models, obtaining the patched image, separate filing and a technical contact for RS-1..RS-9, the PGP channel, and how the credit and acknowledgement letter are presented.
5. Complete the sanitized write-up for review **by 2026-11-02** (10 business days); publish **on or after 2026-11-23**, not postponed for lack of an advisory.

---

## Principles reminder (self-imposed)

- Public content is limited to: the version mapping, the high-level mechanism, the patch-verification method, de-identified before/after evidence, and the AI-collaboration attribution.
- Not published: exploit source code, compiled ELF, kernel addresses, offsets, heap parameters, OTA/partition images, vendor APKs, or the operational details of the privacy LED bypass.
- Positioning accuracy: this is "**validating an existing CVE** on the RV101" for exploitability and patch status, not a claim of discovering a new CVE or of developing the vulnerability entirely independently.
