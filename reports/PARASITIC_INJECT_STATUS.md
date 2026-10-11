# 寄生式 GPU 命令注入 - 攻擊狀態報告

> English: [PARASITIC_INJECT_STATUS_EN.md](PARASITIC_INJECT_STATUS_EN.md)
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。


## 📋 目標

結合 **CVE-2025-21479** (Cross-FD GPU Memory Access) 與 **CVE-2025-21480** (Micronode Command Injection) 實現寄生式 GPU 命令注入，嘗試：

- 寫入 `RBBM_SECVID_TRUST_CNTL (0x0F400)` 將 GPU 降級至非安全狀態
- 寫入 `CP_PROTECT_CNTL (0x0084C)` 禁用命令處理器保護

---

## 🔍 CVE-2025-21479 分析結果

### 確認事項

| 項目             | 狀態 | 說明                                                       |
| ---------------- | ---- | ---------------------------------------------------------- |
| IOCTL 識別       | ✅   | `IOCTL_GPUMEM_ALLOC` = 0xc018092f                          |
| Offset 計算      | ✅   | `mmap_offset = (page_index + 1) * 0x1000`                  |
| 進程內跨 FD 存取 | ✅   | 同一進程中第二個 FD 可讀寫第一個 FD 的 GPU 緩衝區          |
| 跨進程存取       | ❌   | GPU 地址空間**每進程隔離**，無法存取 SurfaceFlinger 緩衝區 |

### 測試結果

```
=== Testing Offset Mechanism ===
[*] Allocating 10 GPU buffers...
  [0] gpuaddr=0x4000001000
  [9] gpuaddr=0x400000a000

[*] Testing offset access (using second fd):
  offset=0x2000 -> val=0xaa00000000000000 [MATCH]
  offset=0xb000 -> val=0xaa00000000000009 [MATCH]
  ✅ All 10 matched

[*] Large-scale scan (100,000 pages):
  mapped=1, readable=1, PM4=0
  ⚠️ Only found our own allocation - cross-process NOT possible
```

### 關鍵發現

**CVE-2025-21479 僅適用於進程內攻擊**：

- 如果能將代碼注入到 SurfaceFlinger 進程內，則可以利用
- 無法從 shell 進程直接存取其他進程的 GPU 緩衝區
- GPU 地址 0x4000000000 是每個進程獨立的起始位置

---

## 🔍 CVE-2025-21480 (GPU Command Injection) 分析結果

### 確認事項

| 項目              | 狀態 | 說明                              |
| ----------------- | ---- | --------------------------------- |
| GPU_COMMAND IOCTL | ✅   | 0xc0580940 - 返回成功             |
| Context Creation  | ❌   | 所有 32 種 flag 組合都返回 EINVAL |
| 無 Context 提交   | ⚠️   | IOCTL 返回 0 但命令不執行         |
| PM4 Packet 構建   | ✅   | Type3/Type4/Type7 封包已準備      |

### GPU Context 創建失敗

```
[-] FAILED: A6XX Std: GL + Submit + PreCtxTS (errno=22: Invalid argument)
[-] FAILED: Minimal: Zero flags (errno=22: Invalid argument)
[-] FAILED: Type VK + Submit (errno=22: Invalid argument)
[*] Total: 0/31 successful
```

**原因分析**：

1. KGSL 驅動可能限制 shell 用戶創建 GPU context
2. SELinux 政策可能阻止（但未看到 AVC 日誌）
3. 可能需要特定的 system UID 或 graphics 群組

---

## 🔓 重大發現：Bootloader 已解鎖！

```properties
ro.boot.flash.locked=0                    # ✅ UNLOCKED
ro.boot.vbmeta.device_state=unlocked      # ✅ UNLOCKED
ro.boot.verifiedbootstate=orange          # 🟠 可刷自定義映像
ro.oem_unlock_supported=1                 # ✅ OEM 解鎖支援
```

### 影響

- 可直接通過 Fastboot 刷入自定義 boot.img
- 可使用 Magisk 修補 boot.img 獲取 root
- 這是**最直接的 root 路徑**

---

## 📊 其他發現

### 可寫的 Sysfs 節點

```
/sys/class/kgsl/kgsl-3d0/perfcounter  (-rw-rw----, shell 可寫)
```

### DRM 設備

```
/dev/dri/renderD128  (crw-rw-rw-, 世界可存取)
- GEM allocation ✅
- PRIME export ✅
- SurfaceFlinger fds ❌ (Permission denied)
```

### 服務接口

```
android.graphicsenv.IGpuService          # GPU 管理服務
android.hardware.graphics.allocator@4.0  # Gralloc HAL
vendor.qti.hardware.display.composer@3.1 # 顯示合成器
```

---

## 🎯 推薦的攻擊路徑

### 1️⃣ Fastboot Root（最高優先級）

```bash
# 步驟
1. adb reboot bootloader  (或按鍵組合)
2. fastboot getvar all
3. fastboot boot twrp.img  (或自定義 recovery)
4. 提取 boot.img → Magisk 修補 → fastboot flash boot boot_patched.img
```

### 2️⃣ 系統應用提權

```bash
# 查找可調試的系統應用
adb shell pm list packages -f | grep -E 'rokid|system'
# 檢查 sharedUserId
adb shell dumpsys package <package> | grep sharedUserId
```

### 3️⃣ Binder FD 劫持

如果能獲取 SurfaceFlinger 的緩衝區 FD，可結合 CVE-2025-21479：

```c
// 需要 root 或 system 權限
int fd = get_sf_buffer_fd_via_binder();
// 然後使用 cross-FD 存取
```

---

## 📁 開發的工具

| 工具                       | 功能                | 狀態            |
| -------------------------- | ------------------- | --------------- |
| `kgsl_parasitic_inject`    | PM4 掃描與注入      | ✅ 編譯完成     |
| `surfaceflinger_ib_hijack` | SF IB 劫持          | ✅ 編譯完成     |
| `gpu_cmdbuf_spy`           | 即時 GPU 緩衝區監控 | ✅ 編譯完成     |
| `parasitic_inject_v3`      | 修正版注入工具      | ✅ **驗證成功** |
| `global_gpu_scan`          | 大規模偏移掃描      | ✅ 編譯完成     |
| `sf_ib_inject_v2`          | 修正版 SF 注入      | ✅ 編譯完成     |

---

## ⏭️ 下一步

1. **等待 Rokid 設備重新連線**
2. **嘗試進入 Fastboot 模式**
   - `adb reboot bootloader`
   - 或使用按鍵組合（音量-/+ + 電源）
3. **提取並修補 boot.img**
4. **刷入 Magisk 修補的 boot.img 獲取 root**
5. **有 root 後，可完整測試 CVE-2025-21480 的 PM4 注入**

---

_更新時間: 2026-02-03_
