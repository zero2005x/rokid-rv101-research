NDK ?= $(ANDROID_NDK_HOME)
ifeq ($(NDK),)
NDK := C:/Users/<USER>/AppData/Local/Android/Sdk/ndk/android-ndk-r27d
endif
API ?= 31
TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/windows-x86_64
CC := $(TOOLCHAIN)/bin/aarch64-linux-android$(API)-clang

all: build/slabprobe build/forktest

build/slabprobe: slabprobe.c
	@if not exist build mkdir build
	$(CC) -O2 -Wall -fPIE -pie -o $@ slabprobe.c

build/forktest: forktest.c
	@if not exist build mkdir build
	$(CC) -O2 -Wall -fPIE -pie -o $@ forktest.c

push: build/slabprobe build/forktest
	adb push build/slabprobe /data/local/tmp/slabprobe
	adb push build/forktest /data/local/tmp/forktest
	adb shell chmod 755 /data/local/tmp/slabprobe /data/local/tmp/forktest

clean:
	rm -rf build
