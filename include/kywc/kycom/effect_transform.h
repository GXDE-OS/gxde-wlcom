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
    struct animation *animation;
    struct kywc_box view_box;
    struct padding padding;
    /* offset relative to parent node */
    int x, y;
    /* layout coordination */
    struct kywc_box dst_box;

    int32_t start_time;
    int32_t time;
    int32_t max_time;

    float time_factor;

    float animation_value;

    struct xy_linear_func time_factor_func;

    float alpha;

    struct xy_linear_func alpha_func;
    struct xy_linear_func view_box_func[4];
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

void kywc_transform_data_calc_time_factor(struct kywc_transform_data *data);

void kywc_transform_data_alpha_func_init(struct kywc_transform_data *data, float end_alpha);

void kywc_transform_data_time_func_init(struct kywc_transform_data *data);

void kywc_transform_data_view_box_func_init(struct kywc_transform_data *data);

void kywc_transform_data_calc_alpha(struct kywc_transform_data *data);

void kywc_transform_data_calc_view_box(struct kywc_transform_data *data);

void kywc_transform_data_calc_padding_region(struct kywc_transform_data *data,
                                             struct kywc_effect_view *view);

void kywc_transform_data_update_location(struct kywc_transform_data *data);

struct kywc_geometry_transform_node *
kywc_transform_geometry_node_create(struct kywc_effect_view *view);

#endif