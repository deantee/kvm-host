/* Simple example: Using judge module to evaluate submissions */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/judge_module.h"

int main(void)
{
    printf("=== Simple Judge Example ===\n\n");

    /* Initialize judge framework */
    printf("[1] Initializing judge module...\n");
    if (judge_module_init(1, 512 * 1024 * 1024) < 0) {
        fprintf(stderr, "Failed to initialize judge\n");
        return 1;
    }
    printf("    OK\n\n");

    /* Create a simple submission */
    printf("[2] Creating submission...\n");
    struct judge_submission sub = {
        .submission_id = 1,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .language = LANG_C,
    };

    /* Source code: Hello World */
    const char *source =
        "#include <stdio.h>\n"
        "int main() {\n"
        "    printf(\"Hello, World!\\n\");\n"
        "    return 0;\n"
        "}\n";

    sub.source_code_size = strlen(source);
    sub.source_code = malloc(sub.source_code_size);
    memcpy(sub.source_code, source, sub.source_code_size);

    /* Expected output */
    const char *expected = "Hello, World!\n";
    sub.expected_output_size = strlen(expected);
    sub.expected_output = malloc(sub.expected_output_size);
    memcpy(sub.expected_output, expected, sub.expected_output_size);

    /* Comparison mode */
    sub.compare_mode = COMPARE_BYTE_FOR_BYTE;
    sub.validator_script_path = NULL;

    printf("    Code: %d bytes\n", sub.source_code_size);
    printf("    Expected: %d bytes\n", sub.expected_output_size);
    printf("    Timeout: %u seconds\n", sub.timeout_seconds);
    printf("    Memory limit: %u MB\n\n", sub.memory_limit_bytes / 1024 / 1024);

    /* Execute with verdict computation */
    printf("[3] Executing submission...\n");
    struct judge_result res;
    memset(&res, 0, sizeof(res));

    judge_comparison_config_t config = {
        .mode = COMPARE_BYTE_FOR_BYTE,
        .validator_script_path = NULL,
    };

    if (judge_module_execute_with_verdict(&sub, &res, 30000, expected,
                                          &config) < 0) {
        fprintf(stderr, "    FAILED to execute\n");
        judge_module_cleanup();
        free(sub.source_code);
        free(sub.expected_output);
        return 1;
    }
    printf("    OK\n\n");

    /* Display results */
    printf("[4] Results:\n");
    const char *verdict_str[] = {
        "AC (Accepted)",
        "WA (Wrong Answer)",
        "RE (Runtime Error)",
        "TLE (Time Limit Exceeded)",
        "MLE (Memory Limit Exceeded)",
        "CE (Compilation Error)",
    };

    if (res.verdict < 6) {
        printf("    Verdict: %s\n", verdict_str[res.verdict]);
    }
    printf("    Compile exit code: %d\n", res.compile_exit_code);
    printf("    Program exit code: %d\n", res.program_exit_code);
    printf("    CPU time: %.3f seconds\n", res.cpu_time_seconds);
    printf("    Peak memory: %lu MB\n", res.peak_memory_bytes / 1024 / 1024);
    printf("    Output: %d bytes\n", res.program_stdout_len);

    if (res.program_stdout_len > 0) {
        printf("    Program output:\n");
        printf("    ---\n");
        fwrite(res.program_stdout, 1, res.program_stdout_len, stdout);
        printf("    ---\n");
    }

    printf("\n[5] Cleanup...\n");
    judge_module_cleanup();
    free(sub.source_code);
    free(sub.expected_output);
    printf("    OK\n\n");

    printf("Example complete.\n");
    return res.verdict == VERDICT_AC ? 0 : 1;
}
