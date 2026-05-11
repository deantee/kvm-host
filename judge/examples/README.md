# Judge Module Examples

## Simple Judge Example

`simple_judge.c` demonstrates how to use the judge module API to evaluate a C program submission.

### Features

- Initialize judge framework
- Submit a "Hello World" program
- Execute with verdict computation
- Display detailed results

### Building

```bash
# Build as standalone program (requires main KVM host binary)
make

# Or compile directly
gcc -O2 -Wall \
    -I.. \
    simple_judge.c \
    ../src/judge_*.c \
    -o simple_judge -lrt -lpthread
```

### Running

```bash
# First start KVM with judge enabled
sudo ../../../build/kvm-host \
    -k /boot/vmlinuz-linux \
    -i ../../../initrd_builder/build/initrd.img \
    -d /dev/root \
    --judge-enable

# In another terminal, run example
./simple_judge
```

### Expected Output

```
=== Simple Judge Example ===

[1] Initializing judge module...
    OK

[2] Creating submission...
    Code: 77 bytes
    Expected: 14 bytes
    Timeout: 5 seconds
    Memory limit: 256 MB

[3] Executing submission...
    OK

[4] Results:
    Verdict: AC (Accepted)
    Compile exit code: 0
    Program exit code: 0
    CPU time: 0.050 seconds
    Peak memory: 1 MB
    Output: 14 bytes
    Program output:
    ---
    Hello, World!
    ---

[5] Cleanup...
    OK

Example complete.
```

## API Overview

### High-Level API

```c
int judge_module_execute_with_verdict(
    struct judge_submission *sub,  // Submission with source code
    struct judge_result *res,      // Result output
    uint32_t poll_timeout_ms,      // Polling timeout
    const char *expected_output,   // Expected output (optional)
    judge_comparison_config_t *compare_config  // Comparison mode
);
```

Returns 0 on success, -1 on error. Sets `res->verdict` to one of:
- `VERDICT_AC`: Accepted (output matches)
- `VERDICT_WA`: Wrong Answer (output mismatch)
- `VERDICT_RE`: Runtime Error (non-zero exit)
- `VERDICT_TLE`: Time Limit Exceeded
- `VERDICT_MLE`: Memory Limit Exceeded
- `VERDICT_CE`: Compilation Error

### Submission Structure

```c
struct judge_submission {
    uint32_t submission_id;           // Unique ID
    uint32_t timeout_seconds;         // CPU time limit
    uint32_t memory_limit_bytes;      // Memory limit
    enum judge_language language;     // LANG_C or LANG_CXX
    uint32_t source_code_size;        // Code size
    char *source_code;                // Pointer to source (malloc'd)
    
    // For verdict computation
    uint32_t expected_output_size;    // Expected output size
    char *expected_output;            // Pointer to expected (malloc'd)
    enum judge_compare_mode compare_mode;  // Comparison strategy
    const char *validator_script_path;     // Custom validator (optional)
};
```

### Result Structure

```c
struct judge_result {
    uint32_t submission_id;
    enum judge_verdict verdict;      // Final verdict
    
    // Compilation info
    int32_t compile_exit_code;
    uint32_t compile_stdout_len;
    uint32_t compile_stderr_len;
    char compile_stdout[10000];
    char compile_stderr[10000];
    
    // Execution info
    int32_t program_exit_code;
    uint32_t program_stdout_len;
    uint32_t program_stderr_len;
    char program_stdout[10000000];   // 10MB
    char program_stderr[10000];
    
    // Resource usage
    double wall_time_seconds;
    double cpu_time_seconds;
    uint64_t peak_memory_bytes;
};
```

## Comparison Modes

### COMPARE_BYTE_FOR_BYTE (0)
Exact match required. Output must match expected exactly.

### COMPARE_WHITESPACE_NORMALIZED (1)
Whitespace-normalized comparison. Ignores leading/trailing whitespace.

### COMPARE_CUSTOM_VALIDATOR (2)
Run custom validator script. Script receives:
- Argument 1: Path to expected output file
- Argument 2: Path to actual output file
- Exit code 0: Accepted
- Non-zero: Wrong answer

## Next Steps

- Modify `simple_judge.c` to test different verdicts:
  - Change source to compilation error (missing include)
  - Change source to infinite loop (TLE)
  - Change output to wrong value (WA)
  - Create a large allocation (MLE)

- Use as template for full judge application:
  - Load submission from database
  - Process many submissions
  - Store results

- See `JUDGE_API.md` for complete API reference
