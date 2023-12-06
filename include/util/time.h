// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _UTIL_TIME_H_
#define _UTIL_TIME_H_

#include <stdint.h>
#include <time.h>

uint32_t current_time_msec(void);

int64_t timespec_diff_usec(const struct timespec *a, const struct timespec *b);

#endif /* _UTIL_TIME_H_ */
