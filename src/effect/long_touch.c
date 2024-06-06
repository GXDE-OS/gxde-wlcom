// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdbool.h>
#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_touch.h>

#include "effect_p.h"
#include "input/cursor.h"
#include "input/input.h"
#include "input/seat.h"
#include "render/opengl.h"
#include "scene/scene.h"
#include "util/matrix.h"
#include "util/time.h"

#include "long_touch_frag.h"
#include "long_touch_vert.h"

#define M_PI 3.14159265358979323846

struct touch_finger {
    struct wl_list link;
    double raw_x, raw_y;
    int32_t x, y;
    uint32_t start_time;
    bool rendering;
    float angle;
};

struct long_touch_effect_config {
    float anti_acciden; // raw input pos 0.0~1.0
    uint32_t animate_delay;
    uint32_t animate_duration;
    float start_angle;
    float end_angle;
    int32_t shape_width;
    int32_t shape_height;
};

struct seat_touch {
    struct wl_list link;
    struct long_touch_effect *effect;
    struct wl_listener touch_up;
    struct wl_listener touch_down;
    struct wl_listener touch_motion;
    struct wl_listener destroy;
};

struct long_touch_effect {
    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct effect_manager *manager;
    struct long_touch_effect_config config;

    struct wl_listener new_seat;
    struct wl_list seat_touchs;

    struct touch_finger *touch_finger;
};

static struct long_touch_gl_shader {
    int32_t program;
    // vs
    GLint in_uv;
    GLint uv2ndc;
    GLint output_invert;
    // fs
    GLint angle;
} gl_shader = { 0 };

static void create_opengl_shader(struct ky_opengl_renderer *renderer)
{
    GLuint prog = ky_opengl_create_program(renderer, long_touch_vert, long_touch_frag);
    if (prog == 0) {
        return;
    }
    gl_shader.program = prog;

    gl_shader.in_uv = glGetAttribLocation(prog, "inUV");
    gl_shader.uv2ndc = glGetUniformLocation(prog, "uv2ndc");
    gl_shader.output_invert = glGetUniformLocation(prog, "outputInvert");
    gl_shader.angle = glGetUniformLocation(prog, "endAngle");
}

static void free_touch_finger(struct long_touch_effect *effect)
{
    free(effect->touch_finger);
    effect->touch_finger = NULL;
}

static void add_damage(struct long_touch_effect *effect)
{
    if (!effect->touch_finger) {
        return;
    }

    struct ky_scene *scene = effect->manager->server->scene;
    pixman_region32_t region;
    pixman_region32_init_rect(&region, effect->touch_finger->x, effect->touch_finger->y,
                              effect->config.shape_width, effect->config.shape_height);
    ky_scene_add_damage(scene, &region);
    pixman_region32_fini(&region);
}

static void gl_render_finger_effect(struct long_touch_effect *effect, struct touch_finger *finger,
                                    struct ky_scene_render_target *target)
{
    if (!effect->touch_finger->rendering) {
        return;
    }

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
    struct ky_mat3 output_invert;
    ky_mat3_invert_output_transform(&output_invert, target->transform);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glUseProgram(gl_shader.program);
    glEnableVertexAttribArray(gl_shader.in_uv);
    glVertexAttribPointer(gl_shader.in_uv, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glUniformMatrix3fv(gl_shader.uv2ndc, 1, GL_FALSE, uv2ndc.matrix);
    glUniformMatrix3fv(gl_shader.output_invert, 1, GL_FALSE, output_invert.matrix);
    glUniform1f(gl_shader.angle, finger->angle);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glUseProgram(0);
    glDisableVertexAttribArray(gl_shader.in_uv);
}

static bool frame_render_pre(struct effect_entity *entity, struct ky_scene_output *output)
{
    struct long_touch_effect *effect = entity->user_data;
    struct touch_finger *finger = effect->touch_finger;
    if (!finger) {
        return true;
    }

    // timer
    uint32_t time = current_time_msec();
    if (time > finger->start_time + effect->config.animate_delay) {
        uint32_t diff_time = time - effect->config.animate_delay - finger->start_time;
        if (diff_time > effect->config.animate_duration &&
            finger->angle > effect->config.end_angle) {
            free_touch_finger(effect);
        } else {
            finger->rendering = true;
            float t = diff_time / (float)effect->config.animate_duration;
            finger->angle = effect->config.start_angle +
                            t * (effect->config.end_angle - effect->config.start_angle);
        }
    }

    return true;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct long_touch_effect *effect = entity->user_data;
    // add damage to trigger render event
    add_damage(effect);
    return true;
}

static bool frame_render_end(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct long_touch_effect *effect = entity->user_data;
    if (gl_shader.program == 0) {
        struct ky_opengl_renderer *renderer =
            ky_opengl_renderer_from_wlr_renderer(effect->manager->server->renderer);
        create_opengl_shader(renderer);
        if (gl_shader.program <= 0) {
            return true;
        }
    }

    if (effect->touch_finger) {
        gl_render_finger_effect(effect, effect->touch_finger, target);
    }

    return true;
}

static void handle_touch_down(struct wl_listener *listener, void *data)
{
    struct wlr_touch_down_event *event = data;
    // first finger touch
    if (event->touch_id != 0) {
        return;
    }

    struct input *input = input_from_wlr_input(&event->touch->base);
    struct wlr_cursor *wlr_cursor = input->seat->cursor->wlr_cursor;
    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(wlr_cursor, input->wlr_input, event->x, event->y, &lx,
                                         &ly);
    int x = roundf(lx);
    int y = roundf(ly);

    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_down);
    struct long_touch_effect *effect = seat_touch->effect;

    if (effect->touch_finger) {
        free(effect->touch_finger);
    }

    effect->touch_finger = calloc(1, sizeof(struct touch_finger));
    effect->touch_finger->raw_x = event->x;
    effect->touch_finger->raw_y = event->y;
    effect->touch_finger->x = x - effect->config.shape_width * 0.5f;
    effect->touch_finger->y = y - effect->config.shape_height * 0.5f;
    effect->touch_finger->start_time = current_time_msec();
    effect->touch_finger->rendering = false;

    add_damage(effect);
}

static void handle_touch_up(struct wl_listener *listener, void *data)
{
    struct wlr_touch_up_event *event = data;
    // first finger touch
    if (event->touch_id != 0) {
        return;
    }

    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_up);
    free_touch_finger(seat_touch->effect);
}

static void handle_touch_motion(struct wl_listener *listener, void *data)
{
    struct wlr_touch_motion_event *event = data;
    // first finger touch
    if (event->touch_id != 0) {
        return;
    }

    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_motion);
    struct touch_finger *touch_finger = seat_touch->effect->touch_finger;
    float anti_acciden = seat_touch->effect->config.anti_acciden;
    if (touch_finger && (fabs(touch_finger->raw_x - event->x) > anti_acciden ||
                         fabs(touch_finger->raw_y - event->y) > anti_acciden)) {
        free_touch_finger(seat_touch->effect);
    }
}

static void seat_touch_destroy(struct seat_touch *seat_touch)
{
    wl_list_remove(&seat_touch->link);
    wl_list_remove(&seat_touch->touch_up.link);
    wl_list_remove(&seat_touch->touch_down.link);
    wl_list_remove(&seat_touch->touch_motion.link);
    wl_list_remove(&seat_touch->destroy.link);
    free(seat_touch);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, destroy);
    seat_touch_destroy(seat_touch);
}

static void seat_touch_create(struct long_touch_effect *effect, struct seat *seat)
{
    struct seat_touch *seat_touch = calloc(1, sizeof(*seat_touch));
    if (!seat_touch) {
        return;
    }

    wl_list_insert(&effect->seat_touchs, &seat_touch->link);
    seat_touch->effect = effect;
    seat_touch->touch_up.notify = handle_touch_up;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_up, &seat_touch->touch_up);
    seat_touch->touch_down.notify = handle_touch_down;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_down, &seat_touch->touch_down);
    seat_touch->touch_motion.notify = handle_touch_motion;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_motion, &seat_touch->touch_motion);
    seat_touch->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &seat_touch->destroy);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct long_touch_effect *effect = wl_container_of(listener, effect, new_seat);
    struct seat *seat = data;
    seat_touch_create(effect, seat);
}

static void handle_seat(struct seat *seat, int index, void *data)
{
    struct long_touch_effect *effect = data;
    seat_touch_create(effect, seat);
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct long_touch_effect *effect = wl_container_of(listener, effect, enable);

    input_manager_for_each_seat(handle_seat, effect);

    effect->new_seat.notify = handle_new_seat;
    seat_add_new_listener(&effect->new_seat);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct long_touch_effect *effect = wl_container_of(listener, effect, disable);

    wl_list_remove(&effect->new_seat.link);
    wl_list_init(&effect->new_seat.link);

    struct seat_touch *seat_touch, *tmp0;
    wl_list_for_each_safe(seat_touch, tmp0, &effect->seat_touchs, link) {
        seat_touch_destroy(seat_touch);
    }

    free_touch_finger(effect);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct long_touch_effect *effect = wl_container_of(listener, effect, destroy);
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

bool long_touch_effect_create(struct effect_manager *manager)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return false;
    }

    struct long_touch_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create("long_touch", 102, true, &effect_impl);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->manager = manager;
    effect->config.anti_acciden = 0.01f;
    effect->config.animate_delay = 200;
    effect->config.animate_duration = 800;
    effect->config.start_angle = 0.0f;
    effect->config.end_angle = 2.0f * M_PI; // 360 degree
    effect->config.shape_width = 100;
    effect->config.shape_height = 100;

    wl_list_init(&effect->new_seat.link);
    wl_list_init(&effect->seat_touchs);
    effect->touch_finger = NULL;

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
