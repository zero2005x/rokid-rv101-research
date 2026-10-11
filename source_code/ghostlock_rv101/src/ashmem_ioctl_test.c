#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#define __ASHMEMIOC 0x77
#define ASHMEM_NAME_LEN 256
#define ASHMEM_SET_NAME _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME _IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_SIZE _IOR(__ASHMEMIOC, 3, size_t)

int main(void) {
  printf("ASHMEM_SET_NAME = 0x%08lx\n", (unsigned long)ASHMEM_SET_NAME);
  printf("ASHMEM_GET_NAME = 0x%08lx\n", (unsigned long)ASHMEM_GET_NAME);
  printf("ASHMEM_GET_SIZE = 0x%08lx\n", (unsigned long)ASHMEM_GET_SIZE);

  int fd = open("/dev/ashmem", O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    printf("open /dev/ashmem failed errno=%d (%s)\n", errno, strerror(errno));
    return 1;
  }
  printf("open /dev/ashmem OK fd=%d\n", fd);

  char name[ASHMEM_NAME_LEN];
  memset(name, 0x41, sizeof(name));
  name[ASHMEM_NAME_LEN - 1] = 0;
  name[21] = 0;  /* as the exploit does: NUL at offset 21 */

  errno = 0;
  int r = ioctl(fd, ASHMEM_SET_NAME, name);
  printf("SET_NAME -> r=%d errno=%d (%s)\n", r, errno, strerror(errno));

  char nm[ASHMEM_NAME_LEN];
  memset(nm, 0, sizeof(nm));
  errno = 0;
  r = ioctl(fd, ASHMEM_GET_NAME, nm);
  printf("GET_NAME -> r=%d errno=%d (%s)\n", r, errno, strerror(errno));
  if (r == 0) {
    printf("GET_NAME[0..24]:");
    for (int i = 0; i < 24; i++) printf(" %02x", (unsigned char)nm[i]);
    printf("\n");
    printf("GET_NAME string: %s\n", nm + 11);
  }

  size_t sz = 0;
  errno = 0;
  r = ioctl(fd, ASHMEM_GET_SIZE, &sz);
  printf("GET_SIZE -> r=%d errno=%d (%s) size=%zu\n", r, errno, strerror(errno), sz);

  close(fd);
  return 0;
}
