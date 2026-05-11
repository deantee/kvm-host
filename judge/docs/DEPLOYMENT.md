# Judge Framework Deployment Guide

## Overview

This guide covers deploying the KVM-Host Online Judge framework for production use.

## Prerequisites

### Hardware
- CPU with KVM support (Intel VT-x or AMD-V)
- 4GB RAM minimum (8GB recommended for concurrent submissions)
- 10GB disk space (for initrd + VM images)

### System Requirements
- Linux kernel 5.4+ with KVM module
- cgroup v2 support (mount: `mount -t cgroup2 none /sys/fs/cgroup`)
- 64-bit x86_64 or ARM64 architecture

### Build Requirements
- GCC 10+ for host compilation
- musl-gcc or glibc for guest initrd
- make, bash, standard Unix tools

## Installation

### 1. Build Judge Framework

```bash
cd /path/to/kvm-host-repo
make clean
make -j$(nproc)
```

Output: `build/kvm-host` binary (judge framework integrated)

### 2. Build Guest Initrd

```bash
cd initrd_builder
bash build_initrd.sh manual
```

Output: `build/initrd.img` (76MB with gcc/g++/libc)

Or with full buildroot (30+ minutes):
```bash
bash build_initrd.sh buildroot
```

### 3. Verify Build

```bash
ls -lh build/kvm-host
ls -lh initrd_builder/build/initrd.img
file initrd_builder/build/initrd.img  # Should be gzip
```

## Configuration

### Judge Module Options

In your application code:

```c
#include "judge_module.h"

// Initialize with 512MB memory limit
judge_module_init(
    1,                      // enable_judge: 1 = enable, 0 = disable
    512 * 1024 * 1024       // guest_memory_limit in bytes
);
```

### Environment Variables

```bash
# Set kernel image path (default: auto-detect)
export KERNEL_IMAGE=/boot/vmlinuz-6.1.0

# Set memory limit for VM
export VM_MEMORY=2G

# Enable verbose logging
export JUDGE_VERBOSE=1
```

## Running the System

### Basic Execution

```bash
./build/kvm-host \
    -k /boot/vmlinuz-$(uname -r) \
    -i ./initrd_builder/build/initrd.img \
    -d /dev/root \
    --judge-enable
```

### With Judge Test Suite

```bash
./judge/tests/run_tests.sh
```

This will:
1. Verify build artifacts
2. Check KVM support
3. Run verdict test cases
4. Report results

### Concurrent Submissions

Submit multiple programs in sequence:

```c
for (int i = 0; i < 100; i++) {
    struct judge_submission sub = {
        .submission_id = i,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
        .source_code = program_code,
        .source_code_size = strlen(program_code),
    };

    judge_module_execute_submission(&sub, &result, 30000);
}
```

## Testing

### Unit Tests

Framework includes 6 verdict test cases:
- **AC**: Accepted (Hello World)
- **WA**: Wrong Answer
- **RE**: Runtime Error (segfault)
- **TLE**: Time Limit Exceeded
- **MLE**: Memory Limit Exceeded
- **CE**: Compilation Error

Run with:
```bash
bash judge/tests/run_tests.sh
```

### Integration Testing

Verify all 7 phases (see INTEGRATION_TESTING.md):

```
Phase 1: Unit tests ✓
Phase 2: Host-guest IPC
Phase 3: Verdict computation
Phase 4: Compilation (gcc/g++)
Phase 5: Execution + timeout
Phase 6: Resource monitoring
Phase 7: Error handling
```

### Performance Testing

Measure throughput and latency:

```bash
# Profile with performance stats
judge_perf_stats_t stats;
judge_perf_init(&stats);

for (int i = 0; i < 100; i++) {
    judge_module_execute_submission(&sub, &res, 30000);
    judge_perf_record_submission(&stats, &sample);
}

judge_perf_compute_stats(&stats);
judge_perf_print_report(&stats);
judge_perf_export_csv(&stats, "results.csv");
```

Expected: >10 submissions/second

## Monitoring

### Shared Memory State

```bash
# View active submissions
ls -lah /dev/shm/judge_*

# Check cgroup limits
cat /sys/fs/cgroup/kvm_host_*/memory.max
cat /sys/fs/cgroup/kvm_host_*/memory.current
```

### Verdict Statistics

```c
judge_stats_t stats = judge_module_get()->stats;
printf("AC: %lu, WA: %lu, RE: %lu, TLE: %lu, MLE: %lu, CE: %lu\n",
       stats.ac_count, stats.wa_count, stats.re_count,
       stats.tle_count, stats.mle_count, stats.ce_count);
```

### Performance Metrics

```bash
# Export performance data
./judge/tests/run_tests.sh > results.txt
grep "Summary" results.txt  # View throughput
```

## Troubleshooting

### KVM Not Available

```
Error: /dev/kvm: No such file or device
```

**Solution**: 
```bash
# Check KVM module
lsmod | grep kvm

# Load module if needed
sudo modprobe kvm
sudo modprobe kvm_intel  # or kvm_amd

# Verify
ls -c /dev/kvm
```

### Initrd Mount Failures

```
Error: Failed to mount rootfs
```

**Solution**:
- Verify initrd is valid: `file initrd.img` (should be gzip)
- Rebuild: `cd initrd_builder && bash build_initrd.sh manual`
- Check disk space: `df -h`

### Compiler Not Available

```
Error: gcc/g++ command not found in guest
```

**Solution**:
- Use enhanced manual build: `bash build_initrd.sh manual`
- Verify initrd size: `ls -lh initrd.img` (should be >50MB)
- Check rootfs contents: `cd build/rootfs && ls usr/bin/gcc`

### Memory Limit Errors

```
Error: MLE verdict on small programs
```

**Solution**:
- Check host cgroup: `cat /sys/fs/cgroup/kvm_host_*/memory.max`
- Increase guest limit: `judge_module_init(1, 1024*1024*1024)`
- Verify cgroup v2: `mount | grep cgroup2`

## Security Considerations

### Seccomp Filtering

Optional: Block dangerous syscalls

```c
judge_config.use_seccomp = 1;  // Enable filtering
```

### Resource Isolation

Two-layer enforcement:
1. **Guest**: setrlimit + cgroup v2
2. **Host**: cgroup v2 safety net

Memory limits prevent OOM-killing the host.

### File Access

Guest can only access:
- Temporary compilation directory
- Shared result buffer
- No direct host filesystem access

## Production Deployment

### Multi-Instance Setup

Run multiple judge instances:

```bash
for i in {0..3}; do
    (./build/kvm-host --judge-instance $i &)
done
```

Each instance:
- Has separate VM
- Uses separate cgroup
- Handles independent submissions
- Can scale to 10+ instances per host

### API Integration

In your online judge web application:

```c
// Include judge_module.h
#include "judge_module.h"

// Initialize at startup
judge_module_init(1, 512 * 1024 * 1024);

// On submission
struct judge_submission sub = {
    .submission_id = submission->id,
    .timeout_seconds = problem->time_limit,
    .memory_limit_bytes = problem->memory_limit,
    .language = get_language(submission->language),
    .source_code = submission->code,
    .source_code_size = strlen(submission->code),
};

struct judge_result res;
if (judge_module_execute_submission(&sub, &res, 30000) == 0) {
    // Update database
    db_update_verdict(submission->id, res.verdict);
    db_store_output(submission->id, res.program_stdout);
}

// Cleanup at shutdown
judge_module_cleanup();
```

## Performance Tuning

### Throughput Optimization

Target: >10 submissions/second

1. **Increase queue size**: Modify JUDGE_QUEUE_SIZE in judge_core.h
2. **Reduce polling latency**: Adjust usleep(100000) interval
3. **Use multiple instances**: Run N judge instances in parallel
4. **Compile caching**: Cache compiled binaries (future enhancement)

### Latency Reduction

Target: <500ms per submission

1. **Profile hot paths**: Use `perf` on judge_module_execute_submission()
2. **Minimize syscalls**: Reduce eventfd signaling frequency
3. **Optimize memory**: Use memory mapping instead of copying

### Memory Efficiency

1. **Pool allocations**: Pre-allocate queue structures
2. **Limit payload size**: Cap output buffers (currently 100MB)
3. **Release resources**: Clean up between submissions

## Backup & Recovery

### State Persistence

Judge framework is stateless:
- All results stored in shared memory (volatile)
- Database stores verdict + output
- Can restart without data loss (previous submissions unaffected)

### Cleanup on Failure

```bash
# Stop all judge instances
pkill -f "kvm-host.*judge"

# Clean shared memory
rm /dev/shm/judge_*

# Release cgroups
rmdir /sys/fs/cgroup/kvm_host_*

# Restart
./build/kvm-host ...
```

## Documentation

- **JUDGE_API.md**: API reference for developers
- **JUDGE_ARCHITECTURE.md**: System design details
- **INTEGRATION_TESTING.md**: Test procedures
- **DEPLOYMENT.md**: This file

## Support

For issues:
1. Check INTEGRATION_TESTING.md for test procedures
2. Review JUDGE_ARCHITECTURE.md for design
3. Check error messages in build.log
4. Verify prerequisites are met
