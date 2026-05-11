# KVM Integration Testing Guide

## Overview

This guide covers end-to-end testing of the online judge system with KVM runtime. The test suite verifies:

- Executor daemon functionality
- Code compilation (C/C++)
- Program execution with timeout/memory enforcement
- All 6 verdict types (AC, WA, RE, TLE, MLE, CE)
- IPC communication between host and guest

## Prerequisites

1. KVM/QEMU installed and working
2. Linux kernel with cgroup v2 support
3. Compiled kvm-host binary: `./build/kvm-host`
4. Built initrd: `./initrd_builder/build/initrd.img`
5. Linux kernel image: `/boot/vmlinuz-linux`

## Quick Start

### Terminal 1: Start KVM with Judge Enabled

```bash
cd /path/to/kvm-host-repo

# Simple way (uses default paths)
bash judge/tests/run_kvm_tests.sh

# Or manually specify paths
sudo ./build/kvm-host \
    -k /boot/vmlinuz-linux \
    -i ./initrd_builder/build/initrd.img \
    -d /dev/root \
    --judge-enable
```

Watch for output like:
```
[executor] Waiting for requests...
[judge] Initialized (memory_limit=536870912)
```

### Terminal 2: Run Integration Tests

```bash
cd /path/to/kvm-host-repo

# Build test if not already built
make build/test_kvm_integration

# Run tests
./build/test_kvm_integration
```

Expected output:
```
=== KVM Integration Test Suite ===

[TEST 1] Hello World (AC expected)
  PASS (CPU: 0.050s, Mem: 1 MB)
[TEST 2] Compilation Error (CE expected)
  PASS (Compile error detected)
[TEST 3] Runtime Error (RE expected)
  PASS (Exit code: 42)
[TEST 4] Wrong Answer (WA expected)
  PASS (Output mismatch detected)
[TEST 5] Time Limit Exceeded (TLE expected)
  PASS (Timeout after 2.0s)
[TEST 6] Memory Limit Exceeded (MLE expected)
  PASS (OOM after 50 MB)

=== Results ===
Passed: 6/6
Failed: 0/6

✓ All KVM integration tests passed!
```

## Test Cases

### Test 1: Accepted (AC)
- **Code**: Simple printf("Hello, World!\n")
- **Expected**: Exact output match
- **Verdict**: AC
- **Verifies**: Basic compilation and execution

### Test 2: Compilation Error (CE)
- **Code**: Missing semicolon in printf
- **Expected**: GCC error
- **Verdict**: CE
- **Verifies**: Compilation error detection

### Test 3: Runtime Error (RE)
- **Code**: Non-zero exit code (42)
- **Expected**: Exit code != 0
- **Verdict**: RE
- **Verifies**: Non-zero exit detection

### Test 4: Wrong Answer (WA)
- **Code**: Prints "Goodbye" instead of "Hello"
- **Expected**: "Hello"
- **Verdict**: WA
- **Verifies**: Output comparison

### Test 5: Time Limit Exceeded (TLE)
- **Code**: Infinite loop with 2s timeout
- **Expected**: Signal or exit after timeout
- **Verdict**: TLE
- **Verifies**: Timeout enforcement via alarm()

### Test 6: Memory Limit Exceeded (MLE)
- **Code**: malloc(500MB) with 50MB limit
- **Expected**: cgroup memory kill
- **Verdict**: MLE
- **Verifies**: Memory limit enforcement via cgroup

## Troubleshooting

### KVM won't start
```bash
# Check KVM support
grep -c kvm /proc/cpuinfo  # Should be > 0

# Enable KVM module if needed
sudo modprobe kvm_intel    # Intel CPUs
sudo modprobe kvm_amd      # AMD CPUs
```

### Executor daemon not starting
Check KVM console output for:
```
[executor] Waiting for requests...
```

If not present, check init script ran:
```bash
# In KVM console, check if executor is running
ps aux | grep executor
```

### Tests hang/timeout
- Increase poll timeout in test (default 60s)
- Check KVM has enough resources (CPU, memory)
- Verify initrd includes gcc/g++: `file initrd.img`

### Memory/Time tests give wrong verdict
- Verify cgroup v2 is mounted: `mount | grep cgroup2`
- Check resource limits are set: `cat /sys/fs/cgroup/submission_*/memory.max`
- Verify timeout is enforced: `ps aux | grep submission`

## Manual Testing

For debugging, submit custom test cases:

```c
// In terminal 2, modify test to submit custom code
const char *source = R"(
#include <stdio.h>
int main() {
    // Your test code here
    return 0;
}
)";

judge_module_execute_with_verdict(&sub, &res, TIMEOUT_MS, expected, &cfg);
printf("Verdict: %d\n", res.verdict);
```

## Performance Expectations

| Operation | Expected Time | Notes |
|-----------|---|---|
| Simple submission | 0.5-1.0s | Dominated by IPC polling (100ms) |
| Compilation | 0.1-0.5s | GCC overhead |
| Execution | 0.01-0.05s | Typical program |
| Timeout detection | ~2.0s | Plus submission time |
| Memory OOM | Varies | Depends on cgroup kill speed |

## Integration with CI/CD

For automated testing:

```bash
#!/bin/bash
set -e

# Build everything
cd kvm-host-repo
make -j4

# Build initrd
cd initrd_builder
./build_initrd.sh manual

# Start KVM in background
cd ..
timeout 120 sudo ./build/kvm-host \
    -k /boot/vmlinuz-linux \
    -i ./initrd_builder/build/initrd.img \
    --judge-enable &
KVM_PID=$!

# Wait for executor to be ready
sleep 5

# Run tests
./build/test_kvm_integration
TEST_RESULT=$?

# Cleanup
kill $KVM_PID 2>/dev/null || true

exit $TEST_RESULT
```

## Architecture Verification

After tests pass, verify:

1. **IPC Communication**: Request/result exchanged via shared memory
2. **Executor Polling**: Guest polls every 100ms for new requests
3. **Compilation**: gcc/g++ properly configured in guest
4. **Timeout**: SIGALRM kills runaway processes
5. **Memory**: cgroup enforces memory.max limit
6. **Verdict Computation**: All 6 verdict types detected correctly

## Next Steps

After KVM tests pass:
1. Stress test with many concurrent submissions
2. Test C++ compilation and execution
3. Test custom validator scripts
4. Profile latency and throughput
5. Integrate into production online judge system

## Additional Resources

- [Judge Module API](../examples/README.md)
- [Executor Implementation](../../initrd_builder/executor.c)
- [Verdict System](../src/judge_verdict.c)
- [IPC Protocol](../include/judge_protocol.h)
