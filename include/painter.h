// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

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

enum text_attr {
    TEXT_ATTR_NONE = 0,
    TEXT_ATTR_SLANT = 1 << 0,
    TEXT_ATTR_SUBMENU = 1 << 1, // ">"
    TEXT_ATTR_CHECKED = 1 << 2, // "✓"
    TEXT_ATTR_ACCEL = 1 << 3,   // "_"
    TEXT_ATTR_SHORTCUT = 1 << 4,
};

enum auto_resize {
    AUTO_RESIZE_NONE = 0,
    AUTO_RESIZE_ONLY,
    AUTO_RESIZE_EXTEND,
};

struct draw_info {
    /* unscaled size */
    int width, height;
    float scale;

    float *solid_rgba;
    float *hover_rgba;

    float *border_rgba;
    float border_width;
    uint32_t border_mask;

    /* rounded rect */
    uint32_t corner_mask;
    float corner_radius;
    float hover_radius;

    const char *text;
    const char *shortcut;
    const char *font;
    float *font_rgba;
    int font_size;
    enum text_align align;
    enum auto_resize auto_resize;
    uint32_t text_attr;
    bool layout_is_right_to_left;

    /* blur support */
    int blur_margin;

    /* svg data */
    const char *svg, *hover_svg;
    /* image file */
    const char *image;
    struct {
        uint32_t width, height;
        void *data; // pixel data  can be null
    } pixel;
};

struct wlr_buffer *painter_create_buffer(int width, int height, float scale);

struct wlr_buffer *painter_draw_buffer(struct draw_info *info);

bool painter_buffer_redraw(struct wlr_buffer *buffer, struct draw_info *info);

void painter_buffer_get_dest_size(struct wlr_buffer *buffer, int *width, int *height);

void painter_buffer_write_to_file(struct wlr_buffer *buffer, const char *name);

void painter_get_text_size(const char *text, const char *font, int font_size, int *width,
                           int *height);

#endif /* _PAINTER_H_ */
