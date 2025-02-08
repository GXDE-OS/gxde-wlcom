// SPDX-FileCopyrightText: 2016 Advanced Micro Devices, Inc.
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_QUEUE_H_
#define _UTIL_QUEUE_H_

#include <pthread.h>
#include <stdbool.h>

struct queue;

typedef void (*queue_execute_func)(void *job, void *gdata, int thread_index);

struct queue_fence {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signalled;
};

static inline void queue_fence_reset(struct queue_fence *fence)
{
    fence->signalled = false;
}

void queue_fence_init(struct queue_fence *fence);

void queue_fence_wait(struct queue_fence *fence);

void queue_fence_finish(struct queue_fence *fence);

struct queue *queue_create(unsigned max_jobs, unsigned num_threads, void *global_data);

void queue_destroy(struct queue *queue);

bool queue_add_job(struct queue *queue, void *job, struct queue_fence *fence,
                   queue_execute_func execute, queue_execute_func cleanup);

#endif /* _UTIL_QUEUE_H_ */
