// SPDX-FileCopyrightText: 2016 Advanced Micro Devices, Inc.
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_QUEUE_H_
#define _UTIL_QUEUE_H_

#include <pthread.h>
#include <stdbool.h>

typedef void (*queue_execute_func)(void *job, void *gdata, int thread_index);

struct queue_job {
    void *job;
    void *global_data;
    queue_execute_func execute;
    queue_execute_func cleanup;
};

struct queue {
    pthread_mutex_t lock;
    pthread_cond_t has_queued_cond;
    pthread_cond_t has_space_cond;

    struct queue_job *jobs;
    int max_jobs;

    int num_queued;
    int write_idx, read_idx;

    pthread_t *threads;
    unsigned num_threads;

    void *global_data;
};

bool queue_init(struct queue *queue, unsigned max_jobs, unsigned num_threads, void *global_data);

void queue_destroy(struct queue *queue);

bool queue_add_job(struct queue *queue, void *job, queue_execute_func execute,
                   queue_execute_func cleanup);

#endif /* _UTIL_QUEUE_H_ */
