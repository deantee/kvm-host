/* Judge test runner - execute and verify test cases */

#include "test_verdicts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern const test_case_t *get_verdict_test_cases(int *count);

static const char *verdict_to_string(enum judge_verdict v) {
    switch (v) {
    case VERDICT_AC: return "AC";
    case VERDICT_WA: return "WA";
    case VERDICT_RE: return "RE";
    case VERDICT_TLE: return "TLE";
    case VERDICT_MLE: return "MLE";
    case VERDICT_CE: return "CE";
    default: return "UNKNOWN";
    }
}

int run_verdict_test(const test_case_t *test) {
    if (!test) return -1;

    printf("\n[TEST] %s\n", test->name);
    printf("  Expected verdict: %s\n", verdict_to_string(test->expected_verdict));

    /* Create submission */
    struct judge_submission sub = {
        .submission_id = (uint32_t)(uintptr_t)test,
        .timeout_seconds = test->timeout_seconds,
        .memory_limit_bytes = test->memory_limit_bytes,
        .language = test->language,
        .source_code = (char *)test->source_code,
        .source_code_size = test->source_size,
    };

    /* Execute submission synchronously */
    struct judge_result res;
    int ret = judge_module_execute_submission(&sub, &res, 30000);
    if (ret < 0) {
        printf("  [FAIL] Submission execution failed\n");
        return -1;
    }

    printf("  Actual verdict: %s\n", verdict_to_string(res.verdict));

    /* Verify verdict */
    if (res.verdict == test->expected_verdict) {
        printf("  [PASS] Verdict matches\n");
        return 0;
    } else {
        printf("  [FAIL] Verdict mismatch (expected %s, got %s)\n",
               verdict_to_string(test->expected_verdict),
               verdict_to_string(res.verdict));
        return -1;
    }
}

int run_all_verdict_tests(void) {
    printf("\n=== Judge Verdict Test Suite ===\n");

    int test_count = 0;
    const test_case_t *tests = get_verdict_test_cases(&test_count);
    if (!tests || test_count <= 0) {
        printf("No test cases found\n");
        return -1;
    }

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < test_count; i++) {
        if (run_verdict_test(&tests[i]) == 0) {
            passed++;
        } else {
            failed++;
        }
    }

    printf("\n=== Test Summary ===\n");
    printf("Total: %d, Passed: %d, Failed: %d\n", test_count, passed, failed);

    return failed == 0 ? 0 : -1;
}
