// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <float.h>
#include <stdbool.h>
#include <stdlib.h>

#include "effect/shape.h"
#include "effect_p.h"
#include "render/opengl.h"
#include "scene/scene.h"
#include "util/matrix.h"
#include "util/time.h"
#include "util/vec2.h"

#include "shape_color_frag.h"
#include "shape_color_vert.h"

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

struct trail_point {
    struct wl_list link;
    struct ky_vec2 point;
    uint32_t start_time;
};

struct trail_info {
    struct wl_list link;
    int32_t id;
    struct wl_list points;
    // bbox for compute damage region
    bool bbox_valid;
    struct ky_vec2 bbox_min;
    struct ky_vec2 bbox_max;
};

struct trail_effect {
    struct effect *base;
    struct effect_manager *manager;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    const struct trail_effect_interface *impl;
    struct trail_effect_options options;
    void *user_data;

    struct wl_list trail_infos;
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

static bool create_opengl_shader(struct ky_opengl_renderer *renderer)
{
    if (gl_shader.program == 0) {
        GLuint prog = ky_opengl_create_program(renderer, shape_color_vert, shape_color_frag);
        if (prog == 0) {
            return false;
        }
        gl_shader.program = prog;

        gl_shader.position = glGetAttribLocation(prog, "position");
        gl_shader.logic2ndc = glGetUniformLocation(prog, "logic2ndc");
        gl_shader.color = glGetUniformLocation(prog, "color");
    }

    return true;
}

static void free_trail_infos(struct trail_effect *effect)
{
    struct trail_info *finger, *tmp0;
    wl_list_for_each_safe(finger, tmp0, &effect->trail_infos, link) {
        struct trail_point *point, *tmp1;
        wl_list_for_each_safe(point, tmp1, &finger->points, link) {
            wl_list_remove(&point->link);
            free(point);
        }

        wl_list_remove(&finger->link);
        free(finger);
    }
}

static void add_damage(struct trail_effect *effect)
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
    struct trail_point *first_point = wl_container_of(path->prev, first_point, link);
    vertices[index++] = first_point->point;

    // thickness small -> big
    struct trail_point *point;
    wl_list_for_each_reverse(point, path, link) {
        struct trail_point *point_next = wl_container_of(point->link.prev, point_next, link);
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
    struct trail_point *last_point = wl_container_of(point->link.next, last_point, link);
    struct trail_point *last2_point = wl_container_of(last_point->link.next, last2_point, link);
    struct ky_vec2 dir;
    ky_vec2_sub(&last_point->point, &last2_point->point, &dir);
    ky_vec2_normalize(&dir);
    ky_vec2_muls(&dir, max_thickness);
    ky_vec2_add(&last_point->point, &dir, &vertices[index]);
}

static void compute_finger_boundbox(struct trail_info *finger, struct ky_vec2 *verts,
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

static void gl_render_finger_effect(struct trail_effect *effect, struct trail_info *finger,
                                    struct ky_scene_render_target *target)
{
    int points_len = wl_list_length(&finger->points);
    if (points_len < 2) {
        return;
    }

    // too many points. use gpu compute final pos, input logic pos
    uint32_t verts_len = points_len * 2;
    struct ky_vec2 verts[verts_len];
    triangulate_path(&finger->points, points_len, 4.0f, effect->options.thickness, verts);
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
    glUniform4fv(gl_shader.color, 1, effect->options.color);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, verts_len);
    glUseProgram(0);
    glDisableVertexAttribArray(gl_shader.position);
}

static bool frame_render_begin(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct trail_effect *effect = entity->user_data;
    // timer
    struct trail_info *finger, *tmp0;
    wl_list_for_each_safe(finger, tmp0, &effect->trail_infos, link) {
        struct trail_point *point, *tmp1;
        wl_list_for_each_safe(point, tmp1, &finger->points, link) {
            uint32_t diff_time = current_time_msec() - point->start_time;
            if (diff_time > effect->options.life_time) {
                wl_list_remove(&point->link);
                free(point);
            }
        }

        if (wl_list_empty(&finger->points)) {
            wl_list_remove(&finger->link);
            free(finger);
            pixman_region32_clear(&effect->damage);
        }
    }

    return true;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct trail_effect *effect = entity->user_data;
    // add damage to trigger render event
    add_damage(effect);
    return true;
}

static bool frame_render_end(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct trail_effect *effect = entity->user_data;
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

    struct trail_info *finger = NULL;
    wl_list_for_each(finger, &effect->trail_infos, link) {
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

static bool handle_effect_configure(struct effect *effect, const struct effect_option *option)
{
    if (effect_option_is_enabled_option(option)) {
        return true;
    }

    return false;
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct trail_effect *effect = wl_container_of(listener, effect, enable);

    if (effect->impl->enable) {
        effect->impl->enable(effect, effect->user_data);
    }
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct trail_effect *effect = wl_container_of(listener, effect, disable);

    if (effect->impl->disable) {
        effect->impl->disable(effect, effect->user_data);
    }

    free_trail_infos(effect);
    pixman_region32_clear(&effect->damage);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct trail_effect *effect = wl_container_of(listener, effect, destroy);

    if (effect->impl->destroy) {
        effect->impl->destroy(effect, effect->user_data);
    }

    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    pixman_region32_fini(&effect->damage);
    free(effect);
}

static const struct effect_interface effect_impl = {
    .frame_render_begin = frame_render_begin,
    .frame_render_end = frame_render_end,
    .frame_render_post = frame_render_post,
    .configure = handle_effect_configure,
};

struct trail_effect *trail_effect_create(struct effect_manager *manager,
                                         struct trail_effect_options *options,
                                         const struct trail_effect_interface *impl,
                                         const char *name, int priority, bool enabled,
                                         void *user_data)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return NULL;
    }

    struct trail_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return NULL;
    }

    effect->base = effect_create(name, priority, enabled, &effect_impl, NULL);
    if (!effect->base) {
        free(effect);
        return NULL;
    }

    struct effect_entity *entity = ky_scene_add_effect(manager->server->scene, effect->base);
    if (!entity) {
        effect_destroy(effect->base);
        free(effect);
        return NULL;
    }

    entity->user_data = effect;
    effect->manager = manager;
    effect->enable.notify = handle_effect_enable;
    wl_signal_add(&effect->base->events.enable, &effect->enable);
    effect->disable.notify = handle_effect_disable;
    wl_signal_add(&effect->base->events.disable, &effect->disable);
    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->base->events.destroy, &effect->destroy);
    effect->impl = impl;
    effect->options = *options;
    effect->user_data = user_data;
    wl_list_init(&effect->trail_infos);
    pixman_region32_init(&effect->damage);

    if (effect->base->enabled) {
        handle_effect_enable(&effect->enable, NULL);
    }

    return effect;
}

void trail_effect_add_trail(struct trail_effect *effect, int32_t id, int32_t start_x,
                            int32_t start_y)
{
    // record multi-touch
    bool find_id = false;
    struct trail_info *finger;
    wl_list_for_each(finger, &effect->trail_infos, link) {
        if (finger->id == id) {
            find_id = true;
            break;
        }
    }
    if (!find_id) {
        struct trail_info *finger = calloc(1, sizeof(struct trail_info));
        wl_list_insert(&effect->trail_infos, &finger->link);
        finger->id = id;
        finger->bbox_valid = false;
        wl_list_init(&finger->points);
        // insert current point
        struct trail_point *point = calloc(1, sizeof(struct trail_point));
        point->point.x = start_x;
        point->point.y = start_y;
        point->start_time = current_time_msec();
        wl_list_insert(&finger->points, &point->link);

        add_damage(effect);
    }
}

void trail_effect_trail_add_point(struct trail_effect *effect, int32_t id, int32_t x, int32_t y)
{
    bool find_id = false;
    struct trail_info *finger = NULL;
    wl_list_for_each(finger, &effect->trail_infos, link) {
        if (finger->id == id) {
            find_id = true;
            break;
        }
    }

    if (find_id) {
        struct trail_point *point = calloc(1, sizeof(struct trail_point));
        point->point.x = x;
        point->point.y = y;
        point->start_time = current_time_msec();
        wl_list_insert(&finger->points, &point->link);
    }
}

void trail_effect_remove_all_trails(struct trail_effect *effect)
{
    free_trail_infos(effect);
    pixman_region32_clear(&effect->damage);
}