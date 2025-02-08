// SPDX-FileCopyrightText: 2016 Advanced Micro Devices, Inc.
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

/* Job queue with execution in a separate thread.
 * Copied from mesa u_queue.c.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "util/queue.h"

struct queue_job {
    void *job;
    void *global_data;
    struct queue_fence *fence;
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

struct thread_input {
    struct queue *queue;
    unsigned thread_index;
};

void queue_fence_init(struct queue_fence *fence)
{
    memset(fence, 0, sizeof(*fence));
    pthread_mutex_init(&fence->mutex, NULL);
    pthread_cond_init(&fence->cond, NULL);
    fence->signalled = true;
}

void queue_fence_wait(struct queue_fence *fence)
{
    if (fence->signalled) {
        return;
    }

    pthread_mutex_lock(&fence->mutex);
    while (!fence->signalled) {
        pthread_cond_wait(&fence->cond, &fence->mutex);
    }
    pthread_mutex_unlock(&fence->mutex);
}

void queue_fence_finish(struct queue_fence *fence)
{
    assert(fence->signalled);
    pthread_mutex_lock(&fence->mutex);
    pthread_mutex_unlock(&fence->mutex);

    pthread_cond_destroy(&fence->cond);
    pthread_mutex_destroy(&fence->mutex);
}

static void queue_fence_signal(struct queue_fence *fence)
{
    pthread_mutex_lock(&fence->mutex);
    fence->signalled = true;
    pthread_cond_broadcast(&fence->cond);
    pthread_mutex_unlock(&fence->mutex);
}

static void *queue_thread_func(void *input)
{
    struct queue *queue = ((struct thread_input *)input)->queue;
    unsigned thread_index = ((struct thread_input *)input)->thread_index;
    free(input);

    while (1) {
        struct queue_job job;

        pthread_mutex_lock(&queue->lock);
        assert(queue->num_queued >= 0 && queue->num_queued <= queue->max_jobs);

        /* wait if the queue is empty */
        while (thread_index < queue->num_threads && queue->num_queued == 0) {
            pthread_cond_wait(&queue->has_queued_cond, &queue->lock);
        }

        /* only kill threads that are above "num_threads" */
        if (thread_index >= queue->num_threads) {
            pthread_mutex_unlock(&queue->lock);
            break;
        }

        job = queue->jobs[queue->read_idx];
        memset(&queue->jobs[queue->read_idx], 0, sizeof(struct queue_job));
        queue->read_idx = (queue->read_idx + 1) % queue->max_jobs;

        queue->num_queued--;
        pthread_cond_signal(&queue->has_space_cond);
        pthread_mutex_unlock(&queue->lock);

        if (job.job) {
            job.execute(job.job, job.global_data, thread_index);
            if (job.fence) {
                queue_fence_signal(job.fence);
            }
            if (job.cleanup) {
                job.cleanup(job.job, job.global_data, thread_index);
            }
        }
    }

    pthread_mutex_lock(&queue->lock);
    if (queue->num_threads == 0) {
        for (int i = queue->read_idx; i != queue->write_idx; i = (i + 1) % queue->max_jobs) {
            if (queue->jobs[i].job) {
                if (queue->jobs[i].fence) {
                    queue_fence_signal(queue->jobs[i].fence);
                }
                queue->jobs[i].job = NULL;
            }
        }
        queue->read_idx = queue->write_idx;
        queue->num_queued = 0;
    }
    pthread_mutex_unlock(&queue->lock);

    return NULL;
}

static bool queue_create_thread(struct queue *queue, unsigned index)
{
    struct thread_input *input = malloc(sizeof(struct thread_input));
    input->queue = queue;
    input->thread_index = index;

    if (pthread_create(queue->threads + index, NULL, queue_thread_func, input) != 0) {
        free(input);
        return false;
    }

    return true;
}

struct queue *queue_create(unsigned max_jobs, unsigned num_threads, void *global_data)
{
    struct queue *queue = calloc(1, sizeof(*queue));
    if (!queue) {
        return NULL;
    }

    queue->max_jobs = max_jobs;
    queue->global_data = global_data;
    queue->num_threads = num_threads;
    queue->num_queued = 0;

    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->has_queued_cond, NULL);
    pthread_cond_init(&queue->has_space_cond, NULL);

    queue->jobs = calloc(max_jobs, sizeof(struct queue_job));
    if (!queue->jobs) {
        goto fail;
    }

    queue->threads = calloc(queue->num_threads, sizeof(pthread_t));
    if (!queue->threads) {
        goto fail;
    }

    /* start threads */
    for (unsigned i = 0; i < queue->num_threads; i++) {
        if (!queue_create_thread(queue, i)) {
            if (i == 0) {
                /* no threads created, fail */
                goto fail;
            } else {
                /* at least one thread created, so use it */
                queue->num_threads = i;
                break;
            }
        }
    }

    return queue;

fail:
    free(queue->threads);

    if (queue->jobs) {
        pthread_cond_destroy(&queue->has_space_cond);
        pthread_cond_destroy(&queue->has_queued_cond);
        pthread_mutex_destroy(&queue->lock);
        free(queue->jobs);
    }
    free(queue);
    return NULL;
}

static void queue_kill_threads(struct queue *queue)
{
    /* Signal all threads to terminate. */
    pthread_mutex_lock(&queue->lock);
    unsigned old_num_threads = queue->num_threads;
    /* Setting num_threads is what causes the threads to terminate.
     * Then cnd_broadcast wakes them up and they will exit their function.
     */
    queue->num_threads = 0;
    pthread_cond_broadcast(&queue->has_queued_cond);
    pthread_mutex_unlock(&queue->lock);

    for (unsigned i = 0; i < old_num_threads; i++) {
        pthread_join(queue->threads[i], NULL);
    }
}

void queue_destroy(struct queue *queue)
{
    if (!queue || !queue->threads) {
        return;
    }

    queue_kill_threads(queue);

    pthread_cond_destroy(&queue->has_space_cond);
    pthread_cond_destroy(&queue->has_queued_cond);
    pthread_mutex_destroy(&queue->lock);
    free(queue->jobs);
    free(queue->threads);
}

bool queue_add_job(struct queue *queue, void *job, struct queue_fence *fence,
                   queue_execute_func execute, queue_execute_func cleanup)
{
    if (!queue || !queue->threads) {
        return false;
    }

    pthread_mutex_lock(&queue->lock);
    if (queue->num_threads == 0) {
        pthread_mutex_unlock(&queue->lock);
        return false;
    }

    if (fence) {
        queue_fence_reset(fence);
    }
    assert(queue->num_queued >= 0 && queue->num_queued <= queue->max_jobs);

    if (queue->num_queued == queue->max_jobs) {
        unsigned new_max_jobs = queue->max_jobs + 8;
        struct queue_job *jobs = calloc(new_max_jobs, sizeof(struct queue_job));
        assert(jobs);

        /* Copy all queued jobs into the new list. */
        int num_jobs = 0;
        int i = queue->read_idx;

        do {
            jobs[num_jobs++] = queue->jobs[i];
            i = (i + 1) % queue->max_jobs;
        } while (i != queue->write_idx);

        assert(num_jobs == queue->num_queued);

        free(queue->jobs);
        queue->jobs = jobs;
        queue->read_idx = 0;
        queue->write_idx = num_jobs;
        queue->max_jobs = new_max_jobs;
    }

    struct queue_job *ptr = &queue->jobs[queue->write_idx];
    assert(ptr->job == NULL);
    ptr->job = job;
    ptr->global_data = queue->global_data;
    ptr->fence = fence;
    ptr->execute = execute;
    ptr->cleanup = cleanup;

    queue->write_idx = (queue->write_idx + 1) % queue->max_jobs;
    queue->num_queued++;
    pthread_cond_signal(&queue->has_queued_cond);
    pthread_mutex_unlock(&queue->lock);

    return true;
}

void queue_drop_job(struct queue *queue, struct queue_fence *fence)
{
    if (!queue || !queue->threads) {
        return;
    }

    if (queue_fence_is_signalled(fence)) {
        return;
    }

    bool removed = false;

    pthread_mutex_lock(&queue->lock);
    for (int i = queue->read_idx; i != queue->write_idx; i = (i + 1) % queue->max_jobs) {
        if (queue->jobs[i].fence == fence) {
            if (queue->jobs[i].cleanup) {
                queue->jobs[i].cleanup(queue->jobs[i].job, queue->global_data, -1);
            }
            /* Just clear it. The threads will treat as a no-op job. */
            memset(&queue->jobs[i], 0, sizeof(queue->jobs[i]));
            removed = true;
            break;
        }
    }
    pthread_mutex_unlock(&queue->lock);

    if (removed) {
        queue_fence_signal(fence);
    } else {
        queue_fence_wait(fence);
    }
}
