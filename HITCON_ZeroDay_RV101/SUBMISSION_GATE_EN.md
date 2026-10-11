# Pre-Submission Gate

> 中文版：[SUBMISSION_GATE.md](SUBMISSION_GATE.md)

Do not press "Report" on HITCON ZeroDay until the following items are completed.

## Must Confirm

- [ ] Fill in the researcher name / alias and a contact method that can receive mail.
- [ ] Reconfirm that the test device is owned by you or explicitly authorized, and that no third-party personal data was obtained or retained.
- [ ] Decide whether to report anonymously; this option affects the researcher attribution on the platform's public page.
- [ ] Confirm with `service@zeroday.hitcon.org` whether this case falls within the scope of "reports related to the Taiwan region". Rokid's original manufacturer is located in Hangzhou, China; the Taiwan official website is operated by 鼎榮匯股份有限公司 and sells RV101, but firmware patching still requires the cooperation of Rokid's original manufacturer.
- [ ] Confirm acceptance of the platform's vulnerability publication timeline, or first obtain the platform's written consent to postpone publication in order to accommodate the vendor's patching.
- [ ] Confirm the "Organization" name to be filled in: it is recommended to first ask the platform whether to select `鼎榮匯股份有限公司`, `Rokid`, or have the platform create / assign the correct organization.
- [ ] Decide whether to first notify `security@rokid.com` directly; if already notified, add the date, ticket, and vendor response status.
- [ ] Confirm that the platform risk field is filled in as "High". The CNA's CVSS 3.1 is 7.8 High, but the platform may re-score based on the ADB / physical access preconditions.

## Best to Complete

- [ ] After ADB reconnects, separately capture `adb devices -l`, device information, and pre-exploit capabilities for the same device in the same time period.
- [ ] Produce 1–3 images containing only de-identified terminal output: firmware / kernel, BEFORE, AFTER. Serial numbers, host paths, kernel addresses, or exploit offsets must not appear.
- [ ] If the platform reviewer needs an actual reproduction, first request a private transfer channel, then provide the PoC ELF; do not upload source code, binary, offsets, or raw run logs in the initial submission.
- [ ] Compute a separate SHA-256 for each image ultimately uploaded, and add them to `Evidence_Attachment_List.md`.

## Materials Forbidden From Submission

- The entire `RV101_Disclosure/confidential/` directory.
- Complete exploit source code / ELF, target offsets, KASLR base/slide, and addresses such as `init_cred`, `selinux_state`.
- The original device serial number (unless the researcher explicitly decides to provide it privately to reviewers only).
- Complete block partition dumps, ADB private keys / authorization keys, OTA keys, researcher local paths.
