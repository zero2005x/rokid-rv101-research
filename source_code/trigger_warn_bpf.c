#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
union bpf_attr {
    struct {
        int map_type;
        int key_size;
        int value_size;
        int max_entries;
    };
};
int main() {
    union bpf_attr attr = {0};
    attr.map_type = 1;
    attr.key_size = 0;
    attr.value_size = 0;
    attr.max_entries = 0;
    syscall(321, 0, &attr, sizeof(attr));
    return 0;
}
