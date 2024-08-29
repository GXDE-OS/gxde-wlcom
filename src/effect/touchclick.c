// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_touch.h>

#include <kywc/log.h>

#include "effect_p.h"
#include "input/cursor.h"
#include "input/input.h"
#include "input/seat.h"
#include "render/opengl.h"
#include "scene/scene.h"
#include "util/matrix.h"
#include "util/time.h"

#include "touchclick_frag.h"
#include "touchclick_vert.h"

struct touch_finger {
    struct wl_list link;
    int32_t id, x, y;
    uint32_t start_time;
    float radius;
    float attenuation;
};

struct touchclick_effect_config {
    int32_t animate_duration;
    float start_radius;
    float end_radius;
    float start_attenuation;
    float end_attenuation;
    int32_t shape_width;
    int32_t shape_height;
};

struct seat_touch {
    struct wl_list link;
    struct touchclick_effect *effect;
    struct wl_listener touch_down;
    struct wl_listener destroy;
};

struct touchclick_effect {
    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct effect_manager *manager;
    struct touchclick_effect_config config;

    struct wl_listener new_seat;
    struct wl_list seat_touchs;
    // multi-touch
    struct wl_list touch_points;
};

struct touchclick_gl_shader {
    int32_t program;
    // vs
    GLint in_uv;
    GLint uv2ndc;
    // fs
    GLint radius;
    GLint attenuation;
};
static struct touchclick_gl_shader gl_shader = { 0 };

static void create_opengl_shader(struct ky_opengl_renderer *renderer)
{
    GLuint prog = ky_opengl_create_program(renderer, touchclick_vert, touchclick_frag);
    if (prog == 0) {
        return;
    }
    gl_shader.program = prog;

    gl_shader.in_uv = glGetAttribLocation(prog, "inUV");
    gl_shader.uv2ndc = glGetUniformLocation(prog, "uv2ndc");
    gl_shader.radius = glGetUniformLocation(prog, "radius");
    gl_shader.attenuation = glGetUniformLocation(prog, "attenuation");
}

static void add_damage(struct touchclick_effect *effect)
{
    struct ky_scene *scene = effect->manager->server->scene;
    struct touch_finger *finger;
    wl_list_for_each(finger, &effect->touch_points, link) {
        pixman_region32_t region;
        pixman_region32_init_rect(&region, finger->x, finger->y, effect->config.shape_width,
                                  effect->config.shape_height);
        ky_scene_add_damage(scene, &region);
        pixman_region32_fini(&region);
    }
}

static void gl_render_finger_effect(struct touchclick_effect *effect, struct touch_finger *finger,
                                    struct ky_scene_render_target *target)
{
    static GLfloat verts[8] = {
        0.0f, 0.0f, // v0
        1.0f, 0.0f, // v1
        1.0f, 1.0f, // v2
        0.0f, 1.0f, // v3
    };

    struct wlr_box box = {
        .x = finger->x - target->logical.x,
        .y = finger->y - target->logical.y,
        .width = effect->config.shape_width,
        .height = effect->config.shape_height,
    };
    ky_scene_render_box(&box, target);

    struct ky_mat3 projection;
    ky_mat3_framebuffer_to_ndc(&projection, target->buffer->width, target->buffer->height);
    struct ky_mat3 uv2pos;
    ky_mat3_init_scale_translate(&uv2pos, box.width, box.height, box.x, box.y);
    struct ky_mat3 uv2ndc;
    ky_mat3_multiply(&projection, &uv2pos, &uv2ndc);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glUseProgram(gl_shader.program);
    glEnableVertexAttribArray(gl_shader.in_uv);
    glVertexAttribPointer(gl_shader.in_uv, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glUniformMatrix3fv(gl_shader.uv2ndc, 1, GL_FALSE, uv2ndc.matrix);
    glUniform1f(gl_shader.radius, finger->radius);
    glUniform1f(gl_shader.attenuation, finger->attenuation);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glUseProgram(0);
    glDisableVertexAttribArray(gl_shader.in_uv);
}

static bool frame_render_pre(struct effect_entity *entity, struct ky_scene_output *output)
{
    struct touchclick_effect *effect = entity->user_data;
    // timer
    struct touch_finger *finger, *tmp;
    wl_list_for_each_safe(finger, tmp, &effect->touch_points, link) {
        uint32_t diff_time = current_time_msec() - finger->start_time;
        if (diff_time > (uint32_t)effect->config.animate_duration) {
            wl_list_remove(&finger->link);
            free(finger);
        } else {
            float t = diff_time / (float)effect->config.animate_duration;
            // easing function
            float factor = 1.f - powf(1.f - t, 4.f);
            // lerp
            finger->radius = effect->config.start_radius +
                             factor * (effect->config.end_radius - effect->config.start_radius);
            finger->attenuation =
                effect->config.start_attenuation +
                factor * (effect->config.end_attenuation - effect->config.start_attenuation);
        }
    }
    return true;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct touchclick_effect *effect = entity->user_data;
    // add damage to trigger render event
    add_damage(effect);
    return true;
}

static bool frame_render_end(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct touchclick_effect *effect = entity->user_data;
    if (gl_shader.program == 0) {
        struct ky_opengl_renderer *renderer =
            ky_opengl_renderer_from_wlr_renderer(effect->manager->server->renderer);
        create_opengl_shader(renderer);
        if (gl_shader.program <= 0) {
            return true;
        }
    }

    struct touch_finger *finger;
    wl_list_for_each(finger, &effect->touch_points, link) {
        gl_render_finger_effect(effect, finger, target);
    }

    return true;
}

static void handle_touch_down(struct wl_listener *listener, void *data)
{
    struct wlr_touch_down_event *event = data;
    struct input *input = input_from_wlr_input(&event->touch->base);
    struct wlr_cursor *wlr_cursor = input->seat->cursor->wlr_cursor;
    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(wlr_cursor, input->wlr_input, event->x, event->y, &lx,
                                         &ly);
    int x = roundf(lx);
    int y = roundf(ly);

    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_down);
    struct touchclick_effect *effect = seat_touch->effect;

    // new touch
    if (event->touch_id == 0) {
        struct touch_finger *finger, *tmp;
        wl_list_for_each_safe(finger, tmp, &effect->touch_points, link) {
            wl_list_remove(&finger->link);
            free(finger);
        }
    }

    // record multi-touch
    bool find_id = false;
    struct touch_finger *finger;
    wl_list_for_each(finger, &effect->touch_points, link) {
        if (finger->id == event->touch_id) {
            find_id = true;
            break;
        }
    }
    if (!find_id) {
        struct touch_finger *finger = calloc(1, sizeof(struct touch_finger));
        finger->id = event->touch_id;
        finger->x = x - effect->config.shape_width * 0.5f;
        finger->y = y - effect->config.shape_height * 0.5f;
        finger->start_time = current_time_msec();
        wl_list_insert(&effect->touch_points, &finger->link);
    }

    add_damage(effect);
}

static void seat_touch_destroy(struct seat_touch *seat_touch)
{
    wl_list_remove(&seat_touch->link);
    wl_list_remove(&seat_touch->touch_down.link);
    wl_list_remove(&seat_touch->destroy.link);
    free(seat_touch);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, destroy);
    seat_touch_destroy(seat_touch);
}

static void seat_touch_create(struct touchclick_effect *effect, struct seat *seat)
{
    struct seat_touch *seat_touch = calloc(1, sizeof(*seat_touch));
    if (!seat_touch) {
        return;
    }

    wl_list_insert(&effect->seat_touchs, &seat_touch->link);
    seat_touch->effect = effect;
    seat_touch->touch_down.notify = handle_touch_down;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_down, &seat_touch->touch_down);
    seat_touch->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &seat_touch->destroy);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct touchclick_effect *effect = wl_container_of(listener, effect, new_seat);
    struct seat *seat = data;
    seat_touch_create(effect, seat);
}

static void handle_seat(struct seat *seat, int index, void *data)
{
    struct touchclick_effect *effect = data;
    seat_touch_create(effect, seat);
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct touchclick_effect *effect = wl_container_of(listener, effect, enable);

    input_manager_for_each_seat(handle_seat, effect);

    effect->new_seat.notify = handle_new_seat;
    seat_add_new_listener(&effect->new_seat);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct touchclick_effect *effect = wl_container_of(listener, effect, disable);

    wl_list_remove(&effect->new_seat.link);
    wl_list_init(&effect->new_seat.link);

    struct seat_touch *seat_touch, *tmp0;
    wl_list_for_each_safe(seat_touch, tmp0, &effect->seat_touchs, link) {
        seat_touch_destroy(seat_touch);
    }

    struct touch_finger *finger, *tmp1;
    wl_list_for_each_safe(finger, tmp1, &effect->touch_points, link) {
        wl_list_remove(&finger->link);
        free(finger);
    }
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct touchclick_effect *effect = wl_container_of(listener, effect, destroy);
    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    free(effect);
}

static const struct effect_interface effect_impl = {
    .frame_render_pre = frame_render_pre,
    .frame_render_end = frame_render_end,
    .frame_render_post = frame_render_post,
};

bool touchclick_effect_create(struct effect_manager *manager)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return false;
    }

    struct touchclick_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create("touchclick", 100, true, &effect_impl, NULL);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->manager = manager;
    effect->config.animate_duration = 500;
    effect->config.start_radius = 0.5f;
    effect->config.end_radius = 0.0f;
    effect->config.start_attenuation = 0.4f;
    effect->config.end_attenuation = 0.8f;
    effect->config.shape_width = 100;
    effect->config.shape_height = 100;

    wl_list_init(&effect->new_seat.link);
    wl_list_init(&effect->seat_touchs);
    wl_list_init(&effect->touch_points);

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
