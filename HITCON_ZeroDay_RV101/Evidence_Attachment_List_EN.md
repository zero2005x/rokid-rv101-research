# HITCON Evidence / Attachment List

> 中文版：[Evidence_Attachment_List.md](Evidence_Attachment_List.md)

## Can Be Provided in the Initial Report

| ID | File | Nature | Source | Purpose |
|---|---|---|---|---|
| H1 | `01_Attachments/A01_Device_Baseline_REDACTED.txt` | Second-pass de-identified derived data | `RV101_Disclosure/02_Device_Information/`, `03_Pre_Exploit_State/` | firmware, kernel, security patch, initial privileges |
| H2 | `01_Attachments/A02_Privilege_Transition_REDACTED.txt` | Second-pass de-identified derived data | `RV101_Disclosure/05_Post_Exploit_State/post_exploit_state_REDACTED.txt` | BEFORE → AFTER, root, caps, SELinux, read-only mounts |
| H3 | `HITCON_RV101_Report_ZH-TW.md` | Platform-version report | Derived document | Complete but non-weaponized technical description |

The platform report page only offers image upload (up to 10 images). H1/H2 can serve as the textual source for producing de-identified images, but the original terminal screenshots must not be uploaded without being checked.

## Only via Private Channels, and Only When Necessary

| Material | Original Location | Condition |
|---|---|---|
| Complete raw success log | `RV101_Disclosure/confidential/raw_evidence/run_w2_v960_143215.log` | Confirm non-publication, designate recipients, encrypt or transfer securely |
| Confidential Technical Appendix | `RV101_Disclosure/confidential/Confidential_Technical_Appendix.md` | Needed for vendor / HITCON reproduction |
| PoC ELF / source | Research material referenced by `RV101_Disclosure/confidential/` | Strictly restricted; must not be placed in fields that will be published automatically |
| Kernel disassembly / offsets | Original research repo | Provide only the minimum fragments needed for validation |

## Must Not Be Provided / Must Be Deleted

- Full device serial number, the researcher's Windows username / local paths.
- ADB private keys, third-party secrets, OTA keys.
- Full partition dumps or user data unrelated to the core finding.
- Raw screenshots that have not been re-checked.

## SHA-256

The hashes of the documents in this package are recorded by `SHA256SUMS.txt`. The original evidence hashes remain authoritative in `RV101_Disclosure/12_Hashes/SHA256SUMS.txt`; H1/H2 are new derived files and must not be passed off as raw evidence.
