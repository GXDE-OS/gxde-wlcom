// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _WIDGET_P_H_
#define _WIDGET_P_H_

#include "widget/widget.h"

enum widget_update_cause {
    WIDGET_UPDATE_CAUSE_NONE = 0,
    WIDGET_UPDATE_CAUSE_SIZE = 1 << 0,
    WIDGET_UPDATE_CAUSE_CONTENT = 1 << 1,
    WIDGET_UPDATE_CAUSE_HOVERED = 1 << 2,
    WIDGET_UPDATE_CAUSE_ENABLED = 1 << 3,
    WIDGET_UPDATE_CAUSE_SCALE = 1 << 4,
    WIDGET_UPDATE_CAUSE_FORCE = 1 << 5,
};

struct widget {
    struct {
        struct ky_scene_buffer *buffer;
        struct ky_scene_node *node;
    } content;

    // struct wl_listener output_frame;
    float scale;

    /* current content in widget */
    const char *svg, *hover_svg;

    const char *text;
    const char *font_name;
    int font_size;
    int text_align;
    bool text_truncated;
    bool slant;

    /* color in this widget */
    float background_color[4];
    float front_color[4];
    float hovered_color[4];

    float border_color[4];
    uint32_t border_mask;
    float border_width;

    uint32_t corner_mask;
    float corner_radius;

    int width, height;
    int max_width, max_height;

    uint32_t pending_cause;

    /* auto resize to content, clamp to max size */
    int auto_resize;
    /* widget support hover state */
    bool hoverable;
    bool enabled, hovered, submenu, checked;
};

#endif /* _WIDGET_P_H_ */
