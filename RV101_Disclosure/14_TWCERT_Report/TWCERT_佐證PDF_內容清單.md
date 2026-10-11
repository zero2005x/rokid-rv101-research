# TWCERT 上傳用「去識別化佐證 PDF」內容清單

> 目的：整理出一份**可直接上傳 TWCERT/CC CVE 通報表單**的佐證 PDF（或 ZIP）。
> 對應表單欄位：「4. 佐證資料上傳」——**檔案限 10MB 以下，格式限 PDF、ZIP**。
> 本清單與 `Evidence_Index.md`、`Evidence_Checklist.md`、`Redaction_Checklist.md` 一致，
> 但比後者更嚴格：**只放「可安全公開/送審」的去識別化內容**，且移除裝置序號與分區佈局。

---

## 0. 目標與限制

| 項目 | 值 |
|---|---|
| 產物 | 單一 PDF（文字型，體積 < 1MB，遠低於 10MB 上限） |
| 建議檔名 | `Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf` |
| 語系 | 繁中章節標題 + 說明；命令輸出**逐字保留英文原文** |
| 定調 | 既有 CVE-2026-43499 之「受影響產品確認 + 可利用性驗證」；**非新 CVE、非 zero-day、非遠端** |

> 若不想產生 PDF，亦可把下列各節整理成單一 Markdown/HTML 後由工具輸出；
> 本機已有 `weasyprint` 與 `chromium` 可離線產生（見 §6 產出方式）。

---

## 1. PDF 章節內容清單（依序）

### 封面／聲明頁
- 標題：`Rokid Glasses RV101 — CVE-2026-43499 受影響產品佐證（去識別化版）`
- 一句話聲明：「本文件為既有 CVE-2026-43499（GhostLock）在 Rokid Glasses RV101 上之
  受影響產品確認與本機提權可利用性驗證；不主張新 CVE、zero-day 或遠端利用。」
- 通報者：〔姓名/alias〕；是否公開：否
- 日期：2026-08-25
- 佐證對應：表單欄位 3（漏洞描述/條件/方法/CVSS/CWE）之實證附件。

### §1 受測產品識別
- 來源：`02_Device_Information/02_identity_build.txt`（E1）、`03_security_baseline.txt`（E2）、`04_kernel_runtime.txt`（E3）
- 保留逐字輸出：
  - `ro.product.manufacturer=Rokid`、`ro.product.model=RG-glasses`、`ro.product.device=glasses`、`ro.board.platform=neo`
  - `ro.build.version.release=12`、`ro.build.version.sdk=32`、`ro.build.id=SKQ1.240613.001`
  - `ro.build.fingerprint=Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys`
  - `ro.build.type=user`、`ro.build.tags=release-keys`
  - `uname`/`/proc/version`：`5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201 … aarch64`
- 去識別化：**不列裝置序號**；刪除 `__collector_last_exit=*` 行。
- 佐證強度：CONFIRMED。

### §2 安全基線（exploit 前環境）
- 來源：`03_Pre_Exploit_State/pre_exploit_security_baseline.txt`
- 保留逐字輸出：
  - `getenforce` → `Enforcing`
  - `ro.build.version.security_patch=2024-07-05`（build + vendor）
  - `ro.debuggable=0`、`ro.secure=1`、`ro.adb.secure=1`
  - `ro.boot.veritymode=enforcing`、`ro.boot.avb_version=1.2`、`ro.boot.selinux=enforcing`、`selinux_enforce=1`
  - `ro.boot.verifiedbootstate=orange`、`ro.boot.vbmeta.device_state=unlocked`、`ro.boot.flash.locked=0`
    （**加註**：bootloader 解鎖為「測試環境之環境性條件」，未證明為 CVE-2026-43499 的必要前置條件）
  - `persist.sys.usb.config=adb,diag`（加註：uid-2000 shell 之取得途徑）
- 去識別化：刪除 `__collector_last_exit=*`。

### §3 起始權限狀態（exploit 前 uid/SELinux）
- 來源：`03_Pre_Exploit_State/pre_exploit_uid_selinux.txt`
- 保留逐字輸出（核心證明之一）：
  - `uid=2000(shell) gid=2000(shell) groups=…,3012(readtracefs) context=u:r:shell:s0`
    （`3012(readtracefs)` 即 RS-1 KASLR 洩漏之權限來源，**刻意保留**以支持附帶發現）
- 去識別化：可刪除 `policy_bytes/policyvers/mls/policy hash` 行（與本案無關）；刪 `__collector_last_exit=*`。

### §4 漏洞根因與 CVE 對照（版本比對證明）
- 來源：`10_Upstream_CVE_Comparison/advisory_nvd_osv_record_20260824.md`、`CVE_Relationship_Assessment.md`
- 內容（全為公開資訊，無需去識別化）：
  - CVE-2026-43499 摘要：`rtmutex: Use waiter::task instead of current in remove_waiter()`
  - 根因一句話：remove_waiter() 於 proxy-lock rollback 誤操作 `current`，waiter task 之 `pi_blocked_on` 未清除 → 懸空指標 UAF
  - 影響範圍：introduced 2.6.39；5.10.x 分支**修於 5.10.261**
  - 受測 kernel `5.10.209-perf` < `5.10.261` → **受影響**（版本比對即成立）
  - 修補 commit：`3bfdc63936dd`；CWE-416；CNA CVSS 3.1 `AV:L/AC:L/PR:L/UI:N/S:U/C:H/I:H/A:H`（7.8 High）
  - 上游參考：Red Hat RHSB-2026-010、AlmaLinux、CloudLinux、NVD 連結

### §5 提權成功狀態（exploit 後，核心證明）
- 來源：`05_Post_Exploit_State/post_exploit_state_REDACTED.txt`（E9，已去識別化）
- 保留逐字輸出：
  - BEFORE：`getenforce` → `Enforcing`；`startup: pid=… uid=2000 euid=2000 gid=2000`
  - AFTER：`whoami` → `root`
  - `id` → `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0`
  - `id -Z` → `u:r:kernel:s0`
  - `getenforce` → `Permissive`
  - `/proc/self/status`：`Uid: 0 0 0 0`、`Gid: 0 0 0 0`、`CapPrm/CapEff/CapBnd = 000001ffffffffff`（完整有效 capabilities）
  - 非破壞性驗證：建立 `/data/root_proof`（root:root）、讀取 root-only `/data/adb`（0700）
  - 掛載狀態（**僅保留結論句**）：`system_ext`/`vendor`/`vendor_dlkm` 仍為 `ro`（dm-verity）——未因 root 變可寫
- 去識別化（**此檔案尚需再處理**）：
  - ❌ 刪除檔頭第 2 行與檔尾的**裝置序號 `<DEVICE_SERIAL>`**（公開版不放）
  - ❌ 刪除 `ls -l /dev/block/by-name` 區塊與 `mmcblk0p51/p76/p80`、`dm-13/15/16` 等**分區佈局**（Redaction_Checklist 明訂保留於 raw/confidential）
  - ❌ 刪除 `/mnt/vendor/persist|qmcs|logfs` 的可寫掛載列舉（同屬分區佈局）

### §6（可選）附帶發現摘要 RS-1~RS-9
- 來源：`RV101_Vulnerability_Report.md` §Potential Rokid-Specific Security Issues
- 內容：以表格列出 RS-1~RS-9 的**一句話描述 + 狀態**，全部標註「需原廠獨立評估，不主張新 CVE」。
- 特別標示：RS-8（相機使用中隱私指示燈控制鏈全程 REE，root 下可經 sysfs/Binder/session_open 三路徑壓制）
  與 RS-9（穿戴/接近態可偽造）——**均為「取得 root 之後」或 defense-in-depth 議題**。
- 去識別化：不附 `led_test.log`/`pathd_test.log` 原文（雖然其本身無位址，但含 root 注入腳本路徑與
  `/data/local/tmp/.ghostlock_root.sh` 名稱，屬 exploit 內部細節）；僅以「已實機驗證」文字描述。

### §7 完整性與雜湊聲明
- 來源：`12_Hashes/SHA256SUMS.txt`
- 內容：一頁表格，將 PDF 各節對應到原始證據檔名 + SHA-256（前 12 字元即可），供審核方事後對照。
- 附註：raw evidence 之完整雜湊留於私密版；公開 PDF 僅列去識別化後的對照表。

---

## 2. 必須移除清單（送審前逐項檢查）

| # | 項目 | 出現在哪 |
|---|---|---|
| R1 | 裝置序號 `<DEVICE_SERIAL>` | post_exploit_state_REDACTED.txt 檔頭/檔尾、00_adb_devices.txt |
| R2 | 核心虛擬位址 / KASLR base-slide / `init_cred` / `selinux_state` / fops / `0xffff…` | raw run log（**不進 PDF**） |
| R3 | exploit 原始碼、ELF（ghostlock_rv101）、`target_rv101.h` offsets、reclaim/`pselect`/`rb_erase` punch 參數 | 一律**不進 PDF** |
| R4 | 揭示上述細節的完整重現命令（`.ghostlock_root.sh` 注入流程、FOPS 窗口） | 一律**不進 PDF** |
| R5 | 分區佈局：`/dev/block/by-name`、`mmcblk0pN`、`dm-N`、`/mnt/vendor/persist|qmcs|logfs` | post_exploit_state_REDACTED.txt §MOUNT |
| R6 | 研究主機路徑 / Windows 使用者名（`C:\Users\<USER>\…`） | 操作說明文件（**不進 PDF**） |
| R7 | 第三方金鑰/密鑰（OTA keys、ADB keys，若任何 log 出現） | 全數排除 |
| R8 | 完整 partition dump / `ls -l /dev/block/by-name` | 全數排除 |

## 3. 可安全保留清單

- `id` / `id -Z` / `getenforce` / `Uid|Gid|Cap*` 行（核心證明）
- 韌體 fingerprint、Android 版本、kernel 版本字串、security patch level
- 「system/vendor 維持唯讀」結論句
- `/data/root_proof` 非破壞性演示
- CVE-2026-43499 與受影響/未修補版本對照
- `uid=2000 … context=u:r:shell:s0`（含 `readtracefs` gid，支持 RS-1）

---

## 4. 送審前驗證步驟（產生 PDF 後執行）

```bash
# 1) 確認無敏感內容（在 PDF 抽取出的文字上跑）
pdftotext Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf - 2>/dev/null | \
  grep -nE '<DEVICE_SERIAL>|0xffff|slide=|init_cred|target_rv101|C:\\\\Users|mmcblk0p|dm-1[0-9]|/dev/block/by-name|ghostlock_root' \
  && echo "!! 發現需再移除的內容" || echo "OK：無敏感內容"

# 2) 確認檔案大小與格式
ls -l Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf   # 應 < 1MB
file  Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf   # PDF document

# 3) 對照原始證據雜湊（非必要但建議）
sha256sum -c 12_Hashes/SHA256SUMS.txt 2>&1 | tail -1
```

---

## 5. 產出方式（本機可離線完成）

因本機無 pandoc，但已有 `weasyprint`（或 `chromium --headless`），建議路徑：**Markdown/HTML → PDF**。

```bash
# 方式 A：HTML + weasyprint（支援 CJK，字型 Droid Sans Fallback）
#  1. 依 §1 把各節整理成 evidence.html（命令輸出包在 <pre> 內）
#  2. weasyprint evidence.html Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf

# 方式 B：HTML + Chromium headless
#  chromium --headless --no-sandbox --print-to-pdf=out.pdf --no-pdf-header-footer evidence.html
```

> 命令輸出的對齊（tab、空格）務必用等寬字型（`<pre>` + `font-family: monospace`），
> 以保持 `/proc/self/status` 的 `Uid:\t0\t0\t0\t0` 逐字原樣。

---

## 6. 一頁式對照（證據檔 → PDF 章節 → 去識別化動作）

| 證據檔（Evidence_Index ID） | 進 PDF 章節 | 去識別化動作 |
|---|---|---|
| 02_identity_build.txt（E1） | §1 | 去序號、去 `__collector_last_exit` |
| 03_security_baseline.txt（E2） | §2 | 去 `__collector_last_exit`；bootloader 解鎖加註「環境性條件」 |
| 04_kernel_runtime.txt（E3） | §1 | 保留 uname/version；可去 `cmdline: Permission denied` |
| pre_exploit_uid_selinux.txt（E4） | §3 | 去 policy hash 行、去 `__collector_last_exit` |
| pre_exploit_security_baseline.txt | §2 | 去 `__collector_last_exit` |
| post_exploit_state_REDACTED.txt（E9） | §5 | **再去序號 + 分區佈局 + 可寫掛載列舉** |
| advisory_nvd_osv_record（D6） | §4 | 無（全公開資訊） |
| RV101_Vulnerability_Report.md RS 表 | §6 | 僅文字摘要，不附 log 原文 |
| 12_Hashes/SHA256SUMS.txt | §7 | 僅列對照表（去序號） |

---

## 7. 送 TWCERT 的最終檢查

- [ ] 檔名 `Rokid_RV101_CVE-2026-43499_Evidence_REDACTED.pdf`，< 10MB，PDF 格式
- [ ] 無序號、無分區佈局、無核心位址、無 exploit 細節、無主機路徑（§2 全過）
- [ ] §1~§5 完整、逐字輸出、可獨立閱讀（審核方不需看 private 資料即能判斷）
- [ ] 明確標註「既有 CVE，非新 CVE／非 zero-day／非遠端」
- [ ] 若 TWCERT 要求 raw PoC，另備私密版 PDF/ZIP，於確認機密處理後私下提供（**不上傳公開表單**）
