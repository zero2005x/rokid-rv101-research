/*
 * run_shell.c — drop from root to shell (uid/gid 2000), then run a command.
 *
 * Purpose: when install-recovery.sh (root) starts the ghostlock bridge helper,
 * run the helper as shell(2000) to match the exploit's original unprivileged
 * environment.
 *
 * Build (Android NDK, aarch64):
 *   <NDK>/toolchains/llvm/prebuilt/<host>/bin/aarch64-linux-android<api>-clang \
 *       -O2 -static -o run_shell run_shell.c
 */
#include <unistd.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <cmd> [args...]\n", argv[0]);
        return 1;
    }
    if (setgid(2000) != 0 || setuid(2000) != 0) {
        perror("setgid/setuid");
        return 1;
    }
    execv(argv[1], &argv[1]);
    perror("execv");
    return 1;
}
