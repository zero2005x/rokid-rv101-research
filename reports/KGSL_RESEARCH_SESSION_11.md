# KGSL GPU Exploitation Research Report - Session 11

> English: [KGSL_RESEARCH_SESSION_11_EN.md](KGSL_RESEARCH_SESSION_11_EN.md)
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


**Date**: 2026-02-02  
**Target**: Rokid AR Glasses (Android 12, Kernel 5.10.209, Adreno 621v1)  
**Research Focus**: GPU Memory, Perfcounter, Information Leak

---

## 📊 Executive Summary

通過深度分析 KGSL GPU 驅動接口，發現了多個潛在的信息洩漏向量和利用原語。雖然無法直接創建 GPU Context（被 hwsched 模式阻止），但仍有可利用的攻擊面。

### Key Findings

| 發現                                     | 嚴重性 | 利用潛力       |
| ---------------------------------------- | ------ | -------------- |
| Perfcounter offset_hi 洩漏 GPU MMIO 偏移 | 中     | KASLR 繞過輔助 |
| Property 0x02 洩漏 GPU 虛擬地址基址      | 中     | 地址預測       |
| IOCTL 0x58 全局計數器                    | 低     | 時序側信道     |
| GPU Memory ID 可預測                     | 中     | UAF 利用輔助   |
| mmapsize 固定 768KB                      | 高     | 堆噴射優化     |

---

## 🔍 Detailed Findings

### 1. Perfcounter Register Offset Leak

**發現**: 所有 perfcounter 組都返回有效的 `offset` 和 `offset_hi` 值

```
Base offset_hi: 0x041b (GPU MMIO 偏移基址)
Max offset_hi:  0x3cd1 (Group 14 RB_RAW)
Range:          0x38b6 bytes (~14.5 KB)
```

**唯一偏移值 (20個)**:

```
0x041b → Group 0  (CP - Command Processor)
0x0423 → Group 1  (RBBM - Ring Buffer Block Manager)
0x0433 → Group 2  (PC - Primitive Cull)
0x0443 → Group 3  (VFD - Vertex Fetch Decode)
0x044f → Group 4  (HLSQ - High Level Shader Queue)
0x045b → Group 5  (VPC - Varying/Parameter Cache)
0x0465 → Group 24 (Unknown - possibly Debug)
0x046d → Group 6  (TSE - Triangle Setup Engine)
0x0475 → Group 7  (RAS - Rasterizer)
0x048d → Group 8  (RB - Render Buffer)
0x04a5 → Group 9  (VSC - Visibility Stream Compress)
0x04d5 → Group 10 (CCU - Color Cache Unit)
0x04e5 → Group 11 (UCHE - Unified Cache)
0x04e9 → Group 23 (Unknown)
0x04f1 → Group 25 (GMU_GENERAL)
0x04f9 → Group 26 (GMU_ALWAYS)
0x0981 → Group 27 (GMU_POWER)
0x3ccf → Group 14 (RB_RAW counter 0)
0x3cd0 → Group 14 (RB_RAW counter 1)
0x3cd1 → Group 14 (RB_RAW counter 2)
```

**分析**:

- 這些偏移值對應 Adreno 621 GPU 的 MMIO 寄存器
- 偏移間距不固定 (8, 12, 16 bytes)，表示不同功能塊
- RB_RAW 組的偏移 (0x3ccc-0x3cd1) 明顯高於其他，可能是 GPU 共享內存區域

---

### 2. Device Property Information Leak

#### Property 0x02 (DEVICE_SHADOW)

```
值: 0xefff_f000
```

**解讀**:

- 這是 GPU 虛擬地址空間的影子映射基址
- 位於 `0xefff_f000` 附近 (~4GB 地址空間頂部)
- 這個地址可用於：
  - 預測 GPU 命令緩衝區位置
  - 輔助構造任意寫入原語

#### Property 0x15

```
值: 0x016dd112 (23,974,162)
```

可能是：

- GPU 時鐘計數器
- 隨機種子值
- 運行時間戳

#### Property 0x29

```
值: 0x65726441 = "Adre" (ASCII)
```

GPU 型號字符串的一部分 ("Adreno")

#### Property 0x2a

```
值: 0x06020100
```

GPU Chip ID: `06 02 01 00` = Adreno 621 revision 1.0

---

### 3. GPU Memory Allocation Patterns

#### ID 分配模式

```
請求大小    ID        增量
4KB        0         -
8KB        4096      +4096
16KB       12288     +8192
64KB       28672     +16384
256KB      94208     +65536
1MB        356352    +262144
4MB        1404928   +1048576
```

**發現**: ID 增量 = 請求大小

這意味著：

1. ID 可以完美預測
2. 如果知道某個分配的大小，可以計算其 ID
3. 這對於 UAF 攻擊中的目標識別非常有用

#### mmapsize 固定

```
所有分配的 mmapsize = 786,432 bytes (768 KB)
```

**重要發現**:

- 不論請求多少內存，內核都會分配固定的 768KB 用戶空間映射
- 這意味著我們可以：
  1. 請求小內存 (4KB)
  2. 獲得大映射 (768KB)
  3. 潛在越界讀/寫

---

### 4. IOCTL 0x56/0x58 Timeline Interface

#### IOCTL 0x56 (Timeline Create?)

```
結構:
  offset 0:  輸入值 (echo back)
  offset 12: 固定值 0x20 (32)
```

可能是 timeline 創建接口，返回的 32 可能是：

- Timeline 容量
- 支持的最大 fence 數量

#### IOCTL 0x58 (Timeline Query?)

```
結構:
  offset 0:  輸入值 (echo back)
  offset 8:  全局單調遞增計數器
```

**利用價值**:

- 這個計數器可用於時序側信道
- 可以探測其他進程的 GPU 活動
- 每次調用增加 1

---

## 🎯 Exploitation Strategies

### Strategy A: GPU Memory OOB Read/Write

**原理**: 利用 mmapsize 固定 768KB 的特性

```c
// 1. 連續分配多個小內存
gpumem_alloc(4096);  // ID=0, mapped 0-768KB
gpumem_alloc(4096);  // ID=4096, mapped 0-768KB

// 2. 如果這些分配在物理上相鄰
// 我們可能可以通過第一個映射訪問第二個分配的數據

// 3. 結合 syncsource 創建來在特定 slab 分配
```

### Strategy B: Perfcounter 寄存器直接訪問

**假設**: 如果 offset 值可以控制

```c
// 1. 分析 PERFCOUNTER_GET 是否真的寫入寄存器
// 2. 如果 offset 可控，可能可以越界讀取 GPU MMIO
// 3. 這可能洩漏更多地址信息
```

### Strategy C: Timeline Counter Side-Channel

**用途**: 探測系統狀態

```c
// 1. 監控 IOCTL 0x58 計數器變化
// 2. 當其他進程使用 GPU 時，計數器會變化
// 3. 可用於時序攻擊或狀態推測
```

### Strategy D: Combined Attack

1. 使用 Property 0x02 獲取 GPU VA 基址
2. 使用 Perfcounter offsets 推算 MMIO 佈局
3. 利用 GPU Memory 的固定 mmapsize 進行堆噴射
4. 結合 Syncsource race 或其他內核漏洞

---

## 🛠️ Tools Created

| 文件                      | 用途                 |
| ------------------------- | -------------------- |
| `kgsl_advanced_exploit.c` | 綜合漏洞利用研究工具 |
| `kgsl_leak_analysis.c`    | 專注信息洩漏分析     |

---

## 📋 Next Steps

### Priority 1: mmap 越界分析

1. 確認 768KB mmapsize 是否真的可以越界訪問
2. 嘗試連續分配後讀取超出分配大小的數據
3. 如果可行，這是一個強大的信息洩漏原語

### Priority 2: Perfcounter 寫入分析

1. 確認 PERFCOUNTER_GET 是否真的設置了硬體寄存器
2. 嘗試使用異常 countable 值
3. 檢查是否有越界寫入可能

### Priority 3: IOCTL 0x5b 深度研究

1. 這個 IOCTL 持續返回 EAGAIN
2. 需要逆向分析 msm_kgsl.ko 來理解所需狀態
3. 可能是 GPU 命令提交的替代路徑

### Priority 4: 繼續 boot.img 方案

由於 Bootloader 已解鎖，獲取官方固件或 OTA 包仍然是最可靠的 root 路徑。

---

## 🔐 Security Assessment

### 攻擊難度評估

| 向量              | 複雜度 | 可行性     |
| ----------------- | ------ | ---------- |
| mmap OOB          | 低     | 待驗證     |
| Perfcounter abuse | 中     | 待研究     |
| Side-channel      | 低     | 有限價值   |
| Context bypass    | 高     | 需要 0-day |

### 防禦建議

1. 修復 mmapsize 計算邏輯，應等於實際分配大小
2. 限制非特權用戶的 perfcounter 訪問
3. 鎖定 Bootloader 並驗證開機鏈
4. 更新安全補丁至最新

---

**Report Generated**: 2026-02-02  
**Status**: Research In Progress
