# 漏洞檢測驗證索引

> English: [VULN_VERIFICATION_INDEX_EN.md](VULN_VERIFICATION_INDEX_EN.md)

> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


> ## ⚠ 2026-09-13 複查結果（本索引開頭原稱「已確認檔案存在」，**不成立**）
>
> 以 `rglob` 逐一核對後：索引提到的 70 個名稱中 **26 個不在工作區**，分兩類：
>
> | 類別 | 數量 | 說明 |
> |------|------|------|
> | A. 只有**二進位**缺（`.c` 原始碼仍在） | 16 | `adsprpc_test`、`ashmem_probe`、`binder_bpf_exploit`、`binder_probe`、`bpf_kaslr_exploit`、`cve_2023_2163_probe`、`dirtypipe_test`、`io_uring_*`(5)、`kgsl_bpf_spray`、`kgsl_test`、`msm_drm_probe`、`tipc_probe` → **編譯產物本來就不納版控**，需自行以 NDK 重建 |
> | B. **原始碼也缺** | 10 | `ashmem_overflow`、`binder_uaf_v2(.c)`、`cve40094_test`、`cve_2024_36971_v2(.c)`、`cve_22555_check`、`cve_scanner`、`kgsl_exploit_v7(.c)` → 這些是 2026-01 當時的實驗檔，**未保留**；本文僅為歷史索引 |
>
> 使用本索引前請先確認對應檔案是否存在（或以 `source_code/` 內的 `.c` 重新編譯）。

此索引用於整理 HANDOVER_SESSION_20260131.md 內所有「漏洞/攻擊面檢測」對應到的專案檔案。~~已確認檔案存在（根據專案清單）~~ **該「已確認」已於 2026-09-13 複查推翻，見上方註記**。本索引不包含實際執行步驟。

## 1) 內核/系統基礎檢測

- 核心/系統資訊收集
  - 參考：HANDOVER_SESSION_20260131.md
- 5.10.x 漏洞掃描器
  - 來源：cve_5_10_x_scanner.c
  - 二進位：cve_scanner

## 2) BPF 相關

- BPF 功能/漏洞探測
  - 來源：bpf_vuln_probe.c
  - 來源：bpf_check.c
  - 來源：bpf_research.c

- BPF KASLR 研究/測試
  - 來源：bpf_kaslr_exploit.c
  - 二進位：bpf_kaslr_exploit

## 3) io_uring 相關 (CVE-2024-0582)

- io_uring 基礎功能/能力探測
  - 來源：io_uring_probe.c
  - 二進位：io_uring_probe
  - 來源：io_uring_capability_probe.c
  - 來源：io_uring_functional_test.c
  - 二進位：io_uring_functional_test

- CVE-2024-0582 檢測/驗證
  - 來源：cve_2024_0582_uaf_check.c
  - 來源：cve_2024_0582_mmap_uaf.c
  - 來源：io_uring_lifecycle.c
  - 二進位：io_uring_lifecycle
  - 來源：io_uring_uaf_verify.c / io_uring_uaf_verify_v2.c / io_uring_uaf_verify_v3.c / io_uring_uaf_verify_v4.c
  - 二進位：io_uring_uaf_verify / io_uring_uaf_verify_v2 / io_uring_uaf_verify_v3 / io_uring_uaf_verify_v4
  - 來源：io_uring_uaf_crash.c
  - 二進位：io_uring_uaf_crash

## 4) Net/Route (CVE-2024-36971)

- PoC/測試版本
  - 來源：cve_2024_36971_poc.c
  - 來源：cve_2024_36971_test.c
  - 來源：cve_2024_36971_bpf_spray.c
  - 來源：cve_2024_36971_v2.c ~ cve_2024_36971_v13.c
  - 二進位：cve_2024_36971_v2 / v3 / v4 / v5 / v6 / v9 / v10 / v11 / v12 / v13 (對應檔名)
  - 其他：cve_36971_v9, cve_36971_v13 (已編譯檔)

## 5) KGSL GPU (CVE-2024-38401)

- 探測/大小/競態
  - 來源：kgsl_test.c
  - 來源：kgsl_size_check.c
  - 來源：kgsl_race.c
  - 二進位：kgsl_test / kgsl_size_check / kgsl_race

- BPF/多策略噴射版本
  - 來源：kgsl_bpf_spray.c, kgsl_bpf_spray_v3.c, kgsl_bpf_spray_v4.c, kgsl_bpf_spray_v5.c, kgsl_bpf_spray_v6.c
  - 二進位：kgsl_bpf_spray / kgsl_bpf_spray_v3 / kgsl_bpf_spray_v4 / kgsl_bpf_spray_v5 / kgsl_bpf_spray_v6

- 其他噴射策略
  - 來源：kgsl_keyring_spray.c
  - 來源：kgsl_pipe_spray.c
  - 來源：kgsl_msg_spray.c
  - 來源：kgsl_uffd_exploit.c

- 深入版本
  - 來源：kgsl_exploit_v7.c / kgsl_exploit_v8.c / kgsl_exploit_v9.c
  - 二進位：kgsl_exploit_v7 / kgsl_exploit_v8 / kgsl_exploit_v9

## 6) Binder

- 探測
  - 來源：binder_probe.c
  - 二進位：binder_probe

- UAF 嘗試版本
  - 來源：binder_uaf_v2.c / binder_uaf_v3.c / binder_uaf_v4.c / binder_uaf_v5.c
  - 二進位：binder_uaf_v2 / binder_uaf_v3 / binder_uaf_v4 / binder_uaf_v5

- BPF 輔助版本
  - 來源：binder_bpf_exploit.c
  - 二進位：binder_bpf_exploit

## 7) Ashmem

- 基礎探測與競態
  - 來源：ashmem_probe.c
  - 二進位：ashmem_probe

- 整數溢出驗證
  - 來源：ashmem_overflow_exploit.c
  - 二進位：ashmem_overflow

## 8) TIPC

- 探測/嘗試
  - 來源：tipc_probe.c
  - 來源：tipc_exploit.c
  - 二進位：tipc_probe / tipc_exploit

## 9) Dirty Pipe (CVE-2022-0847)

- 測試
  - 來源：dirtypipe_test.c
  - 二進位：dirtypipe_test / dirtypipe_test_arm64

## 10) CVE-2021-22555 (Netfilter)

- 檢測/測試
  - 來源：cve_2021_22555_check.c
  - 來源：cve_2021_22555_test.c
  - 來源：cve-2021-22555_exploit.c
  - 二進位：cve_22555_check / cve_22555_test

## 11) CVE-2023-2163 (BPF Verifier)

- 探測
  - 來源：cve_2023_2163_probe.c
  - 二進位：cve_2023_2163_probe

## 12) CVE-2023-40094

- 測試
  - 來源：cve_2023_40094_test.c
  - 二進位：cve40094_test

## 13) Adsprpc

- 探測/測試
  - 來源：adsprpc_test.c
  - 二進位：adsprpc_test

## 14) MSM DRM

- 探測
  - 來源：msm_drm_probe.c
  - 二進位：msm_drm_probe

---

## 補充

- 其他報告與摘要：
  - CVE_5_10_x_Scan_Report.md
  - Rokid_Attack_Vector_Summary.md
  - Rokid*Glasses*資安漏洞報告.md
  - CVE-2024-36971\_\* 分析報告

## 2026-08-21 新增

- §5.2 表驗證報告：
  - `5.2_VERIFICATION_LIVE_ADB_2026.md` — §5.2 逐列 adb 實機複驗（probe 重跑 + 編譯/上機步驟）
  - `5.2_VERIFICATION_2026.md` — §5.2 韌體二進位層驗證（kernel config / msm_kgsl.ko / dmesg 萃取）
- 改進並實機驗證的工具（現存編譯產物 `build2_*` 位於 `binaries/device_probes/`）：
  - `kgsl_uaf_exploit.c`（改用 GPUOBJ_ALLOC 0x45 路徑）、`bpf_leak_v3.c`、
    `cve_2024_0582_mmap_uaf.c`、`cve_2021_22555_test.c`、`adsprpc_secure_exploit.c`
- 實機 config 存檔：`research/kernel_configs/device_config_1.22.009_live.txt`（自 `/proc/config.gz`）
