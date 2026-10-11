/* mmfree_probe — does free_mm run synchronously at memfd close, or via a
 * lazy RCU wave? Clones N children, pins each mm with /proc/pid/mem, kills
 * the children, closes the memfds, then watches the mm_struct line of
 * /proc/slabinfo (active_objs num_objs objsize objperslab pagesperslab,
 * active_slabs num_slabs) over time. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sched.h>
#include <time.h>

#define N_CHILDREN 128

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int read_mm_line(const char *tag) {
  FILE *f = fopen("/proc/slabinfo", "r");
  if (!f) return -1;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    if (!strncmp(line, "mm_struct ", 10)) {
      char name[64];
      unsigned long aobjs, nobjs, objsize, objs, ppers;
      unsigned long aslabs, nslabs;
      if (sscanf(line, "%63s %lu %lu %lu %lu %lu", name, &aobjs, &nobjs,
                 &objsize, &objs, &ppers) == 6) {
        char *sd = strstr(line, "slabdata");
        if (sd && sscanf(sd, "slabdata %lu %lu", &aslabs, &nslabs) == 2) {
          printf("[%7.1fms] %s mm_struct: objs=%lu/%lu slabs=%lu/%lu\n",
                 now_ms(), tag, aobjs, nobjs, aslabs, nslabs);
        }
      }
      break;
    }
  }
  fclose(f);
  return 0;
}

static pid_t clone_pause(void) {
  pid_t c = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (c == 0) {
    for (;;) pause();
  }
  return c;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  double t0 = now_ms();
  printf("[%7.1fms] mmfree_probe: N=%d\n", 0.0, N_CHILDREN);

  pid_t kids[N_CHILDREN];
  int fds[N_CHILDREN];
  int clone_fail = 0;
  read_mm_line("baseline");

  for (int i = 0; i < N_CHILDREN; i++) {
    kids[i] = clone_pause();
    if (kids[i] < 0) { clone_fail++; kids[i] = 0; }
  }
  printf("[%7.1fms] cloned (fail=%d)\n", now_ms() - t0, clone_fail);
  read_mm_line("after-clone");

  int open_fail = 0;
  for (int i = 0; i < N_CHILDREN; i++) {
    if (!kids[i]) continue;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", kids[i]);
    fds[i] = open(path, O_RDONLY);
    if (fds[i] < 0) open_fail++;
  }
  printf("[%7.1fms] memfd pins open (fail=%d)\n", now_ms() - t0, open_fail);

  /* kill all — the memfd pins keep the mms alive */
  for (int i = 0; i < N_CHILDREN; i++) {
    if (kids[i]) { kill(kids[i], SIGKILL); waitpid(kids[i], NULL, 0); }
  }
  printf("[%7.1fms] all killed+reaped (mms pinned)\n", now_ms() - t0);
  read_mm_line("after-kill-pinned");

  /* close all memfds: this drops the last mm refs */
  for (int i = 0; i < N_CHILDREN; i++) {
    if (fds[i] >= 0) { close(fds[i]); fds[i] = -1; }
  }
  printf("[%7.1fms] all memfds closed\n", now_ms() - t0);
  read_mm_line("just-after-closes");

  /* watch for lazy waves: 50 x 100ms */
  for (int i = 0; i < 50; i++) {
    usleep(100000);
    read_mm_line(i % 5 == 0 ? "wave-watch" : "");
  }
  return 0;
}
