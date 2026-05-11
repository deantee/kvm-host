/* Judge comparison strategies - exact, whitespace-normalized, custom validator
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../include/judge_verdict.h"

int judge_compare_exact(const char *expected, const char *actual)
{
    return strcmp(expected, actual) == 0 ? 0 : -1;
}

static int is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int judge_compare_whitespace_normalized(const char *expected,
                                        const char *actual)
{
    if (!expected || !actual)
        return -1;

    const char *e_ptr = expected;
    const char *a_ptr = actual;

    while (*e_ptr || *a_ptr) {
        /* Skip leading whitespace in expected */
        while (*e_ptr && is_whitespace(*e_ptr))
            e_ptr++;
        /* Skip leading whitespace in actual */
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

int judge_run_validator(const char *expected,
                        const char *actual,
                        const char *validator_path)
{
    if (!validator_path)
        return -1;

    /* Write expected output to temp file */
    int expected_fd = open("/tmp/judge_expected.txt", O_WRONLY | O_CREAT, 0644);
    if (expected_fd < 0) {
        perror("open expected");
        return -1;
    }
    write(expected_fd, expected, strlen(expected));
    close(expected_fd);

    /* Write actual output to temp file */
    int actual_fd = open("/tmp/judge_actual.txt", O_WRONLY | O_CREAT, 0644);
    if (actual_fd < 0) {
        perror("open actual");
        return -1;
    }
    write(actual_fd, actual, strlen(actual));
    close(actual_fd);

    /* Invoke validator script */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* Child: execute validator */
        execl(validator_path, validator_path, "/tmp/judge_expected.txt",
              "/tmp/judge_actual.txt", NULL);
        perror("execl");
        exit(127);
    }

    /* Parent: wait for validator */
    int status;
    waitpid(pid, &status, 0);

    /* Clean up temp files */
    unlink("/tmp/judge_expected.txt");
    unlink("/tmp/judge_actual.txt");

    /* Check exit code */
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status) == 0 ? 0 : -1;
    }
    return -1;
}
