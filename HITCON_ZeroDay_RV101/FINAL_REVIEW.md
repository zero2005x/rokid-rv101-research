# HITCON 版最終檢查

> 檢查日期：2026-08-22。結論：**文件包完成，但尚未達到可直接按下「回報」的狀態。**

## 1. 技術檢查

- PASS：受測型號、firmware、Android、security patch 與 kernel 值在報告／欄位草稿／附件一致。
- PASS：清楚區分 BEFORE uid 2000／Enforcing 與 AFTER uid 0／Permissive／full effective caps。
- PASS：只主張 runtime LPE；未主張 persistence、boot-chain bypass、remote exploit 或全版本覆蓋。
- PASS：重現摘要足供理解驗證流程，且未放入 kernel 位址、offset 或 controlled-write 實作細節。

## 2. CVE 檢查

- PASS：CVE-2026-43499 已存在，來源為 kernel.org；未重複主張新 CVE。
- PASS：RV101 漏洞存在與 exploit chain 歸因的證據強度分開標示。
- PASS：tracefs／`printk_formats` 可讀性只列為「Potential independently fixable vulnerability — requires vendor validation」。
- PASS：風險建議採 CNA CVSS 3.1 7.8 High，同時保留平台依前置條件複核的空間。

## 3. 證據檢查

- PASS：H1/H2 明確標示為 derived redacted copy，不冒充 raw evidence。
- PASS：權威 raw log 與來源 baseline 的 SHA-256 重新計算後，均與原 `RV101_Disclosure/12_Hashes/SHA256SUMS.txt` 相符。
- PASS：本包 `SHA256SUMS.txt` 已建立並驗證。
- PASS：HITCON 包未包含裝置序號、研究者本機路徑、kernel address、offset、exploit source 或 ELF。
- PENDING：2026-08-22 17:21 的 ADB transport 中斷，未取得新的 pre-exploit capability baseline。
- PENDING：尚無可直接上傳平台的去識別化 PNG 圖片。

## 4. 揭露檢查

- PASS：初次平台內容不含 weaponized exploit material。
- PASS：已準備地域受理、組織指派、公開時程及私密 PoC 管道的送件前詢問信。
- PASS：對 Rokid 原廠與台灣通路商的角色有清楚區分，未把 upstream bug 錯誤歸因於台灣公司。
- BLOCKING USER DECISION：研究者名稱／alias、聯絡方式、匿名選項尚未決定。
- BLOCKING EXTERNAL CONFIRMATION：HITCON 是否受理、應指派哪個組織、是否能配合延後公開尚未確認。
- BLOCKING TIMELINE ENTRY：Rokid 初始 disclosure 是否已寄出及 ticket 狀態尚待填寫。

## 建議下一步

先寄 `HITCON_Pre_Submission_Inquiry_Email.md`（或用站內訊息詢問），並完成 `SUBMISSION_GATE.md`。在收到平台回覆前，不上傳 PoC、raw log 或任何 confidential material。

