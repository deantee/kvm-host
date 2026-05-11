/* Judge test suite main program */

#include "../include/judge_module.h"
#include "test_verdicts.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int run_all_verdict_tests(void);

int main(int argc, char *argv[]) {
    printf("Initializing Judge Framework for Testing...\n");

    /* Initialize judge module with 512MB limit */
    if (judge_module_init(1, 512 * 1024 * 1024) < 0) {
        fprintf(stderr, "Failed to initialize judge module\n");
        return 1;
    }

    printf("Judge Framework Initialized\n");
    printf("Running verdict tests...\n");

    /* Run all test cases */
    int ret = run_all_verdict_tests();

    /* Cleanup */
    judge_module_cleanup();

    if (ret == 0) {
        printf("\nAll tests passed!\n");
        return 0;
    } else {
        printf("\nSome tests failed!\n");
        return 1;
    }
}
