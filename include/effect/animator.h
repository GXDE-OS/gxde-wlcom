// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_ANIMATOR_H_
#define _EFFECT_ANIMATOR_H_

#include <kywc/boxes.h>

#include "scene/scene.h"

struct animation_data {
    float alpha, angle;
    struct kywc_box geometry;
};

struct animator *animator_create(struct animation_data *start, int64_t start_time,
                                 int64_t end_time);

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
