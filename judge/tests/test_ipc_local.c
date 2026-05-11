/* Local IPC test - simulate host/guest communication without KVM */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "../include/judge_core.h"
#include "../include/judge_protocol.h"
#include "../include/judge_verdict.h"

#define TEST_SHM_PATH "/ipc_test"

static judge_request_t *req_shm = NULL;
static judge_result_t *res_shm = NULL;

/* Initialize shared memory */
int setup_shm(void) {
    int fd_req = shm_open(TEST_SHM_PATH "_req", O_CREAT | O_RDWR, 0666);
    if (fd_req < 0) {
        perror("shm_open request");
        return -1;
    }

    if (ftruncate(fd_req, sizeof(judge_request_t)) < 0) {
        perror("ftruncate request");
        close(fd_req);
        return -1;
    }

    req_shm = mmap(NULL, sizeof(judge_request_t),
                   PROT_READ | PROT_WRITE, MAP_SHARED, fd_req, 0);
    close(fd_req);

    if (req_shm == MAP_FAILED) {
        perror("mmap request");
        return -1;
    }

    int fd_res = shm_open(TEST_SHM_PATH "_res", O_CREAT | O_RDWR, 0666);
    if (fd_res < 0) {
        perror("shm_open result");
        return -1;
    }

    if (ftruncate(fd_res, sizeof(judge_result_t)) < 0) {
        perror("ftruncate result");
        close(fd_res);
        return -1;
    }

    res_shm = mmap(NULL, sizeof(judge_result_t),
                   PROT_READ | PROT_WRITE, MAP_SHARED, fd_res, 0);
    close(fd_res);

    if (res_shm == MAP_FAILED) {
        perror("mmap result");
        return -1;
    }

    memset(req_shm, 0, sizeof(judge_request_t));
    memset(res_shm, 0, sizeof(judge_result_t));
    return 0;
}

void cleanup_shm(void) {
    if (req_shm && req_shm != MAP_FAILED) {
        munmap(req_shm, sizeof(judge_request_t));
        shm_unlink(TEST_SHM_PATH "_req");
    }
    if (res_shm && res_shm != MAP_FAILED) {
        munmap(res_shm, sizeof(judge_result_t));
        shm_unlink(TEST_SHM_PATH "_res");
    }
}

/* Simulate guest executor writing result */
int simulate_guest_result(uint32_t request_id,
                          uint32_t verdict,
                          const char *output) {
    if (!res_shm) return -1;

    memset(res_shm, 0, sizeof(judge_result_t));
    res_shm->protocol_version = JUDGE_PROTOCOL_VERSION;
    res_shm->request_id = request_id;
    res_shm->verdict = verdict;
    res_shm->program_exit_code = 0;
    res_shm->compile_exit_code = 0;
    res_shm->cpu_time_seconds = 0.1;
    res_shm->peak_memory_bytes = 1024 * 1024;

    if (output) {
        uint32_t len = strlen(output);
        if (len > 1000) len = 1000;
        memcpy(res_shm->payload, output, len);
        res_shm->program_stdout_offset = 0;
        res_shm->program_stdout_size = len;
    }

    return 0;
}

/* Test 1: Basic request/response cycle */
int test_basic_ipc(void) {
    printf("[TEST 1] Basic IPC request/response\n");

    if (setup_shm() < 0) {
        fprintf(stderr, "  FAIL: Setup shared memory\n");
        return -1;
    }

    /* Host writes request */
    req_shm->protocol_version = JUDGE_PROTOCOL_VERSION;
    req_shm->request_id = 42;
    req_shm->timeout_seconds = 5;
    req_shm->memory_limit_bytes = 256 * 1024 * 1024;
    const char *src = "printf(\"hi\");";
    req_shm->source_code_size = strlen(src);
    memcpy(req_shm->payload, src, req_shm->source_code_size);

    /* Guest reads request */
    if (req_shm->request_id != 42 ||
        req_shm->protocol_version != JUDGE_PROTOCOL_VERSION) {
        fprintf(stderr, "  FAIL: Request not readable\n");
        cleanup_shm();
        return -1;
    }

    /* Guest writes result */
    simulate_guest_result(42, VERDICT_AC, "hi");

    /* Host reads result */
    if (res_shm->request_id != 42 ||
        res_shm->verdict != VERDICT_AC ||
        res_shm->program_stdout_size != 2) {
        fprintf(stderr, "  FAIL: Result not correct\n");
        cleanup_shm();
        return -1;
    }

    printf("  PASS\n");
    cleanup_shm();
    return 0;
}

/* Test 2: Verdict computation */
int test_verdict_computation(void) {
    printf("[TEST 2] Verdict computation\n");

    struct judge_result res;
    memset(&res, 0, sizeof(res));

    /* AC: exit 0, output matches */
    res.program_exit_code = 0;
    res.compile_exit_code = 0;
    res.cpu_time_seconds = 1.0;
    res.peak_memory_bytes = 10 * 1024 * 1024;
    strcpy(res.program_stdout, "Hello");
    res.program_stdout_len = 5;

    enum judge_verdict v = judge_compute_verdict_from_result(
        &res, "Hello", 5, 256 * 1024 * 1024, NULL);
    if (v != VERDICT_AC) {
        fprintf(stderr, "  FAIL: AC verdict\n");
        return -1;
    }

    /* CE: compile error */
    res.compile_exit_code = 1;
    v = judge_compute_verdict_from_result(
        &res, "Hello", 5, 256 * 1024 * 1024, NULL);
    if (v != VERDICT_CE) {
        fprintf(stderr, "  FAIL: CE verdict\n");
        return -1;
    }

    /* TLE: timeout */
    res.compile_exit_code = 0;
    res.cpu_time_seconds = 10.0;  /* > 5 sec timeout */
    v = judge_compute_verdict_from_result(
        &res, "Hello", 5, 256 * 1024 * 1024, NULL);
    if (v != VERDICT_TLE) {
        fprintf(stderr, "  FAIL: TLE verdict\n");
        return -1;
    }

    /* MLE: memory exceeded */
    res.cpu_time_seconds = 1.0;
    res.peak_memory_bytes = 512 * 1024 * 1024;  /* > 256MB limit */
    v = judge_compute_verdict_from_result(
        &res, "Hello", 5, 256 * 1024 * 1024, NULL);
    if (v != VERDICT_MLE) {
        fprintf(stderr, "  FAIL: MLE verdict\n");
        return -1;
    }

    /* WA: wrong output */
    res.peak_memory_bytes = 10 * 1024 * 1024;
    strcpy(res.program_stdout, "Goodbye");
    res.program_stdout_len = 7;
    v = judge_compute_verdict_from_result(
        &res, "Hello", 5, 256 * 1024 * 1024, NULL);
    if (v != VERDICT_WA) {
        fprintf(stderr, "  FAIL: WA verdict\n");
        return -1;
    }

    /* RE: non-zero exit */
    res.program_exit_code = 1;
    strcpy(res.program_stdout, "Hello");
    res.program_stdout_len = 5;
    v = judge_compute_verdict_from_result(
        &res, "Hello", 5, 256 * 1024 * 1024, NULL);
    if (v != VERDICT_RE) {
        fprintf(stderr, "  FAIL: RE verdict\n");
        return -1;
    }

    printf("  PASS\n");
    return 0;
}

int main(void) {
    printf("=== Local IPC Test Suite ===\n\n");

    int pass = 0, fail = 0;

    if (test_basic_ipc() == 0) pass++; else fail++;
    if (test_verdict_computation() == 0) pass++; else fail++;

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", pass);
    printf("Failed: %d\n", fail);

    return fail > 0 ? 1 : 0;
}
