// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/util/log.h>

#include "kywc/kycom/scene.h"
#include "scene/kycom/opengl_p.h"

#include "scene/kycom/effect_error_impl.h"
#include "scene/kycom/effect_view_impl.h"
#include "scene/kycom/effects_impl.h"

static struct kywc_effect_server effects_server;

static struct wl_list all_effect_hooks[OUTPUT_EFFECT_TOTAL];

/* Init effect module. */
void _kywc_effect_init(struct server *s)
{
    if (!s) {
        _kywc_effect_error_set(EFFECT_ERROR_SERVER);
        return;
    }

    struct kywc_effect_server *server = &effects_server;
    server->kywc_server = s;

    for (int i = 0; i < OUTPUT_EFFECT_TOTAL; i++) {
        wl_list_init(all_effect_hooks + i);
    }
    wl_signal_init(&server->events.new_output);
    wl_signal_init(&server->events.view_maximize);
    wl_signal_init(&server->events.view_minimize);
    wl_signal_init(&server->events.view_switching);
    wl_signal_init(&server->events.view_switched);
    wl_signal_init(&server->events.view_new);
}

void _kywc_effect_add_new_view_listener(void)
{
    effects_server.new_view.notify = _kywc_effect_handle_view_create;
    kywc_view_add_new_listener(&effects_server.new_view);
}

struct kywc_effect_server *kywc_effect_server(void)
{
    return effects_server.kywc_server ? &effects_server : NULL;
}

static struct effect_container *effect_container_create(void)
{
    struct effect_container *container = malloc(sizeof(struct effect_container));
    if (!container) {
        return NULL;
    }
    wl_list_init(&container->link);

    return container;
}

static void effect_container_insert(struct effect_container *effect,
                                    enum kywc_effect_types type)
{
    if (!effect) {
        return;
    }
    effect->type = type;
    struct wl_list *effect_list = all_effect_hooks + type;
    wl_list_insert(effect_list, &effect->link);
}

void kywc_effect_add_hook(enum kywc_effect_types type, kywc_effect_hook hook)
{
    struct effect_container *container = effect_container_create();
    container->effect.hook = hook;

    effect_container_insert(container, type);
}

void kywc_effect_add_post_hook(kywc_effect_post_hook post_hook, enum kywc_effect_types type)
{
    struct effect_container *container = effect_container_create();
    container->effect.post_hook = post_hook;

    effect_container_insert(container, type);
}

void kywc_effect_remove_hook(void *hook)
{
    for (int i = 0; i < OUTPUT_EFFECT_TOTAL; i++) {
        struct effect_container *container, *temp_effect;
        wl_list_for_each_safe(container, temp_effect, all_effect_hooks + i, link) {
            /* Remove all hook type by pointer union */
            if (container != NULL && container->effect.hook == hook) {
                wl_list_remove(&container->link);
                free(container);
                return;
            }
        }
    }
}

static const int default_out_framebuffer_index = 0;

struct kywc_effect_output *_kywc_effect_output_create(const struct kywc_scene_output *output)
{
    struct kywc_effect_output *effect_output = malloc(sizeof(*effect_output));
    if (!effect_output) {
        return NULL;
    }
    effect_output->output_fb = 0;
    effect_output->scene_output = output;
    effect_output->effects = all_effect_hooks;
    effect_output->output_width = output->output->width;
    effect_output->output_height = output->output->height;
    effect_output->default_out_buffer_index = default_out_framebuffer_index;
    for (int i = 0; i < 3; i++) {
        kywc_gl_buffer_init(effect_output->post_buffers + i);
    }
    return effect_output;
}

void _kywc_effect_output_destroy(struct kywc_effect_output *output)
{
    free(output);
}

void _kywc_effects_post_update_framebuffer(struct kywc_effect_output *output)
{
    if (!output) {
        return;
    }
    output->output_fb = kywc_gl_get_current_framebuffer();
}

void _kywc_effects_run_post(struct kywc_effect_output *effect_output)
{
    if (!effect_output) {
        return;
    }
    struct wl_list *post_effects = effect_output->effects + OUTPUT_EFFECT_POST_PROCESSING;
    if (wl_list_empty(post_effects)) {
        return;
    }

    struct kywc_gl_buffer default_framebuffer;
    default_framebuffer.fb = effect_output->output_fb;
    default_framebuffer.fb_tex = 0;

    int last_buffer_index = default_out_framebuffer_index;
    int next_buffer_index = 1;
    int data_index = OUTPUT_EFFECT_POST_PROCESSING;
    struct effect_container *post_effect = NULL;

    struct kywc_gl_buffer *post_buffers = effect_output->post_buffers;
    int length = wl_list_length(all_effect_hooks + data_index);
    wl_list_for_each(post_effect, all_effect_hooks + data_index, link) {
        length--;
        struct kywc_gl_buffer *next_buffer =
            length ? post_buffers + next_buffer_index : &default_framebuffer;

        kywc_gl_buffer_allocate(next_buffer, effect_output->output_fb, effect_output->output_width,
                              effect_output->output_height);

        (*post_effect->effect.post_hook)(post_buffers + last_buffer_index, next_buffer);

        last_buffer_index = next_buffer_index;
        next_buffer_index ^= 0b11;
    }
}

void _kywc_effects_run(enum kywc_effect_types type)
{
    struct effect_container *effect = NULL, *temp;
    wl_list_for_each_safe(effect, temp, &all_effect_hooks[type], link) {
        if (effect) {
            (*effect->effect.hook)();
        }
    }
}
