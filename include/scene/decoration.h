// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _SCENE_DECORATION_H_
#define _SCENE_DECORATION_H_

#include "scene/scene.h"

enum shadow_mask {
    SHADOW_MASK_NONE = 0,
    SHADOW_MASK_TOP_LEFT = 1 << 0,
    SHADOW_MASK_TOP = 1 << 1,
    SHADOW_MASK_TOP_RIGHT = 1 << 2,
    SHADOW_MASK_RIGHT = 1 << 3,
    SHADOW_MASK_BOTTOM_RIGHT = 1 << 4,
    SHADOW_MASK_BOTTOM = 1 << 5,
    SHADOW_MASK_BOTTOM_LEFT = 1 << 6,
    SHADOW_MASK_LEFT = 1 << 7,
    SHADOW_MASK_ALL = (1 << 8) - 1,
};

struct ky_scene_decoration *ky_scene_decoration_create(struct ky_scene_tree *parent);

struct ky_scene_node *ky_scene_node_from_decoration(struct ky_scene_decoration *decoration);

struct ky_scene_decoration *ky_scene_decoration_from_node(struct ky_scene_node *node);

void ky_scene_decoration_set_surface_size(struct ky_scene_decoration *decoration, int width,
                                          int height);

// 0=right-bottom, 1=right-top, 2=left-bottom, 3=left-top
void ky_scene_decoration_set_round_corner_radius(struct ky_scene_decoration *decoration,
                                                 const int round_corner_radius[static 4]);

void ky_scene_decoration_set_margin(struct ky_scene_decoration *decoration, int title_height,
                                    int border_thickness, int shadow_width);

void ky_scene_decoration_set_margin_color(struct ky_scene_decoration *decoration,
                                          const float title_color[static 4],
                                          const float border_color[static 4],
                                          const float shadow_color[static 4]);

void ky_scene_decoration_set_shadow_mask(struct ky_scene_decoration *decoration,
                                         uint32_t shadow_mask);

void ky_scene_decoration_set_resize_width(struct ky_scene_decoration *decoration, int resize_with);

void ky_scene_decoration_set_blurred(struct ky_scene_decoration *decoration, bool blurred);

#endif /* _SCENE_DECORATION_H_ */
