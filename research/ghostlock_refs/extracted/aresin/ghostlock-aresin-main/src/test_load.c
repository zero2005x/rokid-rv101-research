/*
 * test_load.c - 简单的测试程序，用于加载 preload.so
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    printf("=== 测试加载 preload.so ===\n");
    printf("PID: %d\n", getpid());
    printf("等待 5 秒...\n");
    sleep(5);
    printf("测试完成!\n");
    return 0;
}
