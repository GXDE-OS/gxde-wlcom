// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "painter_p.h"

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof(a)[0])
#endif

/*
 * functions 'blur_surface' from weston project:
 * https://gitlab.freedesktop.org/wayland/weston/raw/master/shared/cairo-util.c
 */
static bool blur_surface(cairo_surface_t *surface, int margin)
{
    int32_t width, height, stride, x, y, z, w;
    uint8_t *src, *dst;
    uint32_t *s, *d, a, p;
    int i, j, k, size, half;
    uint32_t kernel[71];
    double f;

    size = ARRAY_LENGTH(kernel);
    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);
    src = cairo_image_surface_get_data(surface);

    dst = malloc(height * stride);
    if (dst == NULL) {
        return false;
    }

    half = size / 2;
    a = 0;
    for (i = 0; i < size; i++) {
        f = (i - half);
        kernel[i] = exp(-f * f / size) * 10000;
        a += kernel[i];
    }

    for (i = 0; i < height; i++) {
        s = (uint32_t *)(src + i * stride);
        d = (uint32_t *)(dst + i * stride);
        for (j = 0; j < width; j++) {
            if (margin < j && j < width - margin) {
                d[j] = s[j];
                continue;
            }

            x = y = z = w = 0;
            for (k = 0; k < size; k++) {
                if (j - half + k < 0 || j - half + k >= width) {
                    continue;
                }
                p = s[j - half + k];

                x += (p >> 24) * kernel[k];
                y += ((p >> 16) & 0xff) * kernel[k];
                z += ((p >> 8) & 0xff) * kernel[k];
                w += (p & 0xff) * kernel[k];
            }
            d[j] = (x / a << 24) | (y / a << 16) | (z / a << 8) | w / a;
        }
    }

    for (i = 0; i < height; i++) {
        s = (uint32_t *)(dst + i * stride);
        d = (uint32_t *)(src + i * stride);
        for (j = 0; j < width; j++) {
            if (margin <= i && i < height - margin) {
                d[j] = s[j];
                continue;
            }

            x = y = z = w = 0;
            for (k = 0; k < size; k++) {
                if (i - half + k < 0 || i - half + k >= height) {
                    continue;
                }
                s = (uint32_t *)(dst + (i - half + k) * stride);
                p = s[j];

                x += (p >> 24) * kernel[k];
                y += ((p >> 16) & 0xff) * kernel[k];
                z += ((p >> 8) & 0xff) * kernel[k];
                w += (p & 0xff) * kernel[k];
            }
            d[j] = (x / a << 24) | (y / a << 16) | (z / a << 8) | w / a;
        }
    }

    free(dst);
    cairo_surface_mark_dirty(surface);

    return true;
}

bool cairo_buffer_draw_blur(struct cairo_buffer *buffer, int margin)
{
    cairo_surface_t *surface = buffer->surface;
    if (blur_surface(surface, margin)) {
        cairo_surface_flush(surface);
        return true;
    }
    return false;
}
