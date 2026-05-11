#pragma once
#include "judge_core.h"
#include "judge_protocol.h"

typedef struct {
    enum judge_compare_mode mode;
    const char *validator_script_path;
} judge_comparison_config_t;

/* Compute verdict from judge_result (host-side format) */
enum judge_verdict judge_compute_verdict_from_result(
    struct judge_result *result,
    const char *expected_output,
    uint32_t timeout_seconds,
    uint32_t memory_limit,
    judge_comparison_config_t *config);

/* Compute verdict from judge_result_t (shared memory format) */
enum judge_verdict judge_compute_verdict(judge_result_t *result,
                                         const char *expected_output,
                                         uint32_t timeout_seconds,
                                         uint32_t memory_limit,
                                         judge_comparison_config_t *config);

int judge_compare_outputs(const char *expected,
                          const char *actual,
                          enum judge_compare_mode mode,
                          const char *validator_path);

int judge_run_validator(const char *expected,
                        const char *actual,
                        const char *validator_path);
