# PASS 表格（可追溯證據版）

> 用途：把「狀態/結論」改寫成**可驗證、可留存、可追溯**的證據清單。
>
> 設計原則：
>
> - **每一列**都能用「對應工具（repo 檔案）」跑出可保存的 stdout/dmesg/系統狀態。
> - **每一列**都定義最小必要證據（Minimum Evidence Set, MES），避免只靠主觀描述。
>
> 來源參考（原始結論散落處）：
>
> - `HANDOVER_SESSION_20260131.md` 末段「Kernel Exploit 路徑最終結論」
> - `Medium_Rokid_Root_Article_EN.md` 的 Attack Vector 表
> - `VULN_VERIFICATION_INDEX.md` 的「檔案 ↔ 檢測工具」索引
>
> ✅ **2026-08-21 更新**：本表定義的 MES（最小必要證據）已實際採集完成。
> 逐列結果見 `reports/5.2_VERIFICATION_LIVE_ADB_2026.md`（adb 實機）與
> `reports/5.2_VERIFICATION_2026.md`（韌體二進位層），本表 10 列判定全部成立（**15 列在 `5.2_VERIFICATION_LIVE_ADB_2026.md` §1.1–1.15；2026-09-13 更正計數**）。

---

## 共同證據欄位（所有列必填）

把以下輸出貼到每一列的 Evidence 區（或存成檔案附上）：

- 裝置版本：`uname -a`、`uname -r`
- Android patch level：`getprop ro.build.version.security_patch`
- Kernel hardening/開關：
  - `cat /proc/cmdline`
  - `cat /proc/sys/kernel/kptr_restrict`
  - `cat /proc/sys/kernel/unprivileged_bpf_disabled`
  - `cat /proc/sys/kernel/dmesg_restrict`
- 能力/權限：`id`、`cat /proc/self/status | grep Cap`
- SELinux：`getenforce`

> 建議命名：`evidence/common_<YYYYMMDD>_<device>.txt`

---

## 可追溯證據表（逐列）

> 欄位說明：
>
> - **Repo 檔案**：指你的檢測/PoC 來源檔（以及若已編譯，對應二進位）。
> - **要抓的輸出欄位（MES）**：只要蒐集這些，就能支撐/反駁該列結論。
> - **PASS 定義**：把「PASS」從口語理由改成可被 MES 驗證的條件。

| 向量 / CVE                   | 目前結論（從交接/摘要整理）                             | Repo 檔案（主要）                                                                                     | 要抓的輸出欄位（MES）                                                                                                                        | PASS 定義（可驗證）                                                                                                                                         |
| ---------------------------- | ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| io_uring / CVE-2024-0582     | PBUF_RING 不存在（N/A）；另外 provide/remove 路徑需另判 | `io_uring_probe.c`、`cve_2024_0582_mmap_uaf.c`、`cve_2024_0582_uaf_check.c`、`io_uring_uaf_verify*.c` | 1) `REGISTER_PBUF_RING` 回傳 errno（預期 `EINVAL`） 2) ops/features 列表 3) `IORING_OP_REMOVE_BUFFERS` 是否 `EINVAL`                         | 若 `REGISTER_PBUF_RING == EINVAL` 且程式明確確認為「功能不支援」→ **CVE-2024-0582(PBUF_RING+mmap) 不成立**；provide/remove 相關測試需獨立成另一列           |
| BPF Verifier / CVE-2023-2163 | 已修補                                                  | `cve_2023_2163_probe.c`（bin: `cve_2023_2163_probe`）                                                 | 1) 每個 Probe 的結果（Patched/Vulnerable） 2) verifier reject/accept 的關鍵行                                                                | 若所有 probes 都顯示 Patched（或 verifier reject 導致無法構造 unsafe path）→ **PASS=已修補/不可利用**                                                       |
| Netfilter / CVE-2021-22555   | 無法利用（缺 SysV IPC / User NS / 權限不足）            | `cve_2021_22555_check.c`、`cve_2021_22555_test.c`、（參考）`cve-2021-22555_exploit.c`                 | 1) `CONFIG_USER_NS`/`CONFIG_SYSVIPC` 檢查結果 2) `setsockopt(IPT_SO_SET_REPLACE)` errno（常見 `EPERM`） 3) `lsmod`/x_tables 模組狀態（若可） | 若 `CONFIG_USER_NS` 關閉或無法取得必要 capabilities 且 `IPT_SO_SET_REPLACE` 路徑不可達 → **PASS=不可利用（在當前權限模型下）**                              |
| Dirty Pipe / CVE-2022-0847   | 已修補                                                  | `dirtypipe_test.c`（bin: `dirtypipe_test`）                                                           | 1) 測試檔案路徑與 hash（前/後） 2) 測試程式 stdout（是否成功覆寫）                                                                           | 若測試確認「無法覆寫只讀檔」（hash 不變 + 程式顯示 not vulnerable）→ **PASS=已修補**                                                                        |
| Route cache / CVE-2024-36971 | PoC 未能觸發；受環境限制（loopback/dummy0）             | `cve_2024_36971_test.c`、`cve_2024_36971_v*.c`、`cve_2024_36971_bpf_spray.c`                          | 1) 活躍網卡（`ip link`/`ip addr`） 2) PoC 嘗試次數/策略版本 3) 是否出現 crash/dmesg 異常 4) spray 成功統計                                   | 若環境證據顯示無法產生「觸發 obsolete 所需條件」且多版 PoC 長時間跑不出 UAF 指標 → **PASS=在此環境下不可觸發（不等於已修補）**                              |
| KGSL / CVE-2024-38401        | 未能觸發；推測已修補/受 hardening 影響                  | `kgsl_race.c`、`kgsl_bpf_spray*.c`、`CVE-2024-38401_Test_Report.md`                                   | 1) `/dev/kgsl-3d0` open 是否可達 2) ioctl race 執行結果 3) dmesg 是否有 GPU fault / kernel oops 4) 報告中的測試版本/輪次                     | 若多策略 race + spray 皆無 crash/異常且 driver/patch 訊息支持已修補 → **PASS=未證實可利用（需明確標示為“未觸發”而非“已修補”）**                             |
| Binder（UAF/race 類）        | 大量測試無 crash；有 DoS/txn errors 但無提權            | `binder_probe.c`、`binder_uaf_v5.c`（**v2–v4 未留存**）、`binder_bpf_exploit.c`                                    | 1) `/dev/binder` 可達性 2) 每版本嘗試次數與結果 3) dmesg binder 錯誤摘要 4) 是否出現 kernel oops                                             | 若在高強度 race 下仍無 UAF 指標（無 oops/無 crash/無權限變化）→ **PASS=未證實可利用（且需註明：非針對單一 CVE）**                                           |
| Ashmem（overflow/size）      | 需以實機行為佐證（程式碼本身是 harness）                | `ashmem_overflow_exploit.c`、`ashmem_probe.c`                                                         | 1) `/dev/ashmem` 是否存在/權限 2) 關鍵測試 case 的 errno（例如 set_size 超大值） 3) dmesg 是否異常                                           | 若所有危險 size case 均被拒絕（errno 明確）且無 kernel oops → **PASS=未觀察到可利用行為（需附 case→errno 對照）**                                           |
| Adsprpc                      | 主要是可達性/權限面（SELinux/節點權限）                 | `adsprpc_test.c`                                                                                      | 1) `/dev/adsprpc-smd` open 結果與 errno 2) 節點權限/SELinux context（`ls -lZ`） 3) ioctl 返回 errno                                          | 若裝置節點不可達或 ioctl 被權限/SELinux 阻擋且證據齊全 → **PASS=攻擊面受控（不是 driver 無洞）**                                                            |
| TIPC                         | 需先釐清是否啟用/可達                                   | `tipc_probe.c`、`tipc_exploit.c`                                                                      | 1) `socket(AF_TIPC, ...)` 結果/errno 2) 是否載入 tipc（/proc/modules） 3) generic netlink 查 family id 是否成功                              | 若 AF_TIPC 直接 `EAFNOSUPPORT` 或 tipc 未載入且無法自動載入 → **PASS=攻擊面不存在**；若 socket 可建立則 PASS 不能寫“未啟用”，需改成“已啟用但未找到可利用點” |

---

## 每列的建議證據檔案命名（方便交付/審計）

- io*uring：`evidence/io_uring_probe*<date>.txt`、`evidence/cve*2024_0582_mmap_uaf*<date>.txt`
- bpf：`evidence/cve_2023_2163_probe_<date>.txt`
- netfilter：`evidence/cve_2021_22555_check_<date>.txt`、`evidence/cve_2021_22555_test_<date>.txt`
- dirtypipe：`evidence/dirtypipe_test_<date>.txt` + `evidence/dirtypipe_targetfile_hash_<date>.txt`
- route：`evidence/cve_2024_36971_v9_<date>.txt` + `evidence/net_ifaces_<date>.txt`
- kgsl：`evidence/kgsl_race_<date>.txt` + `evidence/kgsl_dmesg_<date>.txt`
- binder：`evidence/binder_uaf_v5_<date>.txt` + `evidence/binder_dmesg_<date>.txt`
- ashmem：`evidence/ashmem_overflow_<date>.txt` + `evidence/ashmem_dmesg_<date>.txt`
- adsprpc：`evidence/adsprpc_test_<date>.txt` + `evidence/adsprpc_ls_lZ_<date>.txt`
- tipc：`evidence/tipc_probe_<date>.txt` + `evidence/tipc_modules_<date>.txt`

---

## 備註：如何避免“PASS 理由”與“實作路徑”不一致

- **同名 CVE、不同路徑要拆列**：例如 io_uring 的 PBUF_RING+mmap（CVE-2024-0582 描述） vs provide/remove buffers（另一攻擊面）。
- **“未觸發”≠“已修補”**：競態/heap spray 類 PoC（route cache、KGSL、binder）若沒有版本/patch 證據，只能先寫“未觸發/未證實”。
- **“攻擊面受控”要附權限證據**：如 adsprpc、TIPC，至少需要節點權限/SELinux context 或 socket errno 來支撐結論。
