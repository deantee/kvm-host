#pragma once
#include <stdint.h>

#define JUDGE_SHM_REQUEST_PATH "/dev/shm/judge_request"
#define JUDGE_SHM_RESULT_PATH "/dev/shm/judge_result"
#define JUDGE_EVENTFD_REQUEST "/dev/shm/judge_request_fd"
#define JUDGE_EVENTFD_RESULT "/dev/shm/judge_result_fd"

#define JUDGE_MAX_SOURCE (10 * 1024 * 1024)
#define JUDGE_MAX_OUTPUT (100 * 1024 * 1024)
#define JUDGE_RESULT_BUF_SIZE (256 * 1024 * 1024)

#define JUDGE_PROTOCOL_VERSION 1

enum judge_sync_state {
    SYNC_IDLE = 0,
    SYNC_REQUEST_READY = 1,
    SYNC_RESULT_READY = 2,
};

typedef struct {
    uint32_t protocol_version;
    uint32_t request_id;
    uint32_t timeout_seconds;
    uint32_t memory_limit_bytes;
    uint32_t language;
    uint32_t source_code_offset;
    uint32_t source_code_size;
    uint8_t payload[JUDGE_MAX_SOURCE];
} judge_request_t;

typedef struct {
    uint32_t protocol_version;
    uint32_t request_id;
    uint32_t verdict;

    int32_t compile_exit_code;
    uint32_t compile_stdout_size;
    uint32_t compile_stderr_size;
    uint32_t compile_stdout_offset;
    uint32_t compile_stderr_offset;

    int32_t program_exit_code;
    uint32_t program_stdout_size;
    uint32_t program_stderr_size;
    uint32_t program_stdout_offset;
    uint32_t program_stderr_offset;

    double wall_time_seconds;
    double cpu_time_seconds;
    uint64_t peak_memory_bytes;

    char judge_message[256];

    uint8_t payload[JUDGE_RESULT_BUF_SIZE];
} judge_result_t;

int judge_protocol_open_request_shm(void);
int judge_protocol_open_result_shm(void);
int judge_protocol_open_eventfd(int *req_fd, int *res_fd);
int judge_protocol_write_request(int fd, judge_request_t *req);
int judge_protocol_read_request(int fd, judge_request_t *req);
int judge_protocol_write_result(int fd, judge_result_t *res);
int judge_protocol_read_result(int fd, judge_result_t *res);
