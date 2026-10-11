# HITCON Platform and Coordinated Disclosure Risk

> 中文版：[Coordinated_Disclosure_Risk_Note.md](Coordinated_Disclosure_Risk_Note.md)

## Conclusion

This package can be used for HITCON ZeroDay's initial review, but **sending the complete exploit material directly, before the publication timeline and the acceptance scope have been confirmed, is not recommended**.

## 1. Publication Timeline Conflict

HITCON's reporting process page currently states that the general process results in automatic publication on day 60; if the organization reports that the issue has been patched, publication may occur after 7 days, and after a retest confirms the fix, publication may occur after 3 days; for general-type vulnerabilities it may be extended to a maximum of 100 days. The FAQ also explicitly states that the platform's philosophy is to make the vulnerability-handling process public.

This is in genuine conflict with the original Rokid disclosure package's principle of "not publishing weaponized detail before vendor coordination is complete." Before submitting, it is recommended to obtain written confirmation via on-site message or `service@zeroday.hitcon.org`:

1. Whether this case can have its publication postponed until Rokid has completed the patch;
2. Which attachments will be made public in the future;
3. Whether the PoC ELF / source / offset / raw log can remain permanently private or be transferred through a separate secure channel;
4. Whether the platform accepts an extension if the vendor needs more than 60 / 100 days.

## 2. Geographic Acceptance Scope

HITCON's 2017 announcement states that currently only reports related to the Taiwan region are accepted. Rokid's original manufacturer is headquartered in Hangzhou, China; on the other hand, `rokid.com.tw` is operated by the Taiwan company 鼎榮匯股份有限公司, and the website does indeed sell the RV101 under test.

A reasonable but not yet confirmed path is to describe this case as "a product security issue for RV101 sold in Taiwan, requiring a Taiwan contact window to collaborate with Rokid's original manufacturer on the patch," and to ask HITCON to first determine whether it will accept the report. The firmware root cause must not be incorrectly attributed to the Taiwan distributor in order to satisfy the geographic rule.

## 3. Recommended Submission Order

1. First notify `security@rokid.com` using the original `RV101_Disclosure/11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md`.
2. On the same day, or after obtaining a ticket, first ask HITCON whether it will accept the report, whether it can accommodate a postponed publication, and whether the private PoC can be transferred.
3. Only after confirmation, paste the content into `HITCON_Submission_Fields.md`, and provide only second-pass de-identified evidence on the first submission.
4. Provide the confidential technical appendix or the PoC only when the reviewer / vendor explicitly needs it and the transfer and publication conditions have been confirmed.

## 4. Wording That Should Not Be Used

- "Reported to HITCON ZeroDay" does not equal "this is a zero-day".
- Do not write "I discovered CVE-2026-43499"; write "I verified that it is exploitable on RV101".
- Do not write "permanent root", "Secure Boot bypass", "remote critical", or "all RV101 units are affected".
