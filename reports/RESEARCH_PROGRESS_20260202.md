# Rokid AR Glasses Root 研究進度報告
> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


**日期**: 2026-02-02
**目標設備**: Rokid AR Glasses (Kernel 5.10.209-perf, Android 12)

---

## 當前狀態

### ✅ 已達成

| 項目            | 狀態      | 備註                                       |
| --------------- | --------- | ------------------------------------------ |
| Bootloader 解鎖 | ✅ 完成   | `unlocked: yes`, `critical unlocked: true` |
| Magisk 安裝     | ✅ 已安裝 | v30.6，但未激活                            |
| BPF 驗證        | ✅ 完成   | 資料流成功，但無法洩露指針                 |
| KGSL 探測       | ✅ 完成   | GPUOBJ 可分配，但無信息洩露                |

### ❌ 已確認失敗的攻擊路徑

| CVE/技術                   | 原因                                         |
| -------------------------- | -------------------------------------------- |
| BPF Ringbuf OOB            | Verifier 嚴格追蹤邊界；非 root 禁止指針運算  |
| BPF 指針洩露               | `ringbuf_reserve` 在 socket_filter 返回 NULL |
| KGSL 未初始化內存          | GPU 內存被正確清零                           |
| 直接讀取 boot 分區         | 需要 root 權限 (`brw-------`)                |
| CVE-2024-36971             | Slab isolation                               |
| CVE-2024-0582              | io_uring PBUF_RING 功能不存在                |
| CVE-2022-0847 (Dirty Pipe) | 已修補                                       |
| CVE-2019-2215 (Binder UAF) | 已修補                                       |

---

## 設備詳細資訊

```
Model: Rokid/glasses/glasses
Android: 12 (SKQ1.240613.001)
Kernel: 5.10.209-perf
SoC: Qualcomm (platform=neo, soc_id=579)
GPU: Adreno 6xx (Chip ID: 0x6020100)
Slot: a (boot_a = /dev/block/mmcblk0p17)
Boot partition size: 96MB (0x6000000)
```

---

## 下一步行動計劃

### 方案 A: Fastboot Boot Custom Recovery (推薦)

1. 尋找/編譯適用於 Qualcomm neo 平台的 TWRP recovery
2. 使用 `fastboot boot twrp.img` 臨時啟動
3. 從 TWRP shell 執行 `dd if=/dev/block/mmcblk0p17 of=/sdcard/boot_a.img`
4. 拉取 boot.img 並用 Magisk patch
5. 刷入 patched boot: `fastboot flash boot_a patched_boot.img`

### 方案 B: 尋找官方 OTA 包

1. 搜索 Rokid 官方固件更新
2. 使用 payload-dumper-go 提取 boot.img
3. 用 Magisk patch 並刷入

### 方案 C: 使用 Magisk Direct Install (需要 ADB root)

1. 如果設備有 `adb root` 功能（通常 userdebug 版本有）
2. 執行 Magisk Direct Install

### 方案 D: 尋找其他內核漏洞

1. 研究 Qualcomm 特定 CVE (如 CVE-2023-33063 KGSL UAF)
2. 探索其他 world-writable 設備節點
3. 研究 vendor 特定漏洞

---

## 已創建的工具

| 文件                      | 用途                 |
| ------------------------- | -------------------- |
| `bpf_ringbuf_oob_v1-v6.c` | BPF ringbuf OOB 嘗試 |
| `kgsl_vuln_scan.c`        | KGSL 設備掃描        |
| `kgsl_exploit_v2-v3.c`    | KGSL 內存洩露嘗試    |
| `extract_boot.sh`         | Boot 分區提取腳本    |

---

## 關鍵發現

1. **BPF 限制**: Kernel 5.10.209 對非 root 用戶禁止指針運算
   - 錯誤信息: `pointer arithmetic with it prohibited for !root`

2. **KGSL 可訪問但安全**:
   - `/dev/kgsl-3d0` 和 `/dev/dri/*` 是 world-writable
   - 但 GPU 內存分配被正確初始化（無信息洩露）
   - Context 創建需要特定 flags

3. **Magisk 需要 boot.img**:
   - Magisk v30.6 已安裝但 su 不可用
   - 需要 patched boot.img 才能激活

---

## 建議

優先順序:

1. **方案 A** - 如果能找到兼容的 recovery image
2. **方案 B** - 如果能找到 OTA 更新包
3. **方案 D** - 如果前兩者都不可行，繼續研究內核漏洞
