# GhostLock (CVE-2026-43499) — POCO F3 GT (aresin) 偏移提取报告

## 设备信息

| 项目 | 值 |
|------|-----|
| 设备 | POCO F3 GT (codename: aresin) |
| 芯片 | MediaTek Dimensity 1200 (MT6893) |
| 系统 | Android 13 / MIUI V14.0.4.0.TKJCNXM |
| 内核 | 4.14.186-g0dc1d312efb3 |
| 架构 | aarch64 (ARM64) |
| CONFIG_FUTEX_PI | y ✅ |
| CONFIG_RT_MUTEXES | y ✅ |
| CONFIG_ARM64_VA_BITS | 39 |

## 漏洞条件检查

| 条件 | 状态 |
|------|------|
| 内核版本范围 (2.6.39 ~ 7.0.4) | ✅ 4.14.186 在范围内 |
| CONFIG_FUTEX_PI | ✅ 已启用 |
| CONFIG_RT_MUTEXES | ✅ 已启用 |
| 架构 | ✅ aarch64 |
| CONFIG_RANDOMIZE_BASE | ✅ 已启用 (KASLR) |

## 提取的关键偏移

### rt_mutex_waiter 结构体 (4.14.186 ARM64)

来源: `rt_mutex_init_waiter` 和 `remove_waiter` 反汇编分析

| 字段 | 偏移 | 来源函数 | 验证指令 |
|------|------|----------|----------|
| tree_entry | 0x00 | rt_mutex_init_waiter | `STR X0, [X0, #0x0]` |
| pi_tree_entry | 0x18 | rt_mutex_init_waiter | `STR X8, [X0, #0x18]` |
| task | 0x30 | rt_mutex_init_waiter | `STR XZR, [X0, #0x30]` |
| lock | 0x38 | remove_waiter | `LDR X8, [X0, #0x38]` |
| prio | 0x40 | task_blocks_on_rt_mutex | `STR W8, [X22, #0x40]` |
| deadline | 0x48 | task_blocks_on_rt_mutex | `STR X10, [X22, #0x48]` |

### task_struct 关键字段 (4.14.186 MT6893)

来源: `commit_creds`, `task_blocks_on_rt_mutex`, `rt_mutex_adjust_prio_chain` 反汇编

| 字段 | 偏移 | 来源函数 | 验证指令 |
|------|------|----------|----------|
| prio | 0x84 | task_blocks_on_rt_mutex | `LDR W8, [X20, #0x84]` |
| real_cred | 0x788 | commit_creds | `LDR X19, [X20, #0x788]` |
| cred | 0x790 | commit_creds | `LDR X8, [X20, #0x790]` |
| deadline | 0x388 | task_blocks_on_rt_mutex | `LDR X10, [X20, #0x388]` |
| pi_lock | 0x85c | task_blocks_on_rt_mutex | `ADD X24, X2, #0x85c` |
| pi_waiters | 0x868 | rt_mutex_adjust_prio_chain | `LDR X8, [X19, #0x868]` |
| pi_top_task | 0x870 | rt_mutex_adjust_prio_chain | `LDR X8, [X19, #0x870]` |
| pi_blocked_on | 0x880 | rt_mutex_adjust_prio_chain | `LDR X25, [X19, #0x880]` |

### 内核符号地址

| 符号 | 地址 | 来源 |
|------|------|------|
| KIMAGE_TEXT_BASE | 0xffffff939bc80000 | /proc/kallsyms |
| commit_creds | 0xffffff939bce41e0 | /proc/kallsyms |
| prepare_kernel_cred | 0xffffff939bce4578 | /proc/kallsyms |
| selinux_enforcing | 0xffffff939dd00b28 | enforcing_setup 分析 |
| INIT_TASK | 0xffffff939d8dc058 | swapper/0 字符串位置推断 |
| INIT_CRED | 0xffffff939dae3850 | prepare_kernel_cred 数据引用 |
| ENTRY_TASK | 0xffffff939d404028 | __switch_to 引用 (per-CPU) |
| PER_CPU_OFFSET | 0xffffff939d404028 | per_cpu 区域基址 |
| ROOT_TASK_GROUP | 0xffffff939d8ce1f0 | fork_init 引用 |

### 内存布局

| 项目 | 值 |
|------|-----|
| CONFIG_ARM64_VA_BITS | 39 |
| PAGE_OFFSET | 0xffffff8000000000 |
| PHYS_OFFSET | 0x40000000 |
| KIMAGE_TEXT_BASE | 0xffffff939bc80000 |

## 与 rothko (6.1.138) 偏移对比

| 字段 | aresin (4.14.186) | rothko (6.1.138) | 差异 |
|------|-------------------|------------------|------|
| WAITER_TREE_ENTRY | 0x00 | 0x00 | 相同 |
| WAITER_PI_TREE_ENTRY | 0x18 | 0x18 | 相同 |
| WAITER_TASK | 0x30 | 0x30 | 相同 |
| WAITER_LOCK | 0x38 | 0x38 | 相同 |
| WAITER_PRIO | 0x40 | 0x44 | **不同** |
| WAITER_DEADLINE | 0x48 | 0x48 | 相同 |
| TASK_PRIO | 0x84 | 0x84 | 相同 |
| TASK_REAL_CRED | 0x788 | 0x830 | **不同** |
| TASK_CRED | 0x790 | 0x838 | **不同** |
| TASK_PI_LOCK | 0x85c | 0x924 | **不同** |
| TASK_PI_BLOCKED_ON | 0x880 | 0x958 | **不同** |
| SELINUX_ENFORCING | 0xffffff939dd00b28 | 0xffffffc00a2293d0 | **不同** |

## 提取方法

### 使用的工具
1. **adb shell + su**: 从设备提取 /proc/kallsyms
2. **magiskboot**: 从 boot.img 解压 kernel
3. **Python + struct**: 解析 ARM64 指令，提取偏移

### 关键分析函数
- `rt_mutex_init_waiter`: 确认 waiter 结构体基本偏移
- `remove_waiter`: 确认 lock/prio/deadline 偏移
- `commit_creds`: 提取 real_cred/cred 偏移
- `task_blocks_on_rt_mutex`: 提取 pi_lock/prio/deadline 偏移
- `rt_mutex_adjust_prio_chain`: 提取 pi_waiters/pi_top_task/pi_blocked_on 偏移
- `enforcing_setup`: 定位 selinux_enforcing 地址
- `prepare_kernel_cred`: 定位 init_cred 指针
- `fork_init`: 定位 root_task_group 引用
- `__switch_to`: 定位 entry_task 引用

## 注意事项

1. **INIT_TASK 和 INIT_CRED 地址**: 这些是基于静态分析的估计值。数据符号在 kallsyms 中未导出，地址可能需要在运行时验证。

2. **KASLR**: 内核启用了 KASLR，实际运行时地址可能与编译时地址不同。exploit 需要实现 KASLR slide 检测。

3. **4.14.x vs 6.1.x 差异**:
   - rt_mutex_waiter 使用 rb_node 而非 plist_node
   - task_struct 布局有显著差异
   - 某些字段 (uclamp, ww_ctx) 在 4.14 中不存在

4. **编译注意事项**:
   - 需要 Android NDK r27+
   - 目标架构: aarch64
   - 4.14.x 内核没有 uclamp 相关字段，需要在代码中处理

## 测试建议

1. 先用 Shizuku 或 adb shell 测试基本功能
2. 观察 dmesg 输出，确认漏洞触发
3. 如果内核 panic 但没有提权成功，需要调整偏移
4. 注意: 运行会导致设备重启!
