// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_REGION_H_
#define _UTIL_REGION_H_

#include <pixman.h>

/* if the distance is negative, the region will decrease */
void ky_region_expand(pixman_region32_t *dst, const pixman_region32_t *src, int distance);

#endif /* _UTIL_REGION_H_ */
