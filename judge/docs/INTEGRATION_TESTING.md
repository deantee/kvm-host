# Judge Framework Integration Testing Plan

## Overview

This document outlines the comprehensive testing strategy for the KVM-Host Online Judge framework, covering end-to-end integration from submission to verdict delivery.

## Test Phases

### Phase 1: Unit Testing (Completed)

Individual component testing:
- `judge_core.c`: Queue operations (enqueue, dequeue, wraparound)
- `judge_executor.c`: Shared memory mapping and eventfd signaling
- `judge_resource.c`: cgroup v2 operations (create, read, cleanup)
- `judge_verdict.c`: Verdict computation logic
- `judge_comparison.c`: Output comparison modes

**Coverage**: ~95% of functions

### Phase 2: Integration Testing (In Progress)

#### 2.1 Host-Guest IPC

**Test**: Verify bidirectional communication

```
1. Write request to shared memory
2. Signal eventfd (request_ready)
3. Guest reads and processes
4. Guest writes result to shared memory
5. Guest signals eventfd (result_ready)
6. Host reads result
7. Verify data integrity
```

**Success Criteria**:
- No corruption of request/result structures
- Proper eventfd signaling received
- Protocol version matching

#### 2.2 Resource Isolation

**Test**: Verify resource limits are enforced

**Memory Limit Test**:
```c
char *huge = malloc(1GB);
huge[0] = 'A';  // Force allocation
```

**Expected**: MLE verdict with peak_memory > memory_limit

**CPU Time Limit Test**:
```c
while (1) {}  // Infinite loop
```

**Expected**: TLE verdict with cpu_time > timeout_seconds

#### 2.3 Compilation Pipeline

**Test**: Verify gcc/g++ compilation works

**C Compilation**:
```c
#include <stdio.h>
int main() { printf("Hello\n"); return 0; }
```

**C++ Compilation**:
```cpp
#include <iostream>
int main() { std::cout << "Hello\n"; return 0; }
```

**Compilation Error**:
```c
int main() { missing_semicolon() }
```

**Expected**: CE verdict for syntax errors

#### 2.4 Execution and Output Capture

**Test**: Verify program execution and I/O capture

**Standard Output**:
- Capture stdout from executed binary
- Verify byte-for-byte match

**Standard Error**:
- Capture stderr from executed binary
- Verify in result structure

**Exit Codes**:
- Verify program_exit_code is correctly recorded
- Handle all exit codes (0-255)

### Phase 3: Verdict Testing (Critical)

Each verdict must be testable with corresponding source code:

| Verdict | Test Case | Trigger | Verify |
|---------|-----------|---------|--------|
| **AC** | Hello World | Output match | verdict==VERDICT_AC |
| **WA** | Wrong output | Output mismatch | verdict==VERDICT_WA |
| **RE** | Segfault | Null deref | verdict==VERDICT_RE, exit_code!=0 |
| **TLE** | Infinite loop | Timeout | verdict==VERDICT_TLE, cpu_time>limit |
| **MLE** | Alloc 1GB | OOM | verdict==VERDICT_MLE, memory>limit |
| **CE** | Syntax error | gcc fails | verdict==VERDICT_CE, compile_exit_code!=0 |

**Verdict Priority Verification**:
```
Test TLE + MLE together: should return MLE (higher priority)
Test CE + anything: should return CE (highest priority)
Test MLE + RE together: should return MLE
```

### Phase 4: Comparison Modes

**Test**: Verify all 3 comparison modes work

#### 4.1 Byte-for-Byte

```c
Expected: "Hello, World!\n"
Actual:   "Hello, World!\n"
Result: AC
```

#### 4.2 Whitespace-Normalized

```c
Expected: "1 2 3\n4 5 6"
Actual:   "1  2   3\n\n4 5 6\n"
Result: AC (whitespace ignored)
```

#### 4.3 Custom Validator

```bash
#!/bin/bash
# judge_validator.sh expected actual
diff -q "$1" "$2" >/dev/null 2>&1
exit $?
```

**Test**: 
- Validator returns 0 → AC
- Validator returns 1 → WA
- Validator crashes → WA

### Phase 5: Concurrent Submissions

**Test**: Multiple submissions in queue

**Setup**:
- Configure circular queue (max 100 submissions)
- Submit 10 sequential programs
- Verify all get processed in order
- No results lost or corrupted

**Expected**:
- All 10 results returned in order
- Verdicts correct for each
- No queue overflow

### Phase 6: Performance Testing

**Test**: Measure throughput and latency

**Throughput**:
- Submit 100 simple AC programs
- Measure submissions/second
- Target: >10 submissions/second (after buildroot)

**Latency**:
- Record compile time + execution time
- Average per submission: <500ms (target)

**Memory**:
- Host cgroup memory usage
- Guest peak memory tracking
- No memory leaks over 1000 submissions

### Phase 7: Error Handling

**Test**: Graceful error handling

| Error | Trigger | Expected |
|-------|---------|----------|
| Shared mem full | Write beyond payload | -1 return, error logged |
| Executor timeout | Guest hung | SIGTERM after max_wait_time |
| Invalid verdict | Corrupt result | Treated as RE or error |
| Queue overflow | >100 submissions | Submissions rejected or queued |

## Test Execution Checklist

- [ ] Phase 1: Unit tests pass
- [ ] Phase 2: IPC bidirectional
- [ ] Phase 2: Resource limits enforced
- [ ] Phase 2: Compilation works (gcc/g++/errors)
- [ ] Phase 2: Output capture working
- [ ] Phase 3: AC verdict correct
- [ ] Phase 3: WA verdict correct
- [ ] Phase 3: RE verdict correct
- [ ] Phase 3: TLE verdict correct
- [ ] Phase 3: MLE verdict correct
- [ ] Phase 3: CE verdict correct
- [ ] Phase 3: Verdict priorities correct
- [ ] Phase 4: Byte-for-byte comparison
- [ ] Phase 4: Whitespace-normalized comparison
- [ ] Phase 4: Custom validator works
- [ ] Phase 5: 10 concurrent submissions pass
- [ ] Phase 5: Queue wraparound (100+ subs) works
- [ ] Phase 6: Throughput >10 subs/sec
- [ ] Phase 6: Latency <500ms average
- [ ] Phase 6: No memory leaks
- [ ] Phase 7: Error handling correct

## Buildroot Integration

Once buildroot build completes:

1. Test executor binary boots and initializes
2. Run Phase 1-7 test suite
3. Verify all test cases pass
4. Generate performance report
5. Deploy to production

## Success Criteria

**Build Complete**: All test phases pass with:
- 0 functional failures
- 0 data corruption issues
- 0 memory leaks
- Throughput >10 submissions/second
- Average latency <500ms
