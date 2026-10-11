# HITCON Version Final Review

> 中文版：[FINAL_REVIEW.md](FINAL_REVIEW.md)

> Review date: 2026-08-22. Conclusion: **the document package is complete, but it has not yet reached a state where the "Report" button can be pressed directly.**

## 1. Technical Review

- PASS: The tested model, firmware, Android, security patch, and kernel values are consistent across the report / field draft / attachments.
- PASS: BEFORE uid 2000 / Enforcing and AFTER uid 0 / Permissive / full effective caps are clearly distinguished.
- PASS: Only runtime LPE is claimed; persistence, boot-chain bypass, remote exploit, and all-version coverage are not claimed.
- PASS: The reproduction summary is sufficient to understand the validation flow, and does not include kernel addresses, offsets, or controlled-write implementation details.

## 2. CVE Review

- PASS: CVE-2026-43499 already exists, sourced from kernel.org; no duplicate claim of a new CVE.
- PASS: The strength of the evidence for the existence of the RV101 vulnerability and for the attribution of the exploit chain are labeled separately.
- PASS: tracefs / `printk_formats` readability is listed only as "Potential independently fixable vulnerability — requires vendor validation".
- PASS: The risk recommendation adopts the CNA CVSS 3.1 7.8 High, while preserving room for the platform to re-check based on preconditions.

## 3. Evidence Review

- PASS: H1/H2 are explicitly labeled as derived redacted copies and are not passed off as raw evidence.
- PASS: After recomputation, the SHA-256 of the authoritative raw log and the source baseline both match the original `RV101_Disclosure/12_Hashes/SHA256SUMS.txt`.
- PASS: This package's `SHA256SUMS.txt` has been created and verified.
- PASS: The HITCON package does not contain the device serial number, researcher local paths, kernel address, offset, exploit source, or ELF.
- PENDING: The ADB transport interruption at 2026-08-22 17:21 meant no new pre-exploit capability baseline was obtained.
- PENDING: There are not yet any de-identified PNG images that can be uploaded directly to the platform.

## 4. Disclosure Review

- PASS: The initial platform content does not contain weaponized exploit material.
- PASS: A pre-submission inquiry email has been prepared covering geographic acceptance, organization assignment, publication timeline, and the private PoC channel.
- PASS: The roles of Rokid's original manufacturer and the Taiwan distributor are clearly distinguished; the upstream bug is not incorrectly attributed to the Taiwan company.
- BLOCKING USER DECISION: The researcher name / alias, contact information, and anonymous option have not yet been decided.
- BLOCKING EXTERNAL CONFIRMATION: Whether HITCON will accept the report, which organization should be assigned, and whether a postponed publication can be accommodated have not yet been confirmed.
- BLOCKING TIMELINE ENTRY: Whether the Rokid initial disclosure has been sent and its ticket status remain to be filled in.

## Recommended Next Step

First send `HITCON_Pre_Submission_Inquiry_Email.md` (or ask via on-site message), and complete `SUBMISSION_GATE.md`. Until the platform replies, do not upload the PoC, raw log, or any confidential material.
