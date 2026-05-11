#pragma once
#include "judge_core.h"
#include "judge_verdict.h"
#include <stdint.h>

int judge_module_init(int enable_judge, uint64_t guest_memory_limit);
judge_t *judge_module_get(void);
int judge_module_poll_result(struct judge_result *res);
int judge_module_submit(struct judge_submission *sub);
void judge_module_cleanup(void);

int judge_module_execute_submission(struct judge_submission *sub,
                                    struct judge_result *res,
                                    uint32_t poll_timeout_ms);

/* High-level API: execute submission and compute verdict */
int judge_module_execute_with_verdict(
    struct judge_submission *sub,
    struct judge_result *res,
    uint32_t poll_timeout_ms,
    const char *expected_output,
    judge_comparison_config_t *compare_config);
