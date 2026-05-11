# Quick Start Guide

## Build

```bash
make -j4
cd initrd_builder && ./build_initrd.sh manual
```

## Run Tests (No KVM Required)

```bash
# API unit tests (verdict logic, output comparison)
./build/test_judge_api

# IPC communication tests
./build/test_ipc_local

# Performance benchmarks
./build/test_performance
```

## KVM Integration (Full System)

### Terminal 1: Start KVM
```bash
sudo ./build/kvm-host \
    -k /boot/vmlinuz-linux \
    -i ./initrd_builder/build/initrd.img \
    --judge-enable
```

### Terminal 2: Run Tests
```bash
./build/test_kvm_integration
```

## Use the API (Code)

```c
#include "judge/include/judge_module.h"

int main() {
    // Initialize judge system
    judge_module_init(1, 512 * 1024 * 1024);
    
    // Create submission
    struct judge_submission sub = {
        .submission_id = 1,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
    };
    
    const char *code = "#include <stdio.h>\n"
                       "int main() {\n"
                       "    printf(\"Hello\\n\");\n"
                       "    return 0;\n"
                       "}\n";
    
    sub.source_code = malloc(strlen(code));
    memcpy(sub.source_code, code, strlen(code));
    sub.source_code_size = strlen(code);
    
    const char *expected = "Hello\n";
    sub.expected_output = malloc(strlen(expected));
    memcpy(sub.expected_output, expected, strlen(expected));
    sub.expected_output_size = strlen(expected);
    
    // Execute
    struct judge_result res;
    judge_comparison_config_t cfg = {
        .mode = COMPARE_BYTE_FOR_BYTE,
        .validator_script_path = NULL,
    };
    
    judge_module_execute_with_verdict(&sub, &res, 30000, expected, &cfg);
    
    // Check result
    printf("Verdict: %d (0=AC, 1=WA, 2=RE, 3=TLE, 4=MLE, 5=CE)\n", res.verdict);
    printf("CPU time: %.3fs\n", res.cpu_time_seconds);
    printf("Peak memory: %lu MB\n", res.peak_memory_bytes / 1024 / 1024);
    
    // Cleanup
    judge_module_cleanup();
    free(sub.source_code);
    free(sub.expected_output);
    
    return res.verdict == 0 ? 0 : 1;
}
```

## Verdict Codes

```c
enum judge_verdict {
    VERDICT_AC = 0,   // Accepted
    VERDICT_WA = 1,   // Wrong Answer
    VERDICT_RE = 2,   // Runtime Error
    VERDICT_TLE = 3,  // Time Limit Exceeded
    VERDICT_MLE = 4,  // Memory Limit Exceeded
    VERDICT_CE = 5,   // Compilation Error
};
```

## Comparison Modes

```c
enum judge_compare_mode {
    COMPARE_BYTE_FOR_BYTE = 0,          // Exact match
    COMPARE_WHITESPACE_NORMALIZED = 1,  // Ignore whitespace
    COMPARE_CUSTOM_VALIDATOR = 2,       // Run validator script
};
```

## Example: Test Different Verdicts

### AC - Correct
```c
const char *code = "#include <stdio.h>\nint main() { printf(\"5\\n\"); return 0; }";
const char *expected = "5\n";
```

### WA - Wrong Answer
```c
const char *code = "#include <stdio.h>\nint main() { printf(\"3\\n\"); return 0; }";
const char *expected = "5\n";
```

### CE - Compilation Error
```c
const char *code = "#include <stdio.h>\nint main() { printf(\"hi\") return 0; }";  // Missing semicolon
```

### RE - Runtime Error
```c
const char *code = "#include <stdio.h>\nint main() { return 42; }";  // Non-zero exit
```

### TLE - Timeout
```c
const char *code = "#include <stdio.h>\nint main() { while(1); return 0; }";
sub.timeout_seconds = 2;  // Will timeout after 2s
```

### MLE - Memory Limit
```c
const char *code = "#include <stdlib.h>\nint main() { malloc(500*1024*1024); return 0; }";
sub.memory_limit_bytes = 50 * 1024 * 1024;  // Only 50MB
```

## Troubleshooting

### KVM won't start
```bash
grep -c kvm /proc/cpuinfo          # Should be > 0
mount | grep cgroup2               # Should exist
ls /sys/fs/cgroup/                 # Should have memory, cpu dirs
```

### Tests fail
```bash
# Local tests (no KVM)
./build/test_judge_api             # API logic works?
./build/test_ipc_local             # IPC protocol works?

# If local tests pass but KVM fails:
# - Check initrd was built: ls -lh initrd_builder/build/initrd.img
# - Check executor in initrd: zcat initrd.img | cpio -t | grep executor
# - Check kernel has cgroup2: cat /proc/filesystems | grep cgroup2
```

### High latency (>5 seconds)
```
Normal: 300-500ms per submission
High: >5s indicates:
  - Slow VM boot? (check CPU usage)
  - Slow compilation? (check code size)
  - Polling timeout expired? (increase poll_timeout_ms)
  - Database bottleneck? (check query logs)
```

### Wrong verdict
```
Common issues:
1. Expected output mismatch? (check trailing newlines)
2. Compiler version difference? (check gcc version in guest)
3. Resource limit set wrong? (verify memory_limit_bytes)
4. Timeout too short? (check timeout_seconds)
```

## Performance Expectations

```
Simple "Hello World":     500ms
Medium program:          1-2s
Complex program:         3-5s
Very slow code:          5s+ (TLE expected)

Per submission breakdown:
- IPC polling:   ~100ms (detector latency)
- Compilation:   50-500ms (depends on code)
- Execution:     10-100ms (typical)
- Return:        ~100ms (polling + processing)
Total:           ~300-700ms typical
```

## Production Deployment

See `DEPLOYMENT.md` for:
- Multi-host load balancing
- Database integration
- Monitoring and metrics
- Kubernetes deployment
- Health checks

## Full Documentation

- **ARCHITECTURE.md**: Deep technical design
- **DEPLOYMENT.md**: Production setup
- **judge/tests/KVM_INTEGRATION.md**: Test procedures
- **judge/examples/README.md**: API reference

## Files to Know

| File | Purpose |
|------|---------|
| `judge/include/judge_module.h` | Public API |
| `judge/src/judge_verdict.c` | Verdict logic |
| `initrd_builder/executor.c` | Guest executor |
| `judge/tests/test_judge_api.c` | Unit tests |
| `judge/tests/test_kvm_integration.c` | Integration tests |

---

**Status**: ✅ Ready to use. Build, test, deploy.
