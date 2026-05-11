#pragma once
#include <stdint.h>
#include "judge_protocol.h"

enum executor_verdict {
    EXEC_AC = 0,
    EXEC_WA = 1,
    EXEC_RE = 2,
    EXEC_TLE = 3,
    EXEC_MLE = 4,
    EXEC_CE = 5,
};

enum executor_language {
    EXEC_LANG_C = 0,
    EXEC_LANG_CXX = 1,
};

#define EXECUTOR_MAX_SOURCE (10 * 1024 * 1024)
#define EXECUTOR_MAX_OUTPUT (100 * 1024 * 1024)

struct executor_request {
    uint32_t request_id;
    uint32_t timeout_seconds;
    uint32_t memory_limit_bytes;
    enum executor_language language;
    uint32_t source_code_size;
    uint8_t source_code[EXECUTOR_MAX_SOURCE];
};

/* Local result structure with inline buffers for executor computation */
struct executor_result {
    uint32_t request_id;
    enum executor_verdict verdict;
    int32_t compile_exit_code;
    uint32_t compile_stdout_len;
    uint8_t compile_stdout[10000];
    uint32_t compile_stderr_len;
    uint8_t compile_stderr[10000];
    int32_t program_exit_code;
    uint32_t program_stdout_len;
    uint8_t program_stdout[EXECUTOR_MAX_OUTPUT];
    uint32_t program_stderr_len;
    uint8_t program_stderr[10000];
    double wall_time_seconds;
    double cpu_time_seconds;
    uint64_t peak_memory_bytes;
};

int executor_init(void);
int executor_run_submission(struct executor_request *req,
                            struct executor_result *res);
void executor_cleanup(void);
int executor_cleanup_submission(uint32_t request_id);

int executor_open_shared_mem(void);
int executor_get_request(judge_request_t *req);
int executor_write_result(judge_result_t *res);
void executor_close_shared_mem(void);

int executor_open_eventfd(void);
int executor_wait_request_ready(void);
int executor_signal_result_ready(void);
void executor_close_eventfd(void);

int executor_write_source(const char *filename,
                          const uint8_t *source, uint32_t size);
int executor_compile_c(const char *source_file,
                       const char *binary_file,
                       struct executor_result *res);
int executor_compile_cxx(const char *source_file,
                         const char *binary_file,
                         struct executor_result *res);

int executor_execute_binary(const char *binary_file,
                            const char *input_data,
                            uint32_t input_size,
                            struct executor_result *res);

int executor_execute_with_timeout(const char *binary_file,
                                  const char *input_data,
                                  uint32_t input_size,
                                  uint32_t timeout_seconds,
                                  struct executor_result *res);

int executor_read_cgroup_memory(uint32_t request_id,
                                uint64_t *peak_memory);
int executor_read_cgroup_cpu(uint32_t request_id,
                             uint64_t *cpu_time_usec);
int executor_record_metrics(uint32_t request_id,
                           struct executor_result *res);

int executor_setup_resource_limits(uint32_t memory_limit_bytes,
                                   uint32_t timeout_seconds);
int executor_create_submission_cgroup(uint32_t request_id,
                                      uint32_t memory_limit_bytes);
int executor_add_self_to_cgroup(uint32_t request_id);
