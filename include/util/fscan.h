// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _UTIL_FSCAN_H_
#define _UTIL_FSCAN_H_

#include <stdio.h>

char *fscan_build_fullname(const char *dir, const char *subdir, const char *file);

void fscan_start(const char *scan_path, const char *subdir,
                 void (*load_callback)(const char *, const char *, void *), void *user_data);

void fscan_file(const char *scan_path, const char *subdir, const char *file_name,
                void (*load_callback)(const char *, void *), void *user_data);

char *fscan_search_keyword(FILE *fp, const char *keyword);

#endif /* _UTIL_FSCAN_H_ */
