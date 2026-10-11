# 提交前門檻

> English: [SUBMISSION_GATE_EN.md](SUBMISSION_GATE_EN.md)

在以下項目完成前，請勿按下 HITCON ZeroDay 的「回報」。

## 必須確認

- [ ] 填入研究者名稱／別名與可收信的聯絡方式。
- [ ] 再次確認測試裝置為本人所有或有明確授權，且未取得或保留第三方個資。
- [ ] 決定是否匿名通報；此選項會影響平台公開頁面的研究者署名。
- [ ] 向 `service@zeroday.hitcon.org` 確認本案是否符合「台灣地區相關通報」範圍。Rokid 原廠位於中國杭州；台灣官網由鼎榮匯股份有限公司營運並銷售 RV101，但韌體修補仍需 Rokid 原廠配合。
- [ ] 確認接受平台的漏洞公開時程，或先取得平台書面同意延後公開，以配合供應商修補。
- [ ] 確認要填入的「組織」名稱：建議先詢問平台應選 `鼎榮匯股份有限公司`、`Rokid`，或由平台建立／指派正確組織。
- [ ] 決定是否先直接通知 `security@rokid.com`；若已通知，補上日期、ticket 與供應商回應狀態。
- [ ] 確認平台風險欄填「高」。CNA 的 CVSS 3.1 為 7.8 High，但平台可依 ADB／實體存取前置條件重新評分。

## 最好補齊

- [ ] ADB 重新連線後，另行捕捉同一時段、同一裝置的 `adb devices -l`、裝置資訊及 pre-exploit capabilities。
- [ ] 製作 1–3 張只含去識別化終端輸出的圖片：韌體／kernel、BEFORE、AFTER。不得出現序號、主機路徑、kernel 位址或 exploit offset。
- [ ] 若平台審核員需要實際重現，先要求私密傳輸管道，再提供 PoC ELF；不在初次送件上傳原始碼、binary、offset 或 raw run log。
- [ ] 為最終上傳的每張圖片另計 SHA-256，補入 `Evidence_Attachment_List.md`。

## 禁止送出的材料

- `RV101_Disclosure/confidential/` 全目錄。
- 完整 exploit 原始碼／ELF、目標 offset、KASLR base/slide、`init_cred`、`selinux_state` 等位址。
- 原始裝置序號（除非研究者明確決定只對審核員私下提供）。
- 完整 block partition dump、ADB 私鑰／授權 key、OTA key、研究者本機路徑。
