# Linux 5.10.x 內核漏洞掃描報告
> 📌 **事實依據**：本檔涉及裝置世代、kernel config、CVE 狀態、GhostLock 位移與產品結構等事實時，一律以 [`reports/FACTS.md`](FACTS.md) 為準（單一權威事實表；衝突時以該表裁決）。

> ⚠ **2026-09-13 更正**：本文假設的 Fastboot／Magisk／刷 boot.img 路線**不可行**（取不到 boot.img、`fastboot flash/boot/erase` 被移除）；root 已由 GhostLock LPE 於執行期取得。


## 目標內核版本

**Linux 5.10.209-perf-gb80a8319d11b**

---

## 漏洞清單 (按可利用性排序)

### 🔴 高危 - 可能可利用

| CVE            | 漏洞類型          | 影響版本 | 需要權限      | 修補版本    | 評估        |
| -------------- | ----------------- | -------- | ------------- | ----------- | ----------- |
| CVE-2024-36971 | 路由表 UAF        | 5.10+    | 低權限        | 5.10.220+   | ⚠️ 需驗證   |
| CVE-2024-0582  | io_uring mmap UAF | 5.10-6.4 | 低權限        | 5.10.209 含 | 🔴 可能未修 |
| CVE-2023-6931  | perf_event 堆溢出 | 4.4-6.7  | 低權限        | 5.10.205+   | ⚠️ 需驗證   |
| CVE-2023-4623  | hfsc qdisc UAF    | 2.6.25+  | CAP_NET_ADMIN | 5.10.192+   | ❌ 需權限   |

### 🟡 中危 - 條件利用

| CVE           | 漏洞類型       | 影響版本  | 需要權限      | 修補版本  | 評估      |
| ------------- | -------------- | --------- | ------------- | --------- | --------- |
| CVE-2023-2163 | BPF Verifier   | 4.15-6.4  | 低權限        | 5.10.179+ | ✅ 已修補 |
| CVE-2023-3390 | nftables UAF   | 4.1-6.4   | CAP_NET_ADMIN | 5.10.186+ | ❌ 需權限 |
| CVE-2023-1829 | tcindex UAF    | 4.14-6.3  | CAP_NET_ADMIN | 5.10.176+ | ❌ 需權限 |
| CVE-2022-2586 | nft_object UAF | 3.16-5.19 | CAP_NET_ADMIN | 5.10.136+ | ❌ 需權限 |

### 🟢 低危 - 難以利用

| CVE            | 漏洞類型          | 原因               |
| -------------- | ----------------- | ------------------ |
| CVE-2023-0179  | nftables 棧溢出   | 需要 CAP_NET_ADMIN |
| CVE-2022-34918 | nftables 類型混淆 | 需要 CAP_NET_ADMIN |
| CVE-2022-2588  | route4 UAF        | 需要 CAP_NET_ADMIN |
| CVE-2022-2585  | POSIX timer UAF   | 可能已修補         |

---

## 重點分析

### CVE-2024-0582 (io_uring mmap UAF)

**嚴重性**: 高  
**利用難度**: 中等

```
漏洞描述:
io_uring 中的 ring buffer mmap 處理存在 UAF 漏洞。
當 ring buffer 被 unmap 後，記憶體可能被錯誤釋放，
但用戶空間仍可能持有 mmap 映射。

影響版本: 5.10 - 6.4
修補版本: 需要確認 5.10.209 是否包含修復

檢測方法:
1. 檢查 io_uring 是否可用
2. 嘗試創建 ring buffer
3. 測試 mmap/munmap 順序
```

**快速檢測**:

```bash
# 檢查 io_uring 是否可用
ls -la /dev/io_uring
# 或
cat /proc/sys/kernel/io_uring_disabled
```

### CVE-2023-6931 (perf_event 堆溢出)

**嚴重性**: 高  
**利用難度**: 中等

```
漏洞描述:
perf_event 子系統中的 perf_read_group() 函數存在
堆緩衝區溢出。當讀取 perf event group 時，
如果 group 結構被特殊構造，可能導致越界寫入。

影響版本: 4.4 - 6.7
修補版本: 5.10.205+

檢測方法:
1. 檢查 perf_event_open 是否可用
2. 設置 perf_event_paranoid 級別
3. 嘗試創建 perf event groups
```

**快速檢測**:

```bash
# 檢查 perf 權限
cat /proc/sys/kernel/perf_event_paranoid
# 值越大限制越多: -1=無限制, 0-3=遞增限制

# 嘗試 perf_event_open
strace -e perf_event_open perf stat ls 2>&1
```

### CVE-2024-36971 (路由表 UAF)

**嚴重性**: 高  
**利用難度**: 高

```
漏洞描述:
網路路由表中的 dst_entry 結構存在 UAF。
當路由 entry 被標記為過期並觸發 GC 時，
如果仍有用戶態引用，可能導致釋放後使用。

影響版本: 5.10+
修補版本: 需要確認

當前測試結果:
- BPF heap spray: ✅ 成功 (16384 個 192-byte 物件)
- UAF 觸發: ❌ 未成功
- 需要真實的 ICMP 錯誤或路由操作
```

---

## 快速檢測腳本

```c
// 5.10.x 漏洞快速檢測工具
// cve_5_10_x_scanner.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <linux/io_uring.h>

// io_uring 系統調用號 (ARM64)
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

int check_io_uring() {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    int fd = syscall(__NR_io_uring_setup, 8, &params);
    if (fd >= 0) {
        close(fd);
        return 1; // io_uring 可用
    }
    return 0;
}

int check_perf_event() {
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.size = sizeof(attr);

    int fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
    if (fd >= 0) {
        close(fd);
        return 1; // perf_event 可用
    }
    return 0;
}

int check_bpf_available() {
    int fd = open("/proc/sys/kernel/unprivileged_bpf_disabled", O_RDONLY);
    if (fd < 0) return -1;

    char buf[8];
    read(fd, buf, sizeof(buf));
    close(fd);

    return (buf[0] == '0') ? 1 : 0;
}

int main() {
    printf("=== Linux 5.10.x 漏洞掃描器 ===\n\n");

    // io_uring 檢測
    printf("[1] io_uring 可用性: ");
    if (check_io_uring()) {
        printf("✅ 可用 - CVE-2024-0582 可能可利用\n");
    } else {
        printf("❌ 不可用或需要權限\n");
    }

    // perf_event 檢測
    printf("[2] perf_event 可用性: ");
    if (check_perf_event()) {
        printf("✅ 可用 - CVE-2023-6931 可能可利用\n");
    } else {
        printf("❌ 不可用或需要權限\n");
    }

    // BPF 檢測
    printf("[3] 非特權 BPF: ");
    int bpf = check_bpf_available();
    if (bpf == 1) {
        printf("✅ 啟用 - BPF 相關漏洞可測試\n");
    } else if (bpf == 0) {
        printf("❌ 禁用\n");
    } else {
        printf("⚠️ 無法檢測\n");
    }

    // 內核版本
    printf("\n[4] 內核版本檢測:\n");
    system("uname -r");

    // perf paranoid
    printf("\n[5] Perf 權限級別:\n");
    system("cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 'N/A'");

    // kptr_restrict
    printf("\n[6] 指標限制:\n");
    system("cat /proc/sys/kernel/kptr_restrict 2>/dev/null || echo 'N/A'");

    printf("\n=== 掃描完成 ===\n");
    return 0;
}
```

---

## 下一步建議

### 高優先級

1. **CVE-2024-0582 (io_uring)**
   - 檢查 io_uring 是否可用
   - 如果可用，嘗試 mmap UAF 利用

2. **CVE-2023-6931 (perf_event)**
   - 檢查 perf_event_paranoid 設置
   - 如果 <= 2，嘗試堆溢出利用

### 低優先級

3. **繼續 CVE-2024-36971**
   - 需要找到 UAF 觸發方法
   - 可能需要 root 後進一步測試

4. **CAP_NET_ADMIN 漏洞**
   - 需要先獲得 root
   - 可用於持久化或繞過 SELinux

---

## 結論

**~~最佳路徑仍然是 Fastboot + Magisk~~（**2026-09-13：兩者皆不可行；root 已由 GhostLock LPE 取得**）**

Bootloader 已解鎖是最大的優勢。與其花時間嘗試複雜的內核漏洞利用，
直接使用 Fastboot 刷入修補後的 boot.img 是最可靠的方案。

內核漏洞利用可作為：

1. 研究學習用途
2. 備用方案（如果 Fastboot 方法失敗）
3. Root 後進一步權限提升（如繞過 SELinux）

---

_掃描報告生成時間: **2026-01**（**原寫 2025-01，與專案時序不符，2026-09-13 更正**）_
