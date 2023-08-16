// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _EFFECTS_H_
#define _EFFECTS_H_

#include <wayland-server-core.h>

#include "kywc/boxes.h"
#include "kywc/kycom/opengl.h"

struct kywc_effect_view;
struct kywc_gl_buffer;

enum kywc_effect_types {
    /**
     * Pre hooks are called before starting to repaint the output
     */
    OUTPUT_EFFECT_PRE = 0,
    /**
     * Damage hooks are called before attaching the renderer to the output.
     * They are useful if the output damage needs to be modified, whereas
     * plugins that simply need to update their animation should use PRE hooks.
     */
    OUTPUT_EFFECT_DAMAGE = 1,
    /**
     * Overlay hooks are called right after repainting the output, but
     * before post hooks and before swapping buffers
     */
    OUTPUT_EFFECT_OVERLAY = 2,

    OUTPUT_EFFECT_POST_PROCESSING = 3,
    /**
     * Post hooks are called after the buffers have been swapped
     */
    OUTPUT_EFFECT_POST = 4,
    /**
     * Invalid type for a hook, used internally
     */
    OUTPUT_EFFECT_TOTAL = 5,
};

typedef void (*kywc_effect_hook)(void);

typedef void (*kywc_effect_post_hook)(struct kywc_gl_buffer *source, struct kywc_gl_buffer *dst);

#define kywc_effect_box kywc_box

struct kywc_effect_server {
    struct server *kywc_server;

    struct {
        struct wl_signal new_output;
        struct wl_signal view_maximize;
        struct wl_signal view_minimize;
        struct wl_signal view_switching;
        struct wl_signal view_switched;
        /* data is kywc_effect_view. */
        struct wl_signal view_new;
    } events;

    struct wl_listener new_view;
};

struct kywc_effect_output {
    uint32_t output_fb;
    struct wl_list *effects;
    uint32_t output_width, output_height;
    uint32_t default_out_buffer_index;
    struct kywc_gl_buffer post_buffers[3];
    const struct kywc_scene_output *scene_output;
};

struct kywc_effect_server *kywc_effect_server(void);

void kywc_effect_add_hook(enum kywc_effect_types type, kywc_effect_hook hook);

void kywc_effect_add_post_hook(kywc_effect_post_hook post_hook, enum kywc_effect_types type);

void kywc_effect_remove_hook(void *hook);

void kywc_effect_remove_post(kywc_effect_post_hook hook);

#endif /* _LIBEFFECT_EFFECTS_H */
