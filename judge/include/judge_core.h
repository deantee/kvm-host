#pragma once
#include <stdint.h>
#include <time.h>

#define JUDGE_MAX_SOURCE_SIZE (10 * 1024 * 1024)  // 10MB
#define JUDGE_MAX_OUTPUT_SIZE (100 * 1024 * 1024) // 100MB
#define JUDGE_RESULT_PATH "/dev/shm/judge_result_"

enum judge_verdict {
    VERDICT_AC = 0,   // Accepted
    VERDICT_WA = 1,   // Wrong Answer
    VERDICT_RE = 2,   // Runtime Error
    VERDICT_TLE = 3,  // Time Limit Exceeded
    VERDICT_MLE = 4,  // Memory Limit Exceeded
    VERDICT_CE = 5,   // Compilation Error
};

enum judge_language {
    LANG_C = 0,
    LANG_CXX = 1,
};

enum judge_compare_mode {
    COMPARE_BYTE_FOR_BYTE = 0,
    COMPARE_WHITESPACE_NORMALIZED = 1,
    COMPARE_CUSTOM_VALIDATOR = 2,
};

struct judge_submission {
    uint32_t submission_id;
    uint32_t timeout_seconds;
    uint32_t memory_limit_bytes;
    enum judge_language language;
    uint32_t source_code_size;
    char *source_code;  // Allocated separately

    /* Expected output for verdict comparison (optional) */
    uint32_t expected_output_size;
    char *expected_output;  // Allocated separately, NULL if no comparison needed

    /* Output comparison mode (optional) */
    enum judge_compare_mode compare_mode;
    const char *validator_script_path;  // Custom validator (optional)
};

struct judge_result {
    uint32_t submission_id;
    enum judge_verdict verdict;
    int32_t compile_exit_code;
    uint32_t compile_stdout_len;
    uint32_t compile_stderr_len;
    char compile_stdout[10000];
    char compile_stderr[10000];
    int32_t program_exit_code;
    uint32_t program_stdout_len;
    uint32_t program_stderr_len;
    char program_stdout[10000000];
    char program_stderr[10000];
    double wall_time_seconds;
    double cpu_time_seconds;
    uint64_t peak_memory_bytes;
    char judge_message[256];
};

struct judge_config {
    int enabled;
    uint32_t guest_memory_limit;  // Host-side limit
    int use_seccomp;
    int verbose;
    char cgroup_path[256];  // Host-side cgroup path
};

// Judge framework handle
typedef struct {
    struct judge_config config;
    struct judge_submission *submission_queue;
    uint32_t queue_size;
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t pending_request_id;  // ID of currently pending request
    int32_t last_result_id;       // ID of last result consumed
} judge_t;

// API
int judge_init(judge_t *j, struct judge_config *cfg);
int judge_submit(judge_t *j, struct judge_submission *sub);
int judge_poll_result(judge_t *j, struct judge_result *res);
void judge_cleanup(judge_t *j);
