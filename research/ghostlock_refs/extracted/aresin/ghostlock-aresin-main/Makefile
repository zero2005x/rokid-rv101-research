API ?= 28
PROJECT ?= aresin
OUTDIR ?= build/$(PROJECT)/bin
EMBEDDIR ?= build/embed

TARGET_DIR := src/targets/$(PROJECT)
TARGET_HEADER := $(TARGET_DIR)/target.h

ifeq ($(wildcard $(TARGET_HEADER)),)
$(error unknown PROJECT=$(PROJECT), missing $(TARGET_HEADER))
endif

define pick_src
$(if $(wildcard $(TARGET_DIR)/$(1)),$(TARGET_DIR)/$(1),src/$(1))
endef

EMBED_SU := $(EMBEDDIR)/su_daemon_aarch64_pie
PRELOAD := $(OUTDIR)/preload.so

CORE_SRCS := \
  $(call pick_src,main.c) \
  $(call pick_src,util.c) \
  $(call pick_src,slide.c) \
  $(call pick_src,fops.c) \
  $(call pick_src,pipe.c) \
  src/root.c
PRELOAD_SRCS := $(CORE_SRCS) src/preload.c src/su_blob.S src/wallpaper_blob.S

.DEFAULT_GOAL := all

# Android NDK path (using local NDK 25)
NDK_ROOT := /d/Android/ndk/25.1.8937393
NDK_TOOLCHAIN := $(NDK_ROOT)/toolchains/llvm/prebuilt/windows-x86_64
NDK_CC := $(NDK_TOOLCHAIN)/bin/aarch64-linux-android$(API)-clang

# Use NDK compiler
TARGET_CC := $(NDK_CC)
TARGET_FLAGS :=
TARGET_COMMON_LDFLAGS :=
TARGET_PIE_LDFLAGS :=

COMMON_CFLAGS := -O2 -g0 -Wall -Wextra -Isrc
PIE_CFLAGS := -fPIE -pie $(COMMON_CFLAGS)
SO_CFLAGS := -fPIC $(COMMON_CFLAGS)
WARN_CFLAGS := -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function
TARGET_CFLAGS := -include targets/$(PROJECT)/target.h

.PHONY: all preload executable clean info

all: preload

preload: $(PRELOAD)

$(OUTDIR):
	mkdir -p $@

$(EMBEDDIR):
	mkdir -p $@

$(EMBED_SU): src/su_daemon.c | $(EMBEDDIR)
	$(TARGET_CC) $(TARGET_FLAGS) $(PIE_CFLAGS) $(TARGET_CFLAGS) \
	  $< -o $@

$(PRELOAD): $(PRELOAD_SRCS) $(EMBED_SU) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h | $(OUTDIR)
	$(TARGET_CC) $(TARGET_FLAGS) $(SO_CFLAGS) $(WARN_CFLAGS) $(TARGET_CFLAGS) \
	  $(PRELOAD_SRCS) \
	  -shared -o $@ -pthread
	sha256sum $@

info:
	@echo "PROJECT=$(PROJECT)"
	@echo "TARGET_DIR=$(TARGET_DIR)"
	@echo "TARGET_CC=$(TARGET_CC)"
	@echo "TARGET_FLAGS=$(TARGET_FLAGS)"
	@echo "PRELOAD=$(PRELOAD)"
	@echo "EMBED_SU=$(EMBED_SU)"
	@echo "CORE_SRCS=$(CORE_SRCS)"

clean:
	rm -rf build
