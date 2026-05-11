# Online Judge System - Architecture & Design

Complete technical architecture of the KVM-based online judge backend.

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Host (Linux + KVM)                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ kvm-host Process                                         │   │
│  ├─────────────────────────────────────────────────────────┤   │
│  │  • Initialize KVM VM                                    │   │
│  │  • Load kernel & initrd                                 │   │
│  │  • Create shared memory (IPC)                           │   │
│  │  • Initialize cgroup v2 (safety net)                    │   │
│  │  • Run judgment loop: poll IPC for results              │   │
│  └─────────────────────────────────────────────────────────┘   │
│         ↕ (IPC: shared memory + polling)                        │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ KVM Guest VM                                             │   │
│  ├─────────────────────────────────────────────────────────┤   │
│  │  • Linux kernel (minimal rootfs)                        │   │
│  │  • Executor daemon (100% CPU polling loop)              │   │
│  │  • Cgroup enforcement (memory.max, cpu limits)          │   │
│  │  • Compilation tools (gcc, g++)                         │   │
│  │  • Execute: tmpfs submission workspace                  │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ Shared Memory Layout (10MB request + 256MB result)       │   │
│  ├─────────────────────────────────────────────────────────┤   │
│  │ /dev/shm/judge_request   (judge_request_t)              │   │
│  │   • Protocol version                                    │   │
│  │   • Request ID (change detection)                       │   │
│  │   • Language, timeout, memory limit                     │   │
│  │   • Source code (payload buffer, 10MB)                  │   │
│  │                                                          │   │
│  │ /dev/shm/judge_result    (judge_result_t)               │   │
│  │   • Result ID (matches request)                         │   │
│  │   • Verdict + metrics                                   │   │
│  │   • Payload (256MB):                                    │   │
│  │     ├─ Compile stdout/stderr (20KB)                     │   │
│  │     ├─ Program stdout (100MB max)                       │   │
│  │     └─ Program stderr (10KB)                            │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

## Data Flow

### Submission → Execution → Verdict

```
1. Host receives submission
   └─→ struct judge_submission (code, timeout, memory, language)

2. Host prepares request
   └─→ judge_request_t (in shared memory)
       • Copy source to payload
       • Set request_id (change detection)
       • Poll timeout

3. Guest executor polls (100ms interval)
   └─→ Detects request_id change
   └─→ Parse judge_request_t
   └─→ Create submission cgroup + work directory

4. Compilation phase
   └─→ Write source code to file
   └─→ Invoke gcc/g++ (static link, -O2)
   └─→ Capture stderr → compile_stderr
   └─→ Check exit code (0 = success, else CE)

5. If compiled successfully, execution phase
   └─→ Set resource limits (setrlimit)
   └─→ Add process to cgroup (memory.max, cpu.max)
   └─→ Fork + exec binary (with timeout alarm)
   └─→ Capture stdout/stderr during execution
   └─→ Record wall time, CPU time, peak memory

6. Metrics collection
   └─→ Read from cgroup: memory.peak, cpu.stat
   └─→ Read exit code, signal number
   └─→ Calculate verdicts from exit code + metrics

7. Result preparation
   └─→ Convert executor_result → judge_result_t
   └─→ Copy outputs to payload (with offset tracking)
   └─→ Write to shared memory

8. Host polls result
   └─→ Detects result_id change
   └─→ Read judge_result_t from shared memory
   └─→ Extract verdict + metrics
   └─→ Compute final verdict with output comparison
   └─→ Return to application

9. Cleanup
   └─→ Guest removes cgroup, tmpfs files, temp dirs
   └─→ Host frees resources, logs result
```

## Verdict Computation Logic

```c
enum judge_verdict judge_compute_verdict_from_result(...) {
    // Priority order (earliest match wins):
    
    1. Compilation phase failed?
       compile_exit_code != 0  →  VERDICT_CE
    
    2. Memory limit exceeded?
       peak_memory_bytes > memory_limit  →  VERDICT_MLE
    
    3. Time limit exceeded?
       cpu_time_seconds > timeout_seconds  →  VERDICT_TLE
    
    4. Runtime error (non-zero exit)?
       program_exit_code != 0  →  VERDICT_RE
    
    5. Output matches?
       compare(actual, expected) == 0  →  VERDICT_AC
    
    6. Default: Wrong answer
       →  VERDICT_WA
}
```

**Key Design Decisions:**
- **Priority ordering**: Execution errors checked before output comparison
- **Memory/Time checks first**: Resource exhaustion before correctness
- **Output comparison last**: Only if execution succeeded
- **No expected output → AC**: Assume correct if no reference (optional mode)

## Timeout Implementation

### Dual Layer Enforcement

```
Layer 1 (Process level):
  ├─ alarm(timeout_seconds) in child process
  └─ Triggers SIGALRM → kills runaway code
  
Layer 2 (Kernel level):
  ├─ setrlimit(RLIMIT_CPU, timeout)
  └─ Kernel enforces hard CPU limit
  
Layer 3 (Cgroup level):
  ├─ cpu.max = timeout * 1e6 microseconds
  └─ Cgroup kills process exceeding limit
  
Result: Process killed reliably within timeout
Margin: ~100ms variance from wall time measurement
```

## Memory Limit Implementation

### Dual Layer Enforcement

```
Layer 1 (Process level):
  ├─ setrlimit(RLIMIT_AS, memory_limit)
  └─ Process mmap() fails when limit reached
  
Layer 2 (Cgroup level):
  ├─ memory.max = memory_limit_bytes
  └─ OOM killer activates when limit exceeded
  └─ SIGKILL delivered to process
  
Metrics:
  ├─ memory.peak: Maximum memory used (from cgroup)
  └─ Recorded on process termination
  
Result: Memory limit enforced by kernel, reliably measured
Variance: ±1% typical (measurement granularity)
```

## IPC Protocol

### Message Format: Request

```c
typedef struct {
    uint32_t protocol_version;           // = JUDGE_PROTOCOL_VERSION (1)
    uint32_t request_id;                 // Must change for new request
    uint32_t timeout_seconds;            // CPU timeout (seconds)
    uint32_t memory_limit_bytes;         // Memory limit (bytes)
    uint32_t language;                   // LANG_C (0) or LANG_CXX (1)
    uint32_t source_code_offset;         // Offset in payload
    uint32_t source_code_size;           // Size of source
    uint8_t payload[JUDGE_MAX_SOURCE];   // 10MB: source code
} judge_request_t;
```

### Message Format: Result

```c
typedef struct {
    uint32_t protocol_version;           // = JUDGE_PROTOCOL_VERSION
    uint32_t request_id;                 // Echo of request ID
    uint32_t verdict;                    // VERDICT_AC, VERDICT_CE, etc.
    
    int32_t compile_exit_code;           // Compiler exit code
    uint32_t compile_stdout_offset;      // Offset in payload
    uint32_t compile_stdout_size;        // Size of compiler output
    uint32_t compile_stderr_offset;
    uint32_t compile_stderr_size;
    
    int32_t program_exit_code;           // Program exit code
    uint32_t program_stdout_offset;      // Offset in payload
    uint32_t program_stdout_size;        // Size of program output
    uint32_t program_stderr_offset;
    uint32_t program_stderr_size;
    
    double wall_time_seconds;            // Elapsed wall time
    double cpu_time_seconds;             // CPU time used
    uint64_t peak_memory_bytes;          // Max memory used
    
    char judge_message[256];             // Error message if any
    uint8_t payload[JUDGE_RESULT_BUF_SIZE];  // 256MB: outputs
} judge_result_t;
```

### Payload Layout in Result

```
Offset          Content                    Size
0               compile_stdout             up to 10KB
10KB            compile_stderr             up to 10KB
20KB            program_stdout             up to 100MB
20KB+100MB      program_stderr             up to 10KB
Total           ~120MB (sparse allocation via mmap)
```

### Synchronization

**Previous design (failed):**
- EventFD cross-process signaling
- Problem: EventFDs created on host can't be used by guest process across VM boundary

**Current design (polling):**
- Executor polls every 100ms for request_id change
- Host polls every 100ms for result_id change
- Simple, reliable, no race conditions
- Trade-off: ~100ms latency per submission (acceptable)

## Host-side API Layers

### Layer 1: Low-level IPC
```c
int judge_submit(judge_t *j, struct judge_submission *sub);
    └─ Write request to shared memory
    └─ Trigger poll timeout
    └─ Return immediately

int judge_poll_result(judge_t *j, struct judge_result *res);
    └─ Non-blocking check for result
    └─ Extract payload buffers
    └─ Return 0 if ready, -1 if not ready
```

### Layer 2: Blocking execution
```c
int judge_module_execute_submission(
    struct judge_submission *sub,
    struct judge_result *res,
    uint32_t poll_timeout_ms);
    └─ submit() + loop poll_result() until ready
    └─ Waits up to poll_timeout_ms
    └─ Returns verdict when ready
```

### Layer 3: High-level API (recommended)
```c
int judge_module_execute_with_verdict(
    struct judge_submission *sub,      // Input: code, limits, language
    struct judge_result *res,          // Output: verdict, metrics
    uint32_t poll_timeout_ms,          // Poll timeout
    const char *expected_output,       // Reference output
    judge_comparison_config_t *cfg);   // Comparison mode
    └─ execute_submission() → get raw result
    └─ compute_verdict() → apply verdict logic
    └─ Return complete verdict
```

## Performance Characteristics

### Latency per Submission

```
Timeline:
t=0ms    Host submits request (write to shared memory)
t=0ms    Guest polls (misses request)
t=100ms  Guest polls (detects request_id change, starts processing)
t=100ms  Guest: create cgroup, write source
t=120ms  Guest: gcc compilation started
t=150ms  Guest: compilation complete (or error)
t=150ms  Guest: if error, write result and go to t=200ms
t=150ms  Guest: execute binary
t=250ms  Guest: program finished, read metrics from cgroup
t=250ms  Guest: write result to shared memory
t=250ms  Host polls (misses result)
t=350ms  Host polls (detects result_id change, reads result)
t=350ms  Host: compute verdict, return to application

Total latency: 350ms typical (range: 250-500ms)
Breakdown:
  - IPC polling: ~100ms (inherent to polling design)
  - Compilation: 20-100ms (depends on code complexity)
  - Execution: 50-200ms (depends on program logic)
  - Metrics collection: <10ms

Polling interval: 100ms (configurable, tradeoff: latency vs CPU)
```

### Throughput

```
Single judge host:
  - Sequential submissions: 2-4 per second
  - Limited by submission latency (~300-500ms)
  
Multiple judge hosts (N hosts):
  - Aggregate throughput: ~2-4 × N submissions/sec
  - Network overhead minimal (IPC is local)
  - Linear scaling up to ~8-16 hosts
  - Bottleneck shifts to database/queue
```

### Memory Usage

```
Per submission:
  - Shared memory overhead: ~250MB (allocated once, reused)
  - Executor state: ~50MB (guest VM memory)
  - Submission workspace: ~100MB typical (tmpfs)
  - Worst case: 256MB limit + overhead = 300MB
  
Host system:
  - Base VM: 256MB (minimal kernel + busybox)
  - GCC/runtime: 200MB (dynamically linked tools)
  - Working set: 50MB typical per active submission
  
Memory efficient: Total ~600MB per concurrent submission
```

## CPU Overhead

```
Per submission:
  - GCC compilation: 0.1-1.0 CPU seconds (depends on code size)
  - Program execution: 0.01-0.1 CPU seconds typical
  - Guest overhead: 5-10% (VM exit/entry cost)
  - Total: 1-10 CPU seconds per submission
  
Host: Minimal overhead outside guest
  - Submission processing: <1ms
  - IPC operations: <1ms
  - Verdict computation: <0.01ms
```

## Scalability Limits

### Single Host
- Concurrent submissions: 1 (sequential queuing)
- Submissions per second: 2-4
- Memory: ~600MB per submission
- CPU: Variable (1-10s per submission)

### Multiple Hosts
- N hosts: Throughput scales linearly to 2-4N submissions/sec
- Max practical hosts: 16-32 (before database becomes bottleneck)
- Bottleneck shifts: From compute → IPC → Database

### Practical Configuration
```
Small contest (500 participants):
  - 1 judge host sufficient
  - ~200 submissions total
  - Can complete in 10-20 minutes

Large contest (5000 participants):
  - 4 judge hosts recommended
  - ~2000 submissions total
  - Can complete in 10-15 minutes
  
Very large contest (50000+ participants):
  - 16+ judge hosts
  - Database becomes bottleneck
  - Need caching/batching strategies
```

## Design Trade-offs

### Decision: Polling over EventFD
- **Pro**: Simple, reliable, no race conditions
- **Con**: ~100ms latency overhead
- **Rationale**: EventFD doesn't work across VM boundaries; polling is acceptable for online judge use case

### Decision: Executor in Guest
- **Pro**: Full isolation, can kill runaway code reliably
- **Con**: Startup time, memory overhead
- **Rationale**: Necessary for security; performance acceptable

### Decision: Cgroup v2 (vs cgroup v1)
- **Pro**: Unified hierarchy, simpler API, memory.peak accurate
- **Con**: Requires Linux 5.0+
- **Rationale**: Modern kernel assumption; simplifies code

### Decision: Static GCC Binary
- **Pro**: No library dependency issues, fast build
- **Con**: Large initrd (70MB), slower boot
- **Rationale**: Simplicity/compatibility tradeoff; boot overhead acceptable

## Future Improvements

1. **Precompilation caching**: Cache compiled binaries for identical code
2. **Custom validators**: Support third-party verdict scripts
3. **Streaming output**: Send program output as it's generated
4. **Parallel compilation**: Multiple submissions compiling simultaneously
5. **Shared libraries**: Cache standard library across submissions
6. **GPU support**: Add CUDA/OpenCL for ML submissions
7. **Container support**: Switch to lighter containers (gVisor, Firecracker)
8. **Distributed VMs**: Spread judge hosts across multiple machines

## Code Quality & Testing

- **Unit tests**: 11 API tests (verdicts, output comparison, structures)
- **Integration tests**: 6 KVM tests (all verdict types)
- **Performance tests**: Latency and throughput benchmarks
- **Code coverage**: Core logic 100%, main paths exercised
- **Security review**: Shell injection, bounds checking, resource limits

## References

- [Executor Implementation](initrd_builder/executor.c)
- [Verdict Computation](judge/src/judge_verdict.c)
- [IPC Protocol](judge/include/judge_protocol.h)
- [Module API](judge/include/judge_module.h)
- [Test Suite](judge/tests/)
