# Makefile for KnotFS - Async Log-Structured File System (Teaching Edition)
# Zero external dependencies.  Builds on x86_64, ARM64, ARM32.

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c11
TARGET   = knotfs_test

# Detect or override target architecture
#   make              -> native (x86_64 on this host)
#   make ARCH=arm64   -> aarch64-linux-gnu-gcc
#   make ARCH=arm32   -> arm-linux-gnueabihf-gcc
#   make ARCH=arm32-lite -> arm-linux-gnueabi-gcc (soft-float)
ARCH    ?= native

ifeq ($(ARCH),arm64)
    CC    = aarch64-linux-gnu-gcc
    CFLAGS += -static
else ifeq ($(ARCH),arm32)
    CC    = arm-linux-gnueabihf-gcc
    CFLAGS += -static
else ifeq ($(ARCH),arm32-lite)
    CC    = arm-linux-gnueabi-gcc
    CFLAGS += -static
endif

# Source files
SOURCES  = knotfs.c knotfs_test.c
OBJECTS  = $(SOURCES:.c=.o)

# ----- Targets -----

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c knotfs.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TARGET)
	@echo "=============================================="
	@echo "  ARCH=$(ARCH)  CC=$(CC)"
	@echo "=============================================="
	./$(TARGET)

# Multi-arch build (compiles for all supported architectures)
all-arch:
	@$(MAKE) clean --no-print-directory
	@echo "[1/3] Building for x86_64..."
	@$(MAKE) ARCH=native --no-print-directory
	@mv $(TARGET) $(TARGET)_x86_64
	@echo "[2/3] Building for ARM64..."
	@$(MAKE) clean --no-print-directory
	@$(MAKE) ARCH=arm64 --no-print-directory 2>/dev/null && \
		mv $(TARGET) $(TARGET)_arm64 && \
		file $(TARGET)_arm64 || \
		echo "  SKIP: aarch64-linux-gnu-gcc not found"
	@echo "[3/3] Building for ARM32..."
	@$(MAKE) clean --no-print-directory
	@$(MAKE) ARCH=arm32 --no-print-directory 2>/dev/null && \
		mv $(TARGET) $(TARGET)_arm32 && \
		file $(TARGET)_arm32 || \
		echo "  SKIP: arm-linux-gnueabihf-gcc not found"
	@echo ""
	@echo "Build complete. Binaries:"
	@ls -la $(TARGET)_* 2>/dev/null || true

clean:
	rm -f $(OBJECTS) $(TARGET) $(TARGET)_x86_64 $(TARGET)_arm64 $(TARGET)_arm32
