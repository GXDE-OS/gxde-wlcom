// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_STRING_H_
#define _UTIL_STRING_H_

#include <stdarg.h>
#include <stdio.h>

#ifdef __GNUC__
#define _KYWC_ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _KYWC_ATTRIB_PRINTF(start, end)
#endif

char *string_create(const char *format, ...) _KYWC_ATTRIB_PRINTF(1, 2);

char *string_create_vargs(const char *fmt, va_list args);

char *string_skip_space(char *str);

void string_strip_space(char *str);

void string_replace_unprintable(char *str);

char **string_split(const char *str, const char *delim, size_t *len);

void string_free_split(char **split);

char *string_expand_path(const char *path);

/* both subdir and file can be "" or NULL */
char *string_join_path(const char *dir, const char *subdir, const char *file);

void string_strip_path(char *str);

#endif /* _UTIL_STRING_H_ */
