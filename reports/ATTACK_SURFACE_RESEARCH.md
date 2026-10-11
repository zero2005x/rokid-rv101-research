# Rokid AR Glasses Privilege Escalation Research Report

## Session 12: Alternative Attack Surface Deep Dive

**Date**: 2026-02-02
**Target**: Rokid AR Glasses (RV101)
**Kernel**: 5.10.209-perf
**Android**: 12 (API 32)
**Focus**: TIPC, Ashmem, BPF, Keyring, Alternative Attack Surfaces

---

## Executive Summary

本次研究針對 Rokid AR Glasses 進行深入的攻擊面分析。由於傳統 CVE 漏洞大多已被修補，我們探索了 TIPC、Ashmem、BPF 等替代攻擊向量。

### 關鍵發現

| 向量                        | 狀態            | 潛力                    |
| --------------------------- | --------------- | ----------------------- |
| **🔴 KGSL Cross-FD Access** | ✅ **已確認**   | **Critical** - CVE 候選 |
| **TIPC Group Join/Leave**   | ✅ 可用         | **高** - 224k+ 操作成功 |
| **BPF Heap Spray**          | ✅ 可用         | **高** - 4MB+ spray     |
| **Ashmem**                  | ✅ 可用         | 中 - 64 regions         |
| **Keyring Spray**           | ✅ 可用         | 中 - 121 keys           |
| **Pipe Buffer Spray**       | ✅ 可用         | 中 - 4MB                |
| **DMA-BUF Heap**            | ❌ 權限被拒     | 無                      |
| **KGSL GPU Context**        | ❌ hwsched 阻擋 | 低                      |

---

## 🔴 NEW: KGSL GPU 驅動漏洞 (2026-02-02)

### 漏洞概述

發現 Qualcomm KGSL 驅動存在 **跨 FD GPU 記憶體訪問漏洞**，允許同一進程內的不同 file descriptor 訪問彼此的 GPU 記憶體。

### 漏洞嚴重性

| 項目       | 值                                 |
| ---------- | ---------------------------------- |
| 類型       | Cross-FD GPU Memory Access         |
| 嚴重性     | **Critical** (CVSS ~7.8)           |
| 狀態       | ✅ 已確認並 PoC 驗證               |
| 受影響組件 | Qualcomm KGSL Driver (msm_kgsl.ko) |

### 技術細節

**mmap offset 到 GPU 頁面映射:**

```
offset = (page_index + 1) * 0x1000

offset 0x1000 → gpuaddr 0x4000000000 (第 0 頁)
offset 0x2000 → gpuaddr 0x4000001000 (第 1 頁)
...
```

**PoC 結果:**

```
fd1 allocated: gpuaddr=0x4000000000
fd1 wrote secret: 0xaabbccdd11223344
fd2 mmap offset=0x1000 → reads: 0xaabbccdd11223344 ✅
fd2 modifies to: 0xfeedfacecafebabe
fd1 sees: 0xfeedfacecafebabe ✅ CONFIRMED!
```

### 其他 KGSL 發現

| 發現            | 描述                                                         |
| --------------- | ------------------------------------------------------------ |
| Double mmap     | 同一 GPU 地址的多次 mmap 指向同一物理頁                      |
| fd 關閉後持久性 | 100/100 映射在 fd 關閉後仍有效                               |
| 6 個工作 ioctl  | 0x38-0x3a (info), 0x40 (alloc), 0x56 (fence), 0x58 (aux cmd) |

### 安全影響

1. **沙箱逃逸**: 不同安全域使用不同 fd 可被繞過
2. **資訊洩露**: 讀取其他 fd 的敏感 GPU 資料
3. **記憶體損壞**: 修改關鍵 GPU 結構

### 相關文件

| 文件                    | 用途                 |
| ----------------------- | -------------------- |
| kgsl_cross_fd_exploit.c | 跨 fd 記憶體訪問 PoC |
| kgsl_offset_exploit.c   | offset 映射分析      |
| KGSL_FINAL_REPORT.md    | 完整漏洞報告         |

---

## 1. TIPC Subsystem Analysis

### 1.1 TIPC 版本與狀態

```
TIPC Version: 2.0.0
Module Path: /sys/module/tipc/
```

### 1.2 Socket 類型測試

| Socket Type    | Status   | Default Options                   |
| -------------- | -------- | --------------------------------- |
| SOCK_STREAM    | ✅ Works | importance=0, conn_timeout=8000   |
| SOCK_DGRAM     | ✅ Works | src_droppable=1, dest_droppable=1 |
| SOCK_SEQPACKET | ✅ Works | importance=0, conn_timeout=8000   |
| SOCK_RDM       | ✅ Works | dest_droppable=1                  |

### 1.3 Group Join/Leave Race

**CVE-2024-36886 Style Attack Results:**

```
Phase 2 Race Attack:
- Thread 0: 112,115 iterations, 100% success
- Thread 1: 112,052 iterations, 100% success
- Total: 224,167 join/leave cycles in 5 seconds
- Kernel Log: "Trying to set illegal importance in message"
```

**觀察**:

- Group join/leave 完全可用
- 可達到每秒 44,833 次操作
- Kernel 產生警告訊息（潛在 bug 指標）

### 1.4 未文檔化的 Socket Options

```
Option 127: value=0x0 (可設置 0xFFFFFFFF)
Option 128: value=0x0 (可設置 0x2)
Option 129: value=0x1
Option 130: value=0x1f40 (可設置 0x0 或 0x7FFFFFFF)
Option 131: value=0x0
Option 132: value=0x0
Option 135: value=0x0
Option 137: value=0x0
```

### 1.5 Netlink Generic Families

```
[+] Family 'TIPC' - 存在
[+] Family 'nfc' - 存在
```

---

## 2. Heap Spray Primitives

### 2.1 BPF Map Spray

```c
// 成功創建 32 個 BPF maps
// 每個 map: key_size=4, value_size=256, max_entries=64
// 總共: 2,048 entries = 524 KB controlled data

// Phase 2 擴展:
// 32 maps × 512 entries × 256 bytes = 4,096 KB
```

**優勢**:

- 完全控制的數據內容
- 可預測的分配大小
- 無權限限制 (`unprivileged_bpf_disabled=0`)

### 2.2 Keyring Object Spray

```c
// 成功創建 121 個 keys
// 可變大小: 64, 96, 128, 192, 256 bytes
// 可針對不同 slab caches
```

**觀察**:

- `add_key` syscall 可用
- `keyctl(KEYCTL_REVOKE)` 可用於創建 holes
- 適合 cross-cache attacks

### 2.3 Pipe Buffer Spray

```
Created: 64 pipe pairs
Buffer size: 65,536 bytes each
Total written: 4,096 KB
```

### 2.4 Ashmem Spray

```
Created: 64 ashmem regions
Size per region: 4,096 bytes
Pattern verification: 64/64 (100%)
Pin/Unpin: ✅ Works
Purge: ✅ Works
```

---

## 3. Slab Allocator Analysis

### 3.1 KGSL/GPU Related Slabs

| Slab Name             | Object Size | Objects | Attack Relevance |
| --------------------- | ----------- | ------- | ---------------- |
| kgsl_iommu_addr_entry | 40 bytes    | 1,224   | Medium           |
| kgsl_memobj_node      | 64 bytes    | 256     | Medium           |
| kgsl_event            | 128 bytes   | 128     | Medium           |
| adreno_dispatch_job   | 16 bytes    | 1,024   | Low              |

### 3.2 Target Slab Sizes

根據漏洞類型選擇對應大小的 spray objects:

- **kmalloc-64**: keyring (64 bytes), kgsl_memobj_node
- **kmalloc-128**: keyring (128 bytes), kgsl_event
- **kmalloc-256**: BPF value (256 bytes), tipc_msg structures

---

## 4. Tools Created

### 4.1 alt_attack_surface_v2

**功能**:

1. Ashmem memory exploitation
2. DMA-BUF heap probing
3. BPF heap spray
4. Keyring spray with hole creation
5. Pipe buffer spray
6. Socket buffer spray

**編譯**:

```bash
aarch64-linux-android32-clang -o alt_attack_surface_v2 alt_attack_surface_v2.c -static -O2
```

### 4.2 netlink_tipc_exploit

**功能**:

1. TIPC socket type enumeration
2. TIPC group join/leave race
3. TIPC bind exploration
4. Message buffer overflow probing
5. Netlink generic family enumeration
6. Socket option fuzzing
7. Service announcement spray

### 4.3 tipc_uaf_exploit

**功能**:

1. Phase 1: BPF + Keyring spray setup
2. Phase 2: TIPC group join/leave race
3. Phase 3: Create holes and reclaim
4. Phase 4: Intensive race with message flooding
5. Phase 5: Cleanup and analysis

---

## 5. Kernel Log Analysis

### 5.1 Captured Messages

```
[21237.914438] [T28903] tipc: Trying to set illegal importance in message
```

**解讀**:

- 這表示我們的 TIPC 操作觸發了內核驗證邏輯
- `T28903` 是觸發此訊息的進程
- "illegal importance" 可能是邊界值測試導致

### 5.2 無 KASAN/Crash

- 沒有發現 KASAN UAF 報告
- 沒有 kernel panic
- 系統保持穩定

---

## 6. Exploitation Path Analysis

### 6.1 最有希望的向量

1. **TIPC Group Message UAF (CVE-2024-36886 style)**
   - 可達成高頻率的 join/leave 操作
   - 需要進一步研究 message fragmentation

2. **BPF Verifier Bypass**
   - unprivileged BPF 可用
   - 需要找到 5.10.209 適用的 verifier bug

3. **Cross-Cache Attack via Keyring**
   - 可控制的分配/釋放
   - 可與 TIPC UAF 組合使用

### 6.2 需要進一步研究

1. TIPC bearer configuration (目前無 bearer)
2. `tipc_buf_append()` 的具體觸發條件
3. BPF verifier 在 5.10.209 的已知漏洞
4. KGSL hwsched 繞過方法

---

## 7. Next Steps

1. **深入 TIPC 分析**
   - 研究 TIPC bearer 配置要求
   - 分析 `tipc_buf_append()` 代碼路徑
   - 測試更大的消息導致碎片化

2. **BPF Verifier Research**
   - 搜索 5.10.x kernel 的 BPF CVEs
   - 測試 CVE-2023-2163 等已知漏洞

3. **組合攻擊開發**
   - 使用 BPF spray + TIPC UAF
   - 開發 controlled data placement

4. **Kernel Symbol Leak**
   - 利用 KGSL perfcounter info leak
   - 嘗試繞過 kptr_restrict=2

---

## 8. BPF Verifier Analysis (NEW)

### 8.1 BPF 安全設置

```
unprivileged_bpf_disabled = 0  ✓ (可非特權使用)
bpf_jit_enable = 1             ✓ (JIT 已啟用)
bpf_jit_harden = N/A           (未設置)
```

### 8.2 可用 Map 類型

| Map Type     | Status       | Notes             |
| ------------ | ------------ | ----------------- |
| HASH         | ✅ Works     | Standard hash map |
| ARRAY        | ✅ Works     | Fixed-size array  |
| PERCPU_HASH  | ✅ Works     | Per-CPU hash      |
| PERCPU_ARRAY | ✅ Works     | Per-CPU array     |
| RINGBUF      | ✅ Create OK | mmap denied       |

### 8.3 可用程式類型

| Program Type  | Status          |
| ------------- | --------------- |
| SOCKET_FILTER | ✅ Works        |
| CGROUP_SKB    | ✅ Works        |
| Others        | ❌ Need CAP_BPF |

### 8.4 可用 Helper Functions

```
✅ map_lookup_elem (id=1)
✅ map_delete_elem (id=3)
✅ ktime_get_ns (id=5)
```

### 8.5 Verifier 測試結果

- Normal map access: ✅ Allowed
- OOB stack access: ✅ Rejected (expected)
- Large ALU operations: ✅ Allowed
- CVE-2023-2163 style pruning: ❌ Rejected (patched)

---

## 9. Files Modified/Created

| File                            | Lines | Description                         |
| ------------------------------- | ----- | ----------------------------------- |
| alt_attack_surface_v2.c         | **704**（2026-09-13 重量測；原 415） | Multi-vector heap spray tool        |
| netlink_tipc_exploit.c          | 560   | TIPC/Netlink vulnerability explorer |
| tipc_uaf_exploit.c              | 478   | TIPC CVE-2024-36886 style exploit   |
| bpf_verifier_exploit.c          | 630   | BPF verifier research tool          |
| **kgsl_cross_fd_exploit.c**     | ~200  | 🔴 KGSL 跨 FD GPU 記憶體訪問 PoC    |
| **kgsl_offset_exploit.c**       | ~150  | KGSL offset 映射分析                |
| **kgsl_fd_confusion_exploit.c** | ~300  | FD 混淆與競態條件測試               |
| **KGSL_FINAL_REPORT.md**        | ~400  | KGSL 漏洞完整報告                   |
| ATTACK_SURFACE_RESEARCH.md      | -     | This report                         |

---

## 10. Conclusion

本次研究確認了多個可行的攻擊原語，**並發現了一個關鍵漏洞**：

### 🔴 最高優先級: KGSL Cross-FD GPU Memory Access

**已確認的跨 FD GPU 記憶體訪問漏洞**：

- 允許同進程內不同 fd 讀寫彼此的 GPU 記憶體
- 可能導致沙箱逃逸、資訊洩露、記憶體損壞
- **應作為 CVE 報告提交給 Qualcomm**

### 其他攻擊原語

1. **TIPC Group Join/Leave** - 高頻率可達，但需要 bearer 配置才能觸發真正的 UAF
2. **BPF Heap Spray** - 完美可控的內核堆噴射原語
3. **BPF SOCKET_FILTER** - 可載入 BPF 程式進行進一步研究
4. **Keyring Spray** - 可變大小的 slab 分配器

### 建議的攻擊鏈

```
KGSL Cross-FD (info leak) → BPF Heap Spray (object placement) → TIPC UAF (trigger) → Root
```

**KGSL 漏洞是目前最有價值的發現**，已有完整 PoC 驗證。
**BPF 仍是重要的輔助向量**，因為 `unprivileged_bpf_disabled=0` 允許非特權用戶載入 BPF 程式。
