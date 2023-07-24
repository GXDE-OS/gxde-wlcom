/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * functions 'blur_surface' from weston project:
 * https://gitlab.freedesktop.org/wayland/weston/raw/master/shared/cairo-util.c
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "painter_p.h"

/**
 * Compile-time computation of number of items in a hardcoded array.
 *
 * @param a the array being measured.
 * @return the number of items hardcoded into the array.
 */
#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof(a)[0])
#endif

static bool point_in_circle(int width, int height, int x, int y, double radius)
{
    double cx = (double)width / 2;
    double cy = (double)height / 2;
    double dis = sqrt(pow((x - cx), 2) + pow(y - cy, 2));
    return dis <= radius;
}

static bool blur_surface(cairo_surface_t *surface, int margin, bool circle)
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
            if (circle ? point_in_circle(width, height, j, i, margin)
                       : margin < j && j < width - margin) {
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
            if (circle ? point_in_circle(width, height, j, i, margin)
                       : margin <= i && i < height - margin) {
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

bool cairo_buffer_draw_blur(struct cairo_buffer *buffer, int margin, bool circle)
{
    cairo_surface_t *surface = buffer->surface;
    if (blur_surface(surface, margin, circle)) {
        cairo_surface_flush(surface);
        return true;
    }
    return false;
}
