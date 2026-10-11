/* mmfree_probe3 — pin via /proc/pid/mem, kill, close pins, then queue a
 * 4096-file callback burst, and watch whether the mms free in a flush
 * wave (pin-release works, flush is batch-triggered) or never (pin leak). */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <time.h>

#define N 96

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

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  t0 = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;

  pid_t kids[N];
  int fds[N];
  for (int i = 0; i < N; i++) kids[i] = clone_pause();
  int fail = 0;
  for (int i = 0; i < N; i++) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", kids[i]);
    fds[i] = open(path, O_RDONLY);
    if (fds[i] < 0) fail++;
  }
  printf("[%7.1fms] cloned+pinned (fail=%d), killing\n", now_ms(), fail);
  for (int i = 0; i < N; i++) { kill(kids[i], SIGKILL); waitpid(kids[i], NULL, 0); }
  for (int i = 0; i < N; i++) { if (fds[i] >= 0) close(fds[i]); }
  printf("[%7.1fms] killed, pins closed\n", now_ms());

  unsigned long a, as;
  read_mm(&a, &as);
  printf("[%7.1fms] after-close mm: objs=%lu slabs=%lu\n", now_ms(), a, as);

  /* burst: 4096 file opens/closes -> 4096 call_rcu callbacks */
  for (int i = 0; i < 4096; i++) {
    int fd = open("/dev/zero", O_RDONLY);
    if (fd >= 0) close(fd);
  }
  printf("[%7.1fms] burst done\n", now_ms());

  for (int i = 0; i < 40; i++) {
    usleep(500000);
    read_mm(&a, &as);
    printf("[%7.1fms] watch mm: objs=%lu slabs=%lu\n", now_ms(), a, as);
  }
  return 0;
}
