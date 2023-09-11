// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _FSCAN_H_
#define _FSCAN_H_

#include <stdio.h>

char *fscan_build_fullname(const char *dir, const char *subdir, const char *file);

void fscan_start(const char *scan_path, const char *subdir,
                 void (*load_callback)(FILE *, char *, void *), void *user_data);

#endif /* _FSCAN_H_ */
