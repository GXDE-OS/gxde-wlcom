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
    struct kywc_effect_box geometry_box;
    struct padding padding;
};

struct kywc_transform_geometry_node {
    struct kywc_transform_data *kywc_transform_data;
    struct kywc_group_node node;
    const char *name;
    // texture-local(surface-local) coordinates
    pixman_region32_t opaque_region;
};

extern const char *kywc_transform_name;

extern struct kywc_transform_data kywc_transform_data;

float kywc_current_time_factor(int32_t start_time, int32_t end_time);

int64_t kywc_current_time_msec(void);

void kywc_geometry_current_box(struct kywc_transform_data *kywc_transform_data);

void kywc_transform_local_damage(struct wlr_box *box,
                                 struct kywc_transform_data *kywc_transform_data);

void kywc_transform_geometry_node_destroy(struct kywc_transform_geometry_node *node);

struct kywc_transform_geometry_node *
kywc_transform_geometry_node_create(struct kywc_transform_data *kywc_transform_data);

#endif