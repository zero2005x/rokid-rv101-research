# Rokid AR Glasses (RV101) 權限提升漏洞研究報告

> English: [COMPREHENSIVE_PRIVESC_REPORT_EN.md](COMPREHENSIVE_PRIVESC_REPORT_EN.md)

> ⚠ **歷史文件（非現況）**：本文為當時的紀錄；專案其後已有重大變化（root 已取得、引擎改執行期探測、手機 UI 重做、i18n 193 key）。
> 現行狀態請看 `README.md`、`MANIFEST.md`、`docs/deploy.md`。


## 設備資訊

| 項目    | 值                                |
| ------- | --------------------------------- |
| 型號    | Rokid RV101 (NEO LUNA-V2)         |
| 平台    | Qualcomm Technologies             |
| GPU     | Adreno 621v1 (Chip ID: 0x6020100) |
| Kernel  | 5.10.209-perf-gb80a8319d11b       |
| Android | 12                                |
| SELinux | Enforcing (u:r:shell:s0)          |
| Seccomp | Disabled                          |
| User    | shell (UID 2000)                  |

## 攻擊面總結

### 1. KGSL (GPU Driver) - /dev/kgsl-3d0 ✅

**狀態**: 可存取 (mode 0666)

**已確認的漏洞**:

1. **Use-After-Free (UAF)**
   - mmap 在 fd 關閉後仍可讀寫
   - 但 GPU 地址不會被重用（即使經過 5000 次 spray）
   - **結論**: UAF 存在但不可利用

2. **Out-of-Bounds Read (OOB)**
   - 可使用負偏移讀取其他 GPU 緩衝區
   - 未發現內核指標

3. **Shadow Buffer**
   - GPU addr: 0xeffff000, 大小: 32KB
   - 可 mmap（唯讀）
   - 僅包含計時器值，無內核指標洩漏

### 2. I2C - /dev/i2c-1 ✅ **重要發現**

**狀態**: 可存取 (mode 0666)

**發現的設備**:
| 地址 | 類型 | 可寫 | 詳情 |
|------|------|------|------|
| 0x08 | PMIC (電源管理) | ✅ | ID=0x22, 可控制電源軌 |
| 0x30 | 顯示控制器 | ✅ | 256 個可讀寄存器 |
| 0x3F | RTC/傳感器 | ✅ | 包含時間數據 |
| 0x64 | 電池管理 IC | ✅ | 顯示電量 31% |

**攻擊潛力**:

- **PMIC 控制**: 可能導致電源異常或系統重置
- **顯示控制器**: 可能有調試/後門功能
- **電池 IC**: 可能偽造電量數據

### 3. SPI - /dev/rt600_spidev ✅

**狀態**: 可存取 (mode 0666)

- SPI Mode: 0x03
- Max Speed: 12 MHz
- Bits: 8
- 所有 Flash 指令返回零（設備可能未連接或需初始化）

### 4. Serial - /dev/ttyHS0 ✅

**狀態**: 可寫入

- 高速串口
- 波特率: B4096
- 無待處理數據
- **可能連接外部協處理器**

### 5. Binder ⚠️

**狀態**: 可存取，但受限

- BC_ENTER_LOOPER 導致程序阻塞（預期行為）
- 大多數系統服務需要特權

### 6. DMA-BUF Heaps ❌

**狀態**: 權限被拒

- qcom,system, qcom,qseecom 等 heap 無法存取

### 7. ADSPRPC (DSP RPC) ❌

**狀態**: 唯讀

- /dev/adsprpc-smd 可讀但無法 RDWR
- ioctl 返回 ENOTTY

### 8. QSEE/QCE ❌

**狀態**: 權限被拒

- /dev/qseecom, /dev/qce 無法存取

## 可能的攻擊向量

### 高優先級

1. **I2C 記憶體洩漏**
   - EEPROM 讀取返回非 EEPROM 數據
   - 可能是驅動程序漏洞
   - 建議進一步研究

2. **KGSL 競爭條件**
   - UAF 雖然存在但難以利用
   - 考慮跨進程 spray 或時序攻擊

### 中優先級

3. **SPI Flash 攻擊**
   - rt600_spidev 完全可控
   - 若能初始化設備，可能讀寫外部 flash

4. **Serial 協處理器**
   - ttyHS0 可能連接 RT600 協處理器
   - 發送特定命令可能觸發漏洞

### 低優先級

5. **Property Service**
   - 可設置 persist.\* 屬性
   - 無法設置 ro._ 或 ctl._ 屬性

## 已排除的攻擊向量

| 向量                       | 原因                 |
| -------------------------- | -------------------- |
| Dirty Pipe (CVE-2022-0847) | 已修補               |
| SUID 二進制                | 無 SUID 可執行文件   |
| DRM/Graphics               | SELinux 阻止         |
| ION Allocator              | 設備不存在           |
| Kernel Symbols             | KASLR 啟用，地址歸零 |

## 文件列表

```
/data/local/tmp/privesc_scanner      # 權限掃描器
/data/local/tmp/kgsl_deep_research   # KGSL 深度研究
/data/local/tmp/kgsl_uaf_v2          # UAF 利用嘗試
/data/local/tmp/kgsl_massive_spray   # 大規模 spray
/data/local/tmp/kgsl_shadow_v2       # Shadow 分析
/data/local/tmp/i2c_attack           # I2C 攻擊
/data/local/tmp/spi_serial_attack    # SPI/Serial 攻擊
/data/local/tmp/dsp_ion_attack       # DSP/ION 攻擊
/data/local/tmp/adsprpc_dmabuf       # ADSPRPC 探索
```

## 建議下一步

1. **深入 I2C 研究**: 分析返回的可疑數據，可能存在驅動程序緩衝區溢出
2. **RT600 協處理器**: 研究 SPI/Serial 通訊協議，嘗試與協處理器互動
3. **Vendor HAL 分析**: 反編譯廠商 HAL 庫尋找漏洞
4. **內核模組分析**: 研究 Qualcomm/Rokid 特定內核模組

## 結論

目前未發現可直接利用的權限提升漏洞。主要障礙：

1. SELinux 強制模式限制大部分操作
2. KASLR 防止內核地址洩漏
3. KGSL UAF 存在但 GPU 地址管理器不重用已釋放地址

I2C 驅動程序的可疑行為值得進一步研究，可能是最有希望的攻擊向量。
