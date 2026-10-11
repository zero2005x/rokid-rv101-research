# 寄給 HITCON ZeroDay 的送件前詢問信

> English: [HITCON_Pre_Submission_Inquiry_Email_EN.md](HITCON_Pre_Submission_Inquiry_Email_EN.md)

> 用途：先確認地域受理、組織指派、公開時程及私密 PoC 處理。尚未寄送。

**To:** `service@zeroday.hitcon.org`  
**Subject:** 送件前詢問：在台銷售 Rokid Glasses RV101 的 CVE-2026-43499 Kernel 提權驗證

HITCON ZeroDay 團隊您好：

我是獨立資安研究者 `[姓名／alias]`。我在本人所有且授權測試的一台 Rokid Glasses RV101 上，已實證既有 Linux kernel 漏洞 CVE-2026-43499 可由 uid 2000 ADB shell 提升至執行期 uid 0、完整有效 capabilities 與 SELinux Permissive。

受測 RV101 韌體為 `1.22.009-20260710-150201`，Android 12，kernel `5.10.209-perf`。本案是既有 CVE 在特定產品上的 affected-product／exploit validation，不主張新 CVE 或 zero-day；也不主張 persistent root、AVB／dm-verity／Secure Boot bypass 或遠端利用。

送件前想先請教：

1. Rokid 原廠位於中國杭州，但 `rokid.com.tw` 由台灣公司鼎榮匯股份有限公司營運並在台銷售受測 RV101。此類產品安全通報是否符合貴平台目前的台灣地區受理範圍？
2. 通報頁的「組織」應填 Rokid、鼎榮匯股份有限公司，或由貴團隊協助建立／指派正確窗口？
3. 由於產品 kernel 修補需原廠配合，若 60／100 天內尚未完成 OTA，是否可依實際修補進度延後公開？
4. 完整 PoC ELF、source、kernel offsets、位址與 raw logs 是否可透過不會自動公開的私密管道提供，並與未來公開欄位分離？

我可以先提供不含裝置序號、kernel 位址、offset 與 exploit internals 的繁中報告及 before／after 證據，並願意配合 HITCON 與供應商重現、修補及複測。煩請告知建議流程，謝謝。

研究者：`[姓名／alias]`  
聯絡信箱：`[email]`  
PGP：`[optional]`

