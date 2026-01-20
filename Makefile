# Vib-OS Master Makefile
# ARM64 OS for Apple Silicon and Raspberry Pi

# ============================================================================
# Configuration
# ============================================================================

# Target architecture
ARCH := arm64
TARGET := aarch64-elf

# Directories
ROOT_DIR := $(shell pwd)
BUILD_DIR := $(ROOT_DIR)/build
BOOT_DIR := $(ROOT_DIR)/boot
KERNEL_DIR := $(ROOT_DIR)/kernel
DRIVERS_DIR := $(ROOT_DIR)/drivers
LIBC_DIR := $(ROOT_DIR)/libc
USERSPACE_DIR := $(ROOT_DIR)/userspace
RUNTIMES_DIR := $(ROOT_DIR)/runtimes
IMAGE_DIR := $(ROOT_DIR)/image
SYSROOT := $(BUILD_DIR)/sysroot

# Detect OS
UNAME_S := $(shell uname -s)

# Optional local toolchain overrides (created by scripts/setup-toolchain*.sh)
-include toolchain.mk

# Toolchain - Support both macOS (Homebrew) and Linux (system/apt)
ifeq ($(UNAME_S),Darwin)
    # macOS: prefer Xcode/CLT clang; if not installed, fall back to Homebrew LLVM.
    BREW_PREFIX ?= $(shell command -v brew >/dev/null 2>&1 && brew --prefix || true)
    LLVM_BIN ?= $(firstword \
        $(wildcard $(BREW_PREFIX)/opt/llvm/bin) \
        $(wildcard /opt/homebrew/opt/llvm/bin) \
        $(wildcard /usr/local/opt/llvm/bin))
    LLD_BIN ?= $(firstword \
        $(wildcard $(BREW_PREFIX)/opt/lld/bin) \
        $(wildcard /opt/homebrew/opt/lld/bin) \
        $(wildcard /usr/local/opt/lld/bin))
    ifneq ($(LLVM_BIN),)
        export PATH := $(LLVM_BIN):$(PATH)
    endif
    ifneq ($(LLD_BIN),)
        export PATH := $(LLD_BIN):$(PATH)
    endif

    DETECTED_CC := $(firstword $(shell xcrun --find clang 2>/dev/null) $(wildcard $(LLVM_BIN)/clang) $(shell command -v clang 2>/dev/null))
    DETECTED_LD := $(firstword $(wildcard $(LLD_BIN)/ld.lld) $(wildcard $(LLVM_BIN)/ld.lld) $(shell command -v ld.lld 2>/dev/null))
    DETECTED_AR := $(firstword $(wildcard $(LLVM_BIN)/llvm-ar) $(shell command -v llvm-ar 2>/dev/null) $(shell command -v ar 2>/dev/null))
    DETECTED_OBJCOPY := $(firstword $(wildcard $(LLVM_BIN)/llvm-objcopy) $(shell command -v llvm-objcopy 2>/dev/null))
    DETECTED_OBJDUMP := $(firstword $(wildcard $(LLVM_BIN)/llvm-objdump) $(shell command -v llvm-objdump 2>/dev/null))

    ifneq ($(filter undefined default,$(origin CC)),)
        CC := $(DETECTED_CC)
    endif
    ifneq ($(filter undefined default,$(origin AS)),)
        AS := $(CC)
    endif
    ifneq ($(filter undefined default,$(origin LD)),)
        LD := $(DETECTED_LD)
    endif
    ifneq ($(filter undefined default,$(origin AR)),)
        AR := $(DETECTED_AR)
    endif
    ifneq ($(filter undefined default,$(origin OBJCOPY)),)
        OBJCOPY := $(DETECTED_OBJCOPY)
    endif
    ifneq ($(filter undefined default,$(origin OBJDUMP)),)
        OBJDUMP := $(DETECTED_OBJDUMP)
    endif

    ifeq ($(CC),)
        $(error clang not found. Install Xcode Command Line Tools (xcode-select --install) or Homebrew LLVM (brew install llvm))
    endif
    ifeq ($(LD),)
        $(error ld.lld not found. Install LLD (brew install lld) or set LD=/path/to/ld.lld)
    endif
else
    # Linux (Ubuntu/Debian/etc.) - use system LLVM or allow override
    ifneq ($(filter undefined default,$(origin CC)),)
        CC := clang
    endif
    ifneq ($(filter undefined default,$(origin AS)),)
        AS := clang
    endif
    ifneq ($(filter undefined default,$(origin LD)),)
        LD := ld.lld
    endif
    ifneq ($(filter undefined default,$(origin AR)),)
        AR := llvm-ar
    endif
    ifneq ($(filter undefined default,$(origin OBJCOPY)),)
        OBJCOPY := llvm-objcopy
    endif
    ifneq ($(filter undefined default,$(origin OBJDUMP)),)
        OBJDUMP := llvm-objdump
    endif

    ifeq ($(shell command -v $(CC) 2>/dev/null),)
        $(error clang not found. Run: sudo apt install clang lld)
    endif
    ifeq ($(shell command -v $(LD) 2>/dev/null),)
        $(error ld.lld not found. Run: sudo apt install lld)
    endif
endif

# Cross-compilation target
CROSS_TARGET := --target=aarch64-unknown-none-elf

# Compiler flags
# CPU target: generic works on QEMU and most ARM64 hardware
CFLAGS_COMMON := -Wall -Wextra -Wno-unused-function -ffreestanding -fstack-protector-strong \
                 -fno-pic -mcpu=cortex-a72 -O2 -g

CFLAGS_KERNEL := $(CFLAGS_COMMON) $(CROSS_TARGET) \
                 -I$(KERNEL_DIR)/include -I$(KERNEL_DIR) \
                 -mgeneral-regs-only \
                 -fno-builtin -nostdlib -nostdinc \
                 -DARCH_ARM64

CFLAGS_USER := -Wall -Wextra -O2 -g \
               --target=aarch64-linux-musl \
               --sysroot=$(SYSROOT)

LDFLAGS_KERNEL := -nostdlib -static -T $(KERNEL_DIR)/linker.ld

# QEMU configuration
QEMU := qemu-system-aarch64
QEMU_MACHINE := virt,gic-version=3
QEMU_CPU := max
QEMU_MEMORY := 4G
QEMU_FLAGS := -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m $(QEMU_MEMORY) \
              -nographic -serial mon:stdio \
              -drive if=none,id=hd0,format=raw,file=$(IMAGE_DIR)/unixos.img \
              -device virtio-blk-device,drive=hd0

# ============================================================================
# Main Targets
# ============================================================================

.PHONY: all clean kernel drivers libc userspace runtimes image qemu qemu-debug test help

all: kernel drivers libc userspace runtimes image
	@echo "=========================================="
	@echo "UnixOS build complete!"
	@echo "=========================================="
	@echo "Boot image: $(IMAGE_DIR)/unixos.img"
	@echo "Run 'make qemu' to test in emulator"

help:
	@echo "UnixOS Build System"
	@echo "==================="
	@echo ""
	@echo "Build targets:"
	@echo "  all          - Build everything"
	@echo "  kernel       - Build kernel only"
	@echo "  drivers      - Build device drivers"
	@echo "  libc         - Build C library"
	@echo "  userspace    - Build userspace programs"
	@echo "  runtimes     - Build Python and Node.js"
	@echo "  image        - Create bootable disk image"
	@echo ""
	@echo "Test targets:"
	@echo "  qemu         - Run in QEMU emulator"
	@echo "  qemu-debug   - Run with GDB server"
	@echo "  test         - Run test suite"
	@echo ""
	@echo "Utility targets:"
	@echo "  clean        - Remove build artifacts"
	@echo "  toolchain    - Install build dependencies"

# ============================================================================
# Directory Setup
# ============================================================================

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(BUILD_DIR)/drivers
	@mkdir -p $(BUILD_DIR)/libc
	@mkdir -p $(BUILD_DIR)/userspace
	@mkdir -p $(BUILD_DIR)/runtimes
	@mkdir -p $(SYSROOT)/usr/lib
	@mkdir -p $(SYSROOT)/usr/include
	@mkdir -p $(SYSROOT)/bin
	@mkdir -p $(SYSROOT)/sbin

$(IMAGE_DIR):
	@mkdir -p $(IMAGE_DIR)

# ============================================================================
# Kernel Build
# ============================================================================

KERNEL_SOURCES := $(shell find $(KERNEL_DIR) -name '*.c' -o -name '*.S' 2>/dev/null | grep -v '/x86_64/' | grep -v '/x86/')
# Also include ARM64-specific assembly
KERNEL_SOURCES += $(shell find $(KERNEL_DIR)/arch/arm64 -name '*.S' 2>/dev/null)
KERNEL_OBJECTS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel/%.o,$(filter %.c,$(KERNEL_SOURCES)))
KERNEL_OBJECTS += $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/kernel/%.o,$(filter %.S,$(KERNEL_SOURCES)))

# Include drivers in the kernel
DRIVER_SOURCES := $(shell find $(DRIVERS_DIR) -name '*.c' 2>/dev/null)
DRIVER_OBJECTS := $(patsubst $(DRIVERS_DIR)/%.c,$(BUILD_DIR)/drivers/%.o,$(DRIVER_SOURCES))

ALL_KERNEL_OBJECTS := $(KERNEL_OBJECTS) $(DRIVER_OBJECTS)
KERNEL_BINARY := $(BUILD_DIR)/kernel/unixos.elf

kernel: $(BUILD_DIR) $(ALL_KERNEL_OBJECTS) $(KERNEL_BINARY)
	@echo "[KERNEL] Build complete: $(KERNEL_BINARY)"

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@# Media files need FP support, compile without -mgeneral-regs-only
	@if echo "$<" | grep -q "/media/"; then \
		$(CC) $(CFLAGS_COMMON) $(CROSS_TARGET) -mcpu=cortex-a72 -I$(KERNEL_DIR)/include -fno-builtin -nostdlib -nostdinc -c $< -o $@; \
	else \
		$(CC) $(CFLAGS_KERNEL) -c $< -o $@; \
	fi

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(AS) $(CFLAGS_KERNEL) -c $< -o $@

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS_KERNEL) -I$(KERNEL_DIR)/include -c $< -o $@

$(KERNEL_BINARY): $(ALL_KERNEL_OBJECTS)
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS_KERNEL) -o $@ $^

# ============================================================================
# Drivers Build
# ============================================================================

DRIVER_SOURCES := $(shell find $(DRIVERS_DIR) -name '*.c' 2>/dev/null)
DRIVER_OBJECTS := $(patsubst $(DRIVERS_DIR)/%.c,$(BUILD_DIR)/drivers/%.o,$(DRIVER_SOURCES))

drivers: $(BUILD_DIR) $(DRIVER_OBJECTS)
	@echo "[DRIVERS] Build complete"

$(BUILD_DIR)/drivers/%.o: $(DRIVERS_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS_KERNEL) -I$(DRIVERS_DIR)/include -c $< -o $@

# ============================================================================
# C Library Build
# ============================================================================

libc: $(BUILD_DIR)
	@echo "[LIBC] Building musl libc..."
	@if [ -f $(LIBC_DIR)/Makefile ]; then \
		$(MAKE) -C $(LIBC_DIR) DESTDIR=$(SYSROOT) install; \
	else \
		echo "[LIBC] Source not yet configured"; \
	fi

# ============================================================================
# Userspace Build
# ============================================================================

userspace: $(BUILD_DIR) libc
	@echo "[USERSPACE] Building userspace programs..."
	@if [ -f $(USERSPACE_DIR)/Makefile ]; then \
		$(MAKE) -C $(USERSPACE_DIR) SYSROOT=$(SYSROOT); \
	else \
		echo "[USERSPACE] Source not yet configured"; \
	fi

# ============================================================================
# Runtimes Build (Python, Node.js)
# ============================================================================

runtimes: $(BUILD_DIR) libc
	@echo "[RUNTIMES] Building Python and Node.js..."
	@if [ -f $(RUNTIMES_DIR)/Makefile ]; then \
		$(MAKE) -C $(RUNTIMES_DIR) SYSROOT=$(SYSROOT); \
	else \
		echo "[RUNTIMES] Source not yet configured"; \
	fi

# ============================================================================
# Boot Image Creation
# ============================================================================

image: $(IMAGE_DIR) kernel drivers
	@echo "[IMAGE] Creating bootable disk image..."
	@./scripts/create-boot-image.sh $(BUILD_DIR) $(IMAGE_DIR)
	@echo "[IMAGE] Created: $(IMAGE_DIR)/unixos.img"

# ============================================================================
# QEMU Testing
# ============================================================================

qemu: kernel
	@echo "[QEMU] Starting UnixOS in emulator (direct kernel boot)..."
	@$(QEMU) -M virt,gic-version=3 -cpu max -m 4G \
		-nographic \
		-kernel $(BUILD_DIR)/kernel/unixos.elf

qemu-uefi: image
	@echo "[QEMU] Starting UnixOS with UEFI boot..."
	@echo "[QEMU] Note: Requires UEFI firmware (AAVMF)"
	@if [ ! -f /usr/share/qemu-efi-aarch64/QEMU_EFI.fd ]; then \
		echo "[ERROR] UEFI firmware not found. Install qemu-efi-aarch64 package."; \
		echo "[INFO] Using direct kernel boot instead. Run 'make qemu'"; \
		exit 1; \
	fi
	@$(QEMU) -M virt,gic-version=3 -cpu max -m 4G \
		-nographic \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
		-drive if=none,id=hd0,format=raw,file=$(IMAGE_DIR)/unixos.img \
		-device virtio-blk-device,drive=hd0

qemu-debug: kernel
	@echo "[QEMU] Starting UnixOS with GDB server on port 1234..."
	@$(QEMU) -M virt,gic-version=3 -cpu max -m 4G \
		-nographic \
		-kernel $(BUILD_DIR)/kernel/unixos.elf \
		-s -S

# ============================================================================
# Testing
# ============================================================================

test: kernel
	@echo "[TEST] Running kernel tests..."
	@./scripts/run-tests.sh

# ============================================================================
# Run in QEMU
# ============================================================================

run: kernel
	@echo "[RUN] Starting Vib-OS in QEMU..."
	@qemu-system-aarch64 -M virt,gic-version=3 -cpu max -m 4G -nographic -kernel $(KERNEL_BINARY)

run-gui: kernel
	@echo "[RUN] Starting Vib-OS with GUI display..."
	@qemu-system-aarch64 -M virt,gic-version=3 \
		-cpu max -m 512M \
		-global virtio-mmio.force-legacy=false \
		-device ramfb \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-device virtio-net-device,netdev=net0 \
		-netdev user,id=net0 \
		-audiodev coreaudio,id=snd0 \
		-device intel-hda -device hda-duplex,audiodev=snd0 \
		-serial stdio \
		-kernel $(KERNEL_BINARY)

run-gpu: kernel
	@echo "[RUN] Starting Vib-OS with virtio-GPU acceleration..."
	@qemu-system-aarch64 -M virt,gic-version=3 \
		-cpu max -m 512M \
		-global virtio-mmio.force-legacy=false \
		-device ramfb \
		-device virtio-gpu-pci \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-device virtio-net-device,netdev=net0 \
		-netdev user,id=net0 \
		-audiodev coreaudio,id=snd0 \
		-device intel-hda -device hda-duplex,audiodev=snd0 \
		-serial stdio \
		-kernel $(KERNEL_BINARY)

# ============================================================================
# Toolchain Setup
# ============================================================================

toolchain:
	@echo "[TOOLCHAIN] Installing build dependencies..."
	@if [ "$(UNAME_S)" = "Darwin" ]; then \
		./scripts/setup-toolchain.sh; \
	else \
		./scripts/setup-toolchain-linux.sh; \
	fi

# ============================================================================
# Clean
# ============================================================================

clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(IMAGE_DIR)
	@echo "[CLEAN] Done"

distclean: clean
	@echo "[DISTCLEAN] Removing all generated files..."
	@rm -rf $(SYSROOT)
