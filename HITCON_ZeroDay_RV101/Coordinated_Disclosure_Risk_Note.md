# HITCON 平台與協調揭露風險

## 結論

本包可以用於 HITCON ZeroDay 初次審核，但**不建議在未確認公開時程與受理範圍前直接送出完整 exploit material**。

## 1. 公開時程衝突

HITCON 的通報流程頁目前寫明一般流程在第 60 天自動公開；若組織回報已修補，可能在 7 天後公開，複測確認後可能在 3 天後公開；通用型漏洞最長可能延至 100 天。FAQ 也明確表示平台以公開漏洞處理過程為理念。

這與原始 Rokid disclosure package 的原則「完成供應商協調前不公開 weaponized detail」存在實際衝突。建議在提交前以站內訊息或 `service@zeroday.hitcon.org` 取得書面確認：

1. 本案是否能在 Rokid 修補完成前延後公開；
2. 哪些附件將來會被公開；
3. PoC ELF／source／offset／raw log 是否能維持永久私密或另走安全傳輸；
4. 若供應商需要超過 60／100 天，平台是否接受延長。

## 2. 地域受理範圍

HITCON 2017 年公告寫明目前僅受理台灣地區相關通報。Rokid 原廠辦公地點在中國杭州；另一方面，`rokid.com.tw` 由台灣公司鼎榮匯股份有限公司營運，且網站確實銷售受測 RV101。

合理但尚未確認的路徑，是將本案描述為「在台銷售 RV101 的產品安全問題，需台灣窗口與 Rokid 原廠協作修補」，並請 HITCON 先判斷是否受理。不可為了符合地域規則，把 firmware root cause 錯誤歸因於台灣通路商。

## 3. 建議送件順序

1. 優先以原始 `RV101_Disclosure/11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md` 通知 `security@rokid.com`。
2. 同日或取得 ticket 後，先詢問 HITCON 是否受理、是否可配合延後公開及私密 PoC 傳輸。
3. 確認後才貼入 `HITCON_Submission_Fields.md`，初次只提供二次去識別化證據。
4. 只有在審核員／供應商明確需要且傳輸與公開條件已確認後，才提供機密技術附錄或 PoC。

## 4. 不應使用的措辭

- 「HITCON ZeroDay 通報」不等於「這是 zero-day」。
- 不寫「我發現 CVE-2026-43499」；應寫「我驗證其在 RV101 上可被利用」。
- 不寫「永久 root」、「Secure Boot bypass」、「遠端 critical」或「所有 RV101 都受影響」。

