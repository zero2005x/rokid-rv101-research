# 漏洞檢測實作稽核（CVE 描述 ↔ Repo 程式對照）

> English: [VULN_IMPLEMENTATION_AUDIT_EN.md](VULN_IMPLEMENTATION_AUDIT_EN.md)

> 目標：確認 repo 中各「檢測/PoC」是否**依照權威 CVE 描述的必要條件與觸發鏈**來實作，並能否支持你表格中的「PASS」結論。
>
> 來源真值：以 NVD / cve.org 的 CVE Record **Description** 為主（必要條件、典型觸發鏈、受影響版本區間）。
>
> 注意：本稽核只檢查「程式碼路徑是否對上 CVE 描述」，**不等同於實際在裝置上是否可觸發**（那需要執行輸出、dmesg、版本/補丁證據）。

## 快速結論（高優先差異點）

1. **CVE-2024-0582（io_uring）**
   - `cve_2024_0582_mmap_uaf.c`：觸發鏈與 CVE 描述高度一致（PBUF_RING + mmap + unregister/free）。
   - `cve_2024_0582_uaf_check.c`：自稱 verifier 但主線走 `PROVIDE_BUFFERS/REMOVE_BUFFERS`，與 CVE 描述的 PBUF_RING 觸發點**不等價/需另外證明關聯**。

2. **TIPC（攻擊面）**
   - 你的表格若寫「AF_TIPC 未啟用/未編譯」，但 repo 同時存在 `tipc_probe.c`（能力探測）與 `tipc_exploit.c`（明確以 TIPC 子系統漏洞為目標）。
   - 這代表「結論」很可能仰賴**實際執行結果**（例如 socket 建立、/proc/config.gz、模組載入狀態），僅從 repo 程式碼本身無法支撐「未啟用」。

3. **Ashmem（整數溢出/尺寸處理）**
   - `ashmem_overflow_exploit.c` 是偏「魯棒性/異常值測試」的 harness；它不會單靠程式碼就給出“已嚴格校驗所以 PASS”。
   - PASS 需要你補充：實機跑出來的 errno / 行為、以及對應 kernel/driver 的版本或 commit/patch 證據。

---

## 1) io_uring — CVE-2024-0582

**權威描述核心（NVD/cve.org 摘要）**

- 關鍵觸發鏈：`IORING_REGISTER_PBUF_RING` 註冊 → `mmap` 對應區域 → free/unregister（或 close/unregister）導致 UAF/記憶體問題。

**Repo 對照**

- ✅ 高度吻合：
  - `cve_2024_0582_mmap_uaf.c`
    - 具體使用 `IORING_REGISTER_PBUF_RING` 並進行 `IORING_OFF_PBUF_RING` mmap。
    - 之後做 unregister/close 等行為，符合「mmap still active + free/unregister」的測試方向。
- ⚠️ 可能不吻合（需重新命名或補充說明用途）：
  - `cve_2024_0582_uaf_check.c`
    - 主路徑以 `IORING_OP_PROVIDE_BUFFERS` / `IORING_OP_REMOVE_BUFFERS` 搭配 heap spray/race。
    - 這可能是另一路徑的 UAF/race 檢測，但**不等於 CVE-2024-0582 描述的 PBUF_RING + mmap 觸發鏈**。

**稽核結論**

- `cve_2024_0582_mmap_uaf.c`：可視為「依描述實作」。
- `cve_2024_0582_uaf_check.c`：建議在報告/表格中拆成「io_uring provide/remove buffers race」而非直接當 CVE-2024-0582 verifier。

---

## 2) BPF Verifier — CVE-2023-2163

**權威描述核心（NVD/cve.org 摘要）**

- 類型：BPF verifier 的 pruning/狀態推導錯誤，造成不安全路徑被當作安全，可能導致越界/任意讀寫。

**Repo 對照**

- ✅ 高度吻合：
  - `cve_2023_2163_probe.c`
    - 以多個 probe 組合測試 verifier 是否錯誤接受可疑的 bounds/分支狀態。
    - 輸出邏輯採「若 verifier 接受 → 判為 vulnerable」的典型探測模式。

**稽核結論**

- 依描述實作程度高；PASS/FAIL 的可信度主要取決於：
  - 目標 kernel 是否回報 verifier reject/accept 的行為（執行輸出）。

---

## 3) Netfilter x_tables — CVE-2021-22555

**權威描述核心（NVD/cve.org 摘要）**

- 類型：`net/netfilter/x_tables.c` heap out-of-bounds write。
- 常見利用前提：需要能操作 iptables/setsockopt 路徑；很多情境會透過 user namespace + capabilities（取決於 distro/設定）。

**Repo 對照**

- ✅ 合理的「可達性/前提檢查」：
  - `cve_2021_22555_check.c`：檢查 `CONFIG_USER_NS`、netfilter 相關 config/條件，符合“先判 exploitability”思路。
  - `cve_2021_22555_test.c`：用 `setsockopt(IPT_SO_SET_REPLACE)` 觀察 `EPERM/ENOPROTOOPT/EINVAL`，可用於判斷介面是否可達。
- ⚠️ 參考用 exploit（不一定適用 Android/ARM64）：
  - `cve-2021-22555_exploit.c`：完整 exploit 來源碼（註解中偏向特定 kernel/distro），不代表在 Rokid 裝置上可直接使用。

**稽核結論**

- 檢測/前提確認的部分與 CVE 描述一致。
- 若你表格 PASS 的理由是「CAP/USER_NS 被禁用所以不可利用」，需要附上：
  - 實機 `CONFIG_USER_NS` 狀態證據、capabilities 狀態（執行輸出）。

---

## 4) Dirty Pipe — CVE-2022-0847

**權威描述核心（NVD/cve.org 摘要）**

- 類型：pipe buffer flags 未正確初始化，導致可改寫 page cache（通常可覆寫只讀檔）。

**Repo 對照**

- ✅ 高度吻合：
  - `dirtypipe_test.c`：典型流程（準備 pipe → splice → write）對只讀檔案嘗試覆寫。

**稽核結論**

- 依描述實作程度高。
- PASS/FAIL 要看實機檔案是否真的被修改（以及 kernel 版本是否在受影響區間）。

---

## 5) Net/Route（dst cache / UDP）— CVE-2024-36971

**權威描述核心（NVD/cve.org 摘要）**

- 類型：`__dst_negative_advice` 的 RCU 清理/釋放順序問題導致 UAF；描述中提到 UDP sockets 讓舊 bug 再度可達。

**Repo 對照**

- ✅ 部分吻合（方向合理，但屬於高不確定性的競態 PoC）：
  - `cve_2024_36971_v13.c`（**v9 等未留存，2026-09-13 更正**）：UDP + ICMP error + spray/race 的整體方向與描述相符。
  - `cve_2024_36971_test.c`：偏版本/patch 判斷式的檢查工具。

**稽核結論**

- 方向與描述相符，但是否「已修」或「不受影響」通常應以：
  - 裝置實際 kernel commit/Android patch level、以及跑 PoC 的觀察結果佐證。

---

## 6) KGSL GPU — CVE-2024-38401

**權威描述核心（NVD/cve.org 摘要）**

- 類型：Qualcomm KGSL driver 的 concurrent IOCTL calls 引發 memory corruption。

**Repo 對照**

- ✅ 合理吻合（以 race + ioctl 為核心）：
  - `kgsl_race.c`：併發 ioctl 測試。
  - `kgsl_bpf_spray*.c`：加入 heap spray 以提高可觀測性/命中率。
  - `CVE-2024-38401_Test_Report.md`：有彙整測試結論（未觸發）。

**稽核結論**

- “依描述實作”的方向正確；但 PASS/FAIL 仍主要依賴實機 race 是否可觸發，以及 driver 版本/補丁。

---

## 7) Binder（多版本 UAF/race）

**權威描述核心**

- 這一欄通常不是單一 CVE，而是「binder 子系統歷史上常見的 UAF/race 攻擊面」。

**Repo 對照**

- ✅ 作為“壓力測試/競態嘗試”合理：
  - `binder_probe.c`：能力探測。
  - `binder_uaf_v5.c`（**v2–v4 未留存，2026-09-13 更正**）：多種 race 觸發模式 + spray。

**稽核結論**

- 這些程式更像「探索性 PoC/harness」，不適合直接用來說明“某個特定 CVE 已修補”。
- 若你的表格 PASS 是針對特定 binder CVE，建議：
  - 明確標示 CVE 編號，並檢查程式是否真的覆蓋該 CVE 的 trigger chain。

---

## 8) Ashmem

**權威描述核心**

- 你表格若描述的是 “ashmem mmap/size 驗證導致 overflow 不可行”，那屬於「行為/防護結論」。

**Repo 對照**

- ⚠️ 目前程式碼本身無法直接證明 PASS：
  - `ashmem_overflow_exploit.c`：偏向把大量 edge cases 跑一遍，並提示使用者自行看結果（errno/dmesg）。

**稽核結論**

- 若要支持 PASS：需要附上實機跑 `ashmem_overflow_exploit.c` 的輸出、以及對應 driver 行為。

---

## 9) Adsprpc

**權威描述核心**

- adsprpc 的安全性通常與裝置 node 權限、SELinux policy、以及具體 ioctl/driver bug 相關。

**Repo 對照**

- ⚠️ `adsprpc_test.c` 主要是 “open + ioctl reachability” 探測。

**稽核結論**

- 若表格 PASS 的理由是 “權限/SELinux 受控所以不可達”，需要執行輸出/權限證據支持；程式碼本身不會替你做結論。

---

## 10) TIPC

**權威描述核心**

- TIPC 的攻擊面取決於：是否編譯/是否載入/是否允許 AF_TIPC socket、以及 netlink 控制介面是否可達。

**Repo 對照**

- ✅ 探測工具：`tipc_probe.c`
- ✅ 以 TIPC 漏洞為目標的 exploit 框架：`tipc_exploit.c`

**稽核結論**

- 若你的表格寫 “TIPC 不存在攻擊面”，需要用 `tipc_probe` 的實機輸出或 kernel config 直接佐證。

---

## 下一步（建議你補齊的「可追溯證據」）

- 每個 PASS 欄位至少補 1 個：
  - 目標裝置的 `uname -a` / `uname -r`、Android security patch level。
  - 對應 probe/測試程式的 stdout（關鍵行）與必要時的 dmesg 摘要。
- 針對「程式碼路徑不匹配」的項目（如 `cve_2024_0582_uaf_check.c`）：
  - 在表格中改名/拆欄，避免把不同攻擊面混成同一個 CVE 的驗證。
