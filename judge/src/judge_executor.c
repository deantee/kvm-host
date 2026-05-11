/* Judge executor interface - shared memory and eventfd synchronization */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../include/judge_protocol.h"

static int shm_request_fd = -1;
static int shm_result_fd = -1;
static judge_request_t *shm_request = NULL;
static judge_result_t *shm_result = NULL;

int judge_executor_init_shm(void)
{
    /* Create request shared memory */
    shm_request_fd = shm_open(JUDGE_SHM_REQUEST_PATH, O_CREAT | O_RDWR, 0666);
    if (shm_request_fd < 0) {
        perror("shm_open request");
        return -1;
    }

    /* Set size */
    if (ftruncate(shm_request_fd, sizeof(judge_request_t)) < 0) {
        perror("ftruncate request");
        return -1;
    }

    /* Map into memory */
    shm_request = mmap(NULL, sizeof(judge_request_t), PROT_READ | PROT_WRITE,
                       MAP_SHARED, shm_request_fd, 0);
    if (shm_request == MAP_FAILED) {
        perror("mmap request");
        return -1;
    }

    /* Initialize */
    memset(shm_request, 0, sizeof(judge_request_t));
    shm_request->protocol_version = JUDGE_PROTOCOL_VERSION;

    /* Repeat for result */
    shm_result_fd = shm_open(JUDGE_SHM_RESULT_PATH, O_CREAT | O_RDWR, 0666);
    if (shm_result_fd < 0) {
        perror("shm_open result");
        return -1;
    }

    if (ftruncate(shm_result_fd, sizeof(judge_result_t)) < 0) {
        perror("ftruncate result");
        return -1;
    }

    shm_result = mmap(NULL, sizeof(judge_result_t), PROT_READ | PROT_WRITE,
                      MAP_SHARED, shm_result_fd, 0);
    if (shm_result == MAP_FAILED) {
        perror("mmap result");
        return -1;
    }

    memset(shm_result, 0, sizeof(judge_result_t));
    shm_result->protocol_version = JUDGE_PROTOCOL_VERSION;

    return 0;
}

int judge_executor_write_request(judge_request_t *req)
{
    if (!req || !shm_request)
        return -1;
    memcpy(shm_request, req, sizeof(judge_request_t));
    return 0;
}

judge_result_t *judge_executor_get_result(void)
{
    return shm_result;
}

void judge_executor_cleanup_shm(void)
{
    if (shm_request) {
        munmap(shm_request, sizeof(judge_request_t));
        shm_unlink(JUDGE_SHM_REQUEST_PATH);
    }
    if (shm_result) {
        munmap(shm_result, sizeof(judge_result_t));
        shm_unlink(JUDGE_SHM_RESULT_PATH);
    }
    if (shm_request_fd >= 0)
        close(shm_request_fd);
    if (shm_result_fd >= 0)
        close(shm_result_fd);
}

static int eventfd_request = -1;
static int eventfd_result = -1;

int judge_executor_init_eventfd(void)
{
    /* Create request eventfd (non-semaphore mode) */
    eventfd_request = eventfd(0, EFD_CLOEXEC);
    if (eventfd_request < 0) {
        perror("eventfd request");
        return -1;
    }

    /* Create result eventfd */
    eventfd_result = eventfd(0, EFD_CLOEXEC);
    if (eventfd_result < 0) {
        perror("eventfd result");
        return -1;
    }

    return 0;
}

int judge_executor_signal_request_ready(void)
{
    uint64_t val = 1;
    if (write(eventfd_request, &val, sizeof(val)) < 0) {
        perror("write eventfd_request");
        return -1;
    }
    return 0;
}

int judge_executor_wait_result_ready(void)
{
    uint64_t val;
    if (read(eventfd_result, &val, sizeof(val)) < 0) {
        perror("read eventfd_result");
        return -1;
    }
    return 0;
}

void judge_executor_cleanup_eventfd(void)
{
    if (eventfd_request >= 0)
        close(eventfd_request);
    if (eventfd_result >= 0)
        close(eventfd_result);
}
