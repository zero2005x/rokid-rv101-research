# HITCON 證據／附件清單

> English: [Evidence_Attachment_List_EN.md](Evidence_Attachment_List_EN.md)

## 可在初次通報提供

| ID | 檔案 | 性質 | 來源 | 用途 |
|---|---|---|---|---|
| H1 | `01_Attachments/A01_Device_Baseline_REDACTED.txt` | 二次去識別化衍生資料 | `RV101_Disclosure/02_Device_Information/`、`03_Pre_Exploit_State/` | firmware、kernel、security patch、初始權限 |
| H2 | `01_Attachments/A02_Privilege_Transition_REDACTED.txt` | 二次去識別化衍生資料 | `RV101_Disclosure/05_Post_Exploit_State/post_exploit_state_REDACTED.txt` | BEFORE → AFTER、root、caps、SELinux、read-only mounts |
| H3 | `HITCON_RV101_Report_ZH-TW.md` | 平台版報告 | 衍生文件 | 完整但非武器化的技術說明 |

平台通報頁只提供圖片上傳（最多 10 張）。H1/H2 可作為製作去識別化圖片的文字來源，但不能把原始 terminal screenshot 未檢查即上傳。

## 只在私密管道、必要時提供

| 材料 | 原始位置 | 條件 |
|---|---|---|
| 完整 raw success log | `RV101_Disclosure/confidential/raw_evidence/run_w2_v960_143215.log` | 確認不公開、指定收件人、加密或安全傳輸 |
| Confidential Technical Appendix | `RV101_Disclosure/confidential/Confidential_Technical_Appendix.md` | 供應商／HITCON 重現需要 |
| PoC ELF／source | `RV101_Disclosure/confidential/` 指向的研究資料 | 嚴格限縮；不得放入將自動公開欄位 |
| Kernel disassembly／offsets | 原始研究 repo | 僅提供驗證需要的最小片段 |

## 不得提供／需刪除

- 裝置完整序號、研究者 Windows username／本機路徑。
- ADB private keys、第三方 secrets、OTA keys。
- 完整 partition dump 或與 core finding 無關的使用者資料。
- 未經重新檢查的 raw screenshot。

## SHA-256

本包內文件的 hash 由 `SHA256SUMS.txt` 記錄。原始證據 hash 仍以 `RV101_Disclosure/12_Hashes/SHA256SUMS.txt` 為準；H1/H2 是新的衍生檔，不能冒充 raw evidence。

