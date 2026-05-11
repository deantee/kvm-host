/* Judge verdict determination logic */

#include "../include/judge_verdict.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/judge_core.h"

/* Compute verdict from judge_result (host-side format with inline buffers) */
enum judge_verdict judge_compute_verdict_from_result(
    struct judge_result *result,
    const char *expected_output,
    uint32_t timeout_seconds,
    uint32_t memory_limit,
    judge_comparison_config_t *config)
{
    if (!result) {
        return VERDICT_RE;
    }

    /* 1. Check compilation - highest priority */
    if (result->compile_exit_code != 0) {
        return VERDICT_CE;
    }

    /* 2. Check memory limit - execution happened but used too much */
    if (result->peak_memory_bytes > 0 &&
        result->peak_memory_bytes > memory_limit) {
        return VERDICT_MLE;
    }

    /* 3. Check time limit - execution took too long */
    if (result->cpu_time_seconds > 0 &&
        result->cpu_time_seconds > timeout_seconds) {
        return VERDICT_TLE;
    }

    /* 4. Check runtime error - non-zero exit code */
    if (result->program_exit_code != 0) {
        return VERDICT_RE;
    }

    /* 5. Compare output - if expected_output provided */
    if (expected_output && result->program_stdout_len > 0) {
        if (judge_compare_outputs(
                expected_output, (const char *) result->program_stdout,
                config ? config->mode : COMPARE_BYTE_FOR_BYTE,
                config ? config->validator_script_path : NULL) == 0) {
            return VERDICT_AC;
        } else {
            return VERDICT_WA;
        }
    }

    /* No expected output to compare - assume AC if execution succeeded */
    if (result->program_exit_code == 0) {
        return VERDICT_AC;
    }

    return VERDICT_WA;
}

/* Legacy wrapper for judge_result_t (shared memory format) */
enum judge_verdict judge_compute_verdict(judge_result_t *result,
                                         const char *expected_output,
                                         uint32_t timeout_seconds,
                                         uint32_t memory_limit,
                                         judge_comparison_config_t *config)
{
    if (!result) {
        return VERDICT_RE;
    }

    /* 1. Check compilation */
    if (result->compile_exit_code != 0) {
        return VERDICT_CE;
    }

    /* 2. Check memory limit */
    if (result->peak_memory_bytes > 0 &&
        result->peak_memory_bytes > memory_limit) {
        return VERDICT_MLE;
    }

    /* 3. Check time limit */
    if (result->cpu_time_seconds > 0 &&
        result->cpu_time_seconds > timeout_seconds) {
        return VERDICT_TLE;
    }

    /* 4. Check execution */
    if (result->program_exit_code != 0) {
        return VERDICT_RE;
    }

    /* 5. Compare output if available */
    if (expected_output && result->program_stdout_size > 0) {
        char actual_output[10000];
        uint32_t copy_len = result->program_stdout_size;
        if (copy_len > sizeof(actual_output) - 1) {
            copy_len = sizeof(actual_output) - 1;
        }
        memcpy(actual_output, &result->payload[result->program_stdout_offset],
               copy_len);
        actual_output[copy_len] = '\0';

        if (judge_compare_outputs(
                expected_output, actual_output,
                config ? config->mode : COMPARE_BYTE_FOR_BYTE,
                config ? config->validator_script_path : NULL) == 0) {
            return VERDICT_AC;
        } else {
            return VERDICT_WA;
        }
    }

    return VERDICT_WA;
}

static int is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int judge_compare_outputs(const char *expected,
                          const char *actual,
                          enum judge_compare_mode mode,
                          const char *validator_path)
{
    if (!expected || !actual)
        return -1;

    if (mode == COMPARE_BYTE_FOR_BYTE) {
        return strcmp(expected, actual) == 0 ? 0 : -1;
    }

    if (mode == COMPARE_WHITESPACE_NORMALIZED) {
        /* Tokenize and compare */
        const char *e_ptr = expected;
        const char *a_ptr = actual;

        while (*e_ptr || *a_ptr) {
            /* Skip whitespace in expected */
            while (*e_ptr && is_whitespace(*e_ptr))
                e_ptr++;
            /* Skip whitespace in actual */
            while (*a_ptr && is_whitespace(*a_ptr))
                a_ptr++;

            /* Compare characters */
            if (*e_ptr != *a_ptr)
                return -1;
            if (*e_ptr == '\0')
                break;

            e_ptr++;
            a_ptr++;
        }
        return 0;
    }

    if (mode == COMPARE_CUSTOM_VALIDATOR) {
        if (!validator_path)
            return -1;
        return judge_run_validator(expected, actual, validator_path);
    }

    return -1;
}
