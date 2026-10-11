# Disclosure Timeline — CVE-2026-43499 (GhostLock) on Rokid Glasses RV101

> English: [Disclosure_Timeline_EN.md](Disclosure_Timeline_EN.md)

> **用途**：記錄本研究的協調揭露（coordinated disclosure）時間線。公開 write-up 前以此為準。
> **狀態**：持續更新中。最後更新：2026-09-06。

---

## 摘要

| 項目 | 內容 |
|---|---|
| 目標裝置 | Rokid Glasses RV101（RG-glasses） |
| 漏洞 | CVE-2026-43499 "GhostLock"（rt-mutex futex-PI use-after-free） |
| 報告性質 | **受影響產品確認／exploit 驗證**（非新 CVE） |
| 受影響版本 | `1.22.009-20260710-150201` 及更早 |
| 修補版本 | `1.25.012-20260901-150201`（2026-09-01 推送） |
| 初始報告 | 2026-08-25 18:26 (UTC+8) |
| 提議揭露日 | **2026-11-23**（90 天）或之後，可協調 |
| 廠商窗口 | security@rokid.com → degang.xu@rokid.com |
| 獎金 | 無（廠商無 bounty 計畫） |
| 致謝 | 廠商願公開致謝（credit） |

---

## 時間線

| 日期 | 事件 |
|---|---|
| 2026-08-25 18:26 (UTC+8) | 寄出初始揭露信給 `security@rokid.com`，報告 GhostLock LPE（`uid 2000 → uid 0`、SELinux Permissive），並列出 RS-1..RS-9 附加項目。提議 90 天協調揭露窗口。 |
| 2026-08-27 20:23 | 第一次追蹤：48 小時確認窗口已過、無回應，要求 case/ticket 編號。 |
| 2026-08 底–09 初 | 尋求協調管道：HITCON ZeroDay、TWCERT 均回覆**拒絕協助**（`[待補]` 精確日期與拒絕理由）。 |
| 2026-09-01 | 裝置收到 OTA 通知：`1.22.009-20260710-150201` → `1.25.012-20260901-150201`。 |
| 2026-09-03 23:06 | 反組譯 `remove_waiter()`：判定 `1.25.012` 已 backport upstream `3bfdc63`（改用 `waiter->task`）。注意兩版 SPL 皆為 `2024-07-05`，SPL 不可靠。 |
| 2026-09-04 23:52 | 第二次追蹤：告知疑似修補、請求確認受影響範圍／修補／advisory／致謝，提出揭露日 11-23，並聲明不提供武器化材料。 |
| 2026-09-06 晚間 | **Rokid 回覆**（许德刚 / degang.xu@rokid.com）：確認收到、確認修補、無 broad advisory、無獎金、願公開致謝、要求寄 redacted comparison。 |
| 2026-09-16 | **1.22.009 漏洞版 kernel 反組譯完成**：自原廠 CDN 重新下載 OTA（981,231,124 bytes）→ SHA-256 與留存清單一致 → `payload.bin` → `boot.img` → kernel Image（47,331,340 bytes）→ 還原符號 → 反組譯 `remove_waiter()`。**決定性發現**：兩版 `remove_waiter()` **逐指令相同，唯一差異是 `+0x040`**（`mrs x20, sp_el0` ↔ `ldr x20, [x23, #0x30]`）。並更正一項誤判：原「漏洞版無 `waiter->task` 載入」**不成立**（`+0x160` 有基底為 `x9` 的 `#0x30` 載入，與 `waiter->task` 無關） |
| 2026-09-16 | **1.25.012 修補側獨立複核完成**：下載 OTA（1,045,901,265 bytes，**MD5 與原廠 OTA metadata 一致**）→ 同套工具鏈反組譯。**兩版 `remove_waiter()` 同為 `0x28c`／163 條指令，逐位元組只差 `+0x040` 一條指令**（`mrs x20, sp_el0` ↔ `ldr x20, [x23, #0x30]`）。1.25.012 kernel 字串 = `5.10.209-perf-g9a43350cc9ee-ab1.25.012-20260901-150201`（1.22.009 為 `gd908d71a0404`）|
| 2026-09-16 | **回信草稿完成（尚未寄出）**：`docs/reply-to-rokid-2026-09-16.md`（取代 9/6 舊草稿），附件為重生的 `docs/ghostlock-before-after-redacted.pdf`。決策見下方「9/16 回信決策」。 |

---

## 廠商立場（2026-09-06 信件摘要）

- 確認收到 8/25 原始報告與 9/4 追蹤，並為延遲致歉。
- 確認 `1.25.012-20260901-150201` **有意**納入 CVE-2026-43499 修補。
- 確認 `1.22.009-20260710-150201` 及更早版本**受影響**。
- **不**計畫發布 broad public advisory（理由：非直接暴露於網際網路、影響範圍有限）。
- **無**金錢獎勵（bounty）計畫；願討論其他形式的認可（recognition/appreciation）。
- 要求寄送 **redacted technical comparison** 與其他非武器化材料到 `degang.xu@rokid.com`。
- 已知悉並尊重我方不提供／不公開 device-specific exploit 的立場。
- 已知悉我方提議的揭露日 2026-11-23。

---

## 9/16 回信決策（已定案）

| 項目 | 決定 |
|---|---|
| 對「不發 broad advisory」 | **務實接受**，但要求替代品：release notes／support KB 明確列出 **CVE 編號**與**受影響／已修補版本對照**，並要求授權引用廠商確認 |
| 揭露日 | **不因缺少 advisory 而順延**（信中明寫）；仍歡迎廠商提供時程供協調 |
| 草稿預審 | 送審後 **10 個工作日**，逾期未回視為同意；預定 **2026-11-02** 送草稿 |
| 預審紅線 | **不可刪**：CVE 編號、版本對照、技術事實、去識別 before/after 特權證據。**可談**：時程與措辭 |
| 公開形式 | 只稱「sanitized educational write-up」，**不主動提影片／平台** |
| Case 編號 | 繼續追（兩次去信均未給） |
| 配送範圍 | 追問：`1.25.012` 是否已對**所有 RV101／所有地區**推送；**其他機型共用同一 kernel 世代**者是否受影響 |
| 1.25.012 來源 | 裝置 **OTA 下載**取得（供獨立驗證用；信中表述為「delivered to my test unit」） |
| RS-1..RS-9 | **拉高為第二個正式議題**：要求另開獨立追蹤、**指派技術窗口**、提供**加密通道**；細節不在明文中揭露 |
| 加密 | **堅持 PGP／安全通道**；本次先寄已去識別的 redacted 版本，不阻塞流程 |
| 附件 | redacted comparison 隨本次寄出；To `degang.xu`、**CC `security@rokid.com`**；加註「僅供修補驗證、請勿轉載」 |
| 附件完成度 | ✅ 無 `[待補]`：`1.22.009` OTA 檔名＋SHA-256（取自 `RV101_Disclosure/SHA256SUMS.txt`，MD5 未留存）、`1.25.012` MD5（原廠 OTA metadata）、kernel 版本字串標 `not available`；§3.1 漏洞版改**語意描述**；已移除內部檢查清單；PDF 已重生（`scripts/gen_redacted_pdf.py`） |
| 未竟（選配） | ~~1.22.009 指令層清單~~ ✅ 已補；~~1.25.012 kernel 版本字串／修補側複核~~ ✅ 已完成（見上列）。附件已無任何 `not available` 欄位 |
| 致謝 | **`Mazda Payrang`（只用化名，不公開其他名字）** |
| 書面致謝 | 明確要求**官方主管級簽名的致謝函**（可用於作品集），紙本為佳、**印刷與郵遞費用由研究者自負**；PDF 亦可接受 |
| 9/6→9/16 期間 | **未再寄出任何信件或材料** |
| 測試機 | 仍為 `1.22.009`（**刻意保留舊版**繼續研究） |
| 語氣 | 合作友善 |
| 身分 | **化名 `Mazda Payrang`**（credit 與簽名一致，全篇只用化名；不公開本串先前署名） |

## 未結項目（勿與 GhostLock 一併結案）

1. **RS-1..RS-9**（原報告第 6 點列出的 Rokid 專屬弱點），其中特別重大：
   - `GateServiced` 以 root 權限監聽 `0.0.0.0:8341`。
   - camera 使用中隱私 LED 可由軟體／REE 完全控制（**尚未確認修補，仍在 embargo 內**）。
   - `tracefs` KASLR 洩漏、`dmesg_restrict=0`、`/dev/kgsl-3d0` 與 `/dev/i2c-1` = 0666、可偽造的佩戴/接近狀態。
2. 廠商是否會在 release notes 提及修補（即便無 broad advisory）。

---

## 下一步

1. ~~定案 credit 名字~~ ✅ `Mazda Payrang`（含簽名）。
2. ~~補完 `[待補]` 並重生 PDF~~ ✅，且 **1.22.009 指令層清單已用真實反組譯補上**（2026-09-16）。
3. **寄出**（附件：`docs/ghostlock-before-after-redacted.pdf`；To `degang.xu`、CC `security@rokid.com`） `docs/reply-to-rokid-2026-09-16.md`（To `degang.xu`、CC `security@rokid.com`），附 redacted 比較。
4. 追蹤：case 編號、release notes／KB、配送範圍與其他機型、修補映像取得、RS-1..RS-9 獨立立案與技術窗口、PGP 通道、致謝呈現與致謝函。
5. **2026-11-02 前**完成 sanitized write-up 送審（10 個工作日）；**2026-11-23 或之後**公開，不因無 advisory 順延。

---

## 原則提醒（自我約束）

- 公開內容僅限：版本對照、高層原理、修補驗證方法、去識別 before/after 證據、AI 協作 attribution。
- 不公開：exploit 原始碼、編譯後 ELF、kernel 位址、offsets、heap 參數、OTA/partition images、原廠 APK、隱私 LED 繞過操作細節。
- 定位準確：這是「在 RV101 上**驗證既有 CVE** 的可利用性與修補狀態」，非自稱發現新 CVE 或完全獨立開發漏洞。
