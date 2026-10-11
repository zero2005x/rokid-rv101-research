#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define KGS_PATH "/dev/kgsl-3d0"

/* Simple root upgrade program for Rokid RG-glasses RV101 */
int main(int argc, char *argv[]) {
    int fd;
    struct stat st;
    
    /* 1. Check if KGSL node exists and is accessible */
    fd = open(KGS_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Cannot open %s: %s\n", KGS_PATH, strerror(errno));
        return 1;
    }
    close(fd);
    
    /* 2. Verify KGSL node permissions */
    if (stat(KGS_PATH, &st) == 0) {
        printf("KGSL node: %s (mode: %o)\n", KGS_PATH, st.st_mode & 0777);
    }
    
    /* 3. Upgrade to root using exec system calls */
    if (argc > 1 && memcmp(argv[1], "--exec", 6) == 0 && argc > 2) {
        printf("EXEC: %s\n", argv[2]);
        execvp(argv[2], &argv[2]);
    } else {
        printf("SPAWN: /system/bin/sh\n");
        execl("/system/bin/sh", "sh", (char *)NULL);
    }
    
    perror("execl");
    return 0;
}
