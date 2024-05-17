// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <float.h>
#include <stdbool.h>
#include <stdlib.h>

#include <wayland-util.h>
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
#include "util/vec2.h"

#include "touchtrail_frag.h"
#include "touchtrail_vert.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

struct touch_point {
    struct wl_list link;
    struct ky_vec2 point;
    uint32_t start_time;
};

struct touch_finger {
    struct wl_list link;
    int32_t id;
    struct wl_list touch_points;
    // bbox for compute damage region
    bool bbox_valid;
    struct ky_vec2 bbox_min;
    struct ky_vec2 bbox_max;
};

struct touchtrail_effect_config {
    float color[4];
    float thickness;
    uint32_t life_time;
};

struct seat_touch {
    struct wl_list link;
    struct touchtrail_effect *effect;
    struct wl_listener touch_down;
    struct wl_listener touch_motion;
    struct wl_listener destroy;
};

struct touchtrail_effect {
    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct effect_manager *manager;
    struct touchtrail_effect_config config;

    struct wl_listener new_seat;
    struct wl_list seat_touchs;

    struct wl_list touch_fingers;

    pixman_region32_t damage;
};

static struct touchtrail_gl_shader {
    int32_t program;
    // vs
    GLint position;
    GLint logic2ndc;
    // fs
    GLint color;
} gl_shader = { 0 };

static void create_opengl_shader(struct ky_opengl_renderer *renderer)
{
    GLuint prog = ky_opengl_create_program(renderer, touchtrail_vert, touchtrail_frag);
    if (prog == 0) {
        return;
    }
    gl_shader.program = prog;

    gl_shader.position = glGetAttribLocation(prog, "position");
    gl_shader.logic2ndc = glGetUniformLocation(prog, "logic2ndc");
    gl_shader.color = glGetUniformLocation(prog, "color");
}

static void free_touch_finger(struct touchtrail_effect *effect)
{
    struct touch_finger *finger, *tmp0;
    wl_list_for_each_safe(finger, tmp0, &effect->touch_fingers, link) {
        struct touch_point *point, *tmp1;
        wl_list_for_each_safe(point, tmp1, &finger->touch_points, link) {
            wl_list_remove(&point->link);
            free(point);
        }

        wl_list_remove(&finger->link);
        free(finger);
    }
}

static void add_damage(struct touchtrail_effect *effect)
{
    if (pixman_region32_not_empty(&effect->damage)) {
        struct ky_scene *scene = effect->manager->server->scene;
        ky_scene_add_damage(scene, &effect->damage);
    }
}

// reference:
// simplify_path    simple_threshold = 35.0f;
// https://psimpl.sourceforge.net/radial-distance.html
// smooth_path  smooth_iterations = 2;
// https://www.educative.io/answers/what-is-chaikins-algorithm

// path.length > 1
// vertices buffer size = path.length * 2
static void triangulate_path(struct wl_list *path, uint32_t path_len, float min_thickness,
                             float max_thickness, struct ky_vec2 *vertices)
{
    size_t index = 0;
    // first point as tail
    struct touch_point *first_point = wl_container_of(path->prev, first_point, link);
    vertices[index++] = first_point->point;

    // thickness small -> big
    struct touch_point *point;
    wl_list_for_each_reverse(point, path, link) {
        struct touch_point *point_next = wl_container_of(point->link.prev, point_next, link);
        if (&point_next->link != path) {
            float thickness = MAX(min_thickness, max_thickness * (index - 1 * 0.5f) / path_len);
            struct ky_vec2 dir;
            ky_vec2_sub(&point_next->point, &point->point, &dir);
            ky_vec2_normalize(&dir);
            struct ky_vec2 perp_dir;
            ky_vec2_perpendicular(&dir, &perp_dir);
            ky_vec2_muls(&perp_dir, thickness * 0.5f);

            ky_vec2_sub(&point_next->point, &perp_dir, &vertices[index++]);
            ky_vec2_add(&point_next->point, &perp_dir, &vertices[index++]);
        }
    }

    // head expand triangle
    struct touch_point *last_point = wl_container_of(point->link.next, last_point, link);
    struct touch_point *last2_point = wl_container_of(last_point->link.next, last2_point, link);
    struct ky_vec2 dir;
    ky_vec2_sub(&last_point->point, &last2_point->point, &dir);
    ky_vec2_normalize(&dir);
    ky_vec2_muls(&dir, max_thickness);
    ky_vec2_add(&last_point->point, &dir, &vertices[index]);
}

static void compute_finger_boundbox(struct touch_finger *finger, struct ky_vec2 *verts,
                                    uint32_t verts_len)
{
    struct ky_vec2 *min = &finger->bbox_min;
    struct ky_vec2 *max = &finger->bbox_max;
    min->x = FLT_MAX;
    min->y = FLT_MAX;
    max->x = FLT_MIN;
    max->y = FLT_MIN;
    for (uint32_t i = 0; i < verts_len; i++) {
        ky_vec2_min(&verts[i], min, min);
        ky_vec2_max(&verts[i], max, max);
    }
}

static void gl_render_finger_effect(struct touchtrail_effect *effect, struct touch_finger *finger,
                                    struct ky_scene_render_target *target)
{
    int points_len = wl_list_length(&finger->touch_points);
    if (points_len < 2) {
        return;
    }

    // too many points. use gpu compute final pos, input logic pos
    uint32_t verts_len = points_len * 2;
    struct ky_vec2 verts[verts_len];
    triangulate_path(&finger->touch_points, points_len, 4.0f, effect->config.thickness, verts);
    compute_finger_boundbox(finger, verts, verts_len);
    finger->bbox_valid = true;

    struct ky_mat3 transform;
    ky_mat3_identity(&transform);
    ky_mat3_init_translate(&transform, -target->logical.x, -target->logical.y);
    struct ky_mat3 to_ndc;
    ky_mat3_logic_to_ndc(&to_ndc, target->logical.width, target->logical.height, target->transform);
    struct ky_mat3 logic2ndc;
    ky_mat3_multiply(&to_ndc, &transform, &logic2ndc);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glUseProgram(gl_shader.program);
    glEnableVertexAttribArray(gl_shader.position);
    glVertexAttribPointer(gl_shader.position, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glUniformMatrix3fv(gl_shader.logic2ndc, 1, GL_FALSE, logic2ndc.matrix);
    glUniform4fv(gl_shader.color, 1, effect->config.color);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, verts_len);
    glUseProgram(0);
    glDisableVertexAttribArray(gl_shader.position);
}

static bool frame_render_pre(struct effect_entity *entity, struct ky_scene_output *output)
{
    struct touchtrail_effect *effect = entity->usr_data;
    // timer
    struct touch_finger *finger, *tmp0;
    wl_list_for_each_safe(finger, tmp0, &effect->touch_fingers, link) {
        struct touch_point *point, *tmp1;
        wl_list_for_each_safe(point, tmp1, &finger->touch_points, link) {
            uint32_t diff_time = current_time_msec() - point->start_time;
            if (diff_time > effect->config.life_time) {
                wl_list_remove(&point->link);
                free(point);
            }
        }

        if (wl_list_empty(&finger->touch_points)) {
            wl_list_remove(&finger->link);
            free(finger);
            pixman_region32_clear(&effect->damage);
        }
    }

    return true;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct touchtrail_effect *effect = entity->usr_data;
    // add damage to trigger render event
    add_damage(effect);
    return true;
}

static bool frame_render_end(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct touchtrail_effect *effect = entity->usr_data;
    if (gl_shader.program == 0) {
        struct ky_opengl_renderer *renderer =
            ky_opengl_renderer_from_wlr_renderer(effect->manager->server->renderer);
        create_opengl_shader(renderer);
        if (gl_shader.program <= 0) {
            return true;
        }
    }

    bool bbox_valid = false;
    struct ky_vec2 bbox_min;
    struct ky_vec2 bbox_max;
    bbox_min.x = FLT_MAX;
    bbox_min.y = FLT_MAX;
    bbox_max.x = FLT_MIN;
    bbox_max.y = FLT_MIN;

    struct touch_finger *finger = NULL;
    wl_list_for_each(finger, &effect->touch_fingers, link) {
        gl_render_finger_effect(effect, finger, target);

        if (finger->bbox_valid) {
            ky_vec2_min(&finger->bbox_min, &bbox_min, &bbox_min);
            ky_vec2_max(&finger->bbox_max, &bbox_max, &bbox_max);
            bbox_valid = true;
        }
    }

    if (bbox_valid) {
        pixman_region32_fini(&effect->damage);
        int x = bbox_min.x - 2;
        int y = bbox_min.y - 2;
        int width = bbox_max.x - bbox_min.x + 4;
        int height = bbox_max.y - bbox_min.y + 4;
        pixman_region32_init_rect(&effect->damage, x, y, width, height);
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

    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_down);
    struct touchtrail_effect *effect = seat_touch->effect;

    // new touch
    if (event->touch_id == 0) {
        free_touch_finger(effect);
        pixman_region32_clear(&effect->damage);
    }

    // record multi-touch
    bool find_id = false;
    struct touch_finger *finger;
    wl_list_for_each(finger, &effect->touch_fingers, link) {
        if (finger->id == event->touch_id) {
            find_id = true;
            break;
        }
    }
    if (!find_id) {
        struct touch_finger *finger = calloc(1, sizeof(struct touch_finger));
        wl_list_insert(&effect->touch_fingers, &finger->link);
        finger->id = event->touch_id;
        finger->bbox_valid = false;
        wl_list_init(&finger->touch_points);
        // insert current point
        struct touch_point *point = calloc(1, sizeof(struct touch_point));
        point->point.x = lx;
        point->point.y = ly;
        point->start_time = current_time_msec();
        wl_list_insert(&finger->touch_points, &point->link);
    }

    add_damage(effect);
}

static void handle_touch_motion(struct wl_listener *listener, void *data)
{
    struct wlr_touch_motion_event *event = data;
    struct input *input = input_from_wlr_input(&event->touch->base);
    struct wlr_cursor *wlr_cursor = input->seat->cursor->wlr_cursor;
    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(wlr_cursor, input->wlr_input, event->x, event->y, &lx,
                                         &ly);

    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_motion);
    struct touchtrail_effect *effect = seat_touch->effect;

    bool find_id = false;
    struct touch_finger *finger = NULL;
    wl_list_for_each(finger, &effect->touch_fingers, link) {
        if (finger->id == event->touch_id) {
            find_id = true;
            break;
        }
    }

    if (find_id) {
        struct touch_point *point = calloc(1, sizeof(struct touch_point));
        point->point.x = lx;
        point->point.y = ly;
        point->start_time = current_time_msec();
        wl_list_insert(&finger->touch_points, &point->link);
    }
}

static void seat_touch_destroy(struct seat_touch *seat_touch)
{
    wl_list_remove(&seat_touch->link);
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

static void seat_touch_create(struct touchtrail_effect *effect, struct seat *seat)
{
    struct seat_touch *seat_touch = calloc(1, sizeof(*seat_touch));
    if (!seat_touch) {
        return;
    }

    wl_list_insert(&effect->seat_touchs, &seat_touch->link);
    seat_touch->effect = effect;
    seat_touch->touch_down.notify = handle_touch_down;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_down, &seat_touch->touch_down);
    seat_touch->touch_motion.notify = handle_touch_motion;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_motion, &seat_touch->touch_motion);
    seat_touch->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &seat_touch->destroy);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct touchtrail_effect *effect = wl_container_of(listener, effect, new_seat);
    struct seat *seat = data;
    seat_touch_create(effect, seat);
}

static void handle_seat(struct seat *seat, int index, void *data)
{
    struct touchtrail_effect *effect = data;
    seat_touch_create(effect, seat);
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct touchtrail_effect *effect = wl_container_of(listener, effect, enable);

    input_manager_for_each_seat(handle_seat, effect);

    effect->new_seat.notify = handle_new_seat;
    seat_add_new_listener(&effect->new_seat);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct touchtrail_effect *effect = wl_container_of(listener, effect, disable);

    wl_list_remove(&effect->new_seat.link);
    wl_list_init(&effect->new_seat.link);

    struct seat_touch *seat_touch, *tmp0;
    wl_list_for_each_safe(seat_touch, tmp0, &effect->seat_touchs, link) {
        seat_touch_destroy(seat_touch);
    }

    free_touch_finger(effect);
    pixman_region32_clear(&effect->damage);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct touchtrail_effect *effect = wl_container_of(listener, effect, destroy);
    pixman_region32_fini(&effect->damage);
    free(effect);
}

static const struct effect_interface effect_impl = {
    .frame_render_pre = frame_render_pre,
    .frame_render_end = frame_render_end,
    .frame_render_post = frame_render_post,
};

bool touchtrail_effect_create(struct effect_manager *manager)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return false;
    }

    struct touchtrail_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create("touchtrail", 102, true, &effect_impl);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    effect->manager = manager;
    effect->config.color[0] = 1.0f;
    effect->config.color[1] = 1.0f;
    effect->config.color[2] = 1.0f;
    effect->config.color[3] = 0.4f;
    effect->config.thickness = 8.0f;
    effect->config.life_time = 200;

    wl_list_init(&effect->new_seat.link);
    wl_list_init(&effect->seat_touchs);
    wl_list_init(&effect->touch_fingers);
    pixman_region32_init(&effect->damage);

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
    entity->usr_data = effect;

    return true;
}
