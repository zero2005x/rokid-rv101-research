#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int shared_probe = 0x11111111;

static void dump_slab_mm(void) {
  FILE *f = fopen("/proc/slabinfo", "r");
  if (!f) return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char name[64];
    int a, b;
    if (sscanf(line, "%63s %d %d", name, &a, &b) == 3) {
      if (!strcmp(name, "mm_struct") || !strcmp(name, "vm_area_struct") ||
          !strcmp(name, "task_struct") || !strcmp(name, "pid") ||
          !strcmp(name, "signal_cache") || !strcmp(name, "cred_jar"))
        printf("  %-16s act=%d tot=%d\n", name, a, b);
    }
  }
  fclose(f);
}

int main(void) {
  pid_t kids[8];
  printf("t0:\n");
  dump_slab_mm();
  for (int i = 0; i < 8; i++) {
    kids[i] = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
    if (kids[i] < 0) { printf("fork FAILED errno=%d\n", errno); return 1; }
    if (kids[i] == 0) {
      /* child: write a unique value to the shared global */
      shared_probe = 0x22220000 + i;
      syscall(SYS_exit, 0);
    }
  }
  for (int i = 0; i < 8; i++) waitpid(kids[i], NULL, 0);
  printf("after 8 fork+exit children, parent sees shared_probe=0x%x (expect 0x11111111 if separate mm, changed if shared!)\n", shared_probe);
  printf("t1:\n");
  dump_slab_mm();
  sleep(2);
  printf("t2(+2s):\n");
  dump_slab_mm();
  return 0;
}
