/* mmfree_probe5 — test the flush trigger. Phase 1: kill 200 children
 * (queue 200 mm frees), watch 12s — do the frees flush by themselves?
 * Phase 2: clone 300 children ALIVE (mm allocation burst) — does the
 * pending flush fire? Phase 3: kill the 300, watch. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <time.h>

static double t0;
static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6) - t0;
}

static void read_mm(unsigned long *a, unsigned long *as) {
  *a = *as = 0;
  FILE *f = fopen("/proc/slabinfo", "r");
  if (!f) return;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    if (!strncmp(line, "mm_struct ", 10)) {
      char nm[64];
      unsigned long n, osz, ops, pp, ns;
      sscanf(line, "%63s %lu %lu %lu %lu %lu", nm, a, &n, &osz, &ops, &pp);
      char *sd = strstr(line, "slabdata");
      if (sd) sscanf(sd, "slabdata %lu %lu", as, &ns);
      break;
    }
  }
  fclose(f);
}

static pid_t clone_pause(void) {
  pid_t c = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (c == 0) { for (;;) pause(); }
  return c;
}

static void watch(const char *tag, int n, int step_ms) {
  unsigned long a, as;
  for (int i = 0; i < n; i++) {
    usleep(step_ms * 1000);
    read_mm(&a, &as);
    printf("[%7.1fms] %s mm: objs=%lu slabs=%lu\n", now_ms(), tag, a, as);
  }
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  t0 = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
  printf("mmfree_probe5\n");

  /* Phase 1: kill 200, watch */
  pid_t k[200];
  for (int i = 0; i < 200; i++) k[i] = clone_pause();
  printf("[%7.1fms] cloned 200, killing\n", now_ms());
  for (int i = 0; i < 200; i++) { kill(k[i], SIGKILL); waitpid(k[i], NULL, 0); }
  watch("P1", 24, 500);

  /* Phase 2: clone 300 ALIVE — allocation burst */
  pid_t a[300];
  int fail = 0;
  for (int i = 0; i < 300; i++) { a[i] = clone_pause(); if (a[i] < 0) fail++; }
  printf("[%7.1fms] cloned 300 alive (fail=%d)\n", now_ms(), fail);
  watch("P2", 10, 300);

  /* Phase 3: kill the 300, watch */
  for (int i = 0; i < 300; i++) { if (a[i] > 0) { kill(a[i], SIGKILL); waitpid(a[i], NULL, 0); } }
  printf("[%7.1fms] killed the 300\n", now_ms());
  watch("P3", 10, 500);
  return 0;
}
