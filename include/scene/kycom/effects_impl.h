// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECTS_IMPL_H_
#define _EFFECTS_IMPL_H_

#include "kywc/kycom/effects.h"
#include "kywc/kycom/opengl.h"

struct kywc_effect_view;
struct kywc_scene_output;

struct effect_container {
    union effects {
        kywc_effect_hook hook;
        kywc_effect_post_hook post_hook;
    } effect;
    enum kywc_effect_types type;
    struct wl_list link;
};

void _kywc_effect_init(struct server *s);

void _kywc_effects_run(enum kywc_effect_types type);

struct kywc_effect_output *_kywc_effect_output_create(const struct kywc_scene_output *output);

void _kywc_effect_output_destroy(struct kywc_effect_output *manager);

void _kywc_effects_post_update_framebuffer(struct kywc_effect_output *manager);

void _kywc_effects_run_post(struct kywc_effect_output *manager);

void _kywc_effect_add_new_view_listener(void);

#endif
