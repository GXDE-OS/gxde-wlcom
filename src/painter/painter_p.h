// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

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

    /* unscaled size */
    uint32_t width;
    uint32_t height;
};

struct cairo_buffer *cairo_buffer_create(uint32_t width, uint32_t height, float scale);

bool cairo_buffer_draw_svg(struct cairo_buffer *buffer, const char *data, struct kywc_box *box);

bool cairo_buffer_draw_text(struct cairo_buffer *buffer, struct draw_info *info,
                            struct kywc_box *box);

bool cairo_buffer_draw_blur(struct cairo_buffer *buffer, int margin, bool circle);

void text_extents(const char *font, int font_size, const char *text, int *width, int *height);

struct cairo_buffer *cairo_buffer_from_wlr_buffer(struct wlr_buffer *wlr_buffer);

#endif /* _BUFFER_H_ */
