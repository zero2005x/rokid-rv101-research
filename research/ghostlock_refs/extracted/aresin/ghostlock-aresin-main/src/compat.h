/*
 * compat.h - 兼容性定义
 *
 * 定义 rothko exploit 中使用的函数和宏
 */

#ifndef COMPAT_H
#define COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <sys/resource.h>
#include <stdarg.h>

/* 日志函数 */
static inline void pr_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static inline void pr_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "[INFO] ");
    vfprintf(stdout, fmt, args);
    va_end(args);
}

static inline void pr_success(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "[SUCCESS] ");
    vfprintf(stdout, fmt, args);
    va_end(args);
}

static inline void pr_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[WARNING] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

/* 系统调用检查宏 */
#define SYSCHK(call) ({ \
    long __ret = (long)(call); \
    if (__ret < 0) { \
        pr_error("SYSCHK failed: %s (errno=%d)\n", #call, errno); \
        exit(1); \
    } \
    __ret; \
})

/* CPU 亲和性设置 */
static inline void pin_to_core(int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) < 0) {
        pr_warning("pin_to_core(%d) failed: %s\n", core, strerror(errno));
    }
}

/* 设置无缓冲输出 */
static inline void set_unbuffer(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* 设置资源限制 */
static inline void set_limit(void) {
    /* 提高文件描述符限制 */
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
}

#endif /* COMPAT_H */
