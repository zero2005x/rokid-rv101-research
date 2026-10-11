# HITCON ZeroDay — Rokid Glasses RV101 Report Preparation Package

> 中文版：[00_README.md](00_README.md)

> Status: **Draft, not yet submitted**  
> Created: 2026-08-22 (Asia/Taipei)  
> Vulnerability positioning: validation of the exploitability of the existing `CVE-2026-43499` on Rokid Glasses RV101; **no claim of a new CVE, and no claim that this case is a zero-day**.

This directory holds the HITCON ZeroDay platform version of the material derived from `RV101_Disclosure/`. The original vendor report and the immutable raw evidence remain authoritative in `RV101_Disclosure/`; this directory contains only the Traditional Chinese content suitable for the platform's initial review and the second-pass de-identified evidence.

## Reading Order

1. `SUBMISSION_GATE.md`: items the researcher must confirm before sending.
2. `HITCON_Submission_Fields.md`: organized according to the actual platform fields, ready to copy and paste.
3. `HITCON_RV101_Report_ZH-TW.md`: the Traditional Chinese technical report.
4. `Coordinated_Disclosure_Risk_Note.md`: the conflict between HITCON's publication timeline and coordinated disclosure in this case.
5. `HITCON_Pre_Submission_Inquiry_Email.md`: the inquiry email to the platform regarding jurisdiction, timeline, and the private PoC.
6. `HITCON_Disclosure_Timeline.md`: the HITCON version of the disclosure timeline.
7. `Evidence_Attachment_List.md`: which materials may be provided and which must not be uploaded.
8. `01_Attachments/`: textual supporting evidence that has undergone second-pass de-identification.
9. `FINAL_REVIEW.md`: the results of the four checks covering technical, CVE, evidence, and disclosure.

## Platform Field Verification (2026-08-22)

The HITCON ZeroDay report page after login currently contains: title, organization, introduction, type, risk, related URLs, description, remediation suggestions, up to 10 images, and an anonymous reporting option. The type recommended for this case is "Kernel 提權 (Kernel Privilege Escalation)".

Official references:

- [HITCON ZeroDay reporting process](https://zeroday.hitcon.org/resource/tutorials)
- [HITCON ZeroDay terms of use](https://zeroday.hitcon.org/resource/terms-and-conditions)
- [Explanation of vulnerability acceptance criteria](https://zeroday.hitcon.org/news/3)
- [NVD: CVE-2026-43499](https://nvd.nist.gov/vuln/detail/CVE-2026-43499)
- [Rokid Taiwan RV101 product page](https://www.rokid.com.tw/zh-TW/products/rokid-glasses)

## Important Scope Boundaries

- Confirmed: on one authorized test RV101 running firmware `1.22.009-20260710-150201`, a uid 2000 ADB shell successfully obtained runtime uid 0, full effective capabilities, and SELinux Permissive.
- Not confirmed / not claimed: persistence across reboots, AVB or dm-verity bypass, Secure Boot bypass, remote exploitation, exploitability by arbitrary Android apps, and all RV101 firmware being affected.
- Known CVE: CVE-2026-43499 has already been assigned by kernel.org; this case should not be described as a "new CVE" or a "new zero-day".
- The "ZeroDay" in HITCON's name is the platform name; it cannot be used as a basis to call this case a zero-day.

## ADB Live Status

At 2026-08-22 17:21 (UTC+8), the RV101 under test was briefly seen as `device`, but the very first shell command returned `error: closed`, after which the transport disappeared; an ADB reconnect was then performed and the device was still not relisted. Therefore no new complete device baseline was produced this time, and the existing 2026-08-12 and 2026-08-22 evidence remains the authoritative source. See `ADB_Collection_Attempt_20260822.md` for details.
