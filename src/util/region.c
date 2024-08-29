// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include "util/region.h"

void ky_region_expand(pixman_region32_t *dst, const pixman_region32_t *src, int distance)
{
    if (distance == 0) {
        pixman_region32_copy(dst, src);
        return;
    }

    int nrects;
    const pixman_box32_t *src_rects = pixman_region32_rectangles(src, &nrects);

    pixman_box32_t *dst_rects = malloc(nrects * sizeof(pixman_box32_t));
    if (!dst_rects) {
        return;
    }

    int j = 0;
    for (int i = 0; i < nrects; ++i) {
        dst_rects[j].x1 = src_rects[i].x1 - distance;
        dst_rects[j].x2 = src_rects[i].x2 + distance;
        dst_rects[j].y1 = src_rects[i].y1 - distance;
        dst_rects[j].y2 = src_rects[i].y2 + distance;
        /* skip empty rectangles */
        if ((dst_rects[j].x1 < dst_rects[j].x2) && (dst_rects[j].y1 < dst_rects[j].y2)) {
            j++;
        }
    }

    if (j == 0) {
        pixman_region32_clear(dst);
        free(dst_rects);
        return;
    }

    pixman_region32_fini(dst);
    pixman_region32_init_rects(dst, dst_rects, j);
    free(dst_rects);
}
