# 私密材料：禁止直接上傳 HITCON 公開欄位

本檔是醒目提醒，不包含機密資料本身。

以下材料留在原始 `RV101_Disclosure/confidential/` 或研究 repo，不複製進本包：

- exploit source、compiled ELF、retry wrapper；
- hard-coded kernel offsets、KASLR base／slide；
- `init_cred`、`selinux_state`、task／cred／fops／mm 等 kernel 位址；
- forged object layout、reclaim／heap grooming 參數、controlled-write 細節；
- 完整 raw run logs；
- partition dump、device serial、host path、keys／secrets。

若 HITCON 或 Rokid 要求 PoC，應先確認：指定收件人、用途、保存／刪除規則、是否會被平台自動公開、可接受的加密傳輸方式。只提供重現所需的最小資料。

