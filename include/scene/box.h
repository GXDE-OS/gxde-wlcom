// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _SCENE_BOX_H_
#define _SCENE_BOX_H_

#include "scene.h"

struct ky_scene_box *ky_scene_box_create(struct ky_scene_tree *parent, int width, int height,
                                         const float color[static 4], int border_width);

struct ky_scene_node *ky_scene_node_from_box(struct ky_scene_box *scene_box);

void ky_scene_box_set_color(struct ky_scene_box *scene_box, const float color[static 4]);

void ky_scene_box_set_size(struct ky_scene_box *scene_box, int width, int height);

void ky_scene_box_set_border_width(struct ky_scene_box *scene_box, int width);

#endif /* _SCENE_BOX_H_ */
