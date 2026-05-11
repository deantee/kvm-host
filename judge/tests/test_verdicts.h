#pragma once
#include "../include/judge_module.h"
#include <stdint.h>

typedef struct {
    const char *name;
    const char *source_code;
    uint32_t source_size;
    enum judge_language language;
    uint32_t timeout_seconds;
    uint32_t memory_limit_bytes;
    const char *input_data;
    const char *expected_output;
    enum judge_verdict expected_verdict;
} test_case_t;

/* Test case: Accepted (AC) - simple hello world */
extern const char test_ac_source[];
extern const uint32_t test_ac_source_size;

/* Test case: Wrong Answer (WA) - wrong output */
extern const char test_wa_source[];
extern const uint32_t test_wa_source_size;

/* Test case: Runtime Error (RE) - segfault/crash */
extern const char test_re_source[];
extern const uint32_t test_re_source_size;

/* Test case: Time Limit Exceeded (TLE) - infinite loop */
extern const char test_tle_source[];
extern const uint32_t test_tle_source_size;

/* Test case: Memory Limit Exceeded (MLE) - allocate too much */
extern const char test_mle_source[];
extern const uint32_t test_mle_source_size;

/* Test case: Compilation Error (CE) - syntax error */
extern const char test_ce_source[];
extern const uint32_t test_ce_source_size;

/* Test runner function */
int run_verdict_test(const test_case_t *test);
int run_all_verdict_tests(void);
