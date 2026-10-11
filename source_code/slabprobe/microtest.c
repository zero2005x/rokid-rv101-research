/* microtest: fork 238 (14 slabs), kill them, watch mm_struct node-visible
 * slabs + order2 at 50ms resolution. Decides: do the frees happen at exit,
 * do they hide slabs, and does the pobjects>13 drain fire (+14 jump). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static void pin0(void) {
  cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

static int poll_mm(void) {
  static char buf[65536];
  int fd = open("/proc/slabinfo", O_RDONLY);
  if (fd < 0) return -1;
  size_t n = 0;
  for (;;) {
    ssize_t r = read(fd, buf + n, sizeof(buf) - 1 - n);
    if (r <= 0) break;
    n += (size_t)r;
    if (n >= sizeof(buf) - 1) break;
  }
  close(fd);
  if (!n) return -2;
  buf[n] = 0;
  char *p = strstr(buf, "mm_struct");
  if (!p) return -3;
  char *sd = strstr(p, "slabdata");
  if (!sd) return -4;
  return atoi(sd + 8);
}

static pid_t child_here(void) {
  pid_t p = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (p == 0) { pin0(); for (;;) pause(); }
  return p;
}

static int poll_o2(void) {
  static char buf[16384];
  int fd = open("/proc/pagetypeinfo", O_RDONLY);
  if (fd < 0) return -1;
  size_t n = 0;
  for (;;) {
    ssize_t r = read(fd, buf + n, sizeof(buf) - 1 - n);
    if (r <= 0) break;
    n += (size_t)r;
    if (n >= sizeof(buf) - 1) break;
  }
  close(fd);
  if (!n) return -2;
  buf[n] = 0;
  char *p = strstr(buf, "Free pages count per migrate type at order");
  if (!p) return -3;
  char *u = strstr(p, "Unmovable");
  if (!u) return -4;
  int a, b, c;
  if (sscanf(u, "Unmovable %d %d %d", &a, &b, &c) != 3) return -5;
  return c;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  pin0();
  printf("t0 mm=%d o2=%d\n", poll_mm(), poll_o2());
  int N = 238;
  pid_t kids[300];
  int ok = 0;
  for (int i = 0; i < N; i++) {
    kids[i] = child_here();
    if (kids[i] > 0) ok++;
  }
  printf("forked %d/%d, mm=%d o2=%d\n", ok, N, poll_mm(), poll_o2());
  for (int i = 0; i < ok; i++) {
    kill(kids[i], SIGKILL);
    waitpid(kids[i], NULL, 0);
  }
  printf("killed all, mm=%d o2=%d\n", poll_mm(), poll_o2());
  for (int t = 0; t < 40; t++) {
    usleep(50000);
    printf("t+%dms mm=%d o2=%d\n", (t + 1) * 50, poll_mm(), poll_o2());
  }
  return 0;
}
