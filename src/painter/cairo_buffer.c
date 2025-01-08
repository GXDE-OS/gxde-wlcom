// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>

#include "painter_p.h"

static const struct wlr_buffer_impl cairo_buffer_impl;

struct cairo_buffer *cairo_buffer_from_wlr_buffer(struct wlr_buffer *wlr_buffer)
{
    if (wlr_buffer->impl != &cairo_buffer_impl) {
        return NULL;
    }
    struct cairo_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    return buffer;
}

static void cairo_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
    struct cairo_buffer *buffer = cairo_buffer_from_wlr_buffer(wlr_buffer);
    if (buffer->own_data) {
        free(cairo_image_surface_get_data(buffer->surface));
    }
    cairo_destroy(buffer->cairo);
    cairo_surface_destroy(buffer->surface);
    free(buffer);
}

static bool cairo_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
                                               void **data, uint32_t *format, size_t *stride)
{
    struct cairo_buffer *buffer = cairo_buffer_from_wlr_buffer(wlr_buffer);
    *format = DRM_FORMAT_ARGB8888;
    *data = cairo_image_surface_get_data(buffer->surface);
    *stride = cairo_image_surface_get_stride(buffer->surface);
    return true;
}

static void cairo_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer)
{
    /* noop */
}

static const struct wlr_buffer_impl cairo_buffer_impl = {
    .destroy = cairo_buffer_destroy,
    .begin_data_ptr_access = cairo_buffer_begin_data_ptr_access,
    .end_data_ptr_access = cairo_buffer_end_data_ptr_access,
};

struct cairo_buffer *cairo_buffer_create(uint32_t width, uint32_t height, float scale)
{
    struct cairo_buffer *buffer = calloc(1, sizeof(struct cairo_buffer));
    if (!buffer) {
        return NULL;
    }

    int scaled_width = ceil(width * scale);
    int scaled_height = ceil(height * scale);
    wlr_buffer_init(&buffer->base, &cairo_buffer_impl, scaled_width, scaled_height);

    buffer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scaled_width, scaled_height);
    if (cairo_surface_status(buffer->surface) != CAIRO_STATUS_SUCCESS) {
        free(buffer);
        return NULL;
    }
    cairo_surface_set_device_scale(buffer->surface, scale, scale);

    buffer->cairo = cairo_create(buffer->surface);

    buffer->dst_width = width;
    buffer->dst_height = height;

    return buffer;
}

struct cairo_buffer *cairo_buffer_create_from_pixel(uint32_t width, uint32_t height,
                                                    uint32_t src_width, uint32_t src_height,
                                                    unsigned char *src_data)
{
    struct cairo_buffer *buffer = calloc(1, sizeof(struct cairo_buffer));
    if (!buffer) {
        return NULL;
    }

    uint32_t src_stride = src_width * sizeof(uint32_t);
    buffer->surface = cairo_image_surface_create_for_data(src_data, CAIRO_FORMAT_ARGB32, src_width,
                                                          src_height, src_stride);
    if (cairo_surface_status(buffer->surface) != CAIRO_STATUS_SUCCESS) {
        free(buffer);
        return NULL;
    }

    wlr_buffer_init(&buffer->base, &cairo_buffer_impl, src_width, src_height);

    buffer->cairo = cairo_create(buffer->surface);

    buffer->dst_width = width ? width : src_width;
    buffer->dst_height = height ? height : src_height;

    return buffer;
}

struct cairo_buffer *cairo_buffer_create_from_jpeg(uint32_t width, uint32_t height,
                                                   const char *jpeg_path)
{
    uint32_t src_width, src_height;
    uint8_t *src_data = decode_jpeg(jpeg_path, &src_width, &src_height);
    if (!src_data) {
        return NULL;
    }

    struct cairo_buffer *buffer =
        cairo_buffer_create_from_pixel(width, height, src_width, src_height, src_data);
    if (buffer) {
        buffer->own_data = true;
    } else {
        free(src_data);
    }

    return buffer;
}

struct cairo_buffer *cairo_buffer_create_from_png(uint32_t width, uint32_t height,
                                                  const char *png_path)
{
    uint32_t src_width, src_height;
    uint8_t *src_data = decode_png(png_path, &src_width, &src_height);
    if (!src_data) {
        return NULL;
    }

    struct cairo_buffer *buffer =
        cairo_buffer_create_from_pixel(width, height, src_width, src_height, src_data);
    if (buffer) {
        buffer->own_data = true;
    } else {
        free(src_data);
    }

    return buffer;
}
