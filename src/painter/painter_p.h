// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <cairo/cairo.h>
#include <wlr/types/wlr_buffer.h>

#include <kywc/boxes.h>
#include <kywc/log.h>

#include "painter.h"

struct cairo_buffer {
    struct wlr_buffer base;

    cairo_t *cairo;
    cairo_surface_t *surface;

    /* unscaled size or dest size */
    uint32_t dst_width;
    uint32_t dst_height;

    bool own_data;
};

struct cairo_buffer *cairo_buffer_create(uint32_t width, uint32_t height, float scale);

struct cairo_buffer *cairo_buffer_create_from_png(uint32_t width, uint32_t height,
                                                  const char *png_path);

uint8_t *decode_png(const char *file, uint32_t *width, uint32_t *height);

struct cairo_buffer *cairo_buffer_create_from_pixel(uint32_t width, uint32_t height,
                                                    uint32_t src_width, uint32_t src_height,
                                                    unsigned char *src_data);

struct cairo_buffer *cairo_buffer_create_from_jpeg(uint32_t width, uint32_t height,
                                                   const char *jpeg_path);

uint8_t *decode_jpeg(const char *file, uint32_t *width, uint32_t *height);

bool cairo_buffer_draw_svg(struct cairo_buffer *buffer, const char *data, struct kywc_box *box);

bool cairo_buffer_draw_text(struct cairo_buffer *buffer, struct draw_info *info,
                            struct kywc_box *box);

bool cairo_buffer_draw_blur(struct cairo_buffer *buffer, int margin);

void text_extents(const char *font, int font_size, const char *text, int *width, int *height);

struct cairo_buffer *cairo_buffer_from_wlr_buffer(struct wlr_buffer *wlr_buffer);

bool cairo_surface_write_to_bmp(cairo_surface_t *surface, const char *filename);

#endif /* _BUFFER_H_ */
