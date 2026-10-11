API ?= 35

NDK_ROOT ?= $(or $(ANDROID_NDK_HOME),$(ANDROID_NDK_ROOT),/opt/android-ndk)
NDK_CC := $(NDK_ROOT)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang

# SELinux-disabler build: only the Write 1 path. slide.c, pipe.c, root.c and
# miniadb.c from the full GhostLock exploit are intentionally excluded — the
# CFI/leak/pipe stage and the cred/root/bootstrap code are not needed here.
SRCS := \
  src/core/main.c \
  src/core/util.c \
  src/core/fops.c

CFLAGS := -O2 -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
  -Wno-unused-variable -Wno-unused-but-set-variable \
  -Isrc/core -Isrc/devices -DTARGET_CONFIG_H=\"target.h\"
LDFLAGS := -fPIE -pie -pthread

.PHONY: all clean

all: selinux_disabler

selinux_disabler: $(SRCS)
	$(NDK_CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -f selinux_disabler
