// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _KYWC_BOXES_H_
#define _KYWC_BOXES_H_

// clang-format off

#include <stdbool.h>

#define KYWC_API static __attribute__((unused)) inline

/* rect with top-left and size */
struct kywc_box {
    int x, y, width, height;
};

struct kywc_fbox {
    double x, y, width, height;
};

KYWC_API void kywc_box_adjust(struct kywc_box *box, int dx, int dy, int dw, int dh)
{
    box->x += dx; box->y += dy; box->width += dw; box->height += dh;
}

KYWC_API void kywc_box_adjust_ex(struct kywc_box *box, int dx1, int dy1, int dx2, int dy2)
{
    box->x += dx1; box->y += dy1; box->width += dx1 + dx2; box->height += dy1 + dy2;
}

KYWC_API struct kywc_box kywc_box_adjusted(const struct kywc_box *box, int dx, int dy, int dw, int dh)
{
    return (struct kywc_box){ box->x + dx, box->y + dy, box->width + dw, box->height + dh };
}

KYWC_API struct kywc_box kywc_box_adjusted_ex(const struct kywc_box *box, int dx1, int dy1, int dx2, int dy2)
{
    return (struct kywc_box){ box->x + dx1, box->y + dy1, box->width + dx1 + dx2, box->height + dy1 + dy2 };
}

KYWC_API bool kywc_box_contains_point(const struct kywc_box *box, int x, int y)
{
    return x >= box->x && x < box->x + box->width && y >= box->y && y < box->y + box->height;
}

KYWC_API bool kywc_box_equal(const struct kywc_box *a, const struct kywc_box *b)
{
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

// clang-format on

#endif /* _KYWC_BOXES_H_ */
