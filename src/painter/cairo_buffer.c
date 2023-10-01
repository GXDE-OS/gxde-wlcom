// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <libdrm/drm_fourcc.h>
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

    int scaled_width = width * scale;
    int scaled_height = height * scale;
    wlr_buffer_init(&buffer->base, &cairo_buffer_impl, scaled_width, scaled_height);

    buffer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scaled_width, scaled_height);
    if (cairo_surface_status(buffer->surface) != CAIRO_STATUS_SUCCESS) {
        free(buffer);
        return NULL;
    }
    cairo_surface_set_device_scale(buffer->surface, scale, scale);

    buffer->cairo = cairo_create(buffer->surface);

    buffer->width = width;
    buffer->height = height;

    return buffer;
}
