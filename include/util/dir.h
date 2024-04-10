// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _UTIL_XDG_DIR_H_
#define _UTIL_XDG_DIR_H_

#include <stdbool.h>

bool dir_exists(const char *path);

const char *dir_get_xdg_config(void);

const char *dir_get_xdg_pictures(void);

#endif /* _UTIL_XDG_DIR_H_ */
