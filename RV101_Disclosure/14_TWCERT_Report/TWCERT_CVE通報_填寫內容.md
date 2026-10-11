# TWCERT/CC CVE 漏洞通報 — 填寫內容（Rokid RV101）

> 對應表單：https://www.twcert.org.tw/tw/sp-cve-declare-1.html
> 文件狀態：**草稿，尚未提交**
> 機密等級：Restricted（送出的佐證資料應為去識別化版本，不含 exploit source / offsets / raw kernel 位址）
> 定位聲明：本案通報的是**既有 CVE（CVE-2026-43499）在 Rokid Glasses RV101 上的受影響產品確認與可利用性驗證**，
> **不是**新 CVE 申請、**不是** zero-day、**不是**遠端漏洞。
> 建議時程：先向原廠（security@rokid.com）提交初始通報並取得受理／進入協調窗口；TWCERT 本表作為
> 「台灣市場受影響產品之協調／追蹤」用途，於原廠受理後（或 90 天無回應時作為升級管道）再提交，
> 以避免與原廠「官方 advisory 前不得揭露」的協調政策衝突。

---

## 1. 通報者資訊

| 欄位 | 填寫值 |
|---|---|
| 通報人或單位名稱 * | 〔請填寫：姓名 或 alias〕 |
| 組織 | 獨立資安研究員（可留空） |
| E-mail * | 〔請填寫 E-mail〕 |
| 是否公開名稱及組織 | **否**（建議不公開） |

---

## 2. 通報說明

| 欄位 | 填寫值 |
|---|---|
| 漏洞資訊來源 * | **自己發現**（既有 CVE 之「受影響產品確認 + 可利用性」由本人獨立逆向與實機驗證） |
| 漏洞發現日期 * | **2026-08-22**（於受測設備首次實證 uid 0；前期研究自 2026-01-31 起） |
| 影響產品名稱 * | Rokid Glasses RV101（system model `RG-glasses`，平台代號 `neo` / NEO LUNA-V2） |
| 影響產品版本 * | 韌體 `1.22.009-20260710-150201`（Android 12 / SDK 32，Linux kernel `5.10.209-perf`，安全修補層級 2024-07-05） |
| 產品開發者或廠商 * | Rokid（杭州灵伴科技有限公司） |
| 產品網站 * | https://global.rokid.com （台灣：https://www.rokid.com.tw ） |

---

## 3. 通報內容（1）

| 欄位 | 填寫值 |
|---|---|
| 漏洞類型 * | 本機權限提升（Local Privilege Escalation）／Kernel 記憶體損壞（Use-After-Free） |
| Confidentiality * | High（高） |
| Integrity * | High（高） |
| Availability * | High（高） |
| CVSS v3.1 Vector String * | `CVSS:3.1/AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H`（7.8 High） |
| CWE types * | CWE-416（Use After Free） |

### 漏洞描述 *（可直接貼上，字數上限 1000）

```
受測產品 Rokid Glasses RV101（韌體 1.22.009-20260710-150201，Android 12，
Linux kernel 5.10.209-perf）出貨核心未含上游修補，受既有 Linux 核心漏洞
CVE-2026-43499（「GhostLock」，rtmutex futex-PI 堆疊 use-after-free）影響。

該漏洞根因：核心鎖定子系統 remove_waiter() 於 proxy-lock rollback 路徑
（rt_mutex_start_proxy_lock() 自 futex_requeue() 呼叫）誤對 current 而非
waiter::task 操作，導致 waiter 任務的 pi_blocked_on 未被清除，留下指向已釋放
核心堆疊 rt_mutex_waiter 的懸空指標（dangling pointer），可供 UAF 利用。
上游已於 5.10.261（以及 5.15.212 / 6.1.175 等分支）修正；受測裝置核心為
5.10.209-perf，低於修補版本，經離線反組譯確認 remove_waiter() 未含修補。

實證結果：自 Android ADB 低權限 shell（uid 2000、SELinux Enforcing、
context u:r:shell:s0）起始，可穩定（約 50%/boot，附自動重試）取得執行期
uid 0 root shell、完整有效 Linux capabilities、context u:r:kernel:s0，
並將 SELinux 由 Enforcing 轉為 Permissive。

本案不主張：持久化 root、AVB/dm-verity 或 Secure Boot 繞過、遠端利用、
任意 App 可利用、所有韌體版本皆受影響；亦不主張此為新 CVE。
```

### 觸發漏洞特定條件 *（可直接貼上）

```
1. 本機低權限程式碼執行（demonstrated entry point：Android ADB shell，
   uid 2000）。
2. 可讀取核心追蹤資料以破解 KASLR（受測裝置 /sys/kernel/tracing/printk_formats
   對 shell 使用者可讀；此為可獨立修補之 hardening 議題，非核心 CVE 根因）。
3. SELinux 起始為 Enforcing（exploit 會於執行中將其轉為 Permissive）。

環境性條件（已觀察、未證明為必要）：受測裝置 bootloader 為解鎖狀態（orange）、
USB 偵錯（ADB）已啟用。
```

### 觸發漏洞方法 *（可直接貼上，非武器化描述）

```
1. 於低權限 shell 記錄初始 id / id -Z / getenforce / capabilities。
2. 執行針對本韌體建置之私密 PoC（ELF），其流程為：KASLR 洩漏 → 堆積物件洩漏 →
   觸發 CVE-2026-43499 堆疊 UAF → 回收釋放槽位偽造 waiter → 受控核心寫入 →
   關閉 SELinux enforcement → 覆寫程序 credentials。
3. 完成後記錄 whoami / id / id -Z / getenforce / /proc/self/status 之 capabilities，
   與前後狀態比對。
PoC 原始碼、sensitive offsets 與 raw run log 於確認機密處理後私下提供。
```

### 觸發漏洞所需權限 *（下拉選單）

```
低（本機 uid 2000 / Android `shell`，SELinux Enforcing；無需使用者互動）
```

### 其他說明（選填）

```
1. 依 CVSS 3.1，本漏洞屬 AV:L（本機）、PR:L（低權限）、AC:L、UI:N，C/I/A 皆 High，
   評分 7.8 High。
2. 修補建議：將上游 fix（mainline 3bfdc63936dd 或 5.10.y 對應 backport）移植至
   RV101 5.10.209-perf 分支並經 OTA 發布；修補前後以同一 PoC 比對驗證。
3. 附帶發現（需原廠獨立評估、不主張新 CVE）：核心位址可經 tracefs 洩漏
   （printk_formats）、dmesg_restrict=0、unprivileged_bpf_disabled=0、
   /dev/kgsl-3d0 與 /dev/i2c-1 為 0666、相機使用中隱私指示燈（白光）控制鏈
   全程位於 REE 且可由 root 經 sysfs / Binder / 系統屬性三路徑壓制或偽造。
   上述項目已在 2026-08-22~08-24 實機驗證，惟均屬「取得 root 之後」或
   defense-in-depth 議題，是否獨立成漏洞由原廠／審核單位判斷。
```

---

## 4. 佐證資料上傳

- 建議上傳：**去識別化佐證 PDF**（檔名建議 `Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf`）
- 內容應含：受測韌體／kernel 版本、CVE-2026-43499 與修補版本對照、exploit 前後權限狀態
  （uid 2000/Enforcing → uid 0/Permissive/完整 capabilities）、system/vendor 維持唯讀證明。
- **不得上傳**：exploit 原始碼、ELF、sensitive offsets、含核心位址之 raw run log、
  裝置序號（若採公開版本）。可另備一份私密版於 TWCERT 確認機密處理後私下提供。

---

## 附錄：與 Rokid 原廠通報的分工建議

| 管件 | 建議內容定位 | 時機 |
|---|---|---|
| Rokid（security@rokid.com） | 主要通報：CVE-2026-43499 受影響確認 + RS-1~RS-9 硬化清單 | **先發**（初始通報見 `11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md`） |
| TWCERT/CC（本表） | 協調／追蹤：台灣市場受影響產品通報，**明確標註為既有 CVE** | 原廠受理後，或 90 天無回應時升級 |

> 提醒：兩者都應避免在 Rokid 官方 advisory 發布前，於公開管道揭露漏洞細節。
