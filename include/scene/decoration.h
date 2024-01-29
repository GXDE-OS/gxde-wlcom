// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_DECORATION_H_
#define _SCENE_DECORATION_H_

#include "scene/scene.h"

struct ky_scene_decoration *ky_scene_decoration_create(struct ky_scene_tree *parent);

struct ky_scene_node *ky_scene_node_from_decoration(struct ky_scene_decoration *scene_decoration);

struct ky_scene_decoration *ky_scene_decoration_from_node(struct ky_scene_node *node);

void ky_scene_decoration_set_window_size(struct ky_scene_decoration *scene_decoration, int width,
                                         int height);

// 0=right-bottom, 1=right-top, 2=left-bottom, 3=left-top
void ky_scene_decoration_set_round_corner_radius(struct ky_scene_decoration *scene_decoration,
                                                 const int round_corner_radius[static 4]);

void ky_scene_decoration_set_margin(struct ky_scene_decoration *scene_decoration, int title_height,
                                    int border_thickness, int shadow_width);

void ky_scene_decoration_set_margin_color(struct ky_scene_decoration *scene_decoration,
                                          const float title_color[static 4],
                                          const float border_color[static 4],
                                          const float shadow_color[static 4]);

void ky_scene_decoration_set_resize_width(struct ky_scene_decoration *scene_decoration,
                                          int resize_with);

#endif /* _SCENE_DECORATION_H_ */
