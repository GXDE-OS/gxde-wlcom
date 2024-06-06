// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_FSCAN_H_
#define _UTIL_FSCAN_H_

#include <stdio.h>

#include <bits/types/time_t.h>

char *fscan_build_fullname(const char *dir, const char *subdir, const char *file);

void fscan_start(const char *scan_path, const char *subdir,
                 void (*load_callback)(const char *, const char *, void *), void *user_data);

void fscan_file(const char *scan_path, const char *subdir, const char *file_name,
                void (*load_callback)(const char *, void *), void *user_data);

char *fscan_search_keyword(FILE *fp, const char *keyword);

time_t fscan_get_latest_mtime(const char *scan_path, const char *subdir);

#endif /* _UTIL_FSCAN_H_ */
