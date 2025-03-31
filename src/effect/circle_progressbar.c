// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdbool.h>
#include <stdlib.h>

#include "effect/shape.h"
#include "effect_p.h"
#include "render/opengl.h"
#include "scene/scene.h"
#include "util/matrix.h"
#include "util/time.h"

#include "circle_progressbar_frag.h"
#include "circle_progressbar_vert.h"

#define M_2_PI 6.28318530717958647692f

struct circle_info {
    struct wl_list link;
    int32_t x, y;
    uint32_t start_time;
    bool rendering;
    float angle;
};

struct circle_progressbar_effect {
    struct effect *base;
    struct effect_manager *manager;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    const struct circle_progressbar_effect_interface *impl;
    struct circle_progressbar_effect_options options;
    void *user_data;

    struct circle_info *circle_info;
};

static struct circle_progressbar_gl_shader {
    int32_t program;
    // vs
    GLint in_uv;
    GLint uv2ndc;
    GLint output_invert;
    // fs
    GLint angle;
} gl_shader = { 0 };

static bool create_opengl_shader(struct ky_opengl_renderer *renderer)
{
    if (gl_shader.program == 0) {
        GLuint prog =
            ky_opengl_create_program(renderer, circle_progressbar_vert, circle_progressbar_frag);
        if (prog == 0) {
            return false;
        }
        gl_shader.program = prog;

        gl_shader.in_uv = glGetAttribLocation(prog, "inUV");
        gl_shader.uv2ndc = glGetUniformLocation(prog, "uv2ndc");
        gl_shader.output_invert = glGetUniformLocation(prog, "outputInvert");
        gl_shader.angle = glGetUniformLocation(prog, "endAngle");
    }

    return true;
}

static void free_circle_info(struct circle_progressbar_effect *effect)
{
    if (effect->circle_info) {
        free(effect->circle_info);
        effect->circle_info = NULL;
    }
}

static void add_damage(struct circle_progressbar_effect *effect)
{
    if (!effect->circle_info) {
        return;
    }

    struct ky_scene *scene = effect->manager->server->scene;
    pixman_region32_t region;
    pixman_region32_init_rect(&region, effect->circle_info->x, effect->circle_info->y,
                              effect->options.size, effect->options.size);
    ky_scene_add_damage(scene, &region);
    pixman_region32_fini(&region);
}

static void gl_render_finger_effect(struct circle_progressbar_effect *effect,
                                    struct circle_info *finger,
                                    struct ky_scene_render_target *target)
{
    if (!effect->circle_info->rendering) {
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
        .width = effect->options.size,
        .height = effect->options.size,
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

static bool frame_render_begin(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct circle_progressbar_effect *effect = entity->user_data;
    struct circle_info *finger = effect->circle_info;
    if (!finger) {
        return true;
    }

    // timer
    uint32_t time = current_time_msec();
    if (time > finger->start_time + effect->options.animate_delay) {
        uint32_t diff_time = time - effect->options.animate_delay - finger->start_time;
        if (diff_time > effect->options.animate_duration && finger->angle > M_2_PI) {
            free_circle_info(effect);
        } else {
            finger->rendering = true;
            float t = diff_time / (float)effect->options.animate_duration;
            finger->angle = t * M_2_PI;
        }
    }

    return true;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct circle_progressbar_effect *effect = entity->user_data;
    // add damage to trigger render event
    add_damage(effect);
    return true;
}

static bool frame_render_end(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct circle_progressbar_effect *effect = entity->user_data;

    struct ky_opengl_renderer *renderer =
        ky_opengl_renderer_from_wlr_renderer(effect->manager->server->renderer);
    if (!create_opengl_shader(renderer)) {
        return true;
    }

    if (effect->circle_info) {
        gl_render_finger_effect(effect, effect->circle_info, target);
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
    struct circle_progressbar_effect *effect = wl_container_of(listener, effect, enable);

    if (effect->impl->enable) {
        effect->impl->enable(effect, effect->user_data);
    }
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct circle_progressbar_effect *effect = wl_container_of(listener, effect, disable);

    if (effect->impl->disable) {
        effect->impl->disable(effect, effect->user_data);
    }

    free_circle_info(effect);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct circle_progressbar_effect *effect = wl_container_of(listener, effect, destroy);

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

struct circle_progressbar_effect *
circle_progressbar_effect_create(struct effect_manager *manager,
                                 struct circle_progressbar_effect_options *options,
                                 const struct circle_progressbar_effect_interface *impl,
                                 const char *name, int priority, bool enabled, void *user_data)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return NULL;
    }

    struct circle_progressbar_effect *effect = calloc(1, sizeof(*effect));
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

    if (effect->base->enabled) {
        handle_effect_enable(&effect->enable, NULL);
    }

    return effect;
}

void circle_progressbar_effect_begin(struct circle_progressbar_effect *effect, int32_t x, int32_t y)
{
    free_circle_info(effect);

    effect->circle_info = calloc(1, sizeof(struct circle_info));
    effect->circle_info->x = x - effect->options.size * 0.5f;
    effect->circle_info->y = y - effect->options.size * 0.5f;
    effect->circle_info->start_time = current_time_msec();
    effect->circle_info->rendering = false;

    add_damage(effect);
}

void circle_progressbar_effect_end(struct circle_progressbar_effect *effect)
{
    free_circle_info(effect);
}
