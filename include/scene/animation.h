// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_ANIMATION_H_
#define _SCENE_ANIMATION_H_

#include "effect/animator.h"

void ky_scene_node_set_position_with_animation(struct ky_scene_node *node, int x, int y,
                                               struct animation *animation, uint32_t duration);

void ky_scene_rect_set_size_with_animation(struct ky_scene_rect *rect, int width, int height,
                                           struct animation *animation, uint32_t duration);

#endif /* _SCENE_ANIMATION_H_ */
