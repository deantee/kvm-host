#pragma once
#include "judge_protocol.h"

int judge_executor_init_shm(void);
int judge_executor_write_request(judge_request_t *req);
judge_result_t *judge_executor_get_result(void);
void judge_executor_cleanup_shm(void);

int judge_executor_init_eventfd(void);
int judge_executor_signal_request_ready(void);
int judge_executor_wait_result_ready(void);
void judge_executor_cleanup_eventfd(void);
