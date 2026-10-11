#include <stdio.h>
#include <linux/io_uring.h>
int main() {
    printf("IOSQE_FIXED_FILE = %d\n", IOSQE_FIXED_FILE);
    printf("IOSQE_IO_DRAIN = %d\n", IOSQE_IO_DRAIN);
    printf("IOSQE_IO_LINK = %d\n", IOSQE_IO_LINK);
    printf("IOSQE_IO_HARDLINK = %d\n", IOSQE_IO_HARDLINK);
    printf("IOSQE_ASYNC = %d\n", IOSQE_ASYNC);
    printf("IOSQE_BUFFER_SELECT = %d\n", IOSQE_BUFFER_SELECT);
    return 0;
}
