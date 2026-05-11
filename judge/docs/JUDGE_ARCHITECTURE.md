# Judge Framework Architecture

## System Design

```
┌─────────────────────────────────────────────────────────────┐
│ Host Process (kvm-host)                                     │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│ ┌──────────────────────────────────────────────────────┐    │
│ │ Judge Module (judge_module.c)                        │    │
│ ├──────────────────────────────────────────────────────┤    │
│ │ • judge_module_init()     - Setup shm + eventfd      │    │
│ │ • judge_module_submit()   - Queue submission         │    │
│ │ • judge_module_poll()     - Poll for result          │    │
│ │ • judge_module_cleanup()  - Teardown                 │    │
│ └──────────────────────────────────────────────────────┘    │
│                            │                                  │
│ ┌──────────────────────────┼──────────────────────────────┐  │
│ │                          │                              │  │
│ │ ┌───────────────────────────────────────────────────┐  │  │
│ │ │ Judge Core (judge_core.c)                         │  │  │
│ │ ├───────────────────────────────────────────────────┤  │  │
│ │ │ • judge_init()        - Init queue + config       │  │  │
│ │ │ • judge_submit()      - Enqueue submission        │  │  │
│ │ │ • judge_poll_result() - Dequeue result            │  │  │
│ │ └───────────────────────────────────────────────────┘  │  │
│ │                                                         │  │
│ │ ┌───────────────────────────────────────────────────┐  │  │
│ │ │ Executor (judge_executor.c)                       │  │  │
│ │ ├───────────────────────────────────────────────────┤  │  │
│ │ │ • init_shm()         - Create shared memory       │  │  │
│ │ │ • write_request()    - Write to shm               │  │  │
│ │ │ • init_eventfd()     - Setup notification         │  │  │
│ │ │ • signal_ready()     - Notify guest               │  │  │
│ │ └───────────────────────────────────────────────────┘  │  │
│ │                                                         │  │
│ │ ┌───────────────────────────────────────────────────┐  │  │
│ │ │ Verdict (judge_verdict.c)                         │  │  │
│ │ ├───────────────────────────────────────────────────┤  │  │
│ │ │ • judge_compute_verdict() - Determine verdict    │  │  │
│ │ │ • judge_compare_outputs() - Output comparison    │  │  │
│ │ └───────────────────────────────────────────────────┘  │  │
│ │                                                         │  │
│ │ ┌───────────────────────────────────────────────────┐  │  │
│ │ │ Resource Monitor (judge_resource.c)              │  │  │
│ │ ├───────────────────────────────────────────────────┤  │  │
│ │ │ • create_cgroup()       - Setup cgroup           │  │  │
│ │ │ • read_memory_peak()    - Read memory.peak       │  │  │
│ │ │ • read_cpu_time()       - Read cpu.stat          │  │  │
│ │ │ • check_oom()           - Detect OOM             │  │  │
│ │ └───────────────────────────────────────────────────┘  │  │
│ └──────────────────────────────────────────────────────────┘  │
│                                                               │
│ ┌────────────────────────────────────────────────────────┐   │
│ │ KVM VM                                                 │   │
│ ├────────────────────────────────────────────────────────┤   │
│ │                                                        │   │
│ │ ┌──────────────────────────────────────────────────┐  │   │
│ │ │ Guest Executor (executor.c in initrd)            │  │   │
│ │ ├──────────────────────────────────────────────────┤  │   │
│ │ │ • executor_init()       - Open shared memory     │  │   │
│ │ │ • executor_compile()    - Compile source (gcc)  │  │   │
│ │ │ • executor_run()        - Execute binary         │  │   │
│ │ │ • executor_write_result() - Send result to host  │  │   │
│ │ └──────────────────────────────────────────────────┘  │   │
│ │                                                        │   │
│ │ ┌──────────────────────────────────────────────────┐  │   │
│ │ │ Guest Resource Monitor                           │  │   │
│ │ ├──────────────────────────────────────────────────┤  │   │
│ │ │ • cgroup setup (memory.max, cpu.max)             │  │   │
│ │ │ • setrlimit (CPU time, virtual memory)           │  │   │
│ │ │ • memory.peak + cpu.stat tracking                │  │   │
│ │ └──────────────────────────────────────────────────┘  │   │
│ │                                                        │   │
│ └────────────────────────────────────────────────────────┘   │
│                                                               │
├─────────────────────────────────────────────────────────────┤
│ Shared Memory + IPC                                          │
├─────────────────────────────────────────────────────────────┤
│ • /dev/shm/judge_request (judge_request_t)                   │
│ • /dev/shm/judge_result (judge_result_t)                     │
│ • eventfd for notification (request_ready / result_ready)    │
└─────────────────────────────────────────────────────────────┘
```

## Execution Flow

### Initialization

```
main.c
  └─ judge_module_init(enable_judge, 512MB)
      ├─ malloc(judge_t)
      ├─ judge_init()            ← judge_core.c
      │   └─ calloc(queue[100])
      ├─ judge_executor_init_shm()    ← judge_executor.c
      │   ├─ shm_open("/dev/shm/judge_request")
      │   ├─ shm_open("/dev/shm/judge_result")
      │   ├─ mmap() both regions
      │   └─ init headers (protocol_version=1)
      └─ judge_executor_init_eventfd()
          ├─ eventfd(0, EFD_CLOEXEC)  ← request notification
          └─ eventfd(0, EFD_CLOEXEC)  ← result notification
```

### Submission Flow

#### Synchronous (Unified API)
```
Application
  └─ judge_module_execute_submission(submission, result, timeout_ms)
      ├─ judge_module_submit(submission)
      │   └─ judge_submit()               ← judge_core.c
      │       ├─ Enqueue in circular buffer
      │       ├─ judge_executor_write_request()
      │       │   └─ memcpy(shm_request, req)
      │       └─ judge_executor_signal_request_ready()
      │           └─ write(eventfd_request, 1)
      │
      └─ Poll loop with timeout_ms
          ├─ judge_module_poll_result(result)
          │   └─ judge_poll_result()      ← judge_core.c
          │       └─ Read shm_result
          └─ Return when result ready or timeout
```

#### Asynchronous (Advanced Pattern)
```
Application
  ├─ judge_module_submit(submission)
  │   └─ ... (same as above)
  │
  └─ Later, judge_module_poll_result(result)
      └─ Check if ready, return immediately
```

#### Guest Execution
```
Guest Executor (in VM)
  └─ Receives eventfd notification
      ├─ Read shm_request
      ├─ Write source to disk
      ├─ Compile with gcc/g++
      ├─ Execute binary with timeout
      ├─ Capture stdout/stderr/metrics
      ├─ Determine verdict
      └─ Write result to shm_result
          └─ write(eventfd_result, 1) ← signals host
```

### Verdict Determination

```
judge_compute_verdict()
  ├─ Check compile_exit_code != 0 → VERDICT_CE
  ├─ Check peak_memory > limit → VERDICT_MLE
  ├─ Check cpu_time > timeout → VERDICT_TLE
  ├─ Check program_exit_code != 0 → VERDICT_RE
  └─ Compare output
      ├─ BYTE_FOR_BYTE: strcmp()
      ├─ WHITESPACE_NORMALIZED: token comparison
      └─ CUSTOM_VALIDATOR: fork/exec validator script
          ├─ If exit_code==0: VERDICT_AC
          └─ Else: VERDICT_WA
```

## Resource Isolation

### Two-Layer Model

```
┌─────────────────────────────────────┐
│ Host OS (kernel)                     │
├─────────────────────────────────────┤
│                                      │
│ Layer 1: Host cgroup v2 (kvm-host) │
│   └─ memory.max: 512 MB (safety net)│
│       cpu.max: rate limiting        │
│                                      │
│   ┌──────────────────────────────┐  │
│   │ KVM VM (guest kernel)        │  │
│   ├──────────────────────────────┤  │
│   │                               │  │
│   │ Layer 2: Guest cgroup v2     │  │
│   │   └─ memory.max: 256 MB (app)│  │
│   │       cpu.max: rate limiting  │  │
│   │                               │  │
│   │ Guest setrlimit() (redundant)│  │
│   │   └─ RLIMIT_CPU: 5 seconds   │  │
│   │   └─ RLIMIT_AS: 256 MB       │  │
│   │   └─ RLIMIT_STACK: 16 MB     │  │
│   │                               │  │
│   │ User Program (submission)     │  │
│   │   ├─ Compile (gcc)            │  │
│   │   ├─ Execute (./binary)       │  │
│   │   └─ Monitor resources        │  │
│   │                               │  │
│   └──────────────────────────────┘  │
│                                      │
└─────────────────────────────────────┘
```

### Detection Strategy

**Memory Limit Exceeded**:
1. Guest cgroup memory.max → OOM killer
2. Host polls memory.peak > limit
3. Verdict: MLE

**Time Limit Exceeded**:
1. Guest setrlimit(RLIMIT_CPU) → SIGXCPU
2. Host timeout check: cpu_time > timeout_seconds
3. Verdict: TLE

**Runtime Error**:
1. Program crash (segfault, abort)
2. Exit code != 0
3. Verdict: RE

## Key Design Decisions

1. **Single Guest VM**: Amortizes boot overhead, persistent executor
2. **Shared Memory + eventfd**: Efficient async IPC, no syscall latency
3. **Circular queue**: Batches submissions, handles concurrency
4. **Dual resource limits**: Guest enforces, host validates
5. **Verdict priority**: CE > MLE > TLE > RE > AC/WA (prevents false positives)
6. **Protocol versioning**: Allows safe evolution of shm structures
7. **Optional custom validators**: DOMjudge compatibility

## Error Handling

**Shared Memory Errors**:
- shm_open() fails → Judge disabled, return error
- mmap() fails → Release shm_fd, return error
- Write beyond payload → Clamp to max size, log warning

**Communication Errors**:
- eventfd write fails → Log error, submission may not complete
- Host can't read result → Timeout, return error verdict

**Resource Limit Errors**:
- cgroup creation fails → Non-fatal, use setrlimit as fallback
- Memory peak read fails → Default to 0, continue
- CPU time read fails → Default to 0, check exit code instead

**Executor Errors**:
- Compilation failure → CE verdict with stderr
- Execution failure → RE verdict with exit code
- Timeout enforcement → SIGALRM, SIGTERM fallback → TLE verdict

## Concurrency Model

**Circular Queue**:
- Max 100 queued submissions
- Head/tail pointers wrap at boundary
- No locks needed (single submission thread)
- Head updated after write, tail after result read

**Multi-Submission Handling**:
1. Submit 1..N → Queue in order
2. Guest processes sequentially (single executor thread)
3. Results returned in submission order
4. Host dequeues synchronously (poll blocks until ready)

**Thread Safety**:
- Host: single main thread (no contention)
- Guest: executor runs serially (batches submissions)
- Shared memory: atomic eventfd + structured writes (no corruption)

## Performance Characteristics

**Latency Per Submission**:
- Queue: ~1µs
- Eventfd signal: ~10µs (system call)
- Guest dispatch: ~100µs
- Compilation (simple C): ~100ms
- Execution (simple test): ~10ms
- Result return: ~10µs
- Total: ~120ms (for simple programs)

**Throughput**:
- Target: >10 submissions/second (after overhead)
- Limited by guest compilation time, not IPC
- Batching 100 submissions → ~2ms per submission

**Memory**:
- Host: judge_t struct (~1KB) + queue (~100*submission_size)
- Shared memory: ~200MB (100MB request + 100MB result payload)
- Guest: executor binary 968KB + minimal rootfs ~10MB

## Files Structure

```
judge/
├── include/
│   ├── judge_core.h         - Core data structures + API
│   ├── judge_executor.h     - Shared memory + eventfd
│   ├── judge_protocol.h     - Communication structures
│   ├── judge_resource.h     - cgroup v2 interface
│   ├── judge_verdict.h      - Verdict computation
│   ├── judge_module.h       - VM lifecycle integration
│   ├── judge_runner.h       - Result polling loop
│   ├── judge_perf.h         - Performance profiling
│   └── judge_test.h         - Test framework
├── src/
│   ├── judge_core.c         - Queue + submission management
│   ├── judge_executor.c     - shm + eventfd implementation
│   ├── judge_resource.c     - cgroup v2 operations
│   ├── judge_verdict.c      - Verdict logic + output comparison
│   ├── judge_comparison.c   - Comparison modes (exact, whitespace, validator)
│   ├── judge_module.c       - Module initialization
│   ├── judge_runner.c       - Result polling implementation
│   ├── judge_perf.c         - Performance statistics
│   ├── judge_stats.c        - Verdict statistics tracking
│   └── judge_test.c         - Test framework stubs
├── tests/
│   ├── test_verdicts.h      - Test case definitions
│   ├── test_cases.c         - Source code examples (6 verdicts)
│   ├── test_runner.c        - Test execution + verification
│   └── main.c               - Test suite entrypoint
└── docs/
    ├── JUDGE_API.md              - Usage reference
    ├── JUDGE_ARCHITECTURE.md     - This file
    ├── INTEGRATION_TESTING.md    - Testing strategy
    └── STRESS_TEST_VALIDATION.md - Load testing guide
```

## Evolution & Versioning

**Protocol Version**:
- judge_protocol.h version field (currently 1)
- Allow future extensions without breaking compatibility
- Host checks version on startup

**Build System**:
- Executor compiled statically → runs in any initrd
- Judge module integrates via judge/include headers
- KVM host project bundles judge framework

**Extensibility**:
- New comparison modes: add to judge_comparison.c
- New verdict types: extend enum, update priority order
- New metrics: extend judge_result_t, update statistics tracking
