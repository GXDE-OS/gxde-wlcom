// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_ANIMATOR_H_
#define _EFFECT_ANIMATOR_H_

#include <kywc/boxes.h>

#include "scene/scene.h"

struct server;
struct animation;

enum animation_type {
    ANIMATION_TYPE_NONE = 0,
    ANIMATION_TYPE_LINER,        // (0, 0) (1, 1)
    ANIMATION_TYPE_EASE,         // (0.25, 0.1) (0.25, 1)
    ANIMATION_TYPE_EASE_IN,      // (0.42, 0) (1, 1)
    ANIMATION_TYPE_EASE_OUT,     // (0, 0) (0.58, 1)
    ANIMATION_TYPE_EASE_IN_OUT,  // (0.42, 0) (0.58, 1)
    ANIMATION_TYPE_30_15_10_100, // (0.3, 0.15) (0.1, 1)
    ANIMATION_TYPE_0_40_20_100,  // (0, 0.4) (0.2, 1)
    ANIMATION_TYPE_33_0_100_75,  // (0.33, 0) (1, 0.75)
    ANIMATION_TYPES,
};

struct animation_type_group {
    enum animation_type geometry;
    enum animation_type alpha;
    enum animation_type angle;
};

struct animation_data {
    float alpha, angle;
    struct kywc_box geometry;
};

bool animation_manager_create(struct server *server);

struct animation *animation_manager_get(enum animation_type type);

struct animator *animator_create(struct animation_data *start, struct animation_type_group type,
                                 int64_t start_time, int64_t end_time);

float animation_value(struct animation *animation, float x);

void animator_destroy(struct animator *animator);

void animator_set_time(struct animator *animator, int64_t end_time);

void animator_set_angle(struct animator *animator, float end_angle);

void animator_set_alpha(struct animator *animator, float end_alpha);

void animator_set_position(struct animator *animator, int end_lx, int end_ly);

void animator_set_size(struct animator *animator, int end_width, int end_height);

const struct animation_data *animator_value(struct animator *animator, int64_t current_time);

void animator_render_texture(struct animation_data *animation_data,
                             struct ky_scene_render_target *target, struct wlr_texture *texture);

#endif /* _EFFECT_ANIMATOR_H_ */
