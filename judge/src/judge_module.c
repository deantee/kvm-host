/* Judge module - VM lifecycle integration */

#include "../include/judge_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../include/judge_executor.h"
#include "../include/judge_resource.h"

static judge_t *global_judge = NULL;

int judge_module_init(int enable_judge, uint64_t guest_memory_limit)
{
    if (!enable_judge) {
        return 0; /* Judge mode not enabled */
    }

    /* Allocate judge instance */
    global_judge = malloc(sizeof(judge_t));
    if (!global_judge) {
        fprintf(stderr, "Failed to allocate judge instance\n");
        return -1;
    }

    /* Configure judge */
    struct judge_config cfg = {
        .enabled = 1,
        .guest_memory_limit = guest_memory_limit,
        .use_seccomp = 0, /* Will be set separately */
        .verbose = 1,
    };

    /* Initialize judge core */
    if (judge_init(global_judge, &cfg) < 0) {
        fprintf(stderr, "Failed to initialize judge core\n");
        free(global_judge);
        global_judge = NULL;
        return -1;
    }

    /* Initialize shared memory regions */
    if (judge_executor_init_shm() < 0) {
        fprintf(stderr, "Failed to initialize shared memory\n");
        judge_cleanup(global_judge);
        free(global_judge);
        global_judge = NULL;
        return -1;
    }

    /* Initialize eventfd synchronization */
    if (judge_executor_init_eventfd() < 0) {
        fprintf(stderr, "Failed to initialize eventfd\n");
        judge_executor_cleanup_shm();
        judge_cleanup(global_judge);
        free(global_judge);
        global_judge = NULL;
        return -1;
    }

    /* Create host-side cgroup for safety net */
    char host_cgroup[256];
    snprintf(host_cgroup, sizeof(host_cgroup), "/sys/fs/cgroup/kvm_host_%d",
             getpid());

    if (judge_resource_create_cgroup(host_cgroup, guest_memory_limit) < 0) {
        fprintf(stderr,
                "Warning: Failed to create host cgroup (continuing anyway)\n");
        /* Non-fatal - continue without host-side cgroup */
    } else {
        strncpy(global_judge->config.cgroup_path, host_cgroup,
                sizeof(global_judge->config.cgroup_path) - 1);
        printf("[judge] Host cgroup: %s (limit=%lu)\n", host_cgroup,
               guest_memory_limit);
    }

    printf("[judge] Initialized (memory_limit=%lu)\n", guest_memory_limit);
    return 0;
}

judge_t *judge_module_get(void)
{
    return global_judge;
}

int judge_module_poll_result(struct judge_result *res)
{
    if (!global_judge || !res)
        return -1;
    return judge_poll_result(global_judge, res);
}

int judge_module_submit(struct judge_submission *sub)
{
    if (!global_judge || !sub)
        return -1;
    return judge_submit(global_judge, sub);
}

void judge_module_cleanup(void)
{
    if (!global_judge)
        return;

    judge_executor_cleanup_eventfd();
    judge_executor_cleanup_shm();
    judge_cleanup(global_judge);
    free(global_judge);
    global_judge = NULL;

    printf("[judge] Cleaned up\n");
}

int judge_module_execute_submission(struct judge_submission *sub,
                                    struct judge_result *res,
                                    uint32_t poll_timeout_ms)
{
    if (!global_judge || !sub || !res)
        return -1;

    uint64_t start_time = time(NULL) * 1000;
    uint64_t timeout_end = start_time + poll_timeout_ms;

    if (judge_module_submit(sub) < 0) {
        fprintf(stderr, "Failed to submit\n");
        return -1;
    }

    while (time(NULL) * 1000 < timeout_end) {
        int ret = judge_module_poll_result(res);
        if (ret == 0) {
            return 0;
        }
        usleep(100000);
    }

    fprintf(stderr, "Submission poll timeout\n");
    return -1;
}

int judge_module_execute_with_verdict(struct judge_submission *sub,
                                      struct judge_result *res,
                                      uint32_t poll_timeout_ms,
                                      const char *expected_output,
                                      judge_comparison_config_t *compare_config)
{
    if (!sub || !res)
        return -1;

    /* Execute submission and get raw result */
    if (judge_module_execute_submission(sub, res, poll_timeout_ms) < 0) {
        fprintf(stderr, "Failed to execute submission\n");
        return -1;
    }

    /* Use expected_output from submission if parameter not provided */
    const char *exp_output =
        expected_output ? expected_output : sub->expected_output;

    /* Use comparison config from submission if parameter not provided */
    judge_comparison_config_t cfg = {0};
    if (!compare_config) {
        cfg.mode = sub->compare_mode;
        cfg.validator_script_path = sub->validator_script_path;
        compare_config = &cfg;
    }

    /* Compute verdict based on result and expected output */
    res->verdict = judge_compute_verdict_from_result(
        res, exp_output, sub->timeout_seconds, sub->memory_limit_bytes,
        compare_config);

    return 0;
}
