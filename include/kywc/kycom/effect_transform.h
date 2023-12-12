// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_TRANSFORM_H_
#define _EFFECT_TRANSFORM_H_

#include "kywc/kycom/effect_view.h"
#include "kywc/kycom/scene.h"

#define TRANSFORM_TIME 200.0

struct kywc_transform_node_render_instance {
    struct kywc_render_instance base;
    struct ky_texture_node *texture;
    struct kywc_transform_geometry_node *node;
};

struct kywc_transform_data {
    float time_factor;
    struct kywc_group_node *view_node;
    struct kywc_effect_view *view;
    struct kywc_box geometry_box;
    struct padding padding;
    struct padding shadow_box;
    int32_t time;
    int32_t start_time;
};

struct kywc_transform_geometry_node {
    struct kywc_transform_data *data;
    struct kywc_group_node node;
    // texture-local(surface-local) coordinates
    pixman_region32_t opaque_region;
};

extern const char *kywc_transform_name;

float kywc_calc_time_factor(int32_t start_time, int32_t current_time);

int64_t kywc_get_time_msec(void);

void kywc_transform_data_calc_geometry(struct kywc_transform_data *data);

void kywc_transform_data_calc_padding_region(struct kywc_transform_data *data,
                                             struct kywc_effect_view *view);

void kywc_transform_data_calc_local_damage(struct kywc_transform_data *data,
                                           struct wlr_box *local_damage);

void kywc_transform_data_calc_shadow(struct kywc_transform_data *data, struct kywc_box *bound_box);

void kywc_transform_geometry_node_destroy(struct kywc_transform_geometry_node *node);

struct kywc_transform_geometry_node *
kywc_transform_geometry_node_create(struct kywc_transform_data *data);

#endif