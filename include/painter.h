// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _PAINTER_H_
#define _PAINTER_H_

#include <stdbool.h>
#include <stdint.h>

struct wlr_buffer;

enum corner_mask {
    CORNER_MASK_NONE = 0,
    CORNER_MASK_TOP_LEFT = 1 << 0,
    CORNER_MASK_TOP_RIGHT = 1 << 1,
    CORNER_MASK_BOTTOM_LEFT = 1 << 2,
    CORNER_MASK_BOTTOM_RIGHT = 1 << 3,
    CORNER_MASK_ALL = (1 << 4) - 1,
};

enum border_mask {
    BORDER_MASK_NONE = 0,
    BORDER_MASK_TOP = 1 << 0,
    BORDER_MASK_RIGHT = 1 << 1,
    BORDER_MASK_BOTTOM = 1 << 2,
    BORDER_MASK_LEFT = 1 << 3,
    BORDER_MASK_ALL = (1 << 4) - 1,
};

enum text_align {
    TEXT_ALIGN_LEFT,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT,
};

enum auto_resize {
    AUTO_RESIZE_NONE = 0,
    AUTO_RESIZE_ONLY,
    AUTO_RESIZE_EXTEND,
};

enum circle_type {
    CIRCLE_TYPE_NONE = 0,
    CIRCLE_TYPE_SIMPLE,
    CIRCLE_TYPE_CLEAR,
};

struct draw_info {
    /* unscaled size */
    int width, height;
    float scale;

    float *solid_rgba;
    float *hover_rgba;

    float *border_rgba;
    float border_width;
    enum border_mask border_mask;

    /* rounded rect or circle */
    enum corner_mask corner_mask;
    float corner_radius;
    enum circle_type circle;

    const char *text;
    const char *font;
    float *font_rgba;
    int font_size;
    enum text_align align;
    enum auto_resize auto_resize;
    bool submenu; // ">"
    bool checked; // "✓"

    /* blur support */
    int blur_margin;

    /* image: svg and png */
    const char *svg, *hover_svg;
    const char *png_path;
};

struct wlr_buffer *painter_draw_buffer(struct draw_info *info);

bool painter_redraw_buffer(struct wlr_buffer *buffer, struct draw_info *info);

void painter_buffer_unscaled_size(struct wlr_buffer *buffer, int *width, int *height);

void painter_buffer_size(struct wlr_buffer *buffer, int *width, int *height);

void painter_buffer_to_file(struct wlr_buffer *buffer, const char *name);

struct wlr_buffer *painter_create_buffer(int width, int height, float scale);

#endif /* _PAINTER_H_ */
