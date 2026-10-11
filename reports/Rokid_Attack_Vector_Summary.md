# Rokid AR 眼鏡攻擊向量總結報告

> English: [Rokid_Attack_Vector_Summary_EN.md](Rokid_Attack_Vector_Summary_EN.md)

> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。

> ⚠ **2026-09-13 更正**：本文提及的 **Fastboot／Magisk／boot.img 刷機路線已確認不可行**（取不到 boot.img：`fastboot fetch` 不支援、`dd` 需 root、無相容 recovery／OTA 來源），且 **root 已於 2026-08-22 由 GhostLock LPE 取得**（執行期，每 boot 以 adb 武裝一次）。請以下列段落為歷史評估，勿照做刷機步驟。


## 🎯 執行摘要

**設備**: Rokid AR Glasses (RG-glasses / RV101)  
**內核**: Linux 5.10.209-perf-gb80a8319d11b  
**Android**: 12 (API 32)  
**安全補丁**: 2024-07-05

---

## 🔴 關鍵發現：Bootloader 已解鎖！

### 確認的解鎖狀態

| 屬性                          | 值           | 含義                |
| ----------------------------- | ------------ | ------------------- |
| `ro.boot.vbmeta.device_state` | **unlocked** | 設備狀態已解鎖      |
| `ro.boot.verifiedbootstate`   | **orange**   | 橙色 = 可刷自訂系統 |
| `ro.boot.flash.locked`        | **0**        | Flash 未鎖定        |
| `ro.oem_unlock_supported`     | **1**        | 支援 OEM 解鎖       |
| `ro.boot.slot_suffix`         | **\_a**      | 當前使用 A 槽位     |

### 分區結構 (A/B 動態分區)

```
boot_a     → /dev/block/mmcblk0p17  ← 當前使用
boot_b     → /dev/block/mmcblk0p18
vendor_boot_a → /dev/block/mmcblk0p24
vendor_boot_b → /dev/block/mmcblk0p25
vbmeta_a   → /dev/block/mmcblk0p78
vbmeta_b   → /dev/block/mmcblk0p79
dtbo_a     → /dev/block/mmcblk0p45
dtbo_b     → /dev/block/mmcblk0p46
```

### � 推薦攻擊路徑：Fastboot + Magisk

> **⚠️ 阻塞點**: 需要 boot.img 文件，目前無法取得！

**這是理論上最直接的 root 方式，但目前被阻塞：**

```bash
# 步驟摘要 (需要先取得 boot.img)
1. 取得原廠 boot.img (❌ 目前無法完成)
2. 使用 Magisk App 修補 boot.img
3. adb reboot recovery → 選擇 Enter Fastboot
4. fastboot flash boot_a magisk_patched_boot.img
5. fastboot reboot
```

**無法取得 boot.img 的原因：**

- `dd /dev/block/mmcblk0p17` → Permission denied
- `fastboot fetch` → 設備不支持
- 無法找到官方 OTA 韌體或相容 TWRP

詳細操作請參考: [Rokid_Root_Guide_boot_img.md](./Rokid_Root_Guide_boot_img.md)

---

## 📊 其他攻擊向量分析 (全部失敗)

### 1. CVE-2023-2163 BPF Verifier 漏洞

| 項目     | 狀態                      |
| -------- | ------------------------- |
| 漏洞類型 | BPF Verifier 分支剪枝錯誤 |
| 目標內核 | 4.15 - 6.4                |
| 當前內核 | 5.10.209                  |
| **狀態** | ❌ **已修補**             |

**測試結果**: 所有 8 個探測都顯示漏洞已被修補。5.10.209 包含了反向移植的安全修復。

### 2. CVE-2024-36971 + BPF Heap Spray

| 項目         | 狀態                               |
| ------------ | ---------------------------------- |
| 漏洞類型     | 路由表 UAF (Use-After-Free)        |
| 目標 slab    | ip_dst_cache (192 bytes)           |
| BPF 可用性   | ✅ unprivileged_bpf_disabled=0     |
| Heap Spray   | ✅ 成功噴灑 16384 個 192-byte 物件 |
| **UAF 觸發** | ⚠️ 未成功觸發                      |

**測試結果**:

- BPF heap spray 技術可行
- 成功創建 64 個 BPF maps，每個 256 entries
- 總共 ~3MB 192-byte 結構噴入內核
- 但 UAF 觸發需要真實的 ICMP 錯誤或路由操作
- msgget() 在此 Android 內核不可用

### 3. KASLR 繞過嘗試

| 項目           | 狀態                   |
| -------------- | ---------------------- |
| /proc/kallsyms | ❌ 全部顯示 0 地址     |
| /proc/modules  | ❌ 全部顯示 0x00000000 |
| kptr_restrict  | 2 (最嚴格)             |
| BPF 指標洩漏   | ❌ Verifier 阻止       |
| **狀態**       | ❌ **無法繞過**        |

### 4. 其他潛在漏洞 (5.10.x)

| CVE           | 類型                  | 修補狀態           |
| ------------- | --------------------- | ------------------ |
| CVE-2022-2585 | POSIX CPU timer UAF   | 可能已修補         |
| CVE-2022-2586 | nft_object UAF        | 需要 CAP_NET_ADMIN |
| CVE-2022-2588 | route4_filter UAF     | 需要 CAP_NET_ADMIN |
| CVE-2023-0179 | nftables 棧緩衝區溢出 | 可能已修補         |
| CVE-2023-3390 | nftables UAF          | 需要 CAP_NET_ADMIN |

**注意**: 大多數網路子系統漏洞需要 CAP_NET_ADMIN，而當前 shell 用戶沒有此權限。

### 5. CVE-2024-0582 (io_uring mmap UAF) - 🔴 新發現！

| 項目            | 狀態                          |
| --------------- | ----------------------------- |
| 漏洞類型        | io_uring ring buffer mmap UAF |
| io_uring 可用性 | ✅ **完全可用!**              |
| mmap 操作       | ✅ 成功 (SQ ring, CQ ring)    |
| features        | 0x7ff (完整功能)              |
| **狀態**        | ⚠️ **可能可利用**             |

**測試結果**:

- `io_uring_setup(64, &params)` 成功
- SQ ring mmap 成功，地址 0x7b49244000
- 關閉 fd 後仍可訪問 mmap 區域
- 需要構造完整利用程式驗證 UAF

---

## 🔒 安全配置摘要

### 有利因素 (可利用)

| 配置                      | 值         | 影響                 |
| ------------------------- | ---------- | -------------------- |
| Bootloader                | **已解鎖** | 可刷自訂 boot.img    |
| unprivileged_bpf_disabled | **0**      | 可創建 BPF maps      |
| bpf_jit_enable            | **1**      | JIT 編譯啟用         |
| sched_debug               | 可讀       | 可洩漏任務資訊       |
| **io_uring**              | **可用**   | CVE-2024-0582 可測試 |

### 不利因素 (阻礙利用)

| 配置                | 值            | 影響            |
| ------------------- | ------------- | --------------- |
| kptr_restrict       | **2**         | 阻止地址洩漏    |
| SELinux             | **Enforcing** | 限制 shell 操作 |
| seccomp             | 啟用          | 過濾系統調用    |
| 當前 UID            | 2000 (shell)  | 非特權用戶      |
| perf_event_paranoid | **3**         | perf 完全禁用   |

---

## 📋 結論與建議

### 推薦路徑

```
🥇 第一選擇: Fastboot + Magisk
   - Bootloader 已解鎖
   - 最可靠、最直接
   - 不需要內核漏洞利用
   - 可完全恢復

🥈 第二選擇: 持續測試 CVE-2024-36971
   - BPF heap spray 已驗證可行
   - 需要找到 UAF 觸發方法
   - 可能需要 root 後進一步測試

🥉 第三選擇: 掃描其他 5.10.x 漏洞
   - 大多數需要額外權限
   - 成功率較低
```

### 立即行動

1. **準備 Fastboot 環境**
   - 確認 Windows 安裝 Qualcomm USB 驅動
   - 下載最新 Magisk APK

2. **提取 boot.img**
   - 進入 Fastboot 模式
   - 使用 `fastboot getvar all` 確認分區
   - 嘗試 `fastboot fetch boot_a boot_a.img` (如支援)

3. **Magisk 修補**
   - 在電腦或其他 Android 設備上修補
   - 生成 magisk_patched_boot.img

4. **刷入並測試**
   - 先禁用 vbmeta 驗證
   - 刷入修補後的 boot.img
   - 重啟驗證 root

---

## 📁 相關文件

- [Rokid_Root_Guide_boot_img.md](./Rokid_Root_Guide_boot_img.md) - 詳細刷機指南
- [CVE-2024-36971_deep_analysis.md](./CVE-2024-36971_deep_analysis.md) - UAF 漏洞分析
- [cve_2024_36971_bpf_spray.c](./cve_2024_36971_bpf_spray.c) - BPF Heap Spray 工具
- [cve_2023_2163_probe.c](./cve_2023_2163_probe.c) - BPF Verifier 探測工具

---

_報告生成時間: **2026-01**（原寫 2025-01；內文引用 20260109 build，2026-09-13 更正）_
