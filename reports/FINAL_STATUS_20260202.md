# 🔴 Rokid AR Glasses Root 研究 - 最終狀態報告
> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


**日期**: 2026-02-02  
**結論**: ❌ **所有軟體攻擊路徑均已失敗，尚未成功提權**

> **這是專案的核心狀態文件，請優先閱讀此文件了解當前進度。**

---

## 📊 執行摘要

| 項目             | 狀態                       |
| ---------------- | -------------------------- |
| **Root 權限**    | ❌ **未取得**              |
| **Bootloader**   | ✅ 已解鎖 (orange state)   |
| **Magisk**       | ✅ v30.6 已安裝，❌ 未激活 |
| **boot.img**     | ❌ 無法提取                |
| **核心漏洞利用** | ❌ 全部失敗                |

---

## 🎯 當前阻塞點

**唯一缺少的是 boot.img 文件！**

- Bootloader 已完全解鎖
- Magisk App 已安裝
- 只需要一個可用的 boot.img 來進行 Magisk 修補

### 為何無法取得 boot.img

| 嘗試方法                             | 結果 | 原因                          |
| ------------------------------------ | ---- | ----------------------------- |
| `adb shell dd /dev/block/mmcblk0p17` | ❌   | Permission denied (需要 root) |
| `fastboot fetch boot_a`              | ❌   | 設備不支持此命令              |
| `fastboot oem dump-partitions`       | ❌   | unknown command               |
| Recovery Sideload                    | ❌   | 驅動問題導致卡住              |
| Custom Recovery (TWRP)               | ❌   | 找不到相容版本                |
| 官方 OTA 韌體                        | ❌   | 未找到下載來源                |

---

## ❌ 已測試並確認失敗的攻擊路徑

### 核心漏洞 (Kernel Exploits)

| CVE/技術                     | 版本數 | 失敗原因                     |
| ---------------------------- | ------ | ---------------------------- |
| CVE-2024-36971 (Route UAF)   | v1-v13 | Loopback DST 永不 obsolete   |
| CVE-2024-0582 (io_uring UAF) | -      | PBUF_RING 功能不存在 (5.19+) |
| CVE-2022-0847 (Dirty Pipe)   | -      | 已修補                       |
| CVE-2019-2215 (Binder UAF)   | -      | 已修補                       |
| CVE-2023-2163 (BPF Verifier) | -      | 已修補                       |
| CVE-2024-38401 (KGSL UAF)    | v1-v6  | 已修補                       |
| CVE-2025-21479 (KGSL)        | v1-v4  | Context 創建被阻止           |

### BPF 資訊洩漏 (KASLR Bypass)

| 技術             | 版本數 | 失敗原因                            |
| ---------------- | ------ | ----------------------------------- |
| BPF Ringbuf OOB  | v1-v6  | Verifier 阻止；非 root 禁止指標運算 |
| BPF 指標洩漏     | -      | ringbuf_reserve 返回 NULL           |
| Perfcounter 洩漏 | -      | 返回的是 GPU MMIO 偏移，非核心指標  |

### GPU/驅動攻擊

| 技術                | 版本數 | 失敗原因           |
| ------------------- | ------ | ------------------ |
| KGSL 未初始化記憶體 | v1-v3  | GPU 記憶體完全歸零 |
| DRM msm 利用        | v1-v2  | 無可利用漏洞       |

### 其他

| 技術        | 失敗原因                 |
| ----------- | ------------------------ |
| TIPC 漏洞   | AF_TIPC 可用但無可利用點 |
| Binder 漏洞 | v2-v5 全部失敗，已修補   |
| Ashmem 溢出 | 無可利用漏洞             |
| nf_tables   | CONFIG_NF_TABLES 未啟用  |

---

## ✅ 已驗證可用的技術

> 這些技術可以運作，但**無法單獨達成提權**

| 技術              | 狀態 | 說明                                |
| ----------------- | ---- | ----------------------------------- |
| BPF Heap Spray    | ✅   | 可噴射 192-byte 物件 (ip_dst_cache) |
| KGSL GPUOBJ_ALLOC | ✅   | 可分配 GPU 記憶體                   |
| io_uring 基礎操作 | ✅   | 37 operations 可用                  |
| TIPC Socket 創建  | ✅   | 可創建和綁定                        |
| Fastboot 連接     | ✅   | 可進入 Fastboot 模式                |

---

## 📱 設備完整資訊

```
型號: Rokid RG-glasses (RV101)
Android: 12 (SKQ1.240613.001)
核心: 5.10.209-perf-gb80a8319d11b
SoC: Qualcomm neo (SoC ID: 579)
GPU: Adreno 621v1 (Chip ID: 0x6020100)
安全補丁: 2024-07-05
Boot 分區: boot_a = /dev/block/mmcblk0p17 (96MB)
Bootloader: 已解鎖 (unlocked:yes, secure:no)
```

---

## 🔧 剩餘可行路徑

### 優先順序 1: 獲取 boot.img

1. **尋找 Rokid 官方韌體** (最佳)
   - 版本: 1.12.009 或任何版本
   - 使用 `payload-dumper-go` 解包

2. **尋找相容 TWRP**
   - 平台: Qualcomm SM6375/khaje
   - 使用 `fastboot boot twrp.img` 臨時啟動

3. **EDL 模式** (需要拆機)
   - 短接測試點進入 Qualcomm 下載模式
   - 使用 QFIL 工具讀取分區

### 優先順序 2: Qualcomm 專屬漏洞

- 需要更深入研究 KGSL 驅動
- 可能存在未公開的漏洞

---

## 📁 重要文件索引

### 主要文件 (建議保留)

| 文件                           | 用途              |
| ------------------------------ | ----------------- |
| `HANDOVER_SESSION_20260131.md` | 完整 Session 交接 |
| `FINAL_STATUS_20260202.md`     | 本文件 - 最終狀態 |
| `Rokid_Root_Guide_boot_img.md` | Root 流程指南     |
| `CVE_5_10_x_Scan_Report.md`    | 漏洞掃描報告      |

### 最終版本 PoC (其他中間版本可刪除)

| 文件                    | 用途               |
| ----------------------- | ------------------ |
| `cve_2024_36971_v13.c`  | Route UAF 最終版   |
| `bpf_ringbuf_oob_v6.c`  | Ringbuf 洩漏最終版 |
| `kgsl_exploit_v9.c`     | KGSL 利用最終版    |
| `io_uring_uaf_verify.c` | io_uring 驗證      |

---

## ⚠️ 注意事項

1. **所有標記為「成功」的項目僅表示技術可行**，並非提權成功
2. **Bootloader 解鎖是關鍵優勢**，只需 boot.img 即可完成 root

---

_報告結束 - 2026-02-02_
