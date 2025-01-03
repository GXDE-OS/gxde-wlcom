// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include <kywc/boxes.h>

#include "effect/animator.h"
#include "effect/magic_lamp.h"
#include "effect_p.h"
#include "output.h"
#include "render/opengl.h"
#include "scene/surface.h"
#include "scene/thumbnail.h"
#include "util/matrix.h"
#include "util/time.h"

#include "magic_lamp_frag.h"
#include "magic_lamp_vert.h"

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

enum spout_location {
    SPOUT_LOCATION_LEFT = 0,
    SPOUT_LOCATION_TOP = 1,
    SPOUT_LOCATION_RIGHT = 2,
    SPOUT_LOCATION_BOTTOM = 3,
};

struct vertex {
    float x;
    float y;
    float u;
    float v;
};

// 0 top-left
// 1 top-right
// 2 bottom-right
// 3 bottom-left
typedef struct vertex quad[4];

static struct magic_lamp_gl_shader {
    int32_t program;
    // vs
    GLint in_position;
    GLint in_texcoord;
    GLint logic2ndc;
} gl_shader = { 0 };

struct magic_lamp_entry {
    struct effect_entity *effect_entity;
    struct view *window_view;
    // animate
    bool reversed;
    enum spout_location spout_location;
    float spout_x, spout_y, spout_width, spout_height;
    float window_x, window_y, window_width, window_height;
    uint32_t start_time;
    float progress;
    // screenshot
    struct thumbnail *thumbnail;
    struct wlr_texture *thumbnail_texture;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
    // bbox
    struct kywc_box bbox;
};

struct magic_lamp_effect {
    struct effect *effect;
    struct wl_listener destroy;
    struct effect_manager *manager;
    struct ky_opengl_renderer *renderer;
    struct animation *animation;
    uint32_t animate_duration;
    uint32_t subdiv_count;
};

static struct magic_lamp_effect *magic_lamp_effect = NULL;

static inline float lerp(float a, float b, float t)
{
    return (1.f - t) * a + t * b;
}

static quad *subdivide_quad(quad quad0, uint32_t x, uint32_t y)
{
    quad *quads = malloc(x * y * sizeof(struct vertex) * 4);

    float step_x = 1.f / x;
    float step_y = 1.f / y;
    for (uint32_t i = 0; i < x; i++) {
        for (uint32_t j = 0; j < y; j++) {
            float u1 = i * step_x;
            float u2 = (i + 1) * step_x;
            float v1 = j * step_y;
            float v2 = (j + 1) * step_y;

            quad *sub_quad = &quads[y * i + j];
            (*sub_quad)[0].u = u1;
            (*sub_quad)[0].v = v1;
            (*sub_quad)[1].u = u2;
            (*sub_quad)[1].v = v1;
            (*sub_quad)[2].u = u2;
            (*sub_quad)[2].v = v2;
            (*sub_quad)[3].u = u1;
            (*sub_quad)[3].v = v2;

            // lerp y axis
            float top1x = lerp(quad0[0].x, quad0[1].x, u1);
            float top1y = lerp(quad0[0].y, quad0[1].y, u1);
            float top2x = lerp(quad0[0].x, quad0[1].x, u2);
            float top2y = lerp(quad0[0].y, quad0[1].y, u2);
            float bottom1x = lerp(quad0[3].x, quad0[2].x, u1);
            float bottom1y = lerp(quad0[3].y, quad0[2].y, u1);
            float bottom2x = lerp(quad0[3].x, quad0[2].x, u2);
            float bottom2y = lerp(quad0[3].y, quad0[2].y, u2);

            // lerp x axis get 4 vertex
            (*sub_quad)[0].x = lerp(top1x, bottom1x, v1);
            (*sub_quad)[0].y = lerp(top1y, bottom1y, v1);
            (*sub_quad)[1].x = lerp(top2x, bottom2x, v1);
            (*sub_quad)[1].y = lerp(top2y, bottom2y, v1);

            (*sub_quad)[3].x = lerp(top1x, bottom1x, v2);
            (*sub_quad)[3].y = lerp(top1y, bottom1y, v2);
            (*sub_quad)[2].x = lerp(top2x, bottom2x, v2);
            (*sub_quad)[2].y = lerp(top2y, bottom2y, v2);
        }
    }

    return quads;
}

// https://invent.kde.org/plasma/kwin/-/blob/Plasma/5.27/src/effects/magiclamp/magiclamp.cpp#L206
static void calculate_progress(quad *subquads, uint32_t subdiv_count, float progress,
                               enum spout_location spout_location, float spout_x, float spout_y,
                               float spout_width, float spout_height, float window_x,
                               float window_y, float window_width, float window_height)
{
    float factor = 0.f;
    float offset[2] = { 0.f, 0.f };
    float p_progress[2] = { 0.f, 0.f };
    quad last_quad;
    last_quad[0].x = -1.f;
    last_quad[0].y = -1.f;
    last_quad[1].x = -1.f;
    last_quad[1].y = -1.f;
    last_quad[2].x = -1.f;
    last_quad[2].y = -1.f;
    last_quad[3].x = -1.f;
    last_quad[3].y = -1.f;

    if (spout_location == SPOUT_LOCATION_TOP) {
        const float height_cube = window_height * window_height * window_height;
        const float min_y = spout_y + spout_height;

        for (uint32_t i = 0; i < subdiv_count; i++) {
            quad *subquad = &subquads[i];
            if ((*subquad)[0].y != last_quad[0].y || (*subquad)[2].y != last_quad[2].y) {
                factor = window_height - (*subquad)[0].y + (*subquad)[0].y * progress;
                offset[0] = (-spout_height + window_height + (*subquad)[0].y - spout_y) * progress *
                            ((factor * factor * factor) / height_cube);
                factor = window_height - (*subquad)[2].y + (*subquad)[2].y * progress;
                offset[1] = (-spout_height + window_height + (*subquad)[2].y - spout_y) * progress *
                            ((factor * factor * factor) / height_cube);

                p_progress[0] = MIN(offset[0] / (-spout_height + window_height - spout_y -
                                                 (window_height - (*subquad)[0].y)),
                                    1.f);
                p_progress[1] = MIN(offset[1] / (-spout_height + window_height - spout_y -
                                                 (window_height - (*subquad)[2].y)),
                                    1.f);
            } else {
                memcpy(last_quad, subquad, sizeof(quad));
            }

            offset[0] = -offset[0];
            offset[1] = -offset[1];

            p_progress[0] = fabsf(p_progress[0]);
            p_progress[1] = fabsf(p_progress[1]);

            (*subquad)[0].x +=
                (spout_x + spout_width * ((*subquad)[0].x / window_width) - (*subquad)[0].x) *
                p_progress[0];
            (*subquad)[1].x +=
                (spout_x + spout_width * ((*subquad)[1].x / window_width) - (*subquad)[1].x) *
                p_progress[0];
            (*subquad)[2].x +=
                (spout_x + spout_width * ((*subquad)[2].x / window_width) - (*subquad)[2].x) *
                p_progress[1];
            (*subquad)[3].x +=
                (spout_x + spout_width * ((*subquad)[3].x / window_width) - (*subquad)[3].x) *
                p_progress[1];

            (*subquad)[0].y = MAX(min_y, (*subquad)[0].y + offset[0]);
            (*subquad)[1].y = MAX(min_y, (*subquad)[1].y + offset[0]);
            (*subquad)[2].y = MAX(min_y, (*subquad)[2].y + offset[1]);
            (*subquad)[3].y = MAX(min_y, (*subquad)[3].y + offset[1]);
        }
    } else if (spout_location == SPOUT_LOCATION_BOTTOM) {
        const float height_cube = window_height * window_height * window_height;
        const float max_y = spout_y;

        for (uint32_t i = 0; i < subdiv_count; i++) {
            quad *subquad = &subquads[i];
            if ((*subquad)[0].y != last_quad[0].y || (*subquad)[2].y != last_quad[2].y) {
                factor = (*subquad)[0].y + (window_height - (*subquad)[0].y) * progress;
                offset[0] = (spout_y + (*subquad)[0].y) * progress *
                            ((factor * factor * factor) / height_cube);
                factor = (*subquad)[2].y + (window_height - (*subquad)[2].y) * progress;
                offset[1] = (spout_y + (*subquad)[2].y) * progress *
                            ((factor * factor * factor) / height_cube);

                p_progress[1] = MIN(offset[1] / (spout_y - (*subquad)[2].y), 1.f);
                p_progress[0] = MIN(offset[0] / (spout_y - (*subquad)[0].y), 1.f);
            } else {
                memcpy(last_quad, subquad, sizeof(quad));
            }

            p_progress[0] = fabsf(p_progress[0]);
            p_progress[1] = fabsf(p_progress[1]);

            (*subquad)[0].x +=
                (spout_x + spout_width * ((*subquad)[0].x / window_width) - (*subquad)[0].x) *
                p_progress[0];
            (*subquad)[1].x +=
                (spout_x + spout_width * ((*subquad)[1].x / window_width) - (*subquad)[1].x) *
                p_progress[0];
            (*subquad)[2].x +=
                (spout_x + spout_width * ((*subquad)[2].x / window_width) - (*subquad)[2].x) *
                p_progress[1];
            (*subquad)[3].x +=
                (spout_x + spout_width * ((*subquad)[3].x / window_width) - (*subquad)[3].x) *
                p_progress[1];

            (*subquad)[0].y = MIN(max_y, (*subquad)[0].y + offset[0]);
            (*subquad)[1].y = MIN(max_y, (*subquad)[1].y + offset[0]);
            (*subquad)[2].y = MIN(max_y, (*subquad)[2].y + offset[1]);
            (*subquad)[3].y = MIN(max_y, (*subquad)[3].y + offset[1]);
        }
    } else if (spout_location == SPOUT_LOCATION_LEFT) {
        const float width_cube = window_width * window_width * window_width;
        const float min_x = spout_x + spout_width;

        for (uint32_t i = 0; i < subdiv_count; i++) {
            quad *subquad = &subquads[i];
            if ((*subquad)[0].x != last_quad[0].x || (*subquad)[1].x != last_quad[1].x) {
                factor = window_width - (*subquad)[0].x + (*subquad)[0].x * progress;
                offset[0] = (-spout_width + window_width + (*subquad)[0].x - spout_x) * progress *
                            ((factor * factor * factor) / width_cube);
                factor = window_width - (*subquad)[1].x + (*subquad)[1].x * progress;
                offset[1] = (-spout_width + window_width + (*subquad)[1].x - spout_x) * progress *
                            ((factor * factor * factor) / width_cube);

                p_progress[0] = MIN(offset[0] / (-spout_width + window_width - spout_x -
                                                 (window_width - (*subquad)[0].x)),
                                    1.f);
                p_progress[1] = MIN(offset[1] / (-spout_width + window_width - spout_x -
                                                 (window_width - (*subquad)[1].x)),
                                    1.f);
            } else {
                memcpy(last_quad, subquad, sizeof(quad));
            }

            offset[0] = -offset[0];
            offset[1] = -offset[1];

            p_progress[0] = fabsf(p_progress[0]);
            p_progress[1] = fabsf(p_progress[1]);

            (*subquad)[0].y +=
                (spout_y + spout_height * ((*subquad)[0].y / window_height) - (*subquad)[0].y) *
                p_progress[0];
            (*subquad)[1].y +=
                (spout_y + spout_height * ((*subquad)[1].y / window_height) - (*subquad)[1].y) *
                p_progress[1];
            (*subquad)[2].y +=
                (spout_y + spout_height * ((*subquad)[2].y / window_height) - (*subquad)[2].y) *
                p_progress[1];
            (*subquad)[3].y +=
                (spout_y + spout_height * ((*subquad)[3].y / window_height) - (*subquad)[3].y) *
                p_progress[0];

            (*subquad)[0].x = MAX(min_x, (*subquad)[0].x + offset[0]);
            (*subquad)[1].x = MAX(min_x, (*subquad)[1].x + offset[1]);
            (*subquad)[2].x = MAX(min_x, (*subquad)[2].x + offset[1]);
            (*subquad)[3].x = MAX(min_x, (*subquad)[3].x + offset[0]);
        }
    } else if (spout_location == SPOUT_LOCATION_RIGHT) {
        const float width_cube = window_width * window_width * window_width;
        const float max_x = spout_x;

        for (uint32_t i = 0; i < subdiv_count; i++) {
            quad *subquad = &subquads[i];
            if ((*subquad)[0].x != last_quad[0].x || (*subquad)[1].x != last_quad[1].x) {
                factor = (*subquad)[0].x + (window_width - (*subquad)[0].x) * progress;
                offset[0] = (spout_x + (*subquad)[0].x) * progress *
                            ((factor * factor * factor) / width_cube);
                factor = (*subquad)[1].x + (window_width - (*subquad)[1].x) * progress;
                offset[1] = (spout_x + (*subquad)[1].x) * progress *
                            ((factor * factor * factor) / width_cube);
                p_progress[0] = MIN(offset[0] / (spout_x - (*subquad)[0].x), 1.f);
                p_progress[1] = MIN(offset[1] / (spout_x - (*subquad)[1].x), 1.f);
            } else {
                memcpy(last_quad, subquad, sizeof(quad));
            }

            p_progress[0] = fabsf(p_progress[0]);
            p_progress[1] = fabsf(p_progress[1]);

            (*subquad)[0].y +=
                (spout_y + spout_height * ((*subquad)[0].y / window_height) - (*subquad)[0].y) *
                p_progress[0];
            (*subquad)[1].y +=
                (spout_y + spout_height * ((*subquad)[1].y / window_height) - (*subquad)[1].y) *
                p_progress[1];
            (*subquad)[2].y +=
                (spout_y + spout_height * ((*subquad)[2].y / window_height) - (*subquad)[2].y) *
                p_progress[1];
            (*subquad)[3].y +=
                (spout_y + spout_height * ((*subquad)[3].y / window_height) - (*subquad)[3].y) *
                p_progress[0];

            (*subquad)[0].x = MIN(max_x, (*subquad)[0].x + offset[0]);
            (*subquad)[1].x = MIN(max_x, (*subquad)[1].x + offset[1]);
            (*subquad)[2].x = MIN(max_x, (*subquad)[2].x + offset[1]);
            (*subquad)[3].x = MIN(max_x, (*subquad)[3].x + offset[0]);
        }
    }
}

static void compute_boundbox(struct magic_lamp_entry *entry, quad *subquads, uint32_t subdiv_count,
                             struct ky_scene_render_target *target)
{
    float min_x = FLT_MAX;
    float min_y = FLT_MAX;
    float max_x = FLT_MIN;
    float max_y = FLT_MIN;
    for (uint32_t i = 0; i < subdiv_count; i++) {
        quad *subquad = &subquads[i];
        for (uint32_t j = 0; j < 4; j++) {
            min_x = MIN(min_x, (*subquad)[j].x);
            min_y = MIN(min_y, (*subquad)[j].y);
            max_x = MAX(max_x, (*subquad)[j].x);
            max_y = MAX(max_y, (*subquad)[j].y);
        }
    }

    // avoid float to int precision issue
    entry->bbox.x = floorf(min_x);
    entry->bbox.y = floorf(min_y);
    entry->bbox.width = MAX(1, ceilf(max_x) - entry->bbox.x);
    entry->bbox.height = MAX(1, ceilf(max_y) - entry->bbox.y);

    // use logic coordinates for damage
    entry->bbox.x += target->output->x;
    entry->bbox.y += target->output->y;
}

static void create_opengl_shader(struct wlr_renderer *renderer)
{
    struct ky_opengl_renderer *gl_renderer = ky_opengl_renderer_from_wlr_renderer(renderer);
    ky_egl_make_current(gl_renderer->egl, NULL);

    GLuint prog = ky_opengl_create_program(gl_renderer, magic_lamp_vert, magic_lamp_frag);
    if (prog == 0) {
        return;
    }
    gl_shader.program = prog;

    gl_shader.in_position = glGetAttribLocation(prog, "in_position");
    gl_shader.in_texcoord = glGetAttribLocation(prog, "in_texcoord");
    gl_shader.logic2ndc = glGetUniformLocation(prog, "logic2ndc");

    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    glUseProgram(0);

    ky_egl_unset_current(gl_renderer->egl);
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    struct magic_lamp_entry *entry = wl_container_of(listener, entry, thumbnail_update);
    if (entry->thumbnail_texture) {
        wlr_texture_destroy(entry->thumbnail_texture);
    }
    struct wlr_renderer *renderer = magic_lamp_effect->manager->server->renderer;
    entry->thumbnail_texture = wlr_texture_from_buffer(renderer, event->buffer);
}

static void handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct magic_lamp_entry *entry = wl_container_of(listener, entry, thumbnail_destroy);

    wl_list_remove(&entry->thumbnail_destroy.link);
    wl_list_remove(&entry->thumbnail_update.link);
    entry->thumbnail = NULL;
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&magic_lamp_effect->destroy.link);
    free(magic_lamp_effect);
    magic_lamp_effect = NULL;
}

static void entity_destroy(struct effect_entity *entity)
{
    struct magic_lamp_entry *entry = entity->user_data;
    if (!entry) {
        return;
    }

    if (entry->thumbnail_texture) {
        wlr_texture_destroy(entry->thumbnail_texture);
    }
    if (entry->thumbnail) {
        wl_list_remove(&entry->thumbnail_update.link);
        wl_list_remove(&entry->thumbnail_destroy.link);
        thumbnail_destroy(entry->thumbnail);
    }

    free(entry);
}

static bool entity_bounding_box(struct effect_entity *entity, struct kywc_box *box)
{
    struct magic_lamp_entry *entry = entity->user_data;
    if (!entry) {
        box->x = box->y = box->width = box->height = 0;

        return false;
    }

    box->x = entry->bbox.x;
    box->y = entry->bbox.y;
    box->width = entry->bbox.width;
    box->height = entry->bbox.height;

    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
    int lx, ly;
    ky_scene_node_coords(node_chain->node, &lx, &ly);
    box->x -= lx;
    box->y -= ly;

    return false;
}

static bool node_push_damage(struct effect_entity *entity, struct ky_scene_node *damage_node,
                             uint32_t *damage_type, pixman_region32_t *damage)
{
    struct kywc_box box;
    entity_bounding_box(entity, &box);
    pixman_region32_union_rect(damage, damage, box.x, box.y, box.width, box.height);

    return false;
}

static bool frame_render_begin(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    struct magic_lamp_entry *entry = entity->user_data;
    if (!entry) {
        return true;
    }

    // timer
    uint32_t diff_time = current_time_msec() - entry->start_time;
    if (diff_time > magic_lamp_effect->animate_duration) {
        effect_entity_destroy(entry->effect_entity);
    } else {
        float percent = diff_time / (float)magic_lamp_effect->animate_duration;
        entry->progress = animation_value(magic_lamp_effect->animation, percent);
        if (entry->reversed) {
            entry->progress = 1.f - entry->progress;
        }
    }

    return true;
}

static bool node_render(struct effect_entity *entity, int lx, int ly,
                        struct ky_scene_render_target *target)
{
    struct magic_lamp_entry *entry = entity->user_data;
    if (!entry) {
        return true;
    }

    if (!entry->thumbnail_texture && !wlr_texture_is_opengl(entry->thumbnail_texture)) {
        return true;
    }

    // In the case of extending the secondary screen
    // the x-coordinate value is too large causing spout positional deviation
    // the current output of the view use local coordinates
    float spout_x = entry->spout_x - target->output->x;
    float spout_y = entry->spout_y - target->output->y;
    float window_x = entry->window_x - target->output->x;
    float window_y = entry->window_y - target->output->y;

    // subdivide window quad
    uint32_t subdiv_count = magic_lamp_effect->subdiv_count;
    quad window_quad = { { window_x, window_y, 0.0f, 0.0f },
                         { window_x + entry->window_width, window_y, 1.0f, 0.0f },
                         { window_x + entry->window_width, window_y + entry->window_height, 1.0f,
                           1.0f },
                         { window_x, window_y + entry->window_height, 0.0f, 1.0f } };
    quad *subquads = NULL;
    switch (entry->spout_location) {
    case SPOUT_LOCATION_BOTTOM:
    case SPOUT_LOCATION_TOP:
        subquads = subdivide_quad(window_quad, 1, subdiv_count);
        break;
    case SPOUT_LOCATION_LEFT:
    case SPOUT_LOCATION_RIGHT:
        subquads = subdivide_quad(window_quad, subdiv_count, 1);
        break;
    }

    // correct x position when x too large
    if (entry->spout_location == SPOUT_LOCATION_TOP ||
        entry->spout_location == SPOUT_LOCATION_BOTTOM) {
        spout_x += (spout_x - (window_x + entry->window_width * 0.5f)) * 0.04f;
    }

    // calculate progress. modify quad vertex
    calculate_progress(subquads, subdiv_count, entry->progress, entry->spout_location, spout_x,
                       spout_y, entry->spout_width, entry->spout_height, window_x, window_y,
                       entry->window_width, entry->window_height);

    // compute boundbox for damage region
    compute_boundbox(entry, subquads, subdiv_count, target);

    // subquads grid to triangle strip
    const uint32_t vertices_count = subdiv_count * 4;
    struct vertex vertices[vertices_count];
    switch (entry->spout_location) {
    case SPOUT_LOCATION_BOTTOM:
    case SPOUT_LOCATION_TOP:
        for (uint32_t i = 0; i < subdiv_count; i++) {
            quad *subquad = &subquads[i];
            vertices[i * 4 + 0] = (*subquad)[0];
            vertices[i * 4 + 1] = (*subquad)[3];
            vertices[i * 4 + 2] = (*subquad)[1];
            vertices[i * 4 + 3] = (*subquad)[2];
        }
        break;
    case SPOUT_LOCATION_LEFT:
    case SPOUT_LOCATION_RIGHT:
        for (uint32_t i = 0; i < subdiv_count; i++) {
            quad *subquad = &subquads[i];
            vertices[i * 4 + 0] = (*subquad)[0];
            vertices[i * 4 + 1] = (*subquad)[1];
            vertices[i * 4 + 2] = (*subquad)[3];
            vertices[i * 4 + 3] = (*subquad)[2];
        }
        break;
    }
    free(subquads);

    // render
    struct ky_mat3 logic2ndc;
    ky_mat3_logic_to_ndc(&logic2ndc, target->logical.width, target->logical.height,
                         target->transform);

    struct ky_opengl_texture *ky_tex = ky_opengl_texture_from_wlr_texture(entry->thumbnail_texture);

    glUseProgram(gl_shader.program);
    glEnableVertexAttribArray(gl_shader.in_position);
    glVertexAttribPointer(gl_shader.in_position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          vertices);
    glEnableVertexAttribArray(gl_shader.in_texcoord);
    glVertexAttribPointer(gl_shader.in_texcoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (char *)vertices + 2 * sizeof(float));
    glUniformMatrix3fv(gl_shader.logic2ndc, 1, GL_FALSE, logic2ndc.matrix);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ky_tex->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, vertices_count);

    glUseProgram(0);
    glDisableVertexAttribArray(gl_shader.in_position);
    glDisableVertexAttribArray(gl_shader.in_texcoord);
    glBindTexture(GL_TEXTURE_2D, 0);

    return false;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    // add damage to trigger render event
    struct magic_lamp_entry *entry = entity->user_data;
    if (!entry) {
        return true;
    }

    pixman_region32_t region;
    pixman_region32_init_rect(&region, entry->bbox.x, entry->bbox.y, entry->bbox.width,
                              entry->bbox.height);
    struct ky_scene *scene = magic_lamp_effect->manager->server->scene;
    ky_scene_add_damage(scene, &region);
    pixman_region32_fini(&region);

    return true;
}

static bool handle_effect_configure(struct effect *effect, const struct effect_option *option)
{
    if (effect_option_is_enabled_option(option)) {
        return true;
    }

    return false;
}

static const struct effect_interface effect_impl = {
    .entity_destroy = entity_destroy,
    .entity_bounding_box = entity_bounding_box,
    .node_push_damage = node_push_damage,
    .node_render = node_render,
    .frame_render_begin = frame_render_begin,
    .frame_render_post = frame_render_post,
    .configure = handle_effect_configure,
};

bool magic_lamp_effect_create(struct effect_manager *manager)
{
    if (!wlr_renderer_is_opengl(manager->server->renderer)) {
        return false;
    }

    magic_lamp_effect = calloc(1, sizeof(*magic_lamp_effect));
    if (!magic_lamp_effect) {
        return false;
    }

    magic_lamp_effect->effect = effect_create("magic_lamp", 5, true, &effect_impl, NULL);
    if (!magic_lamp_effect->effect) {
        free(magic_lamp_effect);
        return false;
    }

    magic_lamp_effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&magic_lamp_effect->effect->events.destroy, &magic_lamp_effect->destroy);

    magic_lamp_effect->manager = manager;
    magic_lamp_effect->renderer = ky_opengl_renderer_from_wlr_renderer(manager->server->renderer);
    magic_lamp_effect->animation = animation_manager_get(ANIMATION_TYPE_LINER);
    magic_lamp_effect->animate_duration = 250;
    magic_lamp_effect->subdiv_count = 40;

    return true;
}

bool view_add_magic_lamp_effect(struct view *view)
{
    if (!magic_lamp_effect || !magic_lamp_effect->effect->enabled) {
        return false;
    }

    // create shader
    if (gl_shader.program == 0) {
        create_opengl_shader(magic_lamp_effect->manager->server->renderer);
        if (gl_shader.program <= 0) {
            return false;
        }
    }

    if (!view->minimized_geometry.panel_surface) {
        return false;
    }

    if (view->minimized_when_show_desktop) {
        return false;
    }

    // duplicate add remove pre entity
    struct effect_entity *entity =
        ky_scene_node_find_effect_entity(&view->tree->node, magic_lamp_effect->effect);
    if (entity) {
        struct magic_lamp_entry *entry = entity->user_data;
        if (entry && entry->window_view == view) {
            effect_entity_destroy(entry->effect_entity);
        }
    }

    struct thumbnail *thumbnail = thumbnail_create_from_view(view, THUMBNAIL_DISABLE_SHADOW, 1.0);
    if (!thumbnail) {
        return false;
    }

    entity = ky_scene_node_add_effect(&view->tree->node, magic_lamp_effect->effect);
    if (!entity) {
        return false;
    }

    struct magic_lamp_entry *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        effect_entity_destroy(entity);
        return false;
    }

    entity->user_data = entry;
    entry->effect_entity = entity;
    entry->window_view = view;
    wl_list_init(&entry->thumbnail_update.link);
    wl_list_init(&entry->thumbnail_destroy.link);

    // screenshot window view
    entry->thumbnail = thumbnail;
    entry->thumbnail_update.notify = handle_thumbnail_update;
    thumbnail_add_update_listener(entry->thumbnail, &entry->thumbnail_update);
    entry->thumbnail_destroy.notify = handle_thumbnail_destroy;
    thumbnail_add_destroy_listener(entry->thumbnail, &entry->thumbnail_destroy);
    thumbnail_update(entry->thumbnail);

    // animate reversed
    entry->reversed = !view->base.minimized;

    // calculate taskbar docking
    entry->spout_location = SPOUT_LOCATION_BOTTOM;
    struct output *output = output_from_kywc_output(view->output);
    if (output->geometry.height - output->usable_area.height > 1) {
        if (output->usable_area.y - output->scene_output->y > 1) {
            entry->spout_location = SPOUT_LOCATION_TOP;
        } else {
            entry->spout_location = SPOUT_LOCATION_BOTTOM;
        }
    } else if (output->geometry.width - output->usable_area.width > 1) {
        if (output->usable_area.x - output->scene_output->x > 1) {
            entry->spout_location = SPOUT_LOCATION_LEFT;
        } else {
            entry->spout_location = SPOUT_LOCATION_RIGHT;
        }
    }

    // spout quad
    int lx, ly;
    struct ky_scene_buffer *buffer =
        ky_scene_buffer_try_from_surface(view->minimized_geometry.panel_surface);
    ky_scene_node_coords(&buffer->node, &lx, &ly);
    entry->spout_x = view->minimized_geometry.x + lx;
    entry->spout_y = view->minimized_geometry.y + ly;
    entry->spout_width = view->minimized_geometry.width;
    entry->spout_height = view->minimized_geometry.height;

    // window quad
    entry->window_x = view->base.geometry.x - view->base.margin.off_x;
    entry->window_y = view->base.geometry.y - view->base.margin.off_y;
    entry->window_width = view->base.geometry.width + view->base.margin.off_width;
    entry->window_height = view->base.geometry.height + view->base.margin.off_height;

    entry->start_time = current_time_msec();
    entry->progress = 0.f;

    return true;
}
