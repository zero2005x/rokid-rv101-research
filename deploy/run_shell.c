/*
 * run_shell.c — 從 root 降權到 shell (uid/gid 2000) 後執行命令。
 *
 * 用途：install-recovery.sh（root）啟動 ghostlock bridge helper 時，
 * 讓 helper 以 shell(2000) 執行，符合 exploit 原始的 unprivileged 環境。
 *
 * 建置（Android NDK, aarch64）：
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
