// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_SYSFS_H_
#define _UTIL_SYSFS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool sysfs_read_uint64(const char *filename, uint64_t *val);

bool sysfs_write_uint64(const char *filename, uint64_t val);

size_t sysfs_read_data(const char *filename, void *buf, size_t size);

#endif /* _UTIL_SYSFS_H_ */
