#!/bin/sh

# Mount essential filesystems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs dev /dev

# Create device files
mknod /dev/console c 5 1
mknod /dev/null c 1 3
mknod /dev/zero c 1 5
mknod /dev/urandom c 1 9

# Mount disk image (provided via virtio-blk)
mkdir -p /mnt/testdata
mount /dev/vda /mnt/testdata

# Set up cgroup v2
mount -t cgroup2 cgroup2 /sys/fs/cgroup

# Set hostname
echo "judge-guest" > /proc/sys/kernel/hostname

# Create submission work directory
mkdir -p /tmp/submission
chmod 777 /tmp/submission

# Create mount point for shared memory
mkdir -p /dev/shm
mount -t tmpfs tmpfs /dev/shm

# Set up network (optional)
ip link set lo up
ip a add 127.0.0.1/8 dev lo

# Start executor daemon
/usr/local/bin/executor &
EXECUTOR_PID=$!

# Wait for signals
wait $EXECUTOR_PID

# Cleanup
umount /mnt/testdata 2>/dev/null
umount /sys/fs/cgroup 2>/dev/null
umount /dev/shm 2>/dev/null

# Halt
poweroff
