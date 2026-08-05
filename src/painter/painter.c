// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>

#include "painter_p.h"

static const struct wlr_buffer_impl painter_buffer_impl;

static struct painter_buffer *painter_buffer_from_wlr_buffer(struct wlr_buffer *wlr_buffer)
{
    if (wlr_buffer->impl != &painter_buffer_impl) {
        return NULL;
    }
    struct painter_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    return buffer;
}

static void painter_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
    struct painter_buffer *buffer = painter_buffer_from_wlr_buffer(wlr_buffer);
    if (buffer->own_data) {
        free(buffer->data);
    }
    free(buffer);
}

static bool painter_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
                                                 void **data, uint32_t *format, size_t *stride)
{
    struct painter_buffer *buffer = painter_buffer_from_wlr_buffer(wlr_buffer);
    *format = DRM_FORMAT_ARGB8888;
    *data = buffer->data;
    *stride = buffer->stride;
    return true;
}

static void painter_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer)
{
    /* noop */
}

static const struct wlr_buffer_impl painter_buffer_impl = {
    .destroy = painter_buffer_destroy,
    .begin_data_ptr_access = painter_buffer_begin_data_ptr_access,
    .end_data_ptr_access = painter_buffer_end_data_ptr_access,
};

static struct painter_buffer *painter_buffer_create(int width, int height, int dst_width,
                                                    int dst_height, float scale, void *data)
{
    struct painter_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        return NULL;
    }

    buffer->stride = width * 4;
    buffer->own_data = data == NULL;
    buffer->data = data ? data : malloc(buffer->stride * height);
    if (!buffer->data) {
        free(buffer);
        return NULL;
    }

    wlr_buffer_init(&buffer->base, &painter_buffer_impl, width, height);
    buffer->scale = scale ? scale : 1.0;
    buffer->dst_width = dst_width ? dst_width : width;
    buffer->dst_height = dst_height ? dst_height : height;

    return buffer;
}

struct wlr_buffer *painter_create_buffer(int width, int height, float scale)
{
    int buffer_width = ceil(width * scale);
    int buffer_height = ceil(height * scale);

    struct painter_buffer *buffer =
        painter_buffer_create(buffer_width, buffer_height, width, height, scale, NULL);

    return buffer ? &buffer->base : NULL;
}

static void painter_get_buffer_size(struct draw_info *info, int *buffer_width, int *buffer_height)
{
    if (info->pixel.data) {
        *buffer_width = info->pixel.width;
        *buffer_height = info->pixel.height;
        return;
    }

    /* auto resize to text size */
    if (info->auto_resize && info->text && *info->text) {
        int width, height;
        text_get_size(info->font, info->font_size, info->text, &width, &height);
        if (info->auto_resize == AUTO_RESIZE_EXTEND) {
            height += height / 2 + 4;
            width += height / 2;
            info->align = TEXT_ALIGN_CENTER;
        }
        if (width < info->width) {
            info->width = width;
        }
        if (height < info->height) {
            info->height = height;
        }
        info->text_attr &= ~(TEXT_ATTR_SUBMENU | TEXT_ATTR_CHECKED);
    }

    /* draw hover together */
    if (info->hover_rgba || info->hover_svg) {
        info->height *= 2;
    }

    *buffer_width = ceil(info->width * info->scale);
    *buffer_height = ceil(info->height * info->scale);
}

struct wlr_buffer *painter_draw_buffer(struct draw_info *info)
{
    int width = 0, height = 0;
    painter_get_buffer_size(info, &width, &height);

    uint8_t *data = info->pixel.data;
    if (!data && info->image) {
        data = image_read_from_file(info->image, &width, &height);
        if (!data) {
            return NULL;
        }
    }
    if (width == 0 || height == 0) {
        return NULL;
    }

    struct painter_buffer *buffer =
        painter_buffer_create(width, height, info->width, info->height, info->scale, data);
    if (!buffer) {
        return NULL;
    }

    buffer->own_data = !info->pixel.data;
    render_buffer(buffer, info);

    return &buffer->base;
}

bool painter_buffer_redraw(struct wlr_buffer *buffer, struct draw_info *info)
{
    struct painter_buffer *buf = painter_buffer_from_wlr_buffer(buffer);
    if (!buf) {
        return false;
    }

    /* fix size to buffer unscaled size */
    info->width = buf->dst_width;
    info->height = buf->dst_height;
    info->scale = buf->scale;

    return render_buffer(buf, info);
}

void painter_buffer_get_dest_size(struct wlr_buffer *buffer, int *width, int *height)
{
    struct painter_buffer *buf = painter_buffer_from_wlr_buffer(buffer);
    if (buf && width) {
        *width = buf->dst_width;
    }
    if (buf && height) {
        *height = buf->dst_height;
    }
}

void painter_buffer_write_to_file(struct wlr_buffer *buffer, const char *name)
{
    struct painter_buffer *buf = painter_buffer_from_wlr_buffer(buffer);
    if (!buf) {
        return;
    }

    image_write_to_file(buf, name);
}

bool painter_buffer_encode_png(struct wlr_buffer *buffer, char **data, size_t *size) {
    struct painter_buffer *buf = painter_buffer_from_wlr_buffer(buffer);
    if (!buf) {
        return false;
    }

    return image_write_to_memory(buf, data, size);
}

void painter_get_text_size(const char *text, const char *font, int font_size, int *width,
                           int *height)
{
    text_get_size(font, font_size, text, width, height);
}
