/* Guest-side executor daemon for submission compilation and execution */

#include "executor.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "judge_protocol.h"

#define WORK_DIR "/tmp/submission"

static judge_request_t *shm_request = NULL;
static judge_result_t *shm_result = NULL;
static int eventfd_request = -1;
static int eventfd_result = -1;

int executor_open_shared_mem(void)
{
    int retries = 100;
    int request_fd = -1;

    while (retries-- > 0) {
        request_fd = shm_open(JUDGE_SHM_REQUEST_PATH, O_RDWR, 0);
        if (request_fd >= 0)
            break;
        usleep(100000);
    }

    if (request_fd < 0) {
        perror("shm_open request (guest)");
        return -1;
    }

    shm_request = mmap(NULL, sizeof(judge_request_t), PROT_READ | PROT_WRITE,
                       MAP_SHARED, request_fd, 0);
    close(request_fd);

    if (shm_request == MAP_FAILED) {
        perror("mmap request (guest)");
        return -1;
    }

    int result_fd = shm_open(JUDGE_SHM_RESULT_PATH, O_RDWR, 0);
    if (result_fd < 0) {
        perror("shm_open result (guest)");
        return -1;
    }

    shm_result = mmap(NULL, sizeof(judge_result_t), PROT_READ | PROT_WRITE,
                      MAP_SHARED, result_fd, 0);
    close(result_fd);

    if (shm_result == MAP_FAILED) {
        perror("mmap result (guest)");
        return -1;
    }

    return 0;
}

int executor_get_request(judge_request_t *req)
{
    if (!req || !shm_request)
        return -1;
    memcpy(req, shm_request, sizeof(judge_request_t));
    return 0;
}

int executor_write_result(judge_result_t *res)
{
    if (!res || !shm_result)
        return -1;
    memcpy(shm_result, res, sizeof(judge_result_t));
    return 0;
}

void executor_close_shared_mem(void)
{
    if (shm_request && shm_request != MAP_FAILED) {
        munmap(shm_request, sizeof(judge_request_t));
    }
    if (shm_result && shm_result != MAP_FAILED) {
        munmap(shm_result, sizeof(judge_result_t));
    }
}

int executor_open_eventfd(void)
{
    eventfd_request = eventfd(0, EFD_CLOEXEC);
    if (eventfd_request < 0) {
        perror("eventfd request (guest)");
        return -1;
    }

    eventfd_result = eventfd(0, EFD_CLOEXEC);
    if (eventfd_result < 0) {
        perror("eventfd result (guest)");
        return -1;
    }

    return 0;
}

int executor_wait_request_ready(void)
{
    uint64_t val;
    if (read(eventfd_request, &val, sizeof(val)) < 0) {
        perror("read eventfd_request (guest)");
        return -1;
    }
    return 0;
}

int executor_signal_result_ready(void)
{
    uint64_t val = 1;
    if (write(eventfd_result, &val, sizeof(val)) < 0) {
        perror("write eventfd_result (guest)");
        return -1;
    }
    return 0;
}

void executor_close_eventfd(void)
{
    if (eventfd_request >= 0)
        close(eventfd_request);
    if (eventfd_result >= 0)
        close(eventfd_result);
}

int executor_write_source(const char *filename,
                          const uint8_t *source,
                          uint32_t size)
{
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen source");
        return -1;
    }

    if (fwrite(source, 1, size, f) != size) {
        perror("fwrite source");
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int executor_compile_c(const char *source_file,
                       const char *binary_file,
                       struct executor_result *res)
{
    char cmd[1024];
    FILE *f;
    int ch;

    snprintf(cmd, sizeof(cmd),
             "gcc -O2 -Wall -Wextra -std=c11 -static \"%s\" -o \"%s\" "
             "2>compile_err.txt",
             source_file, binary_file);

    int ret = system(cmd);
    if (WIFEXITED(ret)) {
        res->compile_exit_code = WEXITSTATUS(ret);
    } else {
        res->compile_exit_code = -1;
    }

    /* Capture compiler stderr for error messages */
    f = fopen("compile_err.txt", "r");
    if (f) {
        uint32_t err_size = 0;
        while ((ch = fgetc(f)) != EOF &&
               err_size < sizeof(res->compile_stderr) - 1) {
            res->compile_stderr[err_size++] = ch;
        }
        res->compile_stderr_len = err_size;
        fclose(f);
        unlink("compile_err.txt");
    }

    return 0;
}

int executor_compile_cxx(const char *source_file,
                         const char *binary_file,
                         struct executor_result *res)
{
    char cmd[1024];
    FILE *f;
    int ch;

    snprintf(cmd, sizeof(cmd),
             "g++ -O2 -Wall -Wextra -std=c++11 -static \"%s\" -o \"%s\" "
             "2>compile_err.txt",
             source_file, binary_file);

    int ret = system(cmd);
    if (WIFEXITED(ret)) {
        res->compile_exit_code = WEXITSTATUS(ret);
    } else {
        res->compile_exit_code = -1;
    }

    /* Capture compiler stderr for error messages */
    f = fopen("compile_err.txt", "r");
    if (f) {
        uint32_t err_size = 0;
        while ((ch = fgetc(f)) != EOF &&
               err_size < sizeof(res->compile_stderr) - 1) {
            res->compile_stderr[err_size++] = ch;
        }
        res->compile_stderr_len = err_size;
        fclose(f);
        unlink("compile_err.txt");
    }

    return 0;
}

int executor_execute_binary(const char *binary_file,
                            const char *input_data,
                            uint32_t input_size,
                            struct executor_result *res)
{
    FILE *stdout_pipe = NULL;
    FILE *stderr_pipe = NULL;
    char cmd[512];

    snprintf(cmd, sizeof(cmd), "\"%s\" >stdout.txt 2>stderr.txt", binary_file);

    int ret = system(cmd);
    if (WIFEXITED(ret)) {
        res->program_exit_code = WEXITSTATUS(ret);
    } else if (WIFSIGNALED(ret)) {
        res->program_exit_code = -WTERMSIG(ret);
    } else {
        res->program_exit_code = -1;
    }

    /* Capture program stdout */
    stdout_pipe = fopen("stdout.txt", "r");
    if (stdout_pipe) {
        uint32_t out_size = 0;
        int ch;
        while ((ch = fgetc(stdout_pipe)) != EOF &&
               out_size < EXECUTOR_MAX_OUTPUT) {
            res->program_stdout[out_size++] = ch;
        }
        res->program_stdout_len = out_size;
        fclose(stdout_pipe);
        unlink("stdout.txt");
    }

    /* Capture program stderr */
    stderr_pipe = fopen("stderr.txt", "r");
    if (stderr_pipe) {
        uint32_t err_size = 0;
        int ch;
        while ((ch = fgetc(stderr_pipe)) != EOF && err_size < 10000) {
            res->program_stderr[err_size++] = ch;
        }
        res->program_stderr_len = err_size;
        fclose(stderr_pipe);
        unlink("stderr.txt");
    }

    return 0;
}

int executor_setup_resource_limits(uint32_t memory_limit_bytes,
                                   uint32_t timeout_seconds)
{
    struct rlimit mem_limit, time_limit;

    mem_limit.rlim_cur = memory_limit_bytes;
    mem_limit.rlim_max = memory_limit_bytes;
    if (setrlimit(RLIMIT_AS, &mem_limit) < 0) {
        perror("setrlimit RLIMIT_AS");
        return -1;
    }

    time_limit.rlim_cur = timeout_seconds;
    time_limit.rlim_max = timeout_seconds;
    if (setrlimit(RLIMIT_CPU, &time_limit) < 0) {
        perror("setrlimit RLIMIT_CPU");
        return -1;
    }

    return 0;
}

int executor_create_submission_cgroup(uint32_t request_id,
                                      uint32_t memory_limit_bytes)
{
    char cgroup_path[256];
    char memory_max_path[512];
    char value[64];
    FILE *f;

    snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/submission_%u",
             request_id);

    if (mkdir(cgroup_path, 0755) < 0 && errno != EEXIST) {
        perror("mkdir cgroup");
        return -1;
    }

    snprintf(memory_max_path, sizeof(memory_max_path), "%s/memory.max",
             cgroup_path);

    f = fopen(memory_max_path, "w");
    if (!f) {
        perror("fopen memory.max");
        return -1;
    }

    snprintf(value, sizeof(value), "%u", memory_limit_bytes);
    fprintf(f, "%s", value);
    fclose(f);

    return 0;
}

int executor_add_self_to_cgroup(uint32_t request_id)
{
    char cgroup_procs_path[512];
    FILE *f;
    char pid_str[32];

    snprintf(cgroup_procs_path, sizeof(cgroup_procs_path),
             "/sys/fs/cgroup/submission_%u/cgroup.procs", request_id);

    f = fopen(cgroup_procs_path, "w");
    if (!f) {
        perror("fopen cgroup.procs");
        return -1;
    }

    snprintf(pid_str, sizeof(pid_str), "%d", getpid());
    fprintf(f, "%s", pid_str);
    fclose(f);

    return 0;
}

static pid_t child_pid = -1;

static void timeout_handler(int sig)
{
    if (child_pid > 0) {
        kill(child_pid, SIGKILL);
    }
}

int executor_read_cgroup_memory(uint32_t request_id, uint64_t *peak_memory)
{
    char memory_peak_path[512];
    FILE *f;
    char buf[64];

    snprintf(memory_peak_path, sizeof(memory_peak_path),
             "/sys/fs/cgroup/submission_%u/memory.peak", request_id);

    f = fopen(memory_peak_path, "r");
    if (!f) {
        perror("fopen memory.peak");
        return -1;
    }

    if (fgets(buf, sizeof(buf), f)) {
        *peak_memory = strtoull(buf, NULL, 10);
    }
    fclose(f);

    return 0;
}

int executor_read_cgroup_cpu(uint32_t request_id, uint64_t *cpu_time_usec)
{
    char cpu_stat_path[512];
    FILE *f;
    char buf[256];

    snprintf(cpu_stat_path, sizeof(cpu_stat_path),
             "/sys/fs/cgroup/submission_%u/cpu.stat", request_id);

    f = fopen(cpu_stat_path, "r");
    if (!f) {
        perror("fopen cpu.stat");
        return -1;
    }

    *cpu_time_usec = 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (sscanf(buf, "usage_usec %lu", cpu_time_usec) == 1) {
            break;
        }
    }
    fclose(f);

    return 0;
}

int executor_record_metrics(uint32_t request_id, struct executor_result *res)
{
    if (!res)
        return -1;

    if (executor_read_cgroup_memory(request_id, &res->peak_memory_bytes) < 0) {
        res->peak_memory_bytes = 0;
    }

    if (executor_read_cgroup_cpu(request_id,
                                 (uint64_t *) &res->cpu_time_seconds) < 0) {
        res->cpu_time_seconds = 0;
    } else {
        res->cpu_time_seconds /= 1000000.0;
    }

    return 0;
}

int executor_execute_with_timeout(const char *binary_file,
                                  const char *input_data,
                                  uint32_t input_size,
                                  uint32_t timeout_seconds,
                                  struct executor_result *res)
{
    FILE *stderr_pipe = NULL;
    char cmd[512];
    struct sigaction sa, old_sa;

    snprintf(cmd, sizeof(cmd), "\"%s\" 2>stderr.txt", binary_file);

    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        return -1;
    }

    if (child_pid == 0) {
        if (timeout_seconds > 0) {
            alarm(timeout_seconds);
        }

        execl("/bin/sh", "sh", "-c", cmd, NULL);
        perror("execl");
        exit(1);
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timeout_handler;
    sigaction(SIGALRM, &sa, &old_sa);

    int status;
    waitpid(child_pid, &status, 0);

    alarm(0);
    sigaction(SIGALRM, &old_sa, NULL);

    if (WIFEXITED(status)) {
        res->program_exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res->program_exit_code = -WTERMSIG(status);
    } else {
        res->program_exit_code = -1;
    }

    stderr_pipe = fopen("stderr.txt", "r");
    if (stderr_pipe) {
        uint32_t err_size = 0;
        int ch;
        while ((ch = fgetc(stderr_pipe)) != EOF && err_size < 10000) {
            res->program_stderr[err_size++] = ch;
        }
        res->program_stderr_len = err_size;
        fclose(stderr_pipe);
    }

    return 0;
}

int executor_init(void)
{
    mkdir(WORK_DIR, 0755);

    printf("[executor] Initialized\n");
    return 0;
}

static int executor_request_from_judge_request(
    const judge_request_t *judge_req,
    struct executor_request *exec_req)
{
    if (!judge_req || !exec_req)
        return -1;

    memset(exec_req, 0, sizeof(*exec_req));
    exec_req->request_id = judge_req->request_id;
    exec_req->timeout_seconds = judge_req->timeout_seconds;
    exec_req->memory_limit_bytes = judge_req->memory_limit_bytes;
    exec_req->language = judge_req->language;
    exec_req->source_code_size = judge_req->source_code_size;

    /* Extract source code from payload at source_code_offset */
    if (exec_req->source_code_size > 0) {
        uint32_t copy_len = exec_req->source_code_size;
        if (copy_len > EXECUTOR_MAX_SOURCE) {
            copy_len = EXECUTOR_MAX_SOURCE;
        }

        /* Bounds check: offset + size must be within payload */
        uint32_t max_offset = judge_req->source_code_offset + copy_len;
        if (max_offset > JUDGE_MAX_SOURCE ||
            judge_req->source_code_offset > max_offset) {
            fprintf(stderr,
                    "[executor] Invalid source_code_offset=%u size=%u\n",
                    judge_req->source_code_offset, copy_len);
            return -1;
        }

        memcpy(exec_req->source_code,
               judge_req->payload + judge_req->source_code_offset, copy_len);
        exec_req->source_code_size = copy_len;
    }

    return 0;
}

static int executor_result_to_judge_result(
    const struct executor_result *exec_res,
    judge_result_t *judge_res)
{
    if (!exec_res || !judge_res)
        return -1;

    memset(judge_res, 0, sizeof(*judge_res));
    judge_res->protocol_version = JUDGE_PROTOCOL_VERSION;
    judge_res->request_id = exec_res->request_id;
    judge_res->verdict = exec_res->verdict;
    judge_res->compile_exit_code = exec_res->compile_exit_code;
    judge_res->program_exit_code = exec_res->program_exit_code;
    judge_res->wall_time_seconds = exec_res->wall_time_seconds;
    judge_res->cpu_time_seconds = exec_res->cpu_time_seconds;
    judge_res->peak_memory_bytes = exec_res->peak_memory_bytes;

    /* Layout payload offsets:
     * 0:         compile_stdout (up to 10000 bytes)
     * 10000:     compile_stderr (up to 10000 bytes)
     * 20000:     program_stdout (up to EXECUTOR_MAX_OUTPUT bytes)
     * 20000+EXECUTOR_MAX_OUTPUT: program_stderr (up to 10000 bytes)
     */
    const uint32_t compile_stdout_offset = 0;
    const uint32_t compile_stderr_offset = 10000;
    const uint32_t program_stdout_offset = 20000;
    const uint32_t program_stderr_offset = 20000 + EXECUTOR_MAX_OUTPUT;
    const uint32_t max_payload_end = program_stderr_offset + 10000;

    if (max_payload_end > JUDGE_RESULT_BUF_SIZE) {
        snprintf(judge_res->judge_message, sizeof(judge_res->judge_message),
                 "Payload buffer too small");
        return -1;
    }

    /* Copy compile_stdout */
    uint32_t compile_stdout_len = exec_res->compile_stdout_len;
    if (compile_stdout_len > 0) {
        if (compile_stdout_len + compile_stdout_offset >
            JUDGE_RESULT_BUF_SIZE) {
            compile_stdout_len = JUDGE_RESULT_BUF_SIZE - compile_stdout_offset;
        }
        memcpy(judge_res->payload + compile_stdout_offset,
               exec_res->compile_stdout, compile_stdout_len);
        judge_res->compile_stdout_offset = compile_stdout_offset;
        judge_res->compile_stdout_size = compile_stdout_len;
    }

    /* Copy compile_stderr */
    uint32_t compile_stderr_len = exec_res->compile_stderr_len;
    if (compile_stderr_len > 0) {
        if (compile_stderr_len + compile_stderr_offset >
            JUDGE_RESULT_BUF_SIZE) {
            compile_stderr_len = JUDGE_RESULT_BUF_SIZE - compile_stderr_offset;
        }
        memcpy(judge_res->payload + compile_stderr_offset,
               exec_res->compile_stderr, compile_stderr_len);
        judge_res->compile_stderr_offset = compile_stderr_offset;
        judge_res->compile_stderr_size = compile_stderr_len;
    }

    /* Copy program_stdout */
    uint32_t program_stdout_len = exec_res->program_stdout_len;
    if (program_stdout_len > 0) {
        if (program_stdout_len + program_stdout_offset >
            JUDGE_RESULT_BUF_SIZE) {
            program_stdout_len = JUDGE_RESULT_BUF_SIZE - program_stdout_offset;
        }
        memcpy(judge_res->payload + program_stdout_offset,
               exec_res->program_stdout, program_stdout_len);
        judge_res->program_stdout_offset = program_stdout_offset;
        judge_res->program_stdout_size = program_stdout_len;
    }

    /* Copy program_stderr */
    uint32_t program_stderr_len = exec_res->program_stderr_len;
    if (program_stderr_len > 0) {
        if (program_stderr_len + program_stderr_offset >
            JUDGE_RESULT_BUF_SIZE) {
            program_stderr_len = JUDGE_RESULT_BUF_SIZE - program_stderr_offset;
        }
        memcpy(judge_res->payload + program_stderr_offset,
               exec_res->program_stderr, program_stderr_len);
        judge_res->program_stderr_offset = program_stderr_offset;
        judge_res->program_stderr_size = program_stderr_len;
    }

    return 0;
}

int executor_run_submission(struct executor_request *req,
                            struct executor_result *res)
{
    if (!req || !res)
        return -1;

    memset(res, 0, sizeof(*res));
    res->request_id = req->request_id;

    char submission_dir[256];
    char source_file[512];
    char binary_file[512];

    snprintf(submission_dir, sizeof(submission_dir), "%s/%u", WORK_DIR,
             req->request_id);
    mkdir(submission_dir, 0755);

    snprintf(source_file, sizeof(source_file), "%s/source.c", submission_dir);
    snprintf(binary_file, sizeof(binary_file), "%s/binary", submission_dir);

    chdir(submission_dir);

    if (executor_create_submission_cgroup(req->request_id,
                                          req->memory_limit_bytes) < 0) {
        res->verdict = EXEC_RE;
        return -1;
    }

    if (executor_add_self_to_cgroup(req->request_id) < 0) {
        res->verdict = EXEC_RE;
        return -1;
    }

    if (executor_write_source(source_file, req->source_code,
                              req->source_code_size) < 0) {
        res->verdict = EXEC_RE;
        return -1;
    }

    if (req->language == EXEC_LANG_C) {
        executor_compile_c(source_file, binary_file, res);
    } else if (req->language == EXEC_LANG_CXX) {
        executor_compile_cxx(source_file, binary_file, res);
    }

    if (res->compile_exit_code != 0) {
        res->verdict = EXEC_CE;
        return 0;
    }

    if (executor_setup_resource_limits(req->memory_limit_bytes,
                                       req->timeout_seconds) < 0) {
        res->verdict = EXEC_RE;
        return -1;
    }

    if (executor_execute_with_timeout(binary_file, NULL, 0,
                                      req->timeout_seconds, res) < 0) {
        res->verdict = EXEC_RE;
        return -1;
    }

    if (executor_record_metrics(req->request_id, res) < 0) {
        res->verdict = EXEC_RE;
        return -1;
    }

    if (res->program_exit_code != 0) {
        res->verdict = EXEC_RE;
    } else {
        res->verdict = EXEC_AC;
    }

    chdir(WORK_DIR);
    return 0;
}

void executor_cleanup(void)
{
    printf("[executor] Cleaned up\n");
}

int executor_cleanup_submission(uint32_t request_id)
{
    char submission_dir[256];
    char cmd[512];

    snprintf(submission_dir, sizeof(submission_dir), "%s/%u", WORK_DIR,
             request_id);

    snprintf(cmd, sizeof(cmd), "rm -rf %s 2>/dev/null", submission_dir);

    int ret = system(cmd);
    if (ret < 0) {
        perror("system rm");
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "rmdir /sys/fs/cgroup/submission_%u 2>/dev/null",
             request_id);
    system(cmd);

    return 0;
}

int main(int argc, char *argv[])
{
    if (executor_init() < 0) {
        fprintf(stderr, "[executor] Failed to initialize\n");
        return 1;
    }

    if (executor_open_shared_mem() < 0) {
        fprintf(stderr, "[executor] Failed to open shared memory\n");
        executor_cleanup();
        return 1;
    }

    if (executor_open_eventfd() < 0) {
        fprintf(stderr, "[executor] Failed to open eventfd\n");
        executor_close_shared_mem();
        executor_cleanup();
        return 1;
    }

    printf("[executor] Waiting for requests...\n");
    fflush(stdout);

    uint32_t last_request_id = 0;

    while (1) {
        /* Poll for new request (100ms interval) */
        usleep(100000);

        /* Get request from shared memory */
        judge_request_t judge_req;
        if (executor_get_request(&judge_req) < 0) {
            fprintf(stderr, "[executor] Failed to get request\n");
            continue;
        }

        /* Check if this is a new request */
        if (judge_req.request_id == 0 ||
            judge_req.request_id == last_request_id) {
            continue; /* No new request */
        }

        last_request_id = judge_req.request_id;

        /* Convert judge_request_t to executor_request */
        struct executor_request exec_req;
        if (executor_request_from_judge_request(&judge_req, &exec_req) < 0) {
            fprintf(stderr,
                    "[executor] Failed to convert request (req_id=%u)\n",
                    judge_req.request_id);
            continue;
        }

        printf("[executor] Processing request %u\n", judge_req.request_id);
        fflush(stdout);

        /* Run submission */
        struct executor_result exec_res;
        if (executor_run_submission(&exec_req, &exec_res) < 0) {
            fprintf(stderr, "[executor] Failed to run submission\n");
            exec_res.verdict = EXEC_RE;
        }

        /* Convert result and write to shared memory */
        judge_result_t judge_res;
        if (executor_result_to_judge_result(&exec_res, &judge_res) < 0) {
            fprintf(stderr, "[executor] Failed to convert result\n");
            memset(&judge_res, 0, sizeof(judge_res));
            judge_res.verdict = EXEC_RE;
        }

        if (executor_write_result(&judge_res) < 0) {
            fprintf(stderr, "[executor] Failed to write result\n");
        }

        printf("[executor] Wrote result %u (verdict=%u)\n",
               judge_res.request_id, judge_res.verdict);
        fflush(stdout);

        /* Cleanup submission */
        executor_cleanup_submission(exec_req.request_id);
    }

    executor_close_eventfd();
    executor_close_shared_mem();
    executor_cleanup();
    return 0;
}
