# Rokid AR Glasses - CVE 探測結果報告 (2026-02-03)

> English: [CVE_PROBE_REPORT_20260203_EN.md](CVE_PROBE_REPORT_20260203_EN.md)
> ⚠ **2026-09-13 更正**：本文提及的 **Fastboot／Magisk／boot.img 刷機路線已確認不可行**（取不到 boot.img：`fastboot fetch` 不支援、`dd` 需 root、無相容 recovery／OTA 來源），且 **root 已於 2026-08-22 由 GhostLock LPE 取得**（執行期，每 boot 以 adb 武裝一次）。請以下列段落為歷史評估，勿照做刷機步驟。


## 設備信息

- **Kernel**: 5.10.209
- **Android**: 12
- **SoC**: Qualcomm neo (SoC ID: 579)
- **GPU**: Adreno 621v1
- **安全補丁級別**: 2024-07-05

---

## 一、攻擊面發現

### 1. `/dev/kgsl-3d0` (GPU 驅動) ⭐⭐⭐⭐⭐ 最高優先級

**訪問權限**: **O_RDWR 完全可用！**

**成功的 ioctl 命令**:
| IOCTL | 返回 | 洩漏數據 |
|-------|------|----------|
| 0xc1000938 | ret=0 | GPU 版本: 0x41a, 0x41b |
| 0xc1000939 | ret=0 | (空) |
| 0xc100093a | ret=0 | 計數器: 0xe |
| 0xc1000940 | ret=0 | 狀態: 0x1 |
| 0xc1000956 | ret=0 | 大小: 0x20 |
| 0xc1000958 | ret=0 | 計數器: 0x2 |

**需要權限的命令**:

- `0xXXXX093b`: 返回 **errno=1 (EPERM)** - 敏感操作

**潛在 CVE**:

- CVE-2023-33063: KGSL Use-After-Free
- CVE-2023-33107: KGSL Integer Overflow
- CVE-2024-23380: KGSL OOB Write
- CVE-2024-43098: kgsl_ioctl_timeline_wait UAF

**sysfs 可寫文件**:

```
-rw-rw---- 1 root shell /sys/class/kgsl/kgsl-3d0/perfcounter
```

⚠️ **shell 用戶可直接寫入！** 已驗證可寫。

---

### 2. `/dev/adsprpc-smd` & `/dev/adsprpc-smd-secure` ⭐⭐⭐⭐

**訪問權限**: O_RDONLY (O_RDWR 需要權限)

**成功的 ioctl**:
| IOCTL | 設備 | 結果 |
|-------|------|------|
| 0xc00c5211 (GET_DSP_INFO) | smd | ret=0, 洩漏 DSP 信息 |
| 0xc0045208 (GETINFO) | smd-secure | ret=0, info=0x1 |
| 0xc0285206 (MMAP) | smd | errno=44 (需要 channel) |
| 0xc0405213 | smd | ret=53 |

**DSP 域信息洩漏**:

- Domain 0 (ADSP): attr=6 → cap=0xca73
- Domain 2 (SDSP): attr=6 → cap=0xca73
- Domain 3 (CDSP): attr=1 → cap=0x1 (啟用)
- Domain 3 (CDSP): attr=4 → cap=0x200000 (2MB)
- Domain 3 (CDSP): attr=6 → cap=0x8b73

**潛在 CVE**:

- **CVE-2024-43047**: ADSPRPC fastrpc_mmap UAF (ITW 已確認被利用)
- **CVE-2024-49848**: ADSPRPC UAF in refcount

**下一步**: 需要通過 HWBinder 連接到 `vendor.qti.hardware.dsp@1.0::IDspService` 獲取具有寫權限的 fd。

---

### 3. `/dev/ashmem` ⭐⭐⭐

**訪問權限**: O_RDWR 完全可用

這是 Android 共享記憶體驅動，可用於:

- Heap spray
- 記憶體佈局控制
- 與其他漏洞配合使用

---

## 二、排除的 CVE

| CVE            | 原因                                  |
| -------------- | ------------------------------------- |
| CVE-2024-1086  | `CONFIG_NF_TABLES is not set`         |
| CVE-2024-56720 | `CONFIG_BPF_STREAM_PARSER is not set` |
| CVE-2025-21655 | 需要 kernel 6.1+，當前 5.10           |
| CVE-2024-0582  | `CONFIG_IO_URING is not set`          |

---

## 三、可行的攻擊路徑

### 路徑 A: KGSL GPU 驅動 (最推薦)

1. **目標**: 利用 KGSL 已知漏洞獲得 kernel 任意讀寫
2. **優勢**:
   - O_RDWR 完全可用
   - 多個 ioctl 成功返回數據
   - 有可寫的 sysfs 文件
   - Adreno GPU 驅動歷史上漏洞較多
3. **步驟**:
   - 識別正確的 ioctl 結構
   - 觸發 UAF 或 OOB 漏洞
   - 使用 heap spray 控制記憶體佈局
   - 覆蓋 kernel 結構獲得提權

### 路徑 B: ADSPRPC (備選)

1. **目標**: 通過 CVE-2024-43047/49848 獲得 kernel UAF
2. **挑戰**: 需要 O_RDWR 或通過 HWBinder 獲取 fd
3. **步驟**:
   - 連接 IDspService HAL 服務
   - 請求 ADSPRPC fd
   - 觸發 map collision UAF
   - 使用 inotify_event_info heap spray

### 路徑 C: 組合攻擊

1. 使用 KGSL 信息洩漏繞過 KASLR
2. 使用 ashmem 進行 heap spray
3. 觸發 ADSPRPC 或 KGSL UAF
4. 覆蓋 cred 結構提權

---

## 四、下一步行動

### 優先級 1: KGSL 深度探測

- [ ] 識別 ioctl 0x3b (EPERM) 的功能
- [ ] 嘗試在正確參數下創建 GPU context
- [ ] 探索 perfcounter sysfs 利用可能性
- [ ] 研究 Adreno 621v1 特定漏洞

### 優先級 2: ADSPRPC HWBinder

- [ ] 開發 HWBinder 客戶端連接 IDspService
- [ ] 獲取具有寫權限的 ADSPRPC fd
- [ ] 測試 CVE-2024-43047 exploitation path

### 優先級 3: Boot.img 修改 (備用)

- [ ] 如果 kernel exploit 失敗，嘗試修改 boot.img

---

## 五、技術細節

### KGSL ioctl 命令格式

```
bits 0-7:   command number
bits 8-15:  magic number (0x09 for KGSL)
bits 16-29: size
bits 30-31: direction (00=none, 01=write, 10=read, 11=read+write)

Example: 0xc1000938
  Direction: 11 (R/W)
  Size: 0x100 = 256 bytes
  Magic: 0x09
  Number: 0x38 = 56
```

### ADSPRPC 設備列表

```
/dev/adsprpc-smd         mode=664 (O_RDONLY OK)
/dev/adsprpc-smd-secure  mode=664 (O_RDONLY OK, GETINFO works)
```

### HAL 服務

```
vendor.qti.hardware.dsp@1.0::IDspService/dspservice (PID 804)
```
