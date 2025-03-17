// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include "util/string.h"

char *string_create_vargs(const char *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    int len = vsnprintf(NULL, 0, format, args) + 1;
    char *str = malloc(len);
    if (str == NULL) {
        va_end(args_copy);
        return NULL;
    }

    vsnprintf(str, len, format, args_copy);
    va_end(args_copy);
    return str;
}

char *string_create(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *str = string_create_vargs(format, args);
    va_end(args);
    return str;
}

char *string_skip_space(char *str)
{
    while (isspace(*str)) {
        str++;
    }
    if (*str == '\0') {
        return str;
    }

    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        end--;
    }
    *(end + 1) = '\0';
    return str;
}

void string_strip_space(char *str)
{
    size_t len = strlen(str);
    size_t start = strspn(str, " \f\n\r\t\v");
    memmove(str, &str[start], len + 1 - start);

    if (*str) {
        for (len -= start + 1; isspace(str[len]); --len) {
        }
        str[len + 1] = '\0';
    }
}

void string_replace_unprintable(char *str)
{
    for (char *p = str; *p; ++p) {
        if (*p == ' ' || !isprint(*p)) {
            *p = '_';
        }
    }
}

char **string_split(const char *str, const char *delim, size_t *len)
{
    size_t alloc = 16, length = 0;
    char **split = malloc(sizeof(char *) * alloc);
    if (!split) {
        return NULL;
    }

    char *copy = strdup(str);
    char *save, *token = strtok_r(copy, delim, &save);

    while (token) {
        if (length >= alloc) {
            alloc *= 2;
            split = realloc(split, sizeof(char *) * alloc);
        }

        split[length++] = token;
        token = strtok_r(NULL, delim, &save);
    }

    if (len) {
        *len = length;
    }
    return split;
}

void string_free_split(char **split)
{
    if (!split) {
        return;
    }

    free(split[0]);
    free(split);
}

char *string_expand_path(const char *path)
{
    wordexp_t p = { 0 };
    if (wordexp(path, &p, WRDE_UNDEF) != 0 || !p.we_wordv[0]) {
        wordfree(&p);
        return NULL;
    }
    char *realpath = strdup(p.we_wordv[0]);
    wordfree(&p);
    return realpath;
}

char *string_join_path(const char *dir, const char *subdir, const char *file)
{
    size_t len = strlen(dir);
    size_t subdir_len = subdir ? strlen(subdir) : 0;
    size_t file_len = file ? strlen(file) : 0;

    len += subdir_len + file_len + 3;
    char *path = malloc(len);
    if (!path) {
        return NULL;
    }

    path[0] = '\0';
    strcat(path, dir);

    if (subdir_len > 0) {
        strcat(path, "/");
        strcat(path, subdir);
    }
    if (file_len > 0) {
        strcat(path, "/");
        strcat(path, file);
    }

    return path;
}

void string_strip_path(char *str)
{
    const char *start = strrchr(str, '/');
    start = start ? start + 1 : str;

    const char *end = strchr(start, ' ');
    size_t len = end ? (size_t)(end - start) : strlen(start);

    memmove(str, start, len);
    str[len] = '\0';
}
