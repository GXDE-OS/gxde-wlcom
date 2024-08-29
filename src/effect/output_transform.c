// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include <kywc/log.h>
#include <wayland-util.h>

#include "effect/output_transform.h"
#include "effect_p.h"
#include "output.h"
#include "render/opengl.h"
#include "scene/thumbnail.h"
#include "util/time.h"

#define CLAMP(value, min, max) (((value) < (min)) ? (min) : ((value) > (max)) ? (max) : (value))

// output - output_entry - effect_entity. life cycle: output_entry = effect_entity
struct output_entry {
    struct output_transform_effect *effect;
    struct effect_entity *effect_entity;
    struct ky_scene_output *output;
    struct wl_listener destroy;
    enum wl_output_transform last_transform;
    // animate
    uint32_t start_time;
    float start_angle, end_angle, angle;
    float start_width, end_width, width;
    float start_height, end_height, height;
    float fade_alpha;
    // output screenshot
    struct thumbnail *old_thumbnail;
    struct wlr_texture *old_thumbnail_texture;
    struct wl_listener old_thumbnail_update;
    struct wl_listener old_thumbnail_destroy;
    struct thumbnail *new_thumbnail;
    struct wlr_texture *new_thumbnail_texture;
    struct wl_listener new_thumbnail_update;
    struct wl_listener new_thumbnail_destroy;
    enum wl_output_transform new_thumbnail_transform;
};

struct output_transform_effect {
    struct effect *effect;
    struct wl_listener destroy;
    struct effect_manager *manager;
    uint32_t animate_duration;
};

static int output_transform_to_angle(enum wl_output_transform transform)
{
    if (transform == WL_OUTPUT_TRANSFORM_90) {
        return 90;
    } else if (transform == WL_OUTPUT_TRANSFORM_180) {
        return 180;
    } else if (transform == WL_OUTPUT_TRANSFORM_270) {
        return 270;
    } else if (transform == WL_OUTPUT_TRANSFORM_FLIPPED) {
        return 360;
    } else if (transform == WL_OUTPUT_TRANSFORM_FLIPPED_90) {
        return 90;
    } else if (transform == WL_OUTPUT_TRANSFORM_FLIPPED_180) {
        return 180;
    } else if (transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
        return 270;
    } else {
        return 0;
    }
}

static enum wl_output_transform diff_angle_to_new_thumbnail_transform(int angle)
{
    if (angle == 90) {
        return WL_OUTPUT_TRANSFORM_90;
    } else if (angle == 180) {
        return WL_OUTPUT_TRANSFORM_180;
    } else if (angle == -90) {
        return WL_OUTPUT_TRANSFORM_270;
    } else if (angle == -180) {
        return WL_OUTPUT_TRANSFORM_180;
    } else {
        return WL_OUTPUT_TRANSFORM_NORMAL;
    }
}

static void output_entry_start_animate(struct output_entry *output)
{
    output->start_time = current_time_msec();

    output->start_angle = (float)output_transform_to_angle(output->last_transform);
    output->end_angle = (float)output_transform_to_angle(output->output->output->transform);
    output->end_angle = output->start_angle - output->end_angle;
    // reduce rotate angle
    if (output->end_angle > 180.0f) {
        output->end_angle -= 360.0f;
    } else if (output->end_angle < -180.0f) {
        output->end_angle += 360.0f;
    }
    // keep angle not equal zero
    if (fabsf(output->end_angle) < 0.01f) {
        output->end_angle = 360.0f;
    }
    output->start_angle = 0;
    output->angle = output->start_angle;

    int diff_angle = (int)output->end_angle;
    // vertical rotation need resize animation
    if (diff_angle == 90 || diff_angle == -90) {
        output->start_width = output->output->output->width;
        output->start_height = output->output->output->height;
        output->end_width = output->start_height;
        output->end_height = output->start_width;
    }
    output->new_thumbnail_transform = diff_angle_to_new_thumbnail_transform(diff_angle);

    output->fade_alpha = 0.0f;
}

static void entity_destroy(struct effect_entity *entity)
{
    struct output_entry *output = entity->user_data;
    if (!output) {
        return;
    }

    if (output->old_thumbnail_texture) {
        wlr_texture_destroy(output->old_thumbnail_texture);
    }
    if (output->new_thumbnail_texture) {
        wlr_texture_destroy(output->new_thumbnail_texture);
    }
    if (output->old_thumbnail) {
        wl_list_remove(&output->old_thumbnail_update.link);
        wl_list_remove(&output->old_thumbnail_destroy.link);
        thumbnail_destroy(output->old_thumbnail);
    }
    if (output->new_thumbnail) {
        wl_list_remove(&output->new_thumbnail_update.link);
        wl_list_remove(&output->new_thumbnail_destroy.link);
        thumbnail_destroy(output->new_thumbnail);
    }

    wl_list_remove(&output->destroy.link);
    free(output);
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct output_entry *output = wl_container_of(listener, output, destroy);
    if (output->effect_entity) {
        effect_entity_destroy(output->effect_entity);
    }
}

static struct output_entry *output_entry_create(struct output_transform_effect *effect,
                                                struct ky_scene_output *scene_output,
                                                enum wl_output_transform last_transform)
{
    struct output_entry *output = calloc(1, sizeof(*output));
    if (!output) {
        return NULL;
    }

    output->effect = effect;
    output->last_transform = last_transform;
    output->output = scene_output;
    wl_list_init(&output->old_thumbnail_update.link);
    wl_list_init(&output->old_thumbnail_destroy.link);
    wl_list_init(&output->new_thumbnail_update.link);
    wl_list_init(&output->new_thumbnail_destroy.link);

    output->destroy.notify = handle_output_destroy;
    wl_signal_add(&scene_output->events.destroy, &output->destroy);

    return output;
}

static void handle_old_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    struct output_entry *output = wl_container_of(listener, output, old_thumbnail_update);
    if (output->old_thumbnail_texture) {
        wlr_texture_destroy(output->old_thumbnail_texture);
    }
    struct wlr_renderer *renderer = output->effect->manager->server->renderer;
    output->old_thumbnail_texture = wlr_texture_from_buffer(renderer, event->buffer);
}

static void handle_old_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct output_entry *output = wl_container_of(listener, output, old_thumbnail_destroy);

    wl_list_remove(&output->old_thumbnail_destroy.link);
    wl_list_remove(&output->old_thumbnail_update.link);
    output->old_thumbnail = NULL;
}

static void handle_new_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    struct output_entry *output = wl_container_of(listener, output, new_thumbnail_update);
    if (output->new_thumbnail_texture) {
        wlr_texture_destroy(output->new_thumbnail_texture);
    }
    struct wlr_renderer *renderer = output->effect->manager->server->renderer;
    output->new_thumbnail_texture = wlr_texture_from_buffer(renderer, event->buffer);
}

static void handle_new_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct output_entry *output = wl_container_of(listener, output, new_thumbnail_destroy);

    wl_list_remove(&output->new_thumbnail_destroy.link);
    wl_list_remove(&output->new_thumbnail_update.link);
    output->new_thumbnail = NULL;
}

static struct output_transform_effect *transform_effect = NULL;

// direct call from output.c not from ky_output transform event
bool output_add_transform_effect(struct kywc_output *kywc_output,
                                 struct kywc_output_state *old_state,
                                 struct kywc_output_state *new_state)
{
    if (!transform_effect) {
        return false;
    }

    struct ky_scene_output *scene_output = output_from_kywc_output(kywc_output)->scene_output;
    if (!scene_output) {
        return false;
    }

    // prevent duplicate add
    struct ky_scene *scene = transform_effect->manager->server->scene;
    struct effect_entity *entity = ky_scene_find_effect_entity(scene, transform_effect->effect);
    if (entity) {
        struct output_entry *output = entity->user_data;
        if (output->output == scene_output) {
            return false;
        }
    }

    struct thumbnail *output_old_thumbnail =
        thumbnail_create_from_output(scene_output, old_state, 1.0f);
    if (!output_old_thumbnail) {
        return false;
    }

    struct thumbnail *output_new_thumbnail =
        thumbnail_create_from_output(scene_output, new_state, 1.0f);
    if (!output_new_thumbnail) {
        goto failed;
    }

    entity = ky_scene_add_effect(scene, transform_effect->effect);
    if (!entity) {
        goto failed;
    }

    struct output_entry *output =
        output_entry_create(transform_effect, scene_output, old_state->transform);
    if (!output) {
        effect_entity_destroy(entity);
        goto failed;
    }
    output->effect_entity = entity;
    entity->user_data = output;

    // screenshot before transform
    output->old_thumbnail = output_old_thumbnail;
    output->old_thumbnail_update.notify = handle_old_thumbnail_update;
    thumbnail_add_update_listener(output->old_thumbnail, &output->old_thumbnail_update);
    output->old_thumbnail_destroy.notify = handle_old_thumbnail_destroy;
    thumbnail_add_destroy_listener(output->old_thumbnail, &output->old_thumbnail_destroy);
    // call once update and remove
    thumbnail_update(output->old_thumbnail);
    thumbnail_destroy(output->old_thumbnail);

    // screenshot after transform
    output->new_thumbnail = output_new_thumbnail;
    output->new_thumbnail_update.notify = handle_new_thumbnail_update;
    thumbnail_add_update_listener(output->new_thumbnail, &output->new_thumbnail_update);
    output->new_thumbnail_destroy.notify = handle_new_thumbnail_destroy;
    thumbnail_add_destroy_listener(output->new_thumbnail, &output->new_thumbnail_destroy);
    // unlike old thumbnail, new thumbnail need wait multiple update. so remove later

    // create animate
    output_entry_start_animate(output);

    // trigger render event
    ky_scene_output_damage_whole(scene_output);

    return true;

failed:
    if (output_new_thumbnail) {
        thumbnail_destroy(output_new_thumbnail);
    }
    thumbnail_destroy(output_old_thumbnail);
    return false;
}

static bool frame_render_pre(struct effect_entity *entity, struct ky_scene_output *scene_output)
{
    struct output_entry *output = entity->user_data;
    if (output->output != scene_output) {
        return true;
    }

    struct output_transform_effect *effect = output->effect;

    // timer
    uint32_t diff_time = current_time_msec() - output->start_time;
    if (diff_time > effect->animate_duration) {
        effect_entity_destroy(output->effect_entity);
    } else {
        float t = diff_time / (float)effect->animate_duration;
        // easing function
        float factor = t * t;
        // lerp
        output->angle = output->start_angle + factor * (output->end_angle - output->start_angle);
        output->width = output->start_width + factor * (output->end_width - output->start_width);
        output->height =
            output->start_height + factor * (output->end_height - output->start_height);
        output->fade_alpha = CLAMP((factor - 0.5f) * 2.0f, 0.0f, 1.0f);
    }

    return true;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct output_entry *output = entity->user_data;
    if (output->output != target->output) {
        return true;
    }

    // add damage to trigger render event
    ky_scene_output_damage_whole(output->output);
    return true;
}

static bool frame_render_end(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct output_entry *output = entity->user_data;
    if (output->output != target->output) {
        return true;
    }

    if (!output->old_thumbnail_texture || !output->new_thumbnail_texture) {
        return true;
    }

    struct wlr_box dst_box = {
        .x = 0,
        .y = 0,
        .width = output->output->output->width,
        .height = output->output->output->height,
    };

    // background color
    wlr_render_pass_add_rect(target->render_pass,
                             &(struct wlr_render_rect_options){
                                 .box = dst_box,
                                 .color = { .r = 0.0, .g = 0.0, .b = 0.0, .a = 1.0 },
                             });

    // size transformed
    if (output->start_width != output->end_width) {
        dst_box.x = (dst_box.width - output->width) / 2;
        dst_box.y = (dst_box.height - output->height) / 2;
        dst_box.width = output->width;
        dst_box.height = output->height;
    }

    ky_opengl_render_pass_add_texture(target->render_pass,
                                      &(struct ky_render_texture_options){
                                          .base = { .texture = output->old_thumbnail_texture,
                                                    .alpha = &(float){ 1.0f },
                                                    .dst_box = dst_box },
                                          .rotation_angle = output->angle,
                                      });

    ky_opengl_render_pass_add_texture(target->render_pass,
                                      &(struct ky_render_texture_options){
                                          .base = { .texture = output->new_thumbnail_texture,
                                                    .transform = output->new_thumbnail_transform,
                                                    .alpha = &output->fade_alpha,
                                                    .dst_box = dst_box },
                                          .rotation_angle = output->angle,
                                      });

    return true;
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&transform_effect->destroy.link);
    free(transform_effect);
    transform_effect = NULL;
}

static const struct effect_interface effect_impl = {
    .entity_destroy = entity_destroy,
    .frame_render_pre = frame_render_pre,
    .frame_render_end = frame_render_end,
    .frame_render_post = frame_render_post,
};

bool output_transform_effect_create(struct effect_manager *manager)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return false;
    }

    transform_effect = calloc(1, sizeof(*transform_effect));
    if (!transform_effect) {
        return false;
    }

    transform_effect->effect = effect_create("output_transform", 105, true, &effect_impl, NULL);
    if (!transform_effect->effect) {
        free(transform_effect);
        transform_effect = NULL;
        return false;
    }

    transform_effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&transform_effect->effect->events.destroy, &transform_effect->destroy);
    transform_effect->manager = manager;
    transform_effect->animate_duration = 500;

    return true;
}
