# Judge Framework API Documentation

## Overview

The KVM-Host Online Judge framework provides secure code evaluation with resource isolation.

## Core API

### judge_core.h

```c
int judge_init(judge_t *j, struct judge_config *cfg);
int judge_submit(judge_t *j, struct judge_submission *sub);
int judge_poll_result(judge_t *j, struct judge_result *res);
void judge_cleanup(judge_t *j);
```

- **judge_init()**: Initialize judge framework with config (memory limit, seccomp setting)
- **judge_submit()**: Queue submission for evaluation
- **judge_poll_result()**: Check for completed result (non-blocking)
- **judge_cleanup()**: Release judge resources

## Module API

### judge_module.h

```c
int judge_module_init(int enable_judge, uint64_t guest_memory_limit);
judge_t *judge_module_get(void);
int judge_module_submit(struct judge_submission *sub);
int judge_module_poll_result(struct judge_result *res);
int judge_module_execute_submission(struct judge_submission *sub,
                                    struct judge_result *res,
                                    uint32_t poll_timeout_ms);
void judge_module_cleanup(void);
```

- **judge_module_init()**: Initialize judge framework
- **judge_module_get()**: Get global judge instance
- **judge_module_submit()**: Queue submission
- **judge_module_poll_result()**: Check for result (non-blocking)
- **judge_module_execute_submission()**: Unified synchronous API (submit + poll with timeout)
- **judge_module_cleanup()**: Release resources

Simplifies judge lifecycle. Called from main.c after vm_late_init().

## Structures

### Verdict Enum (6 verdicts)

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

### Languages

```c
enum judge_language {
    LANG_C = 0,
    LANG_CXX = 1,
};
```

### Submission

```c
struct judge_submission {
    uint32_t submission_id;
    uint32_t timeout_seconds;
    uint32_t memory_limit_bytes;
    enum judge_language language;
    uint32_t source_code_size;
    char *source_code;  // Allocated separately
};
```

### Result

```c
struct judge_result {
    uint32_t submission_id;
    enum judge_verdict verdict;
    int32_t compile_exit_code;
    uint32_t compile_stdout_len;
    char *compile_stdout;
    char *compile_stderr;
    int32_t program_exit_code;
    uint32_t program_stdout_len;
    char *program_stdout;
    char *program_stderr;
    double wall_time_seconds;
    double cpu_time_seconds;
    uint64_t peak_memory_bytes;
    char judge_message[256];
};
```

## Usage Examples

### Synchronous Pattern (Recommended)

```c
#include "judge_module.h"

int main() {
    // Initialize judge mode (512 MB memory limit)
    if (judge_module_init(1, 512 * 1024 * 1024) < 0) {
        return 1;
    }

    // Create submission
    struct judge_submission sub = {
        .submission_id = 1,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
        .source_code = "#include <stdio.h>\nint main() { printf(\"Hello\\n\"); return 0; }",
        .source_code_size = strlen(sub.source_code),
    };

    // Execute with timeout (max 30 seconds to wait for result)
    struct judge_result res;
    if (judge_module_execute_submission(&sub, &res, 30000) < 0) {
        fprintf(stderr, "Submission failed\n");
        judge_module_cleanup();
        return 1;
    }

    // Check verdict
    if (res.verdict == VERDICT_AC) {
        printf("Accepted!\n");
    } else {
        printf("Verdict: %d, Exit: %d, Memory: %llu bytes, CPU: %.2f sec\n",
               res.verdict, res.program_exit_code, res.peak_memory_bytes, res.cpu_time_seconds);
    }

    judge_module_cleanup();
    return 0;
}
```

### Asynchronous Pattern (Advanced)

```c
#include "judge_module.h"

int main() {
    judge_module_init(1, 512 * 1024 * 1024);

    // Submit
    struct judge_submission sub = {...};
    judge_module_submit(&sub);

    // Poll result
    struct judge_result res;
    while (judge_module_poll_result(&res) < 0) {
        usleep(100000);  // Wait 100ms
    }

    judge_module_cleanup();
    return 0;
}
```

## Verdict Determination

Verdicts are checked in strict order (first match wins):

1. **CE** (Compilation Error): compile_exit_code != 0
   - Compilation failed (gcc/g++ returned non-zero)
   - compile_stderr contains error messages
   
2. **MLE** (Memory Limit Exceeded): peak_memory > memory_limit
   - Process exceeded memory limit during execution
   - Guest cgroup memory.peak exceeded memory_limit_bytes
   
3. **TLE** (Time Limit Exceeded): cpu_time > timeout_seconds
   - Process exceeded CPU time limit
   - SIGALRM signal triggered timeout enforcement
   
4. **RE** (Runtime Error): program_exit_code != 0 (and not TLE/MLE)
   - Program crashed, segfault, or exited with non-zero code
   - program_stderr may contain error output
   
5. **AC/WA** (Accepted/Wrong Answer): Based on output comparison
   - AC: Output matches expected output exactly (or per comparison mode)
   - WA: Program ran successfully but output doesn't match

## Comparison Modes

- **BYTE_FOR_BYTE**: Exact string match (strcmp)
- **WHITESPACE_NORMALIZED**: Token comparison ignoring whitespace
- **CUSTOM_VALIDATOR**: External validator script

## Resource Limits

### Host Side (cgroup v2)
- memory.max: Hard limit, triggers OOM killer
- memory.peak: Peak memory tracking
- cpu.max: CPU quota/period throttling

### Guest Side
- setrlimit(RLIMIT_CPU): CPU time limit
- setrlimit(RLIMIT_AS): Virtual memory limit
- setrlimit(RLIMIT_STACK): Stack size limit
- cgroup v2 memory.max, cpu.max

## Communication

- **Shared Memory**: /dev/shm/judge_request, /dev/shm/judge_result
- **Signaling**: eventfd for submission ready / result ready notifications
- **Protocol**: judge_protocol.h defines structures

## Error Codes

All functions return:
- **0**: Success
- **-1**: Error

Errors are logged to stderr with perror().
