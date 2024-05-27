// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _SCENE_ANIMATION_H_
#define _SCENE_ANIMATION_H_

#include "scene.h"

struct server;

enum animation_type {
    ANIMATION_TYPE_MOD, // user defined
    ANIMATION_TYPE_LINER,
    ANIMATION_TYPE_EASE,
    ANIMATION_TYPE_EASE_IN,
    ANIMATION_TYPE_EASE_OUT,
    ANIMATION_TYPE_EASE_IN_OUT,
    ANIMATION_TYPES,
};

bool animation_manager_create(struct server *server);

struct animation *animation_create(float p1x, float p1y, float p2x, float p2y);

float animation_value(struct animation *animation, float x);

struct animation *animation_manager_get_default(enum animation_type type);

void animation_destroy(struct animation *animation);

void ky_scene_node_set_position_with_animation(struct ky_scene_node *node, int x, int y,
                                               struct animation *animation, uint32_t duration);

void ky_scene_rect_set_size_with_animation(struct ky_scene_rect *rect, int width, int height,
                                           struct animation *animation, uint32_t duration);

#endif /* _SCENE_ANIMATION_H_ */
