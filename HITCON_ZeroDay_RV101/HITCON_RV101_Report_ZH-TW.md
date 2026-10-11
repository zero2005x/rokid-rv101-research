# Rokid Glasses RV101 本機 Kernel 權限提升通報

> English: [HITCON_RV101_Report_ZH-TW_EN.md](HITCON_RV101_Report_ZH-TW_EN.md)

> 提交對象：HITCON ZeroDay 漏洞審核小組／受通報組織  
> 文件狀態：草稿，尚未提交  
> 機密等級：Restricted；本文件刻意省略武器化細節  
> 研究者：[待填：姓名或 alias／聯絡信箱]

## 1. 摘要

在一台本人所有、授權測試的 Rokid Glasses RV101 上，已成功證明出貨韌體 `1.22.009-20260710-150201` 可受既有 Linux kernel 漏洞 `CVE-2026-43499` 影響。攻擊由 Android ADB uid 2000 shell、SELinux Enforcing 起始，最終取得可工作的執行期 uid 0 root shell、完整有效 Linux capabilities、`u:r:kernel:s0` context 與 SELinux Permissive。

這不是只有 crash／DoS 的版本判定，而是實際的安全邊界跨越。但本案不主張 persistent root、Secure Boot／AVB／dm-verity bypass、遠端利用，亦不主張 CVE-2026-43499 是由 Rokid 引入或應再取得第二個 CVE。

## 2. 受測產品

| 欄位 | 觀察值 | 證據狀態 |
|---|---|---|
| 產品 | Rokid Glasses RV101；system model `RG-glasses` | CONFIRMED |
| 裝置代號 | `glasses` | CONFIRMED |
| Firmware | `1.22.009-20260710-150201` | CONFIRMED |
| Build fingerprint | `Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys` | CONFIRMED |
| Android | 12／SDK 32 | CONFIRMED |
| Security patch | `2024-07-05` | CONFIRMED |
| Kernel | `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` | CONFIRMED |
| 測試範圍 | 1 台本人所有／授權設備 | CONFIRMED（研究者聲明） |

公開／平台版文件不揭露裝置序號；不可變更 raw evidence 中仍保留原始序號以供私密驗證。

## 3. 漏洞分類與既有 CVE 關係

- 類型：Kernel Privilege Escalation
- CVE：CVE-2026-43499（已存在）
- CWE：CWE-416 Use After Free
- 元件：Linux kernel futex PI／rt-mutex `remove_waiter()`
- 邊界：uid 2000 `u:r:shell:s0`／Enforcing → uid 0 `u:r:kernel:s0`／Permissive

kernel.org 的 CVE 說明指出，proxy-lock rollback 的情境中 `waiter::task` 不等於 `current`，舊實作卻對 `current` 操作，可能使 waiter task 的 `pi_blocked_on` 未被清除而留下 dangling pointer。CNA CVSS 3.1 為 7.8 High。

RV101 kernel image 的離線 disassembly 顯示對應函式未含 stable fix；成功 exploit 行為也與該 root cause 相符。因此：

- CVE 在受測 RV101 firmware 中存在：CONFIRMED。
- 成功 chain 利用的是同一 CVE：STRONGLY SUPPORTED。
- 本案需要新 CVE：不主張。
- 另有 Rokid-specific 新漏洞：尚未確認。

## 4. 攻擊前置條件

| 條件 | 分類 | 狀態 |
|---|---|---|
| 本機 code execution at uid 2000 shell | 成功 run 的 entry point | CONFIRMED |
| ADB／USB debugging 已啟用 | 測試環境 | CONFIRMED |
| 已授權 ADB host | 是否每次必要尚待確認 | UNKNOWN |
| 實體 USB 存取 | 是否唯一入口尚待確認 | UNKNOWN |
| Bootloader unlocked／orange | 測試環境 | CONFIRMED；未證明為必要條件 |
| 一般 Android App 可觸發 | 未測試 | UNKNOWN／不主張 |
| 網路存取 | 未見 chain 使用；未正式隔離測試 | INFERRED 非必要 |
| 重開機後仍保持 root | 未測試 | UNKNOWN／不主張 |

## 5. 技術摘要

以避免武器化的層級描述，成功 chain 為：

```text
CVE-2026-43499 kernel stack UAF
→ 受控的 kernel-memory modification
→ 關閉 SELinux enforcement
→ 修改目標程序 credentials
→ uid 0、完整 capabilities、root shell
```

chain 另使用 uid 2000 可讀取 `/sys/kernel/tracing/printk_formats` 的環境條件來取得 KASLR 所需資訊。此可讀性可被獨立修補／hardening，標記為「Potential independently fixable vulnerability — requires vendor validation」，但目前不主張它已有獨立 CVE root cause。

## 6. 成功證據

### BEFORE EXPLOIT

```text
Enforcing
uid=2000(shell) gid=2000(shell) ... context=u:r:shell:s0
startup: pid=[REDACTED] uid=2000 euid=2000 gid=2000
```

### AFTER EXPLOIT

```text
root
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
u:r:kernel:s0
Permissive
Uid:    0       0       0       0
Gid:    0       0       0       0
CapInh: 0000000000000000
CapPrm: 000001ffffffffff
CapEff: 000001ffffffffff
CapBnd: 000001ffffffffff
```

另以建立空白 `/data/root_proof`（root:root）與列出 mode 0700 `/data/adb` 的方式，做非破壞性權限驗證。受 dm-verity 保護的 `system_ext`／`vendor`／`vendor_dlkm` 仍為 `ro`。

## 7. 重現方式

初次平台通報提供可驗證流程，不提供可公開武器化內容：

1. 準備同型號與同 firmware 的 RV101，從 uid 2000 ADB shell 記錄初始 `id`、`id -Z`、`getenforce` 與 capabilities。
2. 執行針對此 firmware 建置的私密 PoC。
3. PoC 成功後，記錄 `whoami`、`id`、`id -Z`、`getenforce`、`/proc/self/status` capabilities 與 process SELinux context。
4. 執行非破壞性 proof file 與 mount read-only 檢查。
5. 與本報告的 BEFORE／AFTER 值比對。

PoC ELF、source、sensitive offsets、kernel 位址與完整 raw run log可在平台確認機密處理與延後公開後，私下提供審核員或供應商重現。

## 8. 影響與風險

runtime uid 0 加上完整有效 capabilities 及 SELinux Permissive，表示攻擊程序已跨越 Android shell 與 kernel／root 邊界，可危及執行中作業系統內資料的機密性與完整性。Availability 也可能受 root process 影響，但本研究未進行破壞性驗證。

建議平台初始風險填「高」，理由是 kernel.org CNA 的 CVSS 3.1 為 7.8 High；HITCON 應依尚待確認的 ADB／實體存取前置條件複核。

## 9. 不支援的主張

| 主張 | 證據狀態 |
|---|---|
| persistent root | UNKNOWN；不主張 |
| Secure Boot／AVB／dm-verity bypass | 無證據；不主張 |
| system/vendor writable | 觀察結果相反，仍為 `ro` |
| remote exploit | 無證據；不主張 |
| 任意 App 可利用 | 未測試；不主張 |
| 所有 RV101 firmware 受影響 | 僅測 1 台／1 版；不主張 |
| 新 CVE／zero-day | CVE 已存在；不主張 |

## 10. 修補建議

1. Backport CVE-2026-43499 對應 kernel.org stable fix 至 RV101 5.10 branch，透過 OTA 發布。
2. 修補前後以同一私密 PoC 比對；修補後應維持 uid 2000、SELinux Enforcing，且不再形成 UAF controlled write。
3. 限制 uid 2000 對 tracefs pointer material（尤其 `/sys/kernel/tracing/printk_formats`）的讀取。
4. 檢視 ADB 預設啟用／授權政策與 Android security patch 更新週期。

## 11. 協調請求

- 請 HITCON 先確認本案是否符合台灣地區受理範圍，以及應指派給 Rokid 原廠或台灣銷售／服務窗口。
- 請在平台自動公開前確認 Rokid 已有合理修補時間；若無法延後公開，請先不要要求上傳 exploit source／binary／offset／raw logs。
- 若供應商發布 advisory 或 release note，希望以研究者名稱／alias `[待填]` 確認其在 RV101 上驗證 CVE-2026-43499 可利用。
- 若供應商另確認可獨立修補的 Rokid-specific weakness，再分別評估 CVE 與研究者 attribution。

## 12. 證據對照

| Claim | Evidence | Confidence |
|---|---|---|
| 受測 firmware／kernel | `01_Attachments/A01_Device_Baseline_REDACTED.txt` | CONFIRMED |
| 初始 uid 2000／Enforcing | A01 + `A02_Privilege_Transition_REDACTED.txt` | CONFIRMED |
| uid 0／完整 caps／Permissive | A02 | CONFIRMED |
| system/vendor 維持唯讀 | A02 | CONFIRMED |
| exploit 為 CVE-2026-43499 | 私密 disassembly + raw run logs | STRONGLY SUPPORTED |
| persistence／boot-chain bypass | 無 | UNKNOWN／不主張 |

