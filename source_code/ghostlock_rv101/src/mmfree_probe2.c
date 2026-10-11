/* mmfree_probe2 — phase A: kill without pins (does exit-time mm free run
 * synchronously?); phase B: pin via /proc/pid/mem, kill, close pins (does
 * the close free the mm — sync, lazy wave, or never?). Watches the
 * mm_struct + task_struct + kmalloc-16k slabinfo lines over 30s each. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
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

static void read_cache(const char *name, unsigned long *aobjs, unsigned long *nobjs,
                       unsigned long *aslabs, unsigned long *nslabs) {
  *aobjs = *nobjs = *aslabs = *nslabs = 0;
  FILE *f = fopen("/proc/slabinfo", "r");
  if (!f) return;
  char line[512];
  size_t nl = strlen(name);
  while (fgets(line, sizeof(line), f)) {
    if (!strncmp(line, name, nl) && line[nl] == ' ') {
      char nm[64];
      unsigned long a, n, osz, ops, pp;
      if (sscanf(line, "%63s %lu %lu %lu %lu %lu", nm, &a, &n, &osz, &ops, &pp) == 6) {
        *aobjs = a; *nobjs = n;
        char *sd = strstr(line, "slabdata");
        if (sd) sscanf(sd, "slabdata %lu %lu", aslabs, nslabs);
      }
      break;
    }
  }
  fclose(f);
}

static void watch(const char *phase, int secs) {
  unsigned long a, n, as, ns, ta, tn, tas, tns, ka, kn, kas, kns;
  printf("[%7.1fms] %s watch start\n", now_ms(), phase);
  for (int i = 0; i <= secs * 2; i++) {
    usleep(500000);
    read_cache("mm_struct", &a, &n, &as, &ns);
    read_cache("task_struct", &ta, &tn, &tas, &tns);
    read_cache("kmalloc-16k", &ka, &kn, &kas, &kns);
    printf("[%7.1fms] %s mm:%lu/%lu s%lu/%lu | task:%lu/%lu s%lu/%lu | k16k:%lu/%lu s%lu/%lu\n",
           now_ms(), phase, a, n, as, ns, ta, tn, tas, tns, ka, kn, kas, kns);
  }
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
  printf("mmfree_probe2: N=%d\n", N);

  /* Phase A: no pins — kill should free the mms at task exit */
  pid_t kids[N];
  for (int i = 0; i < N; i++) kids[i] = clone_pause();
  printf("[%7.1fms] A: cloned, killing without pins\n", now_ms());
  for (int i = 0; i < N; i++) { kill(kids[i], SIGKILL); waitpid(kids[i], NULL, 0); }
  watch("A", 10);

  /* Phase B: pins — kill, then close pins */
  int fds[N];
  for (int i = 0; i < N; i++) kids[i] = clone_pause();
  int fail = 0;
  for (int i = 0; i < N; i++) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", kids[i]);
    fds[i] = open(path, O_RDONLY);
    if (fds[i] < 0) fail++;
  }
  printf("[%7.1fms] B: cloned+pinned (fail=%d), killing\n", now_ms(), fail);
  for (int i = 0; i < N; i++) { kill(kids[i], SIGKILL); waitpid(kids[i], NULL, 0); }
  printf("[%7.1fms] B: killed (pins held), closing pins\n", now_ms());
  for (int i = 0; i < N; i++) { if (fds[i] >= 0) { close(fds[i]); fds[i] = -1; } }
  printf("[%7.1fms] B: pins closed\n", now_ms());
  watch("B", 30);
  return 0;
}
