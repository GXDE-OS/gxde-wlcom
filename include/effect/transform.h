// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _EFFECT_TRANSFORM_H_
#define _EFFECT_TRANSFORM_H_

#include "effect/animator.h"
#include "view/view.h"

struct transform;
struct transform_effect;
struct effect_manager;

struct transform_options {
    struct animation_type_group type;
    struct animation_data start, end;
    int64_t start_time;
    int duration;
    /* thumbnail scale */
    float scale;
    /* for zero copy */
    struct ky_scene_buffer *buffer;
};

struct transform_effect_interface {
    void (*update_transform_options)(struct transform_effect *effect, struct transform *transform,
                                     struct transform_options *options,
                                     struct animation_data *current, bool interrupted, void *data);
    void (*get_render_dst_box)(struct transform_effect *effect, struct transform *transform,
                               struct kywc_box *dst_box, void *data);
    void (*get_render_src_box)(struct transform_effect *effect, struct transform *transform,
                               struct animation_data *current, struct kywc_fbox *src_fbox,
                               void *data);
    void (*enable)(struct transform_effect *effect, bool enable, void *data);
    void (*destroy)(struct transform_effect *effect, void *data);
};

struct transform_effect *transform_effect_create(struct effect_manager *manager,
                                                 struct transform_effect_interface *impl,
                                                 const char *name, int priority, void *data);

struct transform *transform_effect_get_or_create_transform(struct transform_effect *effect,
                                                           struct transform_options *options,
                                                           struct ky_scene_node *node, void *data);

void transform_set_user_data(struct transform *transform, void *data);

void *transform_get_user_data(struct transform *transform);

struct ky_scene_buffer *transform_get_zero_copy_buffer(struct view *view);

void transform_block_source_update(struct transform *transform, bool block);

void transform_destroy(struct transform *transform);

void transform_add_destroy_listener(struct transform *transform, struct wl_listener *listener);

#endif /* _EFFECT_TRANSFORM_H_ */
