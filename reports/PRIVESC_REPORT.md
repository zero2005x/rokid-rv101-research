# Rokid AR Glasses - 權限提升漏洞研究報告

> English: [PRIVESC_REPORT_EN.md](PRIVESC_REPORT_EN.md)

> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


## 設備資訊

| 項目             | 值                                |
| ---------------- | --------------------------------- |
| **設備**         | Rokid AR Glasses (RV101)          |
| **內核**         | 5.10.209-perf                     |
| **Android 版本** | 12                                |
| **GPU**          | Adreno 621v1 (Chip ID: 0x6020100) |
| **SELinux**      | Enforcing (`u:r:shell:s0`)        |
| **當前用戶**     | UID=2000 (shell)                  |

---

## 🔴 已確認的漏洞

### 1. KGSL Use-After-Free (UAF)

**嚴重性**: 高

**描述**:
關閉 KGSL 文件描述符後，已映射的 GPU 內存仍可讀寫訪問。

**重現步驟**:

```c
// 1. 開啟 KGSL 並分配 GPU 內存
int fd = open("/dev/kgsl-3d0", O_RDWR);
struct kgsl_gpumem_alloc alloc = {.size = 0x1000};
ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc);

// 2. 映射 GPU 內存
void *map = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, offset);

// 3. 關閉 fd（觸發 UAF）
close(fd);

// 4. 仍可讀寫已釋放的內存！
*(uint64_t*)map = 0xAAAAAAAAAAAAAAAA;  // 寫入成功
printf("Read: 0x%lx\n", *(uint64_t*)map); // 讀取成功
```

**影響**:

- 潛在的內存腐敗
- 如果能觸發 GPU 地址重用，可實現任意讀寫

---

### 2. KGSL OOB Read via mmap Offset

**嚴重性**: 中

**描述**:
通過負偏移量映射，可以讀取其他 GPU 緩衝區的內容。

**測試結果**:

```
offset 0x59000 (diff -65536): val=0x4a4a4a4a4a4a4a4a  ← 其他緩衝區數據
offset 0x68000 (diff -4096): val=0x0
```

**影響**:

- 同進程內的 GPU 數據洩漏
- 無法跨進程讀取（GPU MMU 隔離）

---

## 🟡 潛在攻擊面

### 3. World-Writable 設備節點

| 設備                  | 權限      | 狀態            |
| --------------------- | --------- | --------------- |
| `/dev/kgsl-3d0`       | rw-rw-rw- | ✅ 可利用       |
| `/dev/binder`         | rw-rw-rw- | ⚠️ 需要更多研究 |
| `/dev/ashmem`         | rw-rw-rw- | ⚠️ 需要更多研究 |
| `/dev/hwbinder`       | rw-rw-rw- | ⚠️ 需要更多研究 |
| `/dev/vndbinder`      | rw-rw-rw- | ⚠️ 需要更多研究 |
| `/dev/i2c-1`          | rw-rw-rw- | ⚠️ 內存洩漏？   |
| `/dev/dri/card0`      | rw-rw-rw- | ❌ SELinux 阻擋 |
| `/dev/dri/renderD128` | rw-rw-rw- | ❌ SELinux 阻擋 |

### 4. Binder BC_ENTER_LOOPER 問題

**描述**: 發送 `BC_ENTER_LOOPER` 命令會導致程序 hang 或崩潰。

**狀態**: 這是預期行為，不是漏洞。

---

## ❌ 已排除的攻擊向量

| 向量                       | 狀態 | 原因                          |
| -------------------------- | ---- | ----------------------------- |
| Dirty Pipe (CVE-2022-0847) | ❌   | 已修補 (5.10.209 >= 5.10.102) |
| io_uring 漏洞              | ❌   | 設備未啟用 io_uring           |
| SUID 二進制                | ❌   | 未發現 SUID 文件              |
| 內核符號洩漏               | ❌   | `/proc/kallsyms` 地址為零     |
| su 二進制                  | ❌   | 未找到                        |
| debuggable 模式            | ❌   | `ro.debuggable=0`             |

---

## 🔬 技術細節

### KGSL 內存映射公式

```
GPU 地址範圍: 0x4000000000 - 0x4001f32000
mmap offset = (page_index + 1) * 0x1000
其中: page_index = (gpuaddr - 0x4000000000) / 0x1000
```

### 工作的 KGSL IOCTL

| IOCTL         | 代碼       | 功能              |
| ------------- | ---------- | ----------------- |
| GPUMEM_ALLOC  | 0xc018092f | GPU 內存分配      |
| GPUOBJ_INFO   | 0xc0200939 | 獲取 GPU 對象信息 |
| GPU_COMMAND   | 0xc0580940 | GPU 命令提交      |
| GPUOBJ_IMPORT | 0xc0280958 | 導入外部內存      |
| GPUOBJ_SYNC   | 0xc0180956 | 同步操作          |

### I2C 總線發現

- `/dev/i2c-1`: 可訪問
- I2C 功能: 0xefe0009 (I2C, Block R/W)
- 讀取結果顯示可能的**內存內容洩漏**（看到指針和 ASCII 字串）

---

## 📋 後續研究方向

1. **KGSL GPU 地址重用**
   - 嘗試更大規模的 spray
   - 研究 GPU 地址分配算法

2. **Binder 事務操作**
   - 研究 `BC_TRANSACTION` 攻擊
   - Binder 死亡通知機制

3. **I2C 寫入測試**
   - 確認是否可寫入 I2C 設備
   - 可能影響硬體配置

4. **Vendor 特定驅動**
   - `rt600_spiboot` 模組
   - `adsprpc-smd` 設備

---

## 📁 測試程序列表

| 文件                   | 用途                    |
| ---------------------- | ----------------------- |
| `privesc_scanner.c`    | 綜合權限提升掃描器      |
| `kgsl_deep_research.c` | KGSL 深度漏洞研究       |
| `kgsl_uaf_v2.c`        | KGSL UAF 利用嘗試       |
| `drm_attack.c`         | DRM/Graphics 攻擊面探測 |
| `i2c_attack.c`         | I2C 總線探測            |
| `binder_cmd_test.c`    | Binder 命令測試         |
| `binder_simple_test.c` | Binder 簡化測試         |

---

## ⚠️ 免責聲明

本研究僅用於授權的安全測試和教育目的。
未經授權使用這些技術可能違反法律。
