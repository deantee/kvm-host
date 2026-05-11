#include "../include/judge_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/judge_executor.h"

int judge_init(judge_t *j, struct judge_config *cfg)
{
    if (!j || !cfg)
        return -1;

    memcpy(&j->config, cfg, sizeof(struct judge_config));

    // Initialize queue (max 100 submissions)
    j->queue_size = 100;
    j->submission_queue =
        calloc(j->queue_size, sizeof(struct judge_submission));
    if (!j->submission_queue)
        return -1;

    j->queue_head = 0;
    j->queue_tail = 0;
    j->pending_request_id = 0;
    j->last_result_id = -1;

    // TODO: Create host-side cgroup
    // TODO: Start guest executor daemon

    return 0;
}

int judge_submit(judge_t *j, struct judge_submission *sub)
{
    if (!j || !sub)
        return -1;

    // Check queue not full
    uint32_t next_tail = (j->queue_tail + 1) % j->queue_size;
    if (next_tail == j->queue_head)
        return -1;  // Queue full

    // Allocate source code buffer
    j->submission_queue[j->queue_tail].source_code =
        malloc(sub->source_code_size);
    if (!j->submission_queue[j->queue_tail].source_code)
        return -1;

    // Copy submission
    memcpy(&j->submission_queue[j->queue_tail], sub,
           sizeof(struct judge_submission));
    memcpy(j->submission_queue[j->queue_tail].source_code, sub->source_code,
           sub->source_code_size);

    // Build judge_request_t for shared memory
    judge_request_t req;
    memset(&req, 0, sizeof(req));
    req.protocol_version = JUDGE_PROTOCOL_VERSION;
    req.request_id = sub->submission_id;
    req.timeout_seconds = sub->timeout_seconds;
    req.memory_limit_bytes = sub->memory_limit_bytes;
    req.language = sub->language;
    req.source_code_offset = 0;
    req.source_code_size = sub->source_code_size;

    // Copy source code to payload (at offset 0)
    if (sub->source_code_size > JUDGE_MAX_SOURCE) {
        fprintf(stderr, "Source code too large: %u > %u\n",
                sub->source_code_size, JUDGE_MAX_SOURCE);
        free(j->submission_queue[j->queue_tail].source_code);
        return -1;
    }
    memcpy(req.payload, sub->source_code, sub->source_code_size);

    // Write to shared memory
    if (judge_executor_write_request(&req) < 0) {
        fprintf(stderr, "Failed to write request to shared memory\n");
        free(j->submission_queue[j->queue_tail].source_code);
        return -1;
    }

    // Signal guest via eventfd
    if (judge_executor_signal_request_ready() < 0) {
        fprintf(stderr, "Failed to signal request ready\n");
        free(j->submission_queue[j->queue_tail].source_code);
        return -1;
    }

    // Track pending request
    j->pending_request_id = sub->submission_id;
    j->last_result_id = -1;

    j->queue_tail = next_tail;
    return 0;
}

int judge_poll_result(judge_t *j, struct judge_result *res)
{
    if (!j || !res)
        return -1;

    // If no pending request, nothing to poll
    if (j->pending_request_id <= 0 ||
        j->last_result_id == j->pending_request_id) {
        return -1;
    }

    // Get result from shared memory (non-blocking check)
    judge_result_t *shm_result = judge_executor_get_result();
    if (!shm_result) {
        return -1;
    }

    // Check if result is for pending request
    if (shm_result->request_id != j->pending_request_id) {
        return -1;  // Result not ready yet
    }

    // Copy result to output
    res->verdict = shm_result->verdict;
    res->compile_exit_code = shm_result->compile_exit_code;
    res->program_exit_code = shm_result->program_exit_code;
    res->wall_time_seconds = shm_result->wall_time_seconds;
    res->cpu_time_seconds = shm_result->cpu_time_seconds;
    res->peak_memory_bytes = shm_result->peak_memory_bytes;

    // Copy output buffers from payload
    if (shm_result->compile_stdout_size > 0) {
        uint32_t copy_len = shm_result->compile_stdout_size;
        if (copy_len > 10000)
            copy_len = 10000;
        memcpy(res->compile_stdout,
               shm_result->payload + shm_result->compile_stdout_offset,
               copy_len);
        res->compile_stdout_len = copy_len;
    }

    if (shm_result->compile_stderr_size > 0) {
        uint32_t copy_len = shm_result->compile_stderr_size;
        if (copy_len > 10000)
            copy_len = 10000;
        memcpy(res->compile_stderr,
               shm_result->payload + shm_result->compile_stderr_offset,
               copy_len);
        res->compile_stderr_len = copy_len;
    }

    if (shm_result->program_stdout_size > 0) {
        uint32_t copy_len = shm_result->program_stdout_size;
        if (copy_len > 100 * 1024 * 1024)
            copy_len = 100 * 1024 * 1024;
        if (copy_len > 10000000)
            copy_len = 10000000;  // Cap at 10MB for safety
        memcpy(res->program_stdout,
               shm_result->payload + shm_result->program_stdout_offset,
               copy_len);
        res->program_stdout_len = copy_len;
    }

    if (shm_result->program_stderr_size > 0) {
        uint32_t copy_len = shm_result->program_stderr_size;
        if (copy_len > 10000)
            copy_len = 10000;
        memcpy(res->program_stderr,
               shm_result->payload + shm_result->program_stderr_offset,
               copy_len);
        res->program_stderr_len = copy_len;
    }

    // Mark result as consumed
    j->last_result_id = j->pending_request_id;

    return 0;
}

void judge_cleanup(judge_t *j)
{
    if (!j)
        return;
    if (j->submission_queue) {
        for (int i = 0; i < j->queue_size; i++) {
            if (j->submission_queue[i].source_code) {
                free(j->submission_queue[i].source_code);
            }
            if (j->submission_queue[i].expected_output) {
                free(j->submission_queue[i].expected_output);
            }
        }
        free(j->submission_queue);
    }
    // TODO: Clean up cgroups
}
