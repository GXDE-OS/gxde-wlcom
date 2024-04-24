// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _WIDGET_H_
#define _WIDGET_H_

#include "scene/scene.h"

struct widget *widget_create(struct ky_scene_tree *parent);

/* actually is a scene buffer */
struct ky_scene_node *ky_scene_node_from_widget(struct widget *widget);

void widget_destroy(struct widget *widget);

void widget_update(struct widget *widget, bool partial);

void widget_set_text(struct widget *widget, const char *text, int align, bool submenu, bool checked,
                     bool slant);

void widget_set_font(struct widget *widget, const char *name, int size);

/* if hover_svg is not NULL, means this widget is hoverable */
void widget_set_image(struct widget *widget, const char *svg, const char *hover_svg);

void widget_set_hovered(struct widget *widget, bool hovered);

void widget_set_enabled(struct widget *widget, bool enabled);

void widget_set_size(struct widget *widget, int width, int height);

void widget_set_max_size(struct widget *widget, int width, int height);

void widget_set_auto_resize(struct widget *widget, int auto_resize);

void widget_set_backgrond_color(struct widget *widget, const float color[static 4]);

void widget_set_front_color(struct widget *widget, const float color[static 4]);

void widget_set_hovered_color(struct widget *widget, const float color[static 4]);

void widget_set_border(struct widget *widget, const float color[static 4], uint32_t mask,
                       float width);

void widget_set_round_coner(struct widget *widget, uint32_t mask, float radius);

void widget_set_opacity(struct widget *widget, float opacity);

void widget_get_size(struct widget *widget, int *width, int *height);

#endif /* _WIDGET_H_ */
