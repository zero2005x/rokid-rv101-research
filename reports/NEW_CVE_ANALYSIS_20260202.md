# 🆕 新發現 CVE 可行性分析報告

> English: [NEW_CVE_ANALYSIS_20260202_EN.md](NEW_CVE_ANALYSIS_20260202_EN.md)
> ⚠ **2026-09-13 更正**：本文假設的 **Fastboot／Magisk／刷 boot.img 路線不可行**（取不到 boot.img、無 Magisk、無持久 root）；**root 已由 GhostLock LPE（CVE-2026-43499）於執行期取得**。


**日期**: 2026-02-02  
**設備**: Rokid AR Glasses (Kernel 5.10.209, Android 12, Qualcomm neo)  
**安全補丁**: 2024-07-05

---

## 📊 執行摘要

基於最新的系統配置檢查，以下是各 CVE 的可行性評估：

| CVE                | 漏洞類型                  | 可行性      | 原因                              |
| ------------------ | ------------------------- | ----------- | --------------------------------- |
| **CVE-2024-49848** | ADSPRPC UAF               | ⭐⭐⭐ 中   | 設備存在但權限不足                |
| **CVE-2024-56720** | BPF sockmap UAF           | ❌ 不可行   | `CONFIG_BPF_STREAM_PARSER` 未啟用 |
| **CVE-2025-21655** | io_uring eventfd UAF      | ❌ 不可行   | 僅影響 Linux 6.1+                 |
| **CVE-2024-1086**  | nf_tables UAF             | ❌ 不可行   | `CONFIG_NF_TABLES` 未啟用         |
| **CVE-2024-26581** | nft_set_rbtree OOB        | ❌ 不可行   | `CONFIG_NF_TABLES` 未啟用         |
| **CVE-2024-53197** | USB Audio driver          | ⭐⭐ 低     | 需要特殊 USB 設備                 |
| **CVE-2023-20938** | Binder UAF                | ⭐⭐ 低     | 需要進一步研究                    |
| **CVE-2024-43047** | ADSPRPC Map collision UAF | ⭐⭐⭐⭐ 高 | **ITW 已證實可利用**              |

---

## 🔴 核心配置檢查結果

### 1. nf_tables (CVE-2024-1086, CVE-2024-26581)

```
# CONFIG_NF_TABLES is not set   ❌ 不可用
CONFIG_NETFILTER=y              ✅ Netfilter 可用
CONFIG_NETFILTER_XTABLES=y      ✅ iptables 可用
```

**結論**: nf_tables 完全未編譯進核心，CVE-2024-1086 和 CVE-2024-26581 **不可利用**。

### 2. BPF sockmap (CVE-2024-56720)

```
CONFIG_BPF_SYSCALL=y            ✅ BPF 系統調用可用
CONFIG_CGROUP_BPF=y             ✅ cgroup BPF 可用
CONFIG_BPF_JIT=y                ✅ BPF JIT 啟用
# CONFIG_BPF_STREAM_PARSER is not set  ❌ Stream parser 未啟用
```

**結論**: `BPF_STREAM_PARSER` 是 sockmap 功能的前提，未啟用意味著 CVE-2024-56720 **不可利用**。

### 3. io_uring eventfd (CVE-2025-21655)

```
CONFIG_IO_URING=y               ✅ io_uring 可用
CONFIG_EVENTFD=y                ✅ eventfd 可用
```

**但是**: CVE-2025-21655 僅影響 **Linux 6.1+** 版本，我們的設備是 **5.10.209**，版本不受影響。

**結論**: CVE-2025-21655 **不可利用** (版本不在受影響範圍)。

### 4. ADSPRPC (CVE-2024-49848, CVE-2024-43047)

```
/dev/adsprpc-smd:
  權限: crw-rw-r-- (664)
  Owner: system:system
  Major: 489

Shell 用戶 groups:
  shell input log adb sdcard_rw sdcard_r ext_data_rw ext_obb_rw
  net_bt_admin net_bt inet net_bw_stats readproc uhid readtracefs
```

**分析**:

- 設備節點存在且權限為 664 (`rw-rw-r--`)
- Owner 和 Group 都是 `system`
- Shell 用戶 (uid=2000) **不在 system 組中**
- 直接 `open()` 會失敗

**但是**: 根據 Google Project Zero 的報告，有以下獲取存取權的方法：

> "untrusted_app does not have the ability to directly open the driver device file, but at least on some devices it can obtain limited access by **receiving a file descriptor** to the device file from the **dspservice** process through the **IDspService HAL interface**, which is reachable through **hwbinder**."

**結論**: 需要進一步研究 HWBinder 存取路徑。這是 **高優先級** 研究目標。

### 5. USB Audio (CVE-2024-53197)

```
CONFIG_SND_USB=y                 ✅ USB Audio 可用
CONFIG_SND_USB_AUDIO=y           ✅ USB Audio driver 啟用
```

**利用條件**:

- 需要連接特製的 USB Audio 設備
- 或者模擬 USB 設備 (需要 USB OTG 支援)

**結論**: 理論上可行，但實際操作複雜度高。

---

## 🎯 最高優先級: CVE-2024-43047 / CVE-2024-49848 (ADSPRPC)

### 漏洞背景

這是 **Google Project Zero 確認的 ITW (In-The-Wild) 漏洞**，被 NoviSpy 間諜軟體使用。

### 漏洞詳情

#### CVE-2024-43047: Map Collision UAF

- **類型**: Use-After-Free
- **觸發條件**: 創建重疊的 fastrpc_mmap 結構
- **利用難度**: 中等
- **影響版本**: Linux 4.x 核心和部分 5.x 配置

#### CVE-2024-49848: FASTRPC_ATTR_KEEP_MAP UAF

- **類型**: Use-After-Free 競爭條件
- **觸發條件**: 特殊的 flags/attrs 組合 + 競爭
- **利用難度**: 中高
- **在野狀態**: 被 ITW 利用

### 利用策略 (來自 Project Zero 分析)

1. **Heap Spray**: 使用 `inotify_event_info` 物件進行 heap spray
2. **UAF 觸發**: 通過 map collision 或 KEEP_MAP 邏輯錯誤觸發
3. **控制物件**: 使用 pipe buffers 重新佔用釋放的記憶體
4. **任意讀寫**: 獲得任意核心記憶體讀寫能力

### 研究方向

1. **確認 HWBinder 存取**:

```bash
# 檢查 dspservice
adb shell "service list | grep -i dsp"
adb shell "dumpsys -l | grep -i dsp"

# 檢查 HWBinder 服務
adb shell "lshal | grep -i adsp"
```

2. **檢查 SELinux 政策**:

```bash
adb shell "sesearch --allow -s shell -t adsprpc_device_device"
```

3. **測試 HAL 存取**:
   - 需要開發 C++ 程式碼通過 HWBinder 獲取 fd

---

## 📋 下一步行動計劃

### 優先級 1: ADSPRPC 存取研究

1. 檢查 `dspservice` 和 `IDspService` HAL
2. 研究 HWBinder 存取路徑
3. 開發 CVE-2024-43047 PoC

### 優先級 2: USB Audio 攻擊 (如果有 OTG)

1. 確認設備是否支持 USB OTG
2. 研究 CVE-2024-53197 利用細節

### 優先級 3: 繼續尋找 boot.img

1. TWRP 仍然是最可靠的路徑
2. 官方韌體下載

---

## ⚠️ 重要發現

### 為什麼 CVE-2024-49848 是最有希望的路徑（**註：同檔表格給 CVE-2024-43047 ⭐⭐⭐⭐ 更高，標題與表格不一致，2026-09-13 註**）

1. **已證實 ITW 利用**: Google Project Zero 確認被實際攻擊者使用
2. **設備節點存在**: `/dev/adsprpc-smd` 存在於系統中
3. **Qualcomm 設備**: 我們的設備是 Qualcomm SoC
4. **內核版本匹配**: 5.10.209 在受影響範圍內
5. **詳細的技術分析可用**: Project Zero 提供了完整的漏洞分析

### 主要挑戰

1. **權限問題**: 無法直接打開設備，需要通過 HWBinder
2. **競爭條件**: 漏洞需要精確的時間控制
3. **Heap Spray**: 需要找到合適的 spray 物件
4. **KASLR**: 需要資訊洩漏繞過 KASLR

---

## 📚 參考資源

1. [Project Zero: The Qualcomm DSP Driver](https://projectzero.google/2024/12/qualcomm-dsp-driver-unexpectedly-excavating-exploit.html)
2. [Amnesty: NoviSpy Analysis](https://securitylab.amnesty.org/latest/2024/12/a-digital-prison-surveillance-and-the-suppression-of-civil-society-in-serbia/)
3. [Qualcomm Security Bulletin](https://docs.qualcomm.com/product/publicresources/securitybulletin/october-2024-bulletin.html)

---

_報告結束 - 2026-02-02_
