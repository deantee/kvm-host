#!/bin/bash

# KVM Integration Test Runner
# Runs comprehensive end-to-end tests for the judge system with KVM

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
KERNEL_PATH="${1:-/boot/vmlinuz-linux}"
INITRD_PATH="${2:-$PROJECT_ROOT/initrd_builder/build/initrd.img}"
DISK_PATH="${3:-/dev/root}"

echo "=== KVM Judge System Integration Tests ==="
echo ""
echo "Configuration:"
echo "  Kernel:  $KERNEL_PATH"
echo "  Initrd:  $INITRD_PATH"
echo "  Disk:    $DISK_PATH"
echo ""

# Verify files exist
if [ ! -f "$KERNEL_PATH" ]; then
    echo "Error: Kernel not found at $KERNEL_PATH"
    echo "Usage: $0 [kernel_path] [initrd_path] [disk_path]"
    exit 1
fi

if [ ! -f "$INITRD_PATH" ]; then
    echo "Error: Initrd not found at $INITRD_PATH"
    echo "Build it with: cd $PROJECT_ROOT/initrd_builder && ./build_initrd.sh manual"
    exit 1
fi

# Verify binary exists
if [ ! -f "$PROJECT_ROOT/build/kvm-host" ]; then
    echo "Error: kvm-host binary not found"
    echo "Build it with: cd $PROJECT_ROOT && make"
    exit 1
fi

# Build test binary if needed
if [ ! -f "$PROJECT_ROOT/build/test_kvm_integration" ]; then
    echo "[*] Building KVM integration test..."
    cd "$PROJECT_ROOT"
    gcc -O2 -Wall -I./judge/include \
        judge/tests/test_kvm_integration.c \
        judge/src/judge_module.c \
        judge/src/judge_core.c \
        judge/src/judge_executor.c \
        judge/src/judge_verdict.c \
        judge/src/judge_resource.c \
        judge/src/judge_comparison.c \
        -o build/test_kvm_integration -lrt -lpthread
    echo "[+] Built: build/test_kvm_integration"
fi

echo ""
echo "[*] Starting KVM with judge enabled..."
echo ""
echo "IMPORTANT: Run the test in another terminal:"
echo "  cd $PROJECT_ROOT"
echo "  ./build/test_kvm_integration"
echo ""
echo "Press Ctrl+C in that terminal when done, then Ctrl+C here to shutdown KVM"
echo ""

# Start KVM (will block until shutdown)
sudo "$PROJECT_ROOT/build/kvm-host" \
    -k "$KERNEL_PATH" \
    -i "$INITRD_PATH" \
    -d "$DISK_PATH" \
    --judge-enable

echo ""
echo "[*] KVM shutdown complete"
echo ""
