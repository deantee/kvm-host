/* Judge API Unit Tests - Test without KVM/executor */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/judge_core.h"
#include "../include/judge_module.h"
#include "../include/judge_verdict.h"

/* Test verdict computation logic without KVM */

int test_verdict_ac(void)
{
    printf("[TEST] Verdict: AC (Accepted)\n");

    struct judge_result res = {
        .compile_exit_code = 0,
        .program_exit_code = 0,
        .cpu_time_seconds = 0.1,
        .peak_memory_bytes = 1024 * 1024,
    };
    strcpy(res.program_stdout, "correct output");
    res.program_stdout_len = 14;

    enum judge_verdict v = judge_compute_verdict_from_result(
        &res, "correct output", 5, 256 * 1024 * 1024, NULL);

    assert(v == VERDICT_AC);
    printf("  PASS\n");
    return 0;
}

int test_verdict_ce(void)
{
    printf("[TEST] Verdict: CE (Compilation Error)\n");

    struct judge_result res = {
        .compile_exit_code = 1,  // Compiler failed
        .program_exit_code = -1,
    };

    enum judge_verdict v = judge_compute_verdict_from_result(
        &res, "output", 5, 256 * 1024 * 1024, NULL);

    assert(v == VERDICT_CE);
    printf("  PASS\n");
    return 0;
}

int test_verdict_re(void)
{
    printf("[TEST] Verdict: RE (Runtime Error)\n");

    struct judge_result res = {
        .compile_exit_code = 0,
        .program_exit_code = 127,  // Non-zero exit
        .cpu_time_seconds = 0.1,
        .peak_memory_bytes = 1024 * 1024,
    };

    enum judge_verdict v = judge_compute_verdict_from_result(
        &res, "output", 5, 256 * 1024 * 1024, NULL);

    assert(v == VERDICT_RE);
    printf("  PASS\n");
    return 0;
}

int test_verdict_tle(void)
{
    printf("[TEST] Verdict: TLE (Time Limit Exceeded)\n");

    struct judge_result res = {
        .compile_exit_code = 0,
        .program_exit_code = -14,  // SIGALRM
        .cpu_time_seconds = 10.5,  // > 5s timeout
        .peak_memory_bytes = 1024 * 1024,
    };

    enum judge_verdict v = judge_compute_verdict_from_result(
        &res, "output", 5, 256 * 1024 * 1024, NULL);

    assert(v == VERDICT_TLE);
    printf("  PASS\n");
    return 0;
}

int test_verdict_mle(void)
{
    printf("[TEST] Verdict: MLE (Memory Limit Exceeded)\n");

    struct judge_result res = {
        .compile_exit_code = 0,
        .program_exit_code = -9,  // SIGKILL from cgroup
        .cpu_time_seconds = 0.5,
        .peak_memory_bytes = 512 * 1024 * 1024,  // > 256MB limit
    };

    enum judge_verdict v = judge_compute_verdict_from_result(
        &res, "output", 5, 256 * 1024 * 1024, NULL);

    assert(v == VERDICT_MLE);
    printf("  PASS\n");
    return 0;
}

int test_verdict_wa(void)
{
    printf("[TEST] Verdict: WA (Wrong Answer)\n");

    struct judge_result res = {
        .compile_exit_code = 0,
        .program_exit_code = 0,
        .cpu_time_seconds = 0.1,
        .peak_memory_bytes = 1024 * 1024,
    };
    strcpy(res.program_stdout, "wrong output");
    res.program_stdout_len = 12;

    enum judge_verdict v = judge_compute_verdict_from_result(
        &res, "correct output", 5, 256 * 1024 * 1024, NULL);

    assert(v == VERDICT_WA);
    printf("  PASS\n");
    return 0;
}

int test_output_comparison_byte_for_byte(void)
{
    printf("[TEST] Output comparison: Byte-for-byte\n");

    const char *expected = "hello world";
    const char *actual = "hello world";

    int result =
        judge_compare_outputs(expected, actual, COMPARE_BYTE_FOR_BYTE, NULL);

    assert(result == 0);  // Match
    printf("  PASS\n");
    return 0;
}

int test_output_comparison_mismatch(void)
{
    printf("[TEST] Output comparison: Mismatch\n");

    const char *expected = "hello world";
    const char *actual = "hello earth";

    int result =
        judge_compare_outputs(expected, actual, COMPARE_BYTE_FOR_BYTE, NULL);

    assert(result != 0);  // No match
    printf("  PASS\n");
    return 0;
}

int test_output_comparison_whitespace(void)
{
    printf("[TEST] Output comparison: Whitespace normalized\n");

    const char *expected = "hello\nworld\n";
    const char *actual = "hello world";

    int result = judge_compare_outputs(expected, actual,
                                       COMPARE_WHITESPACE_NORMALIZED, NULL);

    assert(result == 0);  // Match (whitespace normalized)
    printf("  PASS\n");
    return 0;
}

int test_submission_structure(void)
{
    printf("[TEST] Judge submission structure\n");

    struct judge_submission sub = {
        .submission_id = 100,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
    };

    const char *code = "#include <stdio.h>\nint main() { return 0; }\n";
    sub.source_code = malloc(strlen(code));
    memcpy(sub.source_code, code, strlen(code));
    sub.source_code_size = strlen(code);

    assert(sub.submission_id == 100);
    assert(sub.language == LANG_C);
    assert(sub.source_code_size == strlen(code));

    free(sub.source_code);
    printf("  PASS\n");
    return 0;
}

int test_result_structure(void)
{
    printf("[TEST] Judge result structure\n");

    struct judge_result res = {
        .submission_id = 100,
        .verdict = VERDICT_AC,
        .compile_exit_code = 0,
        .program_exit_code = 0,
        .cpu_time_seconds = 0.05,
        .peak_memory_bytes = 1024 * 1024,
    };

    assert(res.submission_id == 100);
    assert(res.verdict == VERDICT_AC);
    assert(res.compile_exit_code == 0);
    assert(res.peak_memory_bytes == 1024 * 1024);

    printf("  PASS\n");
    return 0;
}

int main(void)
{
    printf("=== Judge API Unit Tests ===\n\n");

    int pass = 0, fail = 0;

    /* Verdict tests */
    if (test_verdict_ac() == 0)
        pass++;
    else
        fail++;
    if (test_verdict_ce() == 0)
        pass++;
    else
        fail++;
    if (test_verdict_re() == 0)
        pass++;
    else
        fail++;
    if (test_verdict_tle() == 0)
        pass++;
    else
        fail++;
    if (test_verdict_mle() == 0)
        pass++;
    else
        fail++;
    if (test_verdict_wa() == 0)
        pass++;
    else
        fail++;

    printf("\n");

    /* Output comparison tests */
    if (test_output_comparison_byte_for_byte() == 0)
        pass++;
    else
        fail++;
    if (test_output_comparison_mismatch() == 0)
        pass++;
    else
        fail++;
    if (test_output_comparison_whitespace() == 0)
        pass++;
    else
        fail++;

    printf("\n");

    /* Structure tests */
    if (test_submission_structure() == 0)
        pass++;
    else
        fail++;
    if (test_result_structure() == 0)
        pass++;
    else
        fail++;

    printf("\n=== Results ===\n");
    printf("Passed: %d/11\n", pass);
    printf("Failed: %d/11\n", fail);

    if (fail == 0) {
        printf("\n✓ All API unit tests passed!\n");
    }

    return fail > 0 ? 1 : 0;
}
