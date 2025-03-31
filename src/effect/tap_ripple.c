// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_touch.h>

#include <kywc/log.h>

#include "effect/shape.h"
#include "effect_p.h"
#include "render/opengl.h"
#include "scene/scene.h"
#include "util/matrix.h"
#include "util/time.h"

#include "tap_ripple_frag.h"
#include "tap_ripple_vert.h"

struct tap_ripple_point {
    struct wl_list link;
    int32_t id, x, y;
    uint32_t start_time;
    float radius;
    float attenuation;
};

struct tap_ripple_effect {
    struct effect *base;
    struct effect_manager *manager;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    const struct tap_ripple_effect_interface *impl;
    struct tap_ripple_effect_options options;
    void *user_data;

    struct wl_list points;
};

struct tap_ripple_gl_shader {
    int32_t program;
    // vs
    GLint in_uv;
    GLint uv2ndc;
    // fs
    GLint radius;
    GLint attenuation;
    GLint color;
};
static struct tap_ripple_gl_shader gl_shader = { 0 };

static bool create_opengl_shader(struct ky_opengl_renderer *renderer)
{
    if (gl_shader.program == 0) {
        GLuint prog = ky_opengl_create_program(renderer, tap_ripple_vert, tap_ripple_frag);
        if (prog == 0) {
            return false;
        }
        gl_shader.program = prog;

        gl_shader.in_uv = glGetAttribLocation(prog, "inUV");
        gl_shader.uv2ndc = glGetUniformLocation(prog, "uv2ndc");
        gl_shader.radius = glGetUniformLocation(prog, "radius");
        gl_shader.attenuation = glGetUniformLocation(prog, "attenuation");
        gl_shader.color = glGetUniformLocation(prog, "color");
    }

    return true;
}

static void opengl_render_point(struct ky_scene_render_target *target,
                                struct tap_ripple_point *point,
                                struct tap_ripple_effect_options *options)
{
    static GLfloat verts[8] = {
        0.0f, 0.0f, // v0
        1.0f, 0.0f, // v1
        1.0f, 1.0f, // v2
        0.0f, 1.0f, // v3
    };

    struct wlr_box box = {
        .x = point->x - target->logical.x,
        .y = point->y - target->logical.y,
        .width = options->size,
        .height = options->size,
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
    glUniform1f(gl_shader.radius, point->radius);
    glUniform1f(gl_shader.attenuation, point->attenuation);
    glUniform3fv(gl_shader.color, 1, options->color);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glUseProgram(0);
    glDisableVertexAttribArray(gl_shader.in_uv);
}

static void free_points(struct tap_ripple_effect *effect)
{
    struct tap_ripple_point *point, *tmp;
    wl_list_for_each_safe(point, tmp, &effect->points, link) {
        wl_list_remove(&point->link);
        free(point);
    }
}

static void add_damage(struct tap_ripple_effect *effect)
{
    struct ky_scene *scene = effect->manager->server->scene;
    struct tap_ripple_point *point;
    wl_list_for_each(point, &effect->points, link) {
        pixman_region32_t region;
        pixman_region32_init_rect(&region, point->x, point->y, effect->options.size,
                                  effect->options.size);
        ky_scene_add_damage(scene, &region);
        pixman_region32_fini(&region);
    }
}

static bool frame_render_begin(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct tap_ripple_effect *effect = entity->user_data;
    // timer
    struct tap_ripple_point *point, *tmp;
    wl_list_for_each_safe(point, tmp, &effect->points, link) {
        uint32_t diff_time = current_time_msec() - point->start_time;
        if (diff_time > (uint32_t)effect->options.animate_duration) {
            wl_list_remove(&point->link);
            free(point);
        } else {
            float t = diff_time / (float)effect->options.animate_duration;
            // easing function
            float factor = 1.f - powf(1.f - t, 4.f);
            // lerp
            point->radius = effect->options.start_radius +
                            factor * (effect->options.end_radius - effect->options.start_radius);
            point->attenuation =
                effect->options.start_attenuation +
                factor * (effect->options.end_attenuation - effect->options.start_attenuation);
        }
    }
    return true;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct tap_ripple_effect *effect = entity->user_data;
    // add damage to trigger render event
    add_damage(effect);
    return true;
}

static bool frame_render_end(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct tap_ripple_effect *effect = entity->user_data;

    struct ky_opengl_renderer *renderer =
        ky_opengl_renderer_from_wlr_renderer(effect->manager->server->renderer);
    if (!create_opengl_shader(renderer)) {
        return true;
    }

    struct tap_ripple_point *point;
    wl_list_for_each(point, &effect->points, link) {
        opengl_render_point(target, point, &effect->options);
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
    struct tap_ripple_effect *effect = wl_container_of(listener, effect, enable);

    if (effect->impl->enable) {
        effect->impl->enable(effect, effect->user_data);
    }
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct tap_ripple_effect *effect = wl_container_of(listener, effect, disable);

    if (effect->impl->disable) {
        effect->impl->disable(effect, effect->user_data);
    }

    free_points(effect);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct tap_ripple_effect *effect = wl_container_of(listener, effect, destroy);

    if (effect->impl->destroy) {
        effect->impl->destroy(effect, effect->user_data);
    }

    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    free(effect);
}

static const struct effect_interface effect_impl = {
    .frame_render_begin = frame_render_begin,
    .frame_render_end = frame_render_end,
    .frame_render_post = frame_render_post,
    .configure = handle_effect_configure,
};

struct tap_ripple_effect *tap_ripple_effect_create(struct effect_manager *manager,
                                                   struct tap_ripple_effect_options *options,
                                                   const struct tap_ripple_effect_interface *impl,
                                                   const char *name, int priority, bool enabled,
                                                   void *user_data)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return NULL;
    }

    struct tap_ripple_effect *effect = calloc(1, sizeof(*effect));
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
    wl_list_init(&effect->points);

    if (effect->base->enabled) {
        handle_effect_enable(&effect->enable, NULL);
    }

    return effect;
}

void tap_ripple_effect_add_point(struct tap_ripple_effect *effect, int32_t id, int32_t x, int32_t y)
{
    // record multi-point
    bool find_id = false;
    struct tap_ripple_point *point;
    wl_list_for_each(point, &effect->points, link) {
        if (point->id == id) {
            find_id = true;
            break;
        }
    }
    if (!find_id) {
        struct tap_ripple_point *point = calloc(1, sizeof(struct tap_ripple_point));
        wl_list_init(&point->link);
        point->id = id;
        point->x = x - effect->options.size * 0.5f;
        point->y = y - effect->options.size * 0.5f;
        point->start_time = current_time_msec();
        wl_list_insert(&effect->points, &point->link);
    }

    add_damage(effect);
}

void tap_ripple_effect_remove_all_points(struct tap_ripple_effect *effect)
{
    free_points(effect);
}