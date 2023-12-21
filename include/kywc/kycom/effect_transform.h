// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECT_TRANSFORM_H_
#define _EFFECT_TRANSFORM_H_

#include "kywc/kycom/effect_view.h"
#include "kywc/kycom/scene.h"

#define TRANSFORM_TIME 200.0

struct kywc_geometry_transform_render_instance {
    struct kywc_render_instance base;
    struct ky_texture_node *texture;
    struct kywc_geometry_transform_node *node;
};

struct xy_linear_func {
    vec2 start_point;
    vec2 end_point;
    float k;
    float b;
};

struct kywc_transform_data {
    struct kywc_group_node *view_node;
    struct kywc_effect_view *view;
    struct kywc_box geometry_box;
    struct padding shadow;
    struct padding padding;
    int32_t start_time;
    int32_t time;

    float max_time_factor;
    float time_factor;

    float alpha;
    struct xy_linear_func alpha_func;
    struct xy_linear_func geometry_func[4];
};

struct kywc_geometry_transform_node {
    struct kywc_group_node node;
    struct kywc_transform_data data;
    // texture-local(surface-local) coordinates
    pixman_region32_t opaque_region;
    kywc_node_destroy_interface group_destroy;
};

extern const char *scale_effect_name;

int64_t kywc_get_current_time_msec(void);

float kywc_transform_data_calc_time_factor(struct kywc_transform_data *data);

void kywc_transform_data_alpha_func_init(struct kywc_transform_data *data, float end_alpha);

void kywc_transform_data_geometry_func_init(struct kywc_transform_data *data);

void kywc_transform_data_calc_alpha(struct kywc_transform_data *data);

void kywc_transform_data_calc_geometry(struct kywc_transform_data *data);

void kywc_transform_data_calc_padding_region(struct kywc_transform_data *data,
                                             struct kywc_effect_view *view);

void kywc_transform_data_calc_local_damage(struct kywc_transform_data *data,
                                           struct wlr_box *local_damage);

void kywc_transform_data_calc_shadow(struct kywc_transform_data *data, struct kywc_box *bound_box);

struct kywc_geometry_transform_node *
kywc_transform_geometry_node_create(struct kywc_effect_view *view);

#endif