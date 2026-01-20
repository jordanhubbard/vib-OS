#!/bin/bash
# UnixOS Toolchain Setup Script
# Installs all dependencies required to build UnixOS on macOS

set -e

echo "========================================"
echo "UnixOS Toolchain Setup"
echo "========================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    log_error "This script is designed for macOS"
    exit 1
fi

# Check for Homebrew
if ! command -v brew &> /dev/null; then
    log_info "Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
else
    log_info "Homebrew is already installed"
fi

# Update Homebrew
log_info "Updating Homebrew..."
brew update

# Prefer Apple/Xcode clang if available; otherwise fall back to Homebrew LLVM.
if xcrun --find clang >/dev/null 2>&1; then
    log_info "Xcode/Command Line Tools clang found; will use Apple clang by default"
    NEED_BREW_LLVM=0
else
    log_warn "Xcode/Command Line Tools not found; installing Homebrew LLVM as fallback"
    NEED_BREW_LLVM=1
fi

# LLD is required for ELF linking on macOS (provided by Homebrew)
log_info "Installing LLD linker..."
brew install lld

if [ "$NEED_BREW_LLVM" -eq 1 ]; then
    log_info "Installing LLVM toolchain (fallback)..."
    brew install llvm
fi

# Add LLD (and LLVM if installed) to PATH for this session
LLD_PREFIX="$(brew --prefix lld)"
export PATH="$LLD_PREFIX/bin:$PATH"
if [ "$NEED_BREW_LLVM" -eq 1 ]; then
    LLVM_PREFIX="$(brew --prefix llvm)"
    export PATH="$LLVM_PREFIX/bin:$PATH"
fi

# Install additional build tools
log_info "Installing build tools..."
brew install \
    nasm \
    mtools \
    xorriso \
    qemu \
    make \
    cmake \
    ninja \
    python3 \
    wget \
    curl \
    git \
    dosfstools \
    e2fsprogs

# Install ARM64 cross-compilation tools
log_info "Installing ARM64 GNU toolchain..."
brew install aarch64-elf-gcc aarch64-elf-binutils || {
    log_warn "ARM64 GCC not available via Homebrew, will use LLVM"
}

# Create toolchain configuration file
log_info "Creating toolchain configuration..."
cat > toolchain.mk << 'EOF'
# Auto-generated toolchain configuration
# Source this file or include in Makefile

XCRUN_CLANG := $(shell xcrun --find clang 2>/dev/null)
XCRUN_CXX := $(shell xcrun --find clang++ 2>/dev/null)

# Homebrew prefixes
LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
LLD_PREFIX := $(shell brew --prefix lld 2>/dev/null)

LLVM_BIN := $(LLVM_PREFIX)/bin
LLD_BIN := $(LLD_PREFIX)/bin

# Toolchain binaries (prefer Xcode/CLT clang if present)
export CC := $(if $(strip $(XCRUN_CLANG)),$(XCRUN_CLANG),$(LLVM_BIN)/clang)
export CXX := $(if $(strip $(XCRUN_CXX)),$(XCRUN_CXX),$(LLVM_BIN)/clang++)
export LD := $(LLD_BIN)/ld.lld
export AR := $(if $(wildcard $(LLVM_BIN)/llvm-ar),$(LLVM_BIN)/llvm-ar,ar)
export AS := $(if $(strip $(XCRUN_CLANG)),$(XCRUN_CLANG),$(LLVM_BIN)/clang)
export OBJCOPY := $(if $(wildcard $(LLVM_BIN)/llvm-objcopy),$(LLVM_BIN)/llvm-objcopy,)
export OBJDUMP := $(if $(wildcard $(LLVM_BIN)/llvm-objdump),$(LLVM_BIN)/llvm-objdump,)
export STRIP := $(if $(wildcard $(LLVM_BIN)/llvm-strip),$(LLVM_BIN)/llvm-strip,strip)
export NM := $(if $(wildcard $(LLVM_BIN)/llvm-nm),$(LLVM_BIN)/llvm-nm,nm)
export RANLIB := $(if $(wildcard $(LLVM_BIN)/llvm-ranlib),$(LLVM_BIN)/llvm-ranlib,ranlib)

# Add to PATH
export PATH := $(LLD_BIN):$(LLVM_BIN):$(PATH)
EOF

# Create shell configuration for manual builds
cat > toolchain.env << 'EOF'
#!/bin/bash
# Source this file before manual builds
# Usage: source toolchain.env

export LLD_PREFIX="$(brew --prefix lld)"

XCRUN_CLANG="$(xcrun --find clang 2>/dev/null || true)"
XCRUN_CXX="$(xcrun --find clang++ 2>/dev/null || true)"
LLVM_PREFIX="$(brew --prefix llvm 2>/dev/null || true)"

export PATH="$LLD_PREFIX/bin:$PATH"
if [ -n "$LLVM_PREFIX" ]; then
  export PATH="$LLVM_PREFIX/bin:$PATH"
fi

export LD="$LLD_PREFIX/bin/ld.lld"

if [ -n "$XCRUN_CLANG" ]; then
  export CC="$XCRUN_CLANG"
  export CXX="$XCRUN_CXX"
  export AS="$XCRUN_CLANG"
else
  export CC="$LLVM_PREFIX/bin/clang"
  export CXX="$LLVM_PREFIX/bin/clang++"
  export AS="$LLVM_PREFIX/bin/clang"
fi

if [ -n "$LLVM_PREFIX" ]; then
  export AR="$LLVM_PREFIX/bin/llvm-ar"
  export OBJCOPY="$LLVM_PREFIX/bin/llvm-objcopy"
  export OBJDUMP="$LLVM_PREFIX/bin/llvm-objdump"
else
  export AR="ar"
fi

echo "Toolchain configured for ARM64 cross-compilation"
EOF

chmod +x toolchain.env

# Verify installation
log_info "Verifying toolchain installation..."
echo ""

echo -n "Clang: "
if clang --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "LLD Linker: "
if "${LLD_PREFIX}/bin/ld.lld" --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "QEMU ARM64: "
if qemu-system-aarch64 --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "Make: "
if make --version | head -1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo ""
echo "========================================"
echo "Toolchain setup complete!"
echo "========================================"
echo ""
echo "Next steps:"
echo "  1. Source the environment: source toolchain.env"
echo "  2. Build the OS: make all"
echo "  3. Test in QEMU: make qemu"
echo ""
