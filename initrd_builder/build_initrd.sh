#!/bin/bash

# Build minimal initrd for judge guest executor
# Two build strategies: buildroot (recommended) or manual minimal rootfs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
ROOTFS_DIR="${BUILD_DIR}/rootfs"
INITRD_IMG="${BUILD_DIR}/initrd.img"
BUILD_STRATEGY="${1:-manual}"

mkdir -p "$BUILD_DIR"

download_file() {
    local url="$1"
    local output="$2"
    if command -v wget &> /dev/null; then
        wget "$url" -O "$output"
    elif command -v curl &> /dev/null; then
        curl -L "$url" -o "$output"
    else
        echo "Error: Neither wget nor curl found"
        return 1
    fi
}

build_with_buildroot() {
    echo "[*] Building with buildroot..."
    local BUILDROOT_REPO="https://github.com/buildroot/buildroot.git"
    local BUILDROOT_DIR="${BUILD_DIR}/buildroot"

    if [ ! -d "$BUILDROOT_DIR" ]; then
        echo "[*] Cloning buildroot repository..."
        cd "$BUILD_DIR"
        git clone --depth 1 "$BUILDROOT_REPO" buildroot
    fi

    echo "[*] Configuring buildroot..."
    cd "$BUILDROOT_DIR"
    cat > .config <<'BUILDROOT_CONFIG'
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_CUSTOM_VERSION=y
BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.6"
BR2_PACKAGE_BUSYBOX=y
BR2_PACKAGE_GCC=y
BR2_PACKAGE_BINUTILS=y
BR2_TARGET_GENERIC_HOSTNAME="judge-guest"
BR2_TARGET_ROOTFS_CPIO_GZIP=y
BR2_SYSTEM_BIN_SH_BASH=y
BR2_JLEVEL=$(nproc)
BUILDROOT_CONFIG

    echo "[*] Building rootfs (20-30 minutes)..."
    make -j$(nproc)

    echo "[*] Extracting rootfs..."
    mkdir -p "$ROOTFS_DIR"
    cd "$ROOTFS_DIR"
    zcat "$BUILDROOT_DIR/output/images/rootfs.cpio.gz" | cpio -idmv
}

build_manual() {
    echo "[*] Building minimal rootfs manually with toolchain..."
    mkdir -p "$ROOTFS_DIR"
    cd "$ROOTFS_DIR"

    # Create directory structure
    mkdir -p bin sbin lib lib64 etc usr/bin usr/lib usr/local/bin usr/share/man/man1 dev proc sys mnt/testdata tmp root

    # Copy essential binaries from host
    echo "[*] Copying host binaries..."
    cp /bin/sh bin/
    cp /bin/bash bin/ 2>/dev/null || true
    cp /bin/busybox bin/ 2>/dev/null || true

    # Copy compiler toolchain
    echo "[*] Copying compiler toolchain..."
    cp /usr/bin/gcc bin/ 2>/dev/null || true
    cp /usr/bin/cc bin/ 2>/dev/null || true
    cp /usr/bin/g++ bin/ 2>/dev/null || true
    cp /usr/bin/c++ bin/ 2>/dev/null || true
    cp /usr/bin/make bin/ 2>/dev/null || true
    cp /usr/bin/nm bin/ 2>/dev/null || true

    # Copy libc, header files, and essential libraries
    echo "[*] Copying libc and libraries..."
    if [ -d /lib64 ]; then
        cp /lib64/libc.so.6 lib64/ 2>/dev/null || true
        cp /lib64/ld-linux-x86-64.so.2 lib64/ 2>/dev/null || true
        cp /lib64/libc++.so* lib64/ 2>/dev/null || true
        cp /lib64/libstdc++* lib64/ 2>/dev/null || true
        cp /lib64/libgcc_s* lib64/ 2>/dev/null || true
        cp /lib64/libm.so* lib64/ 2>/dev/null || true
    fi
    if [ -d /lib ]; then
        cp /lib/ld-musl-x86_64.so.1 lib/ 2>/dev/null || true
        cp /lib/libc.musl-x86_64.so.1 lib/ 2>/dev/null || true
        cp /lib/x86_64-linux-musl/libc.so lib/ 2>/dev/null || true
    fi

    # Copy system includes for compilation
    echo "[*] Copying system headers..."
    mkdir -p usr/include
    cp -r /usr/include/sys usr/include/ 2>/dev/null || true
    cp -r /usr/include/bits usr/include/ 2>/dev/null || true
    cp /usr/include/stdio.h usr/include/ 2>/dev/null || true
    cp /usr/include/stdlib.h usr/include/ 2>/dev/null || true
    cp /usr/include/string.h usr/include/ 2>/dev/null || true
    cp /usr/include/unistd.h usr/include/ 2>/dev/null || true
    cp /usr/include/errno.h usr/include/ 2>/dev/null || true

    # Copy lib directory with gcc libs
    echo "[*] Copying library files..."
    cp -r /usr/lib/gcc usr/lib/ 2>/dev/null || true
    cp -r /usr/lib/x86_64-linux-gnu/lib* usr/lib/ 2>/dev/null || true
    cp /usr/lib/x86_64-linux-musl/libc.so usr/lib/ 2>/dev/null || true

    # Create basic /etc files
    echo "judge-guest" > etc/hostname
    cat > etc/fstab <<'EOF'
proc    /proc      proc    defaults     0 0
sysfs   /sys       sysfs   defaults     0 0
tmpfs   /dev/shm   tmpfs   defaults     0 0
tmpfs   /tmp       tmpfs   defaults     0 0
EOF

    # Compile executor statically
    echo "[*] Compiling executor (static)..."
    cd "$SCRIPT_DIR"
    gcc -static -O2 -Wall -I../judge/include -o executor_static executor.c
    cp executor_static "$ROOTFS_DIR/usr/local/bin/executor"
    chmod +x "$ROOTFS_DIR/usr/local/bin/executor"

    # Install init script
    echo "[*] Installing init script..."
    cp init_script.sh "$ROOTFS_DIR/sbin/init"
    chmod +x "$ROOTFS_DIR/sbin/init"

    echo "[*] Rootfs built successfully"
}

case "$BUILD_STRATEGY" in
    buildroot) build_with_buildroot ;;
    manual)    build_manual ;;
    *)         echo "Usage: $0 [buildroot|manual]"; exit 1 ;;
esac

# Create initrd image (common for both strategies)
echo "[*] Creating initrd image..."
cd "$ROOTFS_DIR"
find . -print0 | cpio --null -ov --format=newc | gzip -9 > "$INITRD_IMG"

echo "[+] Initrd created: $INITRD_IMG"
echo "[+] Size: $(du -h "$INITRD_IMG" | cut -f1)"
echo "[+] Usage: sudo ./build/kvm-host -k <kernel_image> -i $INITRD_IMG -d <rootfs>"
