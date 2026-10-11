# HITCON ZeroDay — Rokid Glasses RV101 通報準備包

> English: [00_README_EN.md](00_README_EN.md)

> 狀態：**草稿，尚未提交**  
> 建立日期：2026-08-22（Asia/Taipei）  
> 漏洞定位：既有 `CVE-2026-43499` 在 Rokid Glasses RV101 上的可利用性驗證；**不主張新 CVE，也不主張本案是 zero-day**。

此目錄是由 `RV101_Disclosure/` 衍生的 HITCON ZeroDay 平台版資料。原始供應商報告及不可變更的 raw evidence 仍以 `RV101_Disclosure/` 為準；本目錄只放入適合平台初次審核的繁中內容與二次去識別化證據。

## 閱讀順序

1. `SUBMISSION_GATE.md`：送出前必須由研究者確認的項目。
2. `HITCON_Submission_Fields.md`：依實際平台欄位整理，可直接複製貼上。
3. `HITCON_RV101_Report_ZH-TW.md`：繁中技術報告。
4. `Coordinated_Disclosure_Risk_Note.md`：HITCON 公開時程與本案協調揭露的衝突。
5. `HITCON_Pre_Submission_Inquiry_Email.md`：寄給平台的地域／時程／私密 PoC 詢問信。
6. `HITCON_Disclosure_Timeline.md`：HITCON 版揭露時間線。
7. `Evidence_Attachment_List.md`：哪些材料可提供、哪些不得上傳。
8. `01_Attachments/`：已二次去識別化的文字佐證。
9. `FINAL_REVIEW.md`：技術、CVE、證據及揭露四項檢查結果。

## 平台欄位核對（2026-08-22）

HITCON ZeroDay 的登入後通報頁目前包含：標題、組織、介紹、類型、風險、相關網址、敘述、修補建議、最多 10 張圖片及匿名通報選項。本案建議類型為「Kernel 提權 (Kernel Privilege Escalation)」。

官方資料：

- [HITCON ZeroDay 通報流程](https://zeroday.hitcon.org/resource/tutorials)
- [HITCON ZeroDay 使用條款](https://zeroday.hitcon.org/resource/terms-and-conditions)
- [漏洞接受標準說明](https://zeroday.hitcon.org/news/3)
- [NVD：CVE-2026-43499](https://nvd.nist.gov/vuln/detail/CVE-2026-43499)
- [Rokid 台灣 RV101 商品頁](https://www.rokid.com.tw/zh-TW/products/rokid-glasses)

## 重要範圍界線

- 已確認：在一台授權測試的 RV101、韌體 `1.22.009-20260710-150201` 上，由 uid 2000 ADB shell 成功取得執行期 uid 0、完整有效 capabilities 與 SELinux Permissive。
- 未確認／不主張：跨重開機持久化、AVB 或 dm-verity 繞過、Secure Boot 繞過、遠端利用、任意 Android App 可利用、所有 RV101 韌體皆受影響。
- 已知 CVE：CVE-2026-43499 已由 kernel.org 指派；本案不應被描述為「新 CVE」或「新 zero-day」。
- HITCON 名稱中的「ZeroDay」是平台名稱，不能據此把本案稱作 zero-day。

## ADB 即時狀態

2026-08-22 17:21（UTC+8）曾看見受測 RV101 為 `device`，但第一個 shell 指令即回傳 `error: closed`，隨後 transport 消失；接著執行 ADB reconnect 後仍未重新列出裝置。因此本次沒有產生新的完整裝置基線，現有 2026-08-12 與 2026-08-22 證據仍是權威來源。詳見 `ADB_Collection_Attempt_20260822.md`。
