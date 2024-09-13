// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdbool.h>
#include <stdlib.h>

#include <kywc/log.h>

#include "effect_p.h"
#include "output.h"
#include "render/opengl.h"

// software support brightness and color temperature

struct output_cursor_buffer {
    struct wl_list link;
    struct output *output;
    struct wl_listener precommit;
    struct wl_listener destroy;
    struct wlr_buffer *buffer;
};

struct soft_gamma_effect {
    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct effect_manager *manager;
    struct wl_list cursor_buffers;
    struct wl_listener new_enabled_output;
};

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct output *output = output_from_wlr_output(target->output->output);
    if (output_use_hardware_gamma(output)) {
        return true;
    }

    // blend function - mul color
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);

    uint32_t brightness = output->base.state.brightness;
    uint32_t color_temp = output->base.state.color_temp;
    float color_rgb[3];
    colortemp_get_rgb(color_rgb, color_temp);
    color_rgb[0] *= brightness * 0.01f;
    color_rgb[1] *= brightness * 0.01f;
    color_rgb[2] *= brightness * 0.01f;

    pixman_region32_t damage;
    pixman_region32_init(&damage);
    pixman_region32_copy(&damage, &target->damage);
    pixman_region32_translate(&damage, -target->logical.x, -target->logical.y);
    ky_scene_render_region(&damage, target);
    pixman_region32_subtract(&damage, &damage, &target->excluded_buffer_damage);

    wlr_render_pass_add_rect(
        target->render_pass,
        &(struct wlr_render_rect_options){
            .box = { .width = target->buffer->width, .height = target->buffer->height },
            .color = { .r = color_rgb[0], .g = color_rgb[1], .b = color_rgb[2], .a = 0.999999f },
            .clip = &damage,
            .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
        });

    pixman_region32_fini(&damage);

    // defalut blend function - premul alpha
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    return true;
}

static void redraw_hardware_cursor(struct output_cursor_buffer *cursor_buffer)
{
    struct wlr_output *wlr_output = cursor_buffer->output->wlr_output;
    struct wlr_buffer *buffer = wlr_output->cursor_front_buffer;
    // softeware cursor is null
    if (buffer == NULL) {
        return;
    }
    // modify once when cursor_buffer update
    if (buffer == cursor_buffer->buffer) {
        return;
    }
    cursor_buffer->buffer = buffer;

    struct wlr_render_pass *render_pass =
        wlr_renderer_begin_buffer_pass(wlr_output->renderer, buffer, NULL);
    // blend function - mul color
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);

    uint32_t brightness = cursor_buffer->output->base.state.brightness;
    uint32_t color_temp = cursor_buffer->output->base.state.color_temp;
    float color_rgb[3];
    colortemp_get_rgb(color_rgb, color_temp);
    color_rgb[0] *= brightness * 0.01f;
    color_rgb[1] *= brightness * 0.01f;
    color_rgb[2] *= brightness * 0.01f;

    wlr_render_pass_add_rect(
        render_pass,
        &(struct wlr_render_rect_options){
            .box = { .width = buffer->width, .height = buffer->height },
            .color = { color_rgb[0], .g = color_rgb[1], .b = color_rgb[2], .a = 0.999999f },
            .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
        });
    wlr_render_pass_submit(render_pass);

    // defalut blend function - premul alpha
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void output_handle_precommit(struct wl_listener *listener, void *data)
{
    struct output_cursor_buffer *cursor_buffer =
        wl_container_of(listener, cursor_buffer, precommit);
    redraw_hardware_cursor(cursor_buffer);
}

static void output_handle_destroy(struct wl_listener *listener, void *data)
{
    struct output_cursor_buffer *cursor_buffer = wl_container_of(listener, cursor_buffer, destroy);
    wl_list_remove(&cursor_buffer->link);
    wl_list_remove(&cursor_buffer->precommit.link);
    wl_list_remove(&cursor_buffer->destroy.link);
    free(cursor_buffer);
}

static void cursor_buffer_create(struct soft_gamma_effect *effect, struct output *output)
{
    if (output_use_hardware_gamma(output)) {
        return;
    }

    struct output_cursor_buffer *cursor_buffer = calloc(1, sizeof(*cursor_buffer));
    if (!cursor_buffer) {
        return;
    }

    cursor_buffer->output = output;
    cursor_buffer->precommit.notify = output_handle_precommit;
    wl_signal_add(&output->scene_output->output->events.precommit, &cursor_buffer->precommit);
    cursor_buffer->destroy.notify = output_handle_destroy;
    wl_signal_add(&output->scene_output->events.destroy, &cursor_buffer->destroy);
    cursor_buffer->buffer = NULL;

    wl_list_insert(&effect->cursor_buffers, &cursor_buffer->link);
}

static void handle_new_enabled_output(struct wl_listener *listener, void *data)
{
    struct soft_gamma_effect *effect = wl_container_of(listener, effect, new_enabled_output);
    struct kywc_output *output = data;
    cursor_buffer_create(effect, output_from_kywc_output(output));
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct soft_gamma_effect *effect = wl_container_of(listener, effect, enable);
    effect->new_enabled_output.notify = handle_new_enabled_output;
    output_manager_add_new_enabled_listener(&effect->new_enabled_output);
    ky_scene_damage_whole(effect->manager->server->scene);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct soft_gamma_effect *effect = wl_container_of(listener, effect, disable);
    wl_list_remove(&effect->new_enabled_output.link);
    wl_list_init(&effect->new_enabled_output.link);
    struct output_cursor_buffer *cursor_buffer, *tmp;
    wl_list_for_each_safe(cursor_buffer, tmp, &effect->cursor_buffers, link) {
        wl_list_remove(&cursor_buffer->link);
        wl_list_remove(&cursor_buffer->precommit.link);
        wl_list_remove(&cursor_buffer->destroy.link);
        free(cursor_buffer);
    }
    ky_scene_damage_whole(effect->manager->server->scene);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct soft_gamma_effect *effect = wl_container_of(listener, effect, destroy);
    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    free(effect);
}

static const struct effect_interface effect_impl = {
    .frame_render_post = frame_render_post,
};

bool soft_gamma_effect_create(struct effect_manager *manager)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return false;
    }

    struct soft_gamma_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create("soft_gamma", 0, true, &effect_impl, NULL);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->manager = manager;
    wl_list_init(&effect->cursor_buffers);

    effect->enable.notify = handle_effect_enable;
    wl_signal_add(&effect->effect->events.enable, &effect->enable);
    effect->disable.notify = handle_effect_disable;
    wl_signal_add(&effect->effect->events.disable, &effect->disable);
    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->effect->events.destroy, &effect->destroy);

    if (effect->effect->enabled) {
        handle_effect_enable(&effect->enable, NULL);
    }

    struct ky_scene *scene = effect->manager->server->scene;
    struct effect_entity *entity = ky_scene_add_effect(scene, effect->effect);
    entity->user_data = effect;

    return true;
}
