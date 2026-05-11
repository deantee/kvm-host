/* Judge resource monitoring - cgroup v2 integration */

#include "../include/judge_resource.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int judge_resource_create_cgroup(const char *cgroup_path, uint64_t memory_limit)
{
    char path[512];

    /* Create directory */
    if (mkdir(cgroup_path, 0755) < 0 && access(cgroup_path, F_OK) < 0) {
        perror("mkdir cgroup");
        return -1;
    }

    /* Set memory.max */
    snprintf(path, sizeof(path), "%s/memory.max", cgroup_path);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("open memory.max");
        return -1;
    }
    dprintf(fd, "%lu\n", memory_limit);
    close(fd);

    return 0;
}

int judge_resource_read_memory_peak(const char *cgroup_path,
                                    uint64_t *peak_bytes)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/memory.peak", cgroup_path);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open memory.peak");
        return -1;
    }

    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n < 0) {
        perror("read memory.peak");
        return -1;
    }

    buf[n] = '\0';
    *peak_bytes = strtoull(buf, NULL, 10);
    return 0;
}

int judge_resource_read_cpu_time(const char *cgroup_path,
                                 double *user_sec,
                                 double *system_sec)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/cpu.stat", cgroup_path);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open cpu.stat");
        return -1;
    }

    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n < 0) {
        perror("read cpu.stat");
        return -1;
    }

    buf[n] = '\0';

    /* Parse cpu.stat format:
     * user_usec 123456
     * system_usec 789012
     */
    uint64_t user_usec = 0, system_usec = 0;
    sscanf(buf, "user_usec %lu\nsystem_usec %lu", &user_usec, &system_usec);

    *user_sec = user_usec / 1e6;
    *system_sec = system_usec / 1e6;

    return 0;
}

int judge_resource_check_oom(const char *cgroup_path)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/memory.events", cgroup_path);

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n < 0)
        return 0;

    buf[n] = '\0';

    /* Parse memory.events format:
     * low 0
     * high 0
     * max 5          <- Count of OOM kills
     * oom 2          <- Another counter
     */
    uint64_t max_count = 0, oom_count = 0;
    sscanf(buf, "low %*u\nhigh %*u\nmax %lu\noom %lu", &max_count, &oom_count);

    return (max_count > 0 || oom_count > 0) ? 1 : 0;
}
