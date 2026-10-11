# ghostlock_rv101 — NDK build
# Requires Android NDK r25+; ANDROID_NDK_HOME or NDK env var, or edit below.
NDK ?= $(ANDROID_NDK_HOME)
ifeq ($(NDK),)
NDK := C:/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d
endif
API ?= 31
TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/windows-x86_64
CC := $(TOOLCHAIN)/bin/aarch64-linux-android$(API)-clang

CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
          -fno-stack-protector -fPIE \
          -Isrc -include target_rv101.h
LDFLAGS := -pie -static-libgcc

SRCS := src/main.c src/util.c src/route.c
OBJS := $(SRCS:.c=.o)
HDRS := target_rv101.h src/common.h

all: build/ghostlock_rv101 build/perf_probe

build/ghostlock_rv101: $(OBJS)
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -o $@ $(OBJS) -pthread

build/perf_probe: src/perf_probe.c
	@if not exist build mkdir build
	$(CC) -O2 -fPIE -pie -o $@ src/perf_probe.c -pthread

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

push: build/ghostlock_rv101 build/perf_probe
	adb push build/ghostlock_rv101 /data/local/tmp/ghostlock_rv101
	adb push build/perf_probe /data/local/tmp/perf_probe
	adb shell chmod 755 /data/local/tmp/ghostlock_rv101 /data/local/tmp/perf_probe

clean:
	rm -rf build $(OBJS)
