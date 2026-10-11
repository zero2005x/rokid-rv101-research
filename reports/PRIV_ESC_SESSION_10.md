# Rokid AR Glasses 提權漏洞研究報告 - Session 10

> English: [PRIV_ESC_SESSION_10_EN.md](PRIV_ESC_SESSION_10_EN.md)

> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。

> ⚠ **2026-09-13 更正**：本文假設的 Fastboot／Magisk／刷 boot.img 路線**不可行**（取不到 boot.img、`fastboot flash/boot/erase` 被移除）；root 已由 GhostLock LPE 於執行期取得。


**日期**: 2026-01-31  
**目標**: Rokid AR Glasses (Android 12, Kernel 5.10.209)  
**研究員**: AI Assistant

---

## 📊 環境安全總結

### 有利因素 ✅

| 項目            | 狀態      | 說明                           |
| --------------- | --------- | ------------------------------ |
| **Bootloader**  | ✅ 已解鎖 | `unlocked:yes`, `secure:no`    |
| **BPF**         | ✅ 可用   | `unprivileged_bpf_disabled=0`  |
| **TIPC Socket** | ✅ 可用   | 所有 socket 類型可創建和綁定   |
| **Seccomp**     | ✅ 無限制 | `Seccomp: 0`                   |
| **dmesg**       | ✅ 可讀   | `dmesg_restrict=0`             |
| **KGSL GPU**    | ✅ 可訪問 | `/dev/kgsl-3d0` world-writable |
| **DRM/DRI**     | ✅ 可開啟 | `/dev/dri/card0` 可訪問        |
| **Keyring**     | ✅ 可用   | `add_key` syscall 可用         |
| **Ashmem**      | ✅ 可訪問 | `/dev/ashmem` world-writable   |

### 不利因素 ❌

| 項目               | 狀態         | 說明                          |
| ------------------ | ------------ | ----------------------------- |
| **Userfaultfd**    | ❌ 受限      | 需要 CAP_SYS_PTRACE           |
| **SLAB 加固**      | ❌ 啟用      | `SLAB_FREELIST_HARDENED=y`    |
| **SLAB 隨機化**    | ❌ 啟用      | `SLAB_FREELIST_RANDOM=y`      |
| **System V IPC**   | ❌ 禁用      | `CONFIG_SYSVIPC is not set`   |
| **User Namespace** | ❌ 禁用      | `CONFIG_USER_NS is not set`   |
| **nf_tables**      | ❌ 禁用      | `CONFIG_NF_TABLES is not set` |
| **kptr_restrict**  | ❌ 啟用      | 值=2，核心指針被隱藏          |
| **SELinux**        | ❌ Enforcing | 需要繞過                      |

---

## 🔍 測試過的漏洞

### 1. CVE-2024-38401 (KGSL SyncSource UAF)

**狀態**: ⚠️ 理論可行，實際未觸發

**測試版本**: v6, v7, v8 (Pipe spray, Keyring spray, Cross-cache)

**發現**:

- `/dev/kgsl-3d0` 完全可訪問
- SYNCSOURCE IOCTLs 功能正常
- BPF heap spray 成功創建 150,000+ 物件
- Keyring spray 可用
- 未能觸發核心崩潰

**原因分析**:

1. `SLAB_FREELIST_HARDENED` 防止簡單的 UAF 利用
2. KGSL 驅動可能在 5.10.209 已修補引用計數
3. 競爭窗口極小，需要更精確的時序控制

### 2. CVE-2024-36971 (Route Cache UAF)

**狀態**: ❌ 不可利用

**原因**:

- 設備無外部網路連接
- Loopback 介面的 DST 永不 obsolete
- 需要真實 ICMP 錯誤觸發

### 3. CVE-2024-0582 (io_uring PBUF_RING UAF)

**狀態**: ❌ 功能不存在

**原因**:

- PBUF_RING 是 Linux 5.19+ 才引入
- 5.10.209 沒有此功能

### 4. Dirty Pipe (CVE-2022-0847)

**狀態**: ❌ 已修補

**原因**:

- 5.10.209 > 5.10.102 (修補版本)

### 5. TIPC 協議

**狀態**: ⭐ 潛在可利用

**發現**:

```
TIPC activated (version 2.0.0)
AF_TIPC/STREAM socket: ✅ 可創建
AF_TIPC/DGRAM socket: ✅ 可創建
AF_TIPC/SEQPACKET socket: ✅ 可創建
AF_TIPC/RDM socket: ✅ 可創建
TIPC bind: ✅ 成功
```

**潛在漏洞**:

- CVE-2022-0435: TIPC stack overflow (需要網路)
- CVE-2021-43267: tipc_crypto_key_rcv heap overflow (需要啟用加密)

---

## 🛠️ 創建的工具

| 文件                   | 用途                              |
| ---------------------- | --------------------------------- |
| `kgsl_uffd_exploit.c`  | KGSL UAF + Userfaultfd (需要 CAP) |
| `kgsl_pipe_spray.c`    | KGSL UAF + Pipe buffer spray      |
| `kgsl_keyring_spray.c` | KGSL UAF + Keyring cross-cache    |
| `tipc_probe.c`         | TIPC 功能探測                     |
| `msm_drm_probe.c`      | MSM DRM 驅動探測                  |

---

## 🎯 下一步建議

### ~~優先路徑 1: Boot.img 修補 (最可行)~~（**2026-09-13：不可行；root 已由 GhostLock LPE 取得**）

由於 **Bootloader 已解鎖**，這仍是最直接的路徑：

1. **獲取官方固件**: 聯繫 Rokid 或搜索開發者社區
2. **提取 boot.img**: 使用 payload-dumper
3. **修補**: Magisk v30.6 已安裝
4. **刷入**: `fastboot flash boot magisk_patched.img`

### 優先路徑 2: TIPC 本地漏洞

TIPC 模組已載入且 socket 可用。研究方向：

- tipc_group_create 相關漏洞
- tipc_link 狀態機漏洞
- 本地觸發的記憶體損壞

### 優先路徑 3: 其他驅動

1. **synx_driver**: 已載入，需要研究介面
2. **msm_drm**: 需要 graphics 組權限
3. **Binder**: 複雜但可能有本地漏洞

### 優先路徑 4: 應用層攻擊

1. **Debuggable App**: `com.cooper.wheellog.debug` 可被調試
2. **Rokid AssistServer**: 擁有大量權限的系統應用
3. **Intent 劫持**: 研究 Rokid 專屬應用的通訊

---

## 📝 核心配置摘要

```
CONFIG_BPF=y                    # BPF 可用 ⭐
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT_ALWAYS_ON=y
CONFIG_USERFAULTFD=y            # UFFD 存在但需要權限
CONFIG_TIPC=y                   # TIPC 可用 ⭐
CONFIG_IO_URING=y
CONFIG_KEYS=y
CONFIG_FUSE_FS=y
CONFIG_SLUB_DEBUG=y
CONFIG_SLAB_FREELIST_RANDOM=y
CONFIG_SLAB_FREELIST_HARDENED=y # 堆利用困難
# CONFIG_KASAN is not set       # 無內存檢測
# CONFIG_SYSVIPC is not set     # msg_msg 不可用
# CONFIG_USER_NS is not set     # 無用戶命名空間
# CONFIG_NF_TABLES is not set   # 無 nftables
```

---

## 🔐 設備資訊

```
設備: Rokid AR Glasses (RG-glasses / RV101)
Android: 12 (API 32)
Kernel: 5.10.209-perf-gb80a8319d11b
Patch Level: 2024-07-05 (落後 ~19 個月)
SoC: Qualcomm SM6375 (neo)
GPU: Adreno 621
Bootloader: UNLOCKED
SELinux: Enforcing
```

---

**報告生成時間**: 2026-01-31 Session 10
