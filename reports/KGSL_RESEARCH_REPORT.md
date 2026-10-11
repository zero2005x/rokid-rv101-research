# KGSL 攻擊面研究報告 - 2026年2月3日更新（**原寫 2025，2026-09-13 更正**）
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


## 執行摘要

經過深入分析，確認 Rokid AR Glasses 上的 `/dev/kgsl-3d0` GPU 驅動提供了 **極具潛力的攻擊面**。

### 確認的能力

| 功能                    | 狀態    | 詳情                            |
| ----------------------- | ------- | ------------------------------- |
| GPUMEM_ALLOC            | ~~✅ 成功~~ **實測 ENOTSUP（功能停用）** | 可分配 0x1000 - 0x10000000 字節（**2026-09-13 更正：見 5.2_VERIFICATION_LIVE_ADB_2026 §1.1**） |
| mmap GPU 記憶體         | ✅ 成功 | 可映射到用戶空間                |
| 讀寫 GPU 記憶體         | ✅ 成功 | 完全控制                        |
| GPU_AUX_COMMAND         | ✅ 成功 | 返回計數器信息                  |
| SYNCSOURCE_CREATE_FENCE | ✅ 成功 | 可創建 fence                    |
| GPU 信息洩漏            | ✅ 成功 | 版本號等                        |

### 有效的 ioctl 列表

```
0x38 - GPU 信息查詢 1
0x39 - GPU 信息查詢 2
0x3a - GPU 信息查詢 3
0x40 - 資源分配 (返回遞增 ID)
0x56 - SYNCSOURCE_CREATE_FENCE
0x58 - GPU_AUX_COMMAND
```

### 潛在攻擊向量

#### 1. PERFCOUNTER (ioctl 0x3b) - errno=1 (EPERM)

- 需要額外權限
- 可能通過 CAP_SYS_ADMIN 或特定 UID 繞過
- **歷史 CVE**: CVE-2020-3668, CVE-2021-30328

#### 2. 等待操作 (ioctl 0x48) - errno=524 (ERESTARTSYS)

- 阻塞等待操作
- 可能存在競態條件
- 需要進一步分析中斷處理

#### 3. GPU 命令提交 (ioctl 0x5b) - errno=11 (EAGAIN)

- 異步操作
- 需要特定條件才能成功
- 可能與 context 創建相關

### 失敗的路徑

1. **Context 創建** - "legacy context switch not supported"
2. **Timeline 操作** - ENODEV
3. **直接記憶體釋放** - 沒有找到有效的 free ioctl

### 建議的下一步

1. **研究 GPU_AUX_COMMAND 的完整功能**
   - 它成功返回了數據
   - 可能可以用於某種控制

2. **探索 perfcounter sysfs 介面**
   - `/sys/class/kgsl/kgsl-3d0/perfcounter` 是 shell 可寫的
   - 可能繞過 ioctl 權限檢查

3. **多進程競態條件**
   - fd 共享 + 並行操作
   - mmap + close 競態

4. **整數溢出嘗試**
   - 在分配路徑中
   - size 參數操作

---

## 技術細節

### GPU 記憶體分配測試結果

```c
[+] Allocated gpuaddr=0x4000000000
[+] mmap ptr=0x7e19455000
[*] Writing to GPU memory...
[+] Read back: 0xdeadbeef   // 成功寫入！
```

### 有效的 ioctl 命令映射

```
ioctl 0xc0040938 (nr=0x38) - GPU info 1
ioctl 0xc0040939 (nr=0x39) - GPU info 2
ioctl 0xc004093a (nr=0x3a) - GPU info 3
ioctl 0xc0040940 (nr=0x40) - Resource alloc
ioctl 0xc0040956 (nr=0x56) - Syncsource fence
ioctl 0xc0200958 (nr=0x58) - GPU aux command
```

### 錯誤碼分析

| errno             | 含義           | ioctl                 |
| ----------------- | -------------- | --------------------- |
| 1 (EPERM)         | 權限不足       | 0x3b (perfcounter)    |
| 11 (EAGAIN)       | 資源暫時不可用 | 0x5b (command submit) |
| 19 (ENODEV)       | 設備不存在     | 0x5c, 0x5d (timeline) |
| 22 (EINVAL)       | 參數無效       | context create        |
| 95 (EOPNOTSUPP)   | 操作不支持     | 0x15, 0x20            |
| 524 (ERESTARTSYS) | 被信號中斷     | 0x48 (wait)           |

### 設備配置

- **GPU**: Adreno 621v1
- **內核**: 5.10.209-perf
- **驅動**: /vendor/lib/modules/msm_kgsl.ko
- **SELinux**: Enforcing

---

## CVE 適用性評估

### CVE-2023-33063 (Timeline UAF)

- **狀態**: 不適用
- **原因**: Timeline ioctl 返回 ENODEV

### CVE-2023-33107 (Integer Overflow)

- **狀態**: 未確認
- **原因**: GPUMEM_ALLOC_ID 返回 EINVAL

### CVE-2024-23380 (GPU Command UAF)

- **狀態**: 部分適用
- **原因**: GPU_AUX_COMMAND 成功，但需要進一步研究

### CVE-2023-21106 (Dispatcher)

- **狀態**: 未確認
- **原因**: 需要 context 創建

---

## 結論

KGSL 提供了強大的用戶空間原語:

1. 任意大小 GPU 記憶體分配
2. 映射到用戶空間
3. 完全讀寫控制

主要限制:

- 無法創建 context (legacy context switch not supported)
- 沒有直接的 free 操作
- perfcounter 需要權限

建議繼續研究 GPU_AUX_COMMAND 和 perfcounter sysfs 介面。
