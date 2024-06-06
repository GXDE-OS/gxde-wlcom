// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_QUIRKS_H_
#define _UTIL_QUIRKS_H_

#include <stdint.h>

enum quirks_mask {
    QUIRKS_MASK_MASTER_FD = 1 << 0,
    QUIRKS_MASK_SOFTWARE_CURSOR = 1 << 1,
    QUIRKS_MASK_EXPLICIT_SYNC = 1 << 2,
    QUIRKS_MASK_NO_MODIFIFIERS = 1 << 3,
};

uint32_t quirks_by_backend(int drm_fd);

uint32_t quirks_by_renderer(int drm_fd, const char *vendor_name);

#endif /* _UTIL_QUIRKS_H */
