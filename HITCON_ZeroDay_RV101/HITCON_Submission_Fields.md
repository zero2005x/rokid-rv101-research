# HITCON ZeroDay 通報欄位草稿

> 依 2026-08-22 登入後通報頁的實際欄位整理。尚未提交。方括號內文字需在送出前處理。

## 標題

```text
{Rokid} RV101 未修補 CVE-2026-43499 可由 ADB shell 提權至 root
```

## 組織

```text
[先請 HITCON 確認：Rokid／鼎榮匯股份有限公司（Rokid 樂奇台灣銷售與服務窗口）]
```

說明：RV101 由 Rokid 原廠維護韌體；台灣官方銷售網站頁尾列出鼎榮匯股份有限公司（統一編號 60711556）。不可暗示鼎榮匯撰寫或引入上游 kernel 漏洞。

## 介紹

```text
在本人所有且已授權測試的 Rokid Glasses RV101 上，已實證既有 Linux kernel 漏洞 CVE-2026-43499 可由 uid 2000 ADB shell 提升為執行期 uid 0、完整有效 capabilities 與 SELinux Permissive。
```

## 類型

```text
Kernel 提權 (Kernel Privilege Escalation)
```

## 風險

```text
高
```

評分備註：kernel.org CNA 所列 CVSS 3.1 為 `7.8 High (AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H)`。本案目前實證入口是已授權的 uid 2000 ADB shell；物理存取、Developer Options、ADB RSA 授權、解鎖 bootloader 是否為必要條件仍未完整分離驗證，請 HITCON 依實際前置條件複核。

## 相關網址

```text
https://www.rokid.com.tw/zh-TW/products/rokid-glasses
https://nvd.nist.gov/vuln/detail/CVE-2026-43499
https://git.kernel.org/stable/c/3bfdc63936dd4773109b7b8c280c0f3b5ae7d349
```

第一個網址僅用來識別在台銷售的受測產品；漏洞位於裝置韌體的 Linux kernel，不是該網頁本身。

## 敘述

以下內容可直接貼入平台；送出前請完成所有 `[待填]`：

---

### 一、授權與測試範圍

本測試僅在本人所有／明確授權的 Rokid Glasses RV101 上進行，未對第三方設備、服務或帳號測試，未取得或保留第三方個資。驗證採非破壞方式；未修改 persistent firmware，未嘗試破壞 Secure Boot、AVB 或 dm-verity。

### 二、受影響產品與版本

- 產品：Rokid Glasses RV101（系統型號 `RG-glasses`；Android device `glasses`）
- 韌體：`1.22.009-20260710-150201`
- Build fingerprint：`Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys`
- Android：12（SDK 32）
- Android security patch：`2024-07-05`
- Kernel：`5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201`
- 測試數量：1 台

### 三、漏洞性質與既有 CVE 關係

本案使用的 kernel root cause 是已指派的 `CVE-2026-43499`，屬 futex PI／rt-mutex `remove_waiter()` 路徑中的 use-after-free（CWE-416）。我不主張這是新的 CVE，也不主張本案是 zero-day。本通報的新增價值是：確認該漏洞可在上述 RV101 韌體轉化為可工作的 uid 0 root shell，而不只是 crash 或 DoS。

RV101 出貨 kernel 的 `remove_waiter()` 經離線 disassembly 比對，未包含對應 stable fix。完整 disassembly 與 exploit engineering 細節可在安全的私密管道提供 HITCON／供應商驗證，但不放在將來可能公開的欄位。

### 四、已實證的權限轉換

BEFORE：

```text
# getenforce
Enforcing

# exploit entry privilege
startup: pid=[REDACTED] uid=2000 euid=2000 gid=2000
```

AFTER：

```text
# whoami
root

# id
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0

# id -Z
u:r:kernel:s0

# getenforce
Permissive

# grep -E '^(Uid|Gid|CapInh|CapPrm|CapEff|CapBnd):' /proc/self/status
Uid:    0       0       0       0
Gid:    0       0       0       0
CapInh: 0000000000000000
CapPrm: 000001ffffffffff
CapEff: 000001ffffffffff
CapBnd: 000001ffffffffff
```

非破壞性證明包括：在 `/data` 建立空白 proof file（root:root），並確認可列出 mode 0700 root-only 的 `/data/adb`。`system_ext`、`vendor` 與 `vendor_dlkm` 在 exploit 後仍為唯讀掛載。

### 五、重現概述

1. 在同版 RV101 上，由已授權主機取得 ADB shell；確認初始 `id` 為 uid 2000、SELinux 為 Enforcing。
2. 將針對該韌體編譯的私密驗證 PoC 放入 `/data/local/tmp` 並由 uid 2000 shell 執行。
3. PoC 觸發 CVE-2026-43499 的 futex PI／rt-mutex stack UAF，經 controlled kernel-memory modification 關閉 SELinux enforcement 並修改目前程序 credentials。
4. 在新產生的 shell 執行 `whoami`、`id`、`id -Z`、`getenforce` 與 `/proc/self/status` capability 檢查，得到上列 AFTER 輸出。
5. 執行非破壞 proof file 與 read-only mount 檢查後停止測試。

完整 PoC ELF、source、offset、KASLR 與 kernel 位址不附在初次平台通報；如 HITCON 審核或供應商重現需要，我願在確認不會自動公開且有適當私密傳輸方式後提供。最新 PoC 在已測韌體上約每次 boot 50% 成功，失敗時可能需重新開機後再試；此可靠度數字來自有限次研究 run，不代表統計保證。

### 六、影響

已確認攻擊程序在執行期取得 uid 0、完整有效 Linux capabilities 且 SELinux 變為 Permissive，因而跨越 Android shell 與 kernel／root 安全邊界。這足以危及執行中系統內資料機密性與完整性。

目前沒有證據支持跨重開機 persistence、Secure Boot／AVB／dm-verity bypass、system/vendor 可寫、遠端利用、任意 Android App 可直接利用，或所有 RV101 韌體均受影響。

### 七、攻擊前置條件

已實證入口為 uid 2000 ADB shell。測試裝置當時 USB debugging／ADB 已啟用，bootloader 狀態為 unlocked／orange；這些是測試環境觀察，不等於已證明都是 CVE 的必要條件。是否可由一般 App context 到達、是否必須有實體存取／既有 ADB RSA 授權、bootloader 解鎖是否必要，仍待分離驗證。

### 八、希望協調的事項

1. 請確認 RV101 與哪些韌體版本受 CVE-2026-43499 影響。
2. 請 backport kernel.org stable fix 並提供修復韌體版本與時程。
3. 請確認台灣銷售／服務窗口與 Rokid 原廠之間的修補協調窗口。
4. 請將本案定位為既有 CVE 的 RV101 affected-product／exploit validation；如另確認產品特有、可獨立修補的弱點，再分別評估。
5. 請在未完成修補協調前不要公開 exploit source、binary、offset 或 raw log。

發現／首次完整成功證據日期：2026-08-22（UTC+8）。  
供應商直接通報狀態：[待填：尚未寄送／已於 YYYY-MM-DD 寄送，ticket ...]。

---

## 修補建議

```text
1. 將 CVE-2026-43499 對應的 kernel.org stable 修補 backport 至 RV101 5.10 kernel，並由 OTA 發布。
2. 修補後在同版硬體重跑私密 PoC；預期 UAF trigger 無法再建立 dangling pi_blocked_on／controlled write，且 shell 維持 uid 2000、SELinux Enforcing。
3. 獨立檢查 uid 2000 對 /sys/kernel/tracing/printk_formats 的可讀性；此條件在本 exploit chain 中被用於 KASLR defeat，屬可獨立 harden 的候選弱點，但尚不主張為新的 CVE。
4. 重新檢視 ADB 預設啟用與授權政策，並更新 Android security patch level。
```

## 上傳圖片

目前沒有可安全上傳的原生截圖。建議送出前準備：

1. `IMG#1`：韌體、Android security patch、kernel（序號已遮蔽）。
2. `IMG#2`：BEFORE：uid 2000、`u:r:shell:s0`、Enforcing。
3. `IMG#3`：AFTER：uid 0、`u:r:kernel:s0`、Permissive、完整 capability mask。

不得上傳 raw run log 的截圖，以免洩漏 kernel 位址、offset、主機路徑或裝置序號。

## 匿名通報

```text
[由研究者決定；若希望公開後獲得署名，通常不要勾匿名，並先確認平台顯示的帳號名稱是否合適。]
```

