// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include "util/time.h"

uint32_t current_time_msec(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int64_t timespec_diff_usec(const struct timespec *a, const struct timespec *b)
{
    struct timespec r;
    r.tv_sec = a->tv_sec - b->tv_sec;
    r.tv_nsec = a->tv_nsec - b->tv_nsec;
    if (r.tv_nsec < 0) {
        r.tv_sec--;
        r.tv_nsec += 1000000000;
    }
    return r.tv_sec * 1000000 + r.tv_nsec / 1000;
}
