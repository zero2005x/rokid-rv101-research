cmake_minimum_required(VERSION 3.22.1)
project("ghostlock")

add_library(ghostlock SHARED
    ghostlock-bridge.c
    # 未来加入 GhostLock exploit 源文件：
    # trigger.c
    # spray.c
    # rb_write.c
    # pwn.c
)

target_link_libraries(ghostlock
    log
    dl
)

target_compile_options(ghostlock PRIVATE
    -Wall -Wextra
    -O2
    -fvisibility=hidden
    -fno-stack-protector
)
