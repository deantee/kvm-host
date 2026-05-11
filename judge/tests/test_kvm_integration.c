/* KVM Integration Tests - End-to-end judge system verification */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../include/judge_module.h"

#define TIMEOUT_MS 60000 /* 60 second timeout per submission */

/* Test 1: Hello World - should get AC (Accepted) */
int test_hello_world(void)
{
    printf("[TEST 1] Hello World (AC expected)\n");

    struct judge_submission sub = {
        .submission_id = 1001,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
    };

    const char *source =
        "#include <stdio.h>\n"
        "int main() {\n"
        "    printf(\"Hello, World!\\n\");\n"
        "    return 0;\n"
        "}\n";

    const char *expected = "Hello, World!\n";

    sub.source_code = malloc(strlen(source));
    memcpy(sub.source_code, source, strlen(source));
    sub.source_code_size = strlen(source);

    sub.expected_output = malloc(strlen(expected));
    memcpy(sub.expected_output, expected, strlen(expected));
    sub.expected_output_size = strlen(expected);
    sub.compare_mode = COMPARE_BYTE_FOR_BYTE;

    struct judge_result res;
    judge_comparison_config_t cfg = {
        .mode = COMPARE_BYTE_FOR_BYTE,
        .validator_script_path = NULL,
    };

    if (judge_module_execute_with_verdict(&sub, &res, TIMEOUT_MS, expected,
                                          &cfg) < 0) {
        fprintf(stderr, "  FAIL: Execution error\n");
        free(sub.source_code);
        free(sub.expected_output);
        return -1;
    }

    if (res.verdict != VERDICT_AC) {
        fprintf(stderr, "  FAIL: Expected AC, got verdict %d\n", res.verdict);
        free(sub.source_code);
        free(sub.expected_output);
        return -1;
    }

    printf("  PASS (CPU: %.3fs, Mem: %lu MB)\n", res.cpu_time_seconds,
           res.peak_memory_bytes / 1024 / 1024);
    free(sub.source_code);
    free(sub.expected_output);
    return 0;
}

/* Test 2: Compilation Error - should get CE */
int test_compilation_error(void)
{
    printf("[TEST 2] Compilation Error (CE expected)\n");

    struct judge_submission sub = {
        .submission_id = 1002,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
    };

    const char *source =
        "#include <stdio.h>\n"
        "int main() {\n"
        "    printf(\"test\")  // Missing semicolon\n"
        "    return 0;\n"
        "}\n";

    sub.source_code = malloc(strlen(source));
    memcpy(sub.source_code, source, strlen(source));
    sub.source_code_size = strlen(source);

    struct judge_result res;
    judge_comparison_config_t cfg = {.mode = COMPARE_BYTE_FOR_BYTE};

    if (judge_module_execute_with_verdict(&sub, &res, TIMEOUT_MS, NULL, &cfg) <
        0) {
        fprintf(stderr, "  FAIL: Execution error\n");
        free(sub.source_code);
        return -1;
    }

    if (res.verdict != VERDICT_CE) {
        fprintf(stderr, "  FAIL: Expected CE, got verdict %d\n", res.verdict);
        free(sub.source_code);
        return -1;
    }

    printf("  PASS (Compile error detected)\n");
    free(sub.source_code);
    return 0;
}

/* Test 3: Runtime Error - non-zero exit code */
int test_runtime_error(void)
{
    printf("[TEST 3] Runtime Error (RE expected)\n");

    struct judge_submission sub = {
        .submission_id = 1003,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
    };

    const char *source =
        "#include <stdio.h>\n"
        "int main() {\n"
        "    printf(\"Before exit\\n\");\n"
        "    return 42;  // Non-zero exit\n"
        "}\n";

    sub.source_code = malloc(strlen(source));
    memcpy(sub.source_code, source, strlen(source));
    sub.source_code_size = strlen(source);

    struct judge_result res;
    judge_comparison_config_t cfg = {.mode = COMPARE_BYTE_FOR_BYTE};

    if (judge_module_execute_with_verdict(&sub, &res, TIMEOUT_MS, NULL, &cfg) <
        0) {
        fprintf(stderr, "  FAIL: Execution error\n");
        free(sub.source_code);
        return -1;
    }

    if (res.verdict != VERDICT_RE) {
        fprintf(stderr, "  FAIL: Expected RE, got verdict %d (exit=%d)\n",
                res.verdict, res.program_exit_code);
        free(sub.source_code);
        return -1;
    }

    printf("  PASS (Exit code: %d)\n", res.program_exit_code);
    free(sub.source_code);
    return 0;
}

/* Test 4: Wrong Answer - output mismatch */
int test_wrong_answer(void)
{
    printf("[TEST 4] Wrong Answer (WA expected)\n");

    struct judge_submission sub = {
        .submission_id = 1004,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
    };

    const char *source =
        "#include <stdio.h>\n"
        "int main() {\n"
        "    printf(\"Goodbye\\n\");\n"
        "    return 0;\n"
        "}\n";

    const char *expected = "Hello\\n";

    sub.source_code = malloc(strlen(source));
    memcpy(sub.source_code, source, strlen(source));
    sub.source_code_size = strlen(source);

    sub.expected_output = malloc(strlen(expected));
    memcpy(sub.expected_output, expected, strlen(expected));
    sub.expected_output_size = strlen(expected);

    struct judge_result res;
    judge_comparison_config_t cfg = {.mode = COMPARE_BYTE_FOR_BYTE};

    if (judge_module_execute_with_verdict(&sub, &res, TIMEOUT_MS, expected,
                                          &cfg) < 0) {
        fprintf(stderr, "  FAIL: Execution error\n");
        free(sub.source_code);
        free(sub.expected_output);
        return -1;
    }

    if (res.verdict != VERDICT_WA) {
        fprintf(stderr, "  FAIL: Expected WA, got verdict %d\n", res.verdict);
        free(sub.source_code);
        free(sub.expected_output);
        return -1;
    }

    printf("  PASS (Output mismatch detected)\n");
    free(sub.source_code);
    free(sub.expected_output);
    return 0;
}

/* Test 5: Time Limit Exceeded - infinite loop */
int test_time_limit_exceeded(void)
{
    printf("[TEST 5] Time Limit Exceeded (TLE expected)\n");

    struct judge_submission sub = {
        .submission_id = 1005,
        .timeout_seconds = 2, /* 2 second timeout */
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
    };

    const char *source =
        "#include <stdio.h>\n"
        "int main() {\n"
        "    while (1) {}  // Infinite loop\n"
        "    return 0;\n"
        "}\n";

    sub.source_code = malloc(strlen(source));
    memcpy(sub.source_code, source, strlen(source));
    sub.source_code_size = strlen(source);

    struct judge_result res;
    judge_comparison_config_t cfg = {.mode = COMPARE_BYTE_FOR_BYTE};

    time_t start = time(NULL);
    if (judge_module_execute_with_verdict(&sub, &res, TIMEOUT_MS, NULL, &cfg) <
        0) {
        fprintf(stderr, "  FAIL: Execution error\n");
        free(sub.source_code);
        return -1;
    }
    time_t elapsed = time(NULL) - start;

    if (res.verdict != VERDICT_TLE) {
        fprintf(stderr,
                "  FAIL: Expected TLE, got verdict %d (cpu_time=%.1fs)\n",
                res.verdict, res.cpu_time_seconds);
        free(sub.source_code);
        return -1;
    }

    printf("  PASS (Timeout after %.1fs)\n", res.cpu_time_seconds);
    free(sub.source_code);
    return 0;
}

/* Test 6: Memory Limit Exceeded - large allocation */
int test_memory_limit_exceeded(void)
{
    printf("[TEST 6] Memory Limit Exceeded (MLE expected)\n");

    struct judge_submission sub = {
        .submission_id = 1006,
        .timeout_seconds = 5,
        .memory_limit_bytes = 50 * 1024 * 1024, /* 50 MB limit */
        .language = LANG_C,
    };

    const char *source =
        "#include <stdlib.h>\n"
        "#include <stdio.h>\n"
        "int main() {\n"
        "    char *big = malloc(500 * 1024 * 1024);  // 500 MB\n"
        "    if (!big) {\n"
        "        printf(\"malloc failed\\n\");\n"
        "        return 1;\n"
        "    }\n"
        "    big[0] = 'x';\n"
        "    return 0;\n"
        "}\n";

    sub.source_code = malloc(strlen(source));
    memcpy(sub.source_code, source, strlen(source));
    sub.source_code_size = strlen(source);

    struct judge_result res;
    judge_comparison_config_t cfg = {.mode = COMPARE_BYTE_FOR_BYTE};

    if (judge_module_execute_with_verdict(&sub, &res, TIMEOUT_MS, NULL, &cfg) <
        0) {
        fprintf(stderr, "  FAIL: Execution error\n");
        free(sub.source_code);
        return -1;
    }

    if (res.verdict != VERDICT_MLE) {
        fprintf(stderr, "  FAIL: Expected MLE, got verdict %d (mem=%lu MB)\n",
                res.verdict, res.peak_memory_bytes / 1024 / 1024);
        free(sub.source_code);
        return -1;
    }

    printf("  PASS (OOM after %lu MB)\n", res.peak_memory_bytes / 1024 / 1024);
    free(sub.source_code);
    return 0;
}

int main(void)
{
    printf("=== KVM Integration Test Suite ===\n\n");
    printf("Requires: KVM running with judge enabled\n");
    printf("Usage: Run after 'sudo ./build/kvm-host ... --judge-enable'\n\n");

    if (judge_module_init(1, 512 * 1024 * 1024) < 0) {
        fprintf(stderr, "Failed to initialize judge module\n");
        fprintf(stderr, "Make sure KVM is running with --judge-enable\n");
        return 1;
    }

    int pass = 0, fail = 0;

    if (test_hello_world() == 0)
        pass++;
    else
        fail++;
    if (test_compilation_error() == 0)
        pass++;
    else
        fail++;
    if (test_runtime_error() == 0)
        pass++;
    else
        fail++;
    if (test_wrong_answer() == 0)
        pass++;
    else
        fail++;
    if (test_time_limit_exceeded() == 0)
        pass++;
    else
        fail++;
    if (test_memory_limit_exceeded() == 0)
        pass++;
    else
        fail++;

    judge_module_cleanup();

    printf("\n=== Results ===\n");
    printf("Passed: %d/6\n", pass);
    printf("Failed: %d/6\n", fail);

    if (fail == 0) {
        printf("\n✓ All KVM integration tests passed!\n");
    } else {
        printf("\n✗ Some tests failed\n");
    }

    return fail > 0 ? 1 : 0;
}
