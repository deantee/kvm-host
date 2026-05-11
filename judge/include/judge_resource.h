#pragma once
#include <stdint.h>

typedef struct {
    uint64_t memory_max;
    uint64_t memory_peak;
    uint64_t cpu_max_quota;
    uint64_t cpu_max_period;
    double cpu_user_time;
    double cpu_system_time;
} judge_resource_stats_t;

int judge_resource_create_cgroup(const char *cgroup_path,
                                 uint64_t memory_limit);

int judge_resource_read_memory_peak(const char *cgroup_path,
                                    uint64_t *peak_bytes);

int judge_resource_read_cpu_time(const char *cgroup_path,
                                 double *user_sec,
                                 double *system_sec);

int judge_resource_check_oom(const char *cgroup_path);
