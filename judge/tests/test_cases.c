/* Judge verdict test cases - source code examples for each verdict type */

#include "test_verdicts.h"
#include <string.h>

/* AC: Accepted - Correct program that prints "Hello, World!" */
const char test_ac_source[] =
"#include <stdio.h>\n"
"int main() {\n"
"    printf(\"Hello, World!\\n\");\n"
"    return 0;\n"
"}\n";
const uint32_t test_ac_source_size = sizeof(test_ac_source) - 1;

/* WA: Wrong Answer - Prints wrong output */
const char test_wa_source[] =
"#include <stdio.h>\n"
"int main() {\n"
"    printf(\"Wrong output\\n\");\n"
"    return 0;\n"
"}\n";
const uint32_t test_wa_source_size = sizeof(test_wa_source) - 1;

/* RE: Runtime Error - Null pointer dereference */
const char test_re_source[] =
"#include <stdio.h>\n"
"int main() {\n"
"    int *ptr = NULL;\n"
"    *ptr = 42;  /* Segfault */\n"
"    return 0;\n"
"}\n";
const uint32_t test_re_source_size = sizeof(test_re_source) - 1;

/* TLE: Time Limit Exceeded - Infinite loop */
const char test_tle_source[] =
"#include <stdio.h>\n"
"int main() {\n"
"    while (1) {\n"
"        /* Infinite loop - will timeout */\n"
"    }\n"
"    return 0;\n"
"}\n";
const uint32_t test_tle_source_size = sizeof(test_tle_source) - 1;

/* MLE: Memory Limit Exceeded - Allocate massive array */
const char test_mle_source[] =
"#include <stdlib.h>\n"
"int main() {\n"
"    /* Allocate 1GB - will exceed memory limit */\n"
"    char *huge = malloc(1024 * 1024 * 1024);\n"
"    if (!huge) return 1;\n"
"    huge[0] = 'A';\n"  /* Force kernel to allocate pages */
"    return 0;\n"
"}\n";
const uint32_t test_mle_source_size = sizeof(test_mle_source) - 1;

/* CE: Compilation Error - Syntax error */
const char test_ce_source[] =
"#include <stdio.h>\n"
"int main() {\n"
"    printf(\"Missing semicolon\")\n"  /* Missing ; */
"    return 0;\n"
"}\n";
const uint32_t test_ce_source_size = sizeof(test_ce_source) - 1;

/* Test case definitions */
static const test_case_t verdict_tests[] = {
    {
        .name = "AC: Accepted - Hello World",
        .source_code = test_ac_source,
        .source_size = sizeof(test_ac_source) - 1,
        .language = LANG_C,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .input_data = NULL,
        .expected_output = "Hello, World!\n",
        .expected_verdict = VERDICT_AC,
    },
    {
        .name = "WA: Wrong Answer",
        .source_code = test_wa_source,
        .source_size = sizeof(test_wa_source) - 1,
        .language = LANG_C,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .input_data = NULL,
        .expected_output = "Hello, World!\n",
        .expected_verdict = VERDICT_WA,
    },
    {
        .name = "RE: Runtime Error - Segfault",
        .source_code = test_re_source,
        .source_size = sizeof(test_re_source) - 1,
        .language = LANG_C,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .input_data = NULL,
        .expected_output = NULL,
        .expected_verdict = VERDICT_RE,
    },
    {
        .name = "TLE: Time Limit Exceeded",
        .source_code = test_tle_source,
        .source_size = sizeof(test_tle_source) - 1,
        .language = LANG_C,
        .timeout_seconds = 2,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .input_data = NULL,
        .expected_output = NULL,
        .expected_verdict = VERDICT_TLE,
    },
    {
        .name = "MLE: Memory Limit Exceeded",
        .source_code = test_mle_source,
        .source_size = sizeof(test_mle_source) - 1,
        .language = LANG_C,
        .timeout_seconds = 5,
        .memory_limit_bytes = 64 * 1024 * 1024,
        .input_data = NULL,
        .expected_output = NULL,
        .expected_verdict = VERDICT_MLE,
    },
    {
        .name = "CE: Compilation Error",
        .source_code = test_ce_source,
        .source_size = sizeof(test_ce_source) - 1,
        .language = LANG_C,
        .timeout_seconds = 5,
        .memory_limit_bytes = 256 * 1024 * 1024,
        .input_data = NULL,
        .expected_output = NULL,
        .expected_verdict = VERDICT_CE,
    },
};

const test_case_t *get_verdict_test_cases(int *count) {
    if (count) *count = sizeof(verdict_tests) / sizeof(verdict_tests[0]);
    return verdict_tests;
}
