// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_SYSFS_H_
#define _UTIL_SYSFS_H_

#include <stdbool.h>
#include <stdint.h>

bool sysfs_read_uint64(const char *filename, uint64_t *val);

bool sysfs_write_uint64(const char *filename, uint64_t val);

#endif /* _UTIL_SYSFS_H_ */
