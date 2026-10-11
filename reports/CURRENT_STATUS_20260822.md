# 目前狀況快照 — 2026-08-22

> English: [CURRENT_STATUS_20260822_EN.md](CURRENT_STATUS_20260822_EN.md)

> ## ⚠ 2026-09-13 補充（本快照之後的變更）
> 
> 本檔在 2026-08-22 是權威快照，但之後已有以下變化，**現況請以 `docs/deploy.md` 與
> `reports/su_escalation_roi_assessment.md` §8 為準**：
> 1. 引擎選擇由編譯期 `USE_ENGINE` 改為**執行期自動探測**（`AutoGhostlockEngine`＋helper 心跳檔）。
> 2. 新增提權維運層 P0–P3：root daemon keep-alive（9–18 秒自動救回）、`oom_score_adj=-1000`、
>    App 側看守 `RootDaemonKeeper`、手機端 root 狀態／度量顯示。
> 3. 手機端 UI 全面重做（四分頁、可折疊設定、淺色/深色主題）、i18n 擴至 193 key×14 語系。
> 4. 戒指與 ghostlock 訊息改為**結構化代碼**傳給手機（不再傳中文句子）。
> 5. `adb over TCP` 實測**本 build 不支援**（`persist.adb.tcp.port` 無效）。


> 本文件是「當前狀態」的權威快照，取代歷史交接文件中「尚未取得 root」的過時描述。
> 歷史交接（`GHOSTLOCK_HANDOFF_V2~V13`、`FINAL_STATUS_20260202.md` 等）保留為各時點的
> 原始紀錄，不予回溯改寫。

## 一句話結論

**Root 已達成（2026-08-22）**：從 adb shell（uid=2000、SELinux Enforcing）利用
CVE-2026-43499（GhostLock）取得 `uid=0 gid=0`、SELinux Permissive、完整 capabilities
的 root shell。現況已轉向**協調式漏洞揭露**準備。

## 設備 / 韌體

| 項目 | 值 |
|------|-----|
| 型號 | Rokid RV101（`RG-glasses` / `glasses`，代號 `neo`） |
| 序號 | `<DEVICE_SERIAL>` |
| 韌體 / build | `1.22.009-20260710-150201` |
| Fingerprint | `Rokid/glasses/glasses:12/SKQ1.240613.001/1.22.009-20260710-150201:user/release-keys` |
| Android | 12（SDK 32，user build） |
| Kernel | `5.10.209-perf-gd908d71a0404-ab1.22.009-20260710-150201` |
| 安全補丁 | `2024-07-05` |
| Bootloader | 已解鎖（`orange` / `unlocked`）——**環境條件，非提權必要條件（待確認）** |

## 已確認結果（證據）

| 階段 | 觀察 | 證據 |
|------|------|------|
| 前 | `uid=2000(shell) … context=u:r:shell:s0`、`getenforce=Enforcing` | `assets/RV101_Validation/…/17_selinux_runtime.txt`、run log |
| 後 | `uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0` | `run_w2_v960_143215.log`（frozen：`RV101_Disclosure/confidential/raw_evidence/`） |
| 後 | `getenforce=Permissive`、`CapPrm/CapEff/CapBnd=000001ffffffffff` | 同上 |
| 非破壞示範 | 建立 `/data/root_proof`（root:root）、讀 `/data/adb`（0700 root） | 同上 |
| 分區狀態 | `system/system_ext/vendor` 仍 `ro`（dm-verity 未繞過） | mount 輸出 |

> 主證據 SHA-256（frozen snapshot）：`1b1953ee07c5f45222900a0fe4773ec27e263aecfbe5aafe879d2f352c4ef89e`
> 對應 `reports/ghostlock_runlogs/run_w2_v960_143215.log` 的 14:49 凍結副本。

## 目前焦點（協調式揭露）

揭露套件已建立於 `RV101_Disclosure/`：

- `RV101_Vulnerability_Report.md` — vendor 主報告（敏感細節移入 confidential appendix）
- `11_Vendor_Communication/Rokid_Initial_Disclosure_Email.md` — 初始通報（最小必要資訊）+ 6 份 follow-up
- `CVE_Relationship_Assessment.md` — 既有 CVE-2026-43499 vs 獨立可修弱點（RS-1…RS-7）評估
- `Evidence_Checklist.md` / `Evidence_Index.md` / `SHA256SUMS.txt` — 證據與 hash
- `confidential/` — exploit source / offsets / 原始證據（限閱）

## 待補證據（會影響 CVSS 與前置條件結論）

1. bootloader 由**誰**解鎖（研究者 vs 出廠）——決定其為產品觀察或測試 artifact。
2. uid-2000 shell 的**取得來源**（研究者自備 ADB key vs 預載 key）。
3. app 情境可行性（`untrusted_app` 是否可達相同 primitive）。
4. pre-exploit 的 `Cap*` 快照（對稱於 post-exploit）。
5. 穩定的最終 `run_w2_v960_143215.log` 重新凍結（原檔仍在被附加上）。

## 下一步（依優先序）

1. 補齊上述待補證據。
2. 發送初始通報至 security@rokid.com。
3. 依 vendor 回應走協調式揭露時程（90 天，可協商）。
