# Pre-Submission Inquiry Email to HITCON ZeroDay

> 中文版：[HITCON_Pre_Submission_Inquiry_Email.md](HITCON_Pre_Submission_Inquiry_Email.md)

> Purpose: to first confirm geographic acceptance, organization assignment, publication timeline, and private PoC handling. Not yet sent.

**To:** `service@zeroday.hitcon.org`  
**Subject:** Pre-submission inquiry: verification of CVE-2026-43499 Kernel privilege escalation on Rokid Glasses RV101 sold in Taiwan

Dear HITCON ZeroDay team:

I am an independent security researcher, `[name / alias]`. On a Rokid Glasses RV101 that I own and am authorized to test, I have empirically demonstrated that the existing Linux kernel vulnerability CVE-2026-43499 can be used to escalate from a uid 2000 ADB shell to runtime uid 0, full effective capabilities, and SELinux Permissive.

The RV101 under test runs firmware `1.22.009-20260710-150201`, Android 12, kernel `5.10.209-perf`. This case is an affected-product / exploit validation of an existing CVE on a specific product; I do not claim a new CVE or a zero-day, nor do I claim persistent root, AVB / dm-verity / Secure Boot bypass, or remote exploitation.

Before submitting, I would like to ask:

1. Rokid's original manufacturer is located in Hangzhou, China, but `rokid.com.tw` is operated by the Taiwan company 鼎榮匯股份有限公司, which sells the RV101 under test in Taiwan. Does a product security report of this kind fall within your platform's current Taiwan-region acceptance scope?
2. Should the "Organization" field on the report page be filled in as Rokid, 鼎榮匯股份有限公司, or would your team help establish / assign the correct contact window?
3. Because patching the product's kernel requires the cooperation of the original manufacturer, if the OTA is not completed within 60 / 100 days, can publication be postponed according to the actual patching progress?
4. Can the complete PoC ELF, source, kernel offsets, addresses, and raw logs be provided through a private channel that will not be published automatically, and kept separate from the fields that will be made public in the future?

I can first provide a Traditional Chinese report and before / after evidence that excludes the device serial number, kernel addresses, offsets, and exploit internals, and I am willing to cooperate with HITCON and the vendor on reproduction, patching, and retesting. Please let me know the recommended process. Thank you.

Researcher: `[name / alias]`  
Contact email: `[email]`  
PGP: `[optional]`
