// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_FILE_H_
#define _UTIL_FILE_H_

#include <stdbool.h>
#include <stdio.h>

bool file_exists(const char *path);

/* return home for fallback */
const char *file_get_xdg_pictures_dir(void);

/* delim used in strtok_r, needle used in strstr */
struct file *file_open(const char *name, const char *delim, const char *needle);

void file_close(struct file *file);

char *file_get_data(struct file *file, size_t *size);

const char *file_get_value(struct file *file, const char *key);

/* free is needed */
char *file_get_value_copy(struct file *file, const char *key);

/* return true to break the parse */
typedef bool (*file_parse_func_t)(struct file *file, const char *key, const char *value,
                                  void *data);

void file_parse(struct file *file, file_parse_func_t parse, void *data);

/* return true to break the iterator */
typedef bool (*file_iterator_func_t)(const char *fullpath, const char *subpath, const char *name,
                                     void *data);

void file_for_each(const char *dirs, const char *subdir, file_iterator_func_t iterator, void *data);

#endif /* _UTIL_FILE_H_ */
