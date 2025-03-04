// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <float.h>
#include <stdbool.h>
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

enum PointAxis { POINT_AXIS_X = 0, POINT_AXIS_Y = 1 };

typedef float Point[2];

// Cubic Bezier curve
struct BezierCurve {
    Point sp;  // start point
    Point cp1; // control point 1
    Point cp2; // control point 2
    Point ep;  // end point
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
    // mesh
    uint32_t subquads_size;
    quad *subquads;
    quad *subquads_cache;
    uint32_t vertices_size;
    struct vertex *vertices;
    // animate
    bool overtime;
    bool reversed;
    enum spout_location spout_location;
    float spout_x, spout_y, spout_width, spout_height;
    float window_x, window_y, window_width, window_height;
    uint32_t start_time;
    float progress; // two part animation. deformation + track
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
    uint32_t subdiv_count2;
};

static struct magic_lamp_effect *magic_lamp_effect = NULL;

static inline float lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

static void subdivide_quad(quad quad0, uint32_t x, uint32_t y, quad *quads)
{
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
}

static inline float bezier_point_axis(struct BezierCurve *curve, float t, enum PointAxis axis)
{
    float u = 1.f - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;

    return uuu * curve->sp[axis] + 3.f * uu * t * curve->cp1[axis] +
           3.f * u * tt * curve->cp2[axis] + ttt * curve->ep[axis];
}

static inline float bezier_derivative_axis(struct BezierCurve *curve, float t, enum PointAxis axis)
{
    float u = 1.f - t;
    float tt = t * t;
    float uu = u * u;

    return 3.f * uu * (curve->cp1[axis] - curve->sp[axis]) +
           6.f * u * t * (curve->cp2[axis] - curve->cp1[axis]) +
           3.f * tt * (curve->ep[axis] - curve->cp2[axis]);
}

static inline float bezier_axis_intersection(struct BezierCurve *curve, enum PointAxis axis,
                                             float axisPos)
{
    // Initial guess value
    float t = (axisPos - curve->sp[axis]) / (curve->ep[axis] - curve->sp[axis]);

    for (uint32_t i = 0; i < 3; i++) {
        float current = bezier_point_axis(curve, t, axis);
        float df = bezier_derivative_axis(curve, t, axis);

        if (fabsf(df) < 0.001f) {
            break;
        }

        // Newton-Raphson iterations. (Derivative correction)
        t -= (current - axisPos) / df;
    }
    return t;
}

// Determine the curve trajectory
static inline void calculate_bezier_control_points(struct BezierCurve *curve)
{
    curve->cp1[POINT_AXIS_X] =
        curve->sp[POINT_AXIS_X] + (curve->ep[POINT_AXIS_X] - curve->sp[POINT_AXIS_X]) * 0.3f;
    curve->cp1[POINT_AXIS_Y] =
        curve->sp[POINT_AXIS_Y] + (curve->ep[POINT_AXIS_Y] - curve->sp[POINT_AXIS_Y]) * 0.8f;

    curve->cp2[POINT_AXIS_X] =
        curve->ep[POINT_AXIS_X] - (curve->ep[POINT_AXIS_X] - curve->sp[POINT_AXIS_X]) * 0.2f;
    curve->cp2[POINT_AXIS_Y] =
        curve->ep[POINT_AXIS_Y] - (curve->ep[POINT_AXIS_Y] - curve->sp[POINT_AXIS_Y]) * 0.5f;
}

static void calculate_vertex_curve_position(struct magic_lamp_entry *entry, float progress)
{
    if (entry->spout_location == SPOUT_LOCATION_BOTTOM) {
        struct BezierCurve curve0 = { .sp = { entry->window_x, entry->window_y },
                                      .ep = { entry->spout_x, entry->spout_y } };
        calculate_bezier_control_points(&curve0);

        struct BezierCurve curve1 = {
            .sp = { entry->window_x + entry->window_width, entry->window_y },
            .ep = { entry->spout_x + entry->spout_width, entry->spout_y }
        };
        calculate_bezier_control_points(&curve1);

        for (uint32_t i = 0; i < entry->subquads_size; i++) {
            quad *subquad = &entry->subquads[i];
            for (uint32_t j = 0; j < 4; j++) {
                struct vertex *vtx = &(*subquad)[j];

                float y = lerp(curve0.sp[POINT_AXIS_Y], curve0.ep[POINT_AXIS_Y], progress) +
                          vtx->v * entry->window_height;
                y = fmin(y, curve0.ep[POINT_AXIS_Y]);

                float t0 = bezier_axis_intersection(&curve0, POINT_AXIS_Y, y);
                float x0 = bezier_point_axis(&curve0, t0, POINT_AXIS_X);

                float t1 = bezier_axis_intersection(&curve1, POINT_AXIS_Y, y);
                float x1 = bezier_point_axis(&curve1, t1, POINT_AXIS_X);

                vtx->x = lerp(x0, x1, vtx->u);
                vtx->y = y;
            }
        }
    } else if (entry->spout_location == SPOUT_LOCATION_TOP) {
        struct BezierCurve curve0 = {
            .sp = { entry->window_x, entry->window_y + entry->window_height },
            .ep = { entry->spout_x, entry->spout_y + entry->spout_height }
        };
        calculate_bezier_control_points(&curve0);

        struct BezierCurve curve1 = {
            .sp = { entry->window_x + entry->window_width, entry->window_y + entry->window_height },
            .ep = { entry->spout_x + entry->spout_width, entry->spout_y + entry->spout_height }
        };
        calculate_bezier_control_points(&curve1);

        for (uint32_t i = 0; i < entry->subquads_size; i++) {
            quad *subquad = &entry->subquads[i];
            for (uint32_t j = 0; j < 4; j++) {
                struct vertex *vtx = &(*subquad)[j];

                float y = lerp(curve0.sp[POINT_AXIS_Y], curve0.ep[POINT_AXIS_Y], progress) -
                          (1.f - vtx->v) * entry->window_height;
                y = fmax(y, curve0.ep[POINT_AXIS_Y]);

                float t0 = bezier_axis_intersection(&curve0, POINT_AXIS_Y, y);
                float x0 = bezier_point_axis(&curve0, t0, POINT_AXIS_X);

                float t1 = bezier_axis_intersection(&curve1, POINT_AXIS_Y, y);
                float x1 = bezier_point_axis(&curve1, t1, POINT_AXIS_X);

                vtx->x = lerp(x0, x1, vtx->u);
                vtx->y = y;
            }
        }
    } else if (entry->spout_location == SPOUT_LOCATION_LEFT) {
        struct BezierCurve curve0 = {
            .sp = { entry->window_x + entry->window_width, entry->window_y },
            .ep = { entry->spout_x + entry->spout_width, entry->spout_y }
        };
        calculate_bezier_control_points(&curve0);

        struct BezierCurve curve1 = {
            .sp = { entry->window_x + entry->window_width, entry->window_y + entry->window_height },
            .ep = { entry->spout_x + entry->spout_width, entry->spout_y + entry->spout_height }
        };
        calculate_bezier_control_points(&curve1);

        for (uint32_t i = 0; i < entry->subquads_size; i++) {
            quad *subquad = &entry->subquads[i];
            for (uint32_t j = 0; j < 4; j++) {
                struct vertex *vtx = &(*subquad)[j];

                float x = lerp(curve0.sp[POINT_AXIS_X], curve0.ep[POINT_AXIS_X], progress) -
                          (1.f - vtx->u) * entry->window_width;
                x = fmax(x, curve0.ep[POINT_AXIS_X]);

                float t0 = bezier_axis_intersection(&curve0, POINT_AXIS_X, x);
                float y0 = bezier_point_axis(&curve0, t0, POINT_AXIS_Y);

                float t1 = bezier_axis_intersection(&curve1, POINT_AXIS_X, x);
                float y1 = bezier_point_axis(&curve1, t1, POINT_AXIS_Y);

                vtx->x = x;
                vtx->y = lerp(y0, y1, vtx->v);
            }
        }
    } else if (entry->spout_location == SPOUT_LOCATION_RIGHT) {
        struct BezierCurve curve0 = { .sp = { entry->window_x, entry->window_y },
                                      .ep = { entry->spout_x, entry->spout_y } };
        calculate_bezier_control_points(&curve0);

        struct BezierCurve curve1 = {
            .sp = { entry->window_x, entry->window_y + entry->window_height },
            .ep = { entry->spout_x, entry->spout_y + entry->spout_height }
        };
        calculate_bezier_control_points(&curve1);

        for (uint32_t i = 0; i < entry->subquads_size; i++) {
            quad *subquad = &entry->subquads[i];
            for (uint32_t j = 0; j < 4; j++) {
                struct vertex *vtx = &(*subquad)[j];

                float x = lerp(curve0.sp[POINT_AXIS_X], curve0.ep[POINT_AXIS_X], progress) +
                          vtx->u * entry->window_width;
                x = fmin(x, curve0.ep[POINT_AXIS_X]);

                float t0 = bezier_axis_intersection(&curve0, POINT_AXIS_X, x);
                float y0 = bezier_point_axis(&curve0, t0, POINT_AXIS_Y);

                float t1 = bezier_axis_intersection(&curve1, POINT_AXIS_X, x);
                float y1 = bezier_point_axis(&curve1, t1, POINT_AXIS_Y);

                vtx->x = x;
                vtx->y = lerp(y0, y1, vtx->v);
            }
        }
    }
}

static void compute_boundbox(struct magic_lamp_entry *entry, struct ky_scene_output *output)
{
    float min_x = FLT_MAX;
    float min_y = FLT_MAX;
    float max_x = FLT_MIN;
    float max_y = FLT_MIN;
    for (uint32_t i = 0; i < entry->subquads_size; i++) {
        quad *subquad = &entry->subquads[i];
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
    free(entry->vertices);
    free(entry->subquads);
    free(entry->subquads_cache);

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

static bool frame_render_pre(struct effect_entity *entity, struct ky_scene_output *scene_output)
{
    struct magic_lamp_entry *entry = entity->user_data;
    if (!entry) {
        return true;
    }

    if (entry->overtime) {
        effect_entity_destroy(entry->effect_entity);
        return true;
    }

    // timer
    uint32_t diff_time = current_time_msec() - entry->start_time;
    if (diff_time > magic_lamp_effect->animate_duration) {
        diff_time = magic_lamp_effect->animate_duration;
        entry->overtime = true;
    }
    float percent = diff_time / (float)magic_lamp_effect->animate_duration;
    entry->progress = animation_value(magic_lamp_effect->animation, percent);
    if (entry->reversed) {
        entry->progress = 1.f - entry->progress;
    }

    // reset subdivide quad
    memcpy(entry->subquads, entry->subquads_cache, entry->subquads_size * sizeof(quad));

    // calculate progress. modify quad vertex
    const float first_anim_dura = 0.3f;
    const float second_anim_start = 0.2f;
    if (entry->progress <= first_anim_dura) {
        // sub animation 1: deformation animation = window rect -> sub animation 2 first frame
        float progress = entry->progress / first_anim_dura;
        calculate_vertex_curve_position(entry, second_anim_start);

        for (uint32_t i = 0; i < entry->subquads_size; i++) {
            quad *subquad = &entry->subquads[i];
            for (uint32_t j = 0; j < 4; j++) {
                struct vertex *vtx = &(*subquad)[j];
                vtx->x = lerp(entry->window_x + vtx->u * entry->window_width, vtx->x, progress);
                vtx->y = lerp(entry->window_y + vtx->v * entry->window_height, vtx->y, progress);
            }
        }
    } else {
        // sub animation 2: track animation
        float progress = (entry->progress - first_anim_dura) / (1.f - first_anim_dura);
        // 0~1 -> second_anim_start~1
        progress = lerp(second_anim_start, 1.f, progress);
        calculate_vertex_curve_position(entry, progress);
    }

    // compute boundbox for damage region
    compute_boundbox(entry, scene_output);

    // triangulate
    uint32_t vtx_count = 0;
    for (uint32_t i = 0; i < entry->subquads_size; i++) {
        quad *subquad = &entry->subquads[i];
        entry->vertices[vtx_count++] = (*subquad)[0];
        entry->vertices[vtx_count++] = (*subquad)[2];
        entry->vertices[vtx_count++] = (*subquad)[1];
        entry->vertices[vtx_count++] = (*subquad)[0];
        entry->vertices[vtx_count++] = (*subquad)[3];
        entry->vertices[vtx_count++] = (*subquad)[2];
    }

    effect_entity_push_damage(entity, KY_SCENE_DAMAGE_BOTH);
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

    struct ky_mat3 transform;
    ky_mat3_identity(&transform);
    ky_mat3_init_translate(&transform, -target->logical.x, -target->logical.y);
    struct ky_mat3 to_ndc;
    ky_mat3_logic_to_ndc(&to_ndc, target->logical.width, target->logical.height, target->transform);
    struct ky_mat3 logic2ndc;
    ky_mat3_multiply(&to_ndc, &transform, &logic2ndc);

    struct ky_opengl_texture *ky_tex = ky_opengl_texture_from_wlr_texture(entry->thumbnail_texture);

    GLfloat *verts = (GLfloat *)entry->vertices;
    glUseProgram(gl_shader.program);
    glEnableVertexAttribArray(gl_shader.in_position);
    glVertexAttribPointer(gl_shader.in_position, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts);
    glEnableVertexAttribArray(gl_shader.in_texcoord);
    glVertexAttribPointer(gl_shader.in_texcoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          verts + 2);
    glUniformMatrix3fv(gl_shader.logic2ndc, 1, GL_FALSE, logic2ndc.matrix);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ky_tex->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glDrawArrays(GL_TRIANGLES, 0, entry->vertices_size);

    glUseProgram(0);
    glDisableVertexAttribArray(gl_shader.in_position);
    glDisableVertexAttribArray(gl_shader.in_texcoord);
    glBindTexture(GL_TEXTURE_2D, 0);

    return false;
}

static bool frame_render_post(struct effect_entity *entity, struct ky_scene_render_target *target)
{
    effect_entity_push_damage(entity, KY_SCENE_DAMAGE_BOTH);
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
    .frame_render_pre = frame_render_pre,
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
    magic_lamp_effect->animate_duration = 400;
    magic_lamp_effect->subdiv_count = 32;
    magic_lamp_effect->subdiv_count2 = 8;

    return true;
}

static void subdivide_window_quad(struct magic_lamp_entry *entry)
{
    uint32_t subdiv_count = magic_lamp_effect->subdiv_count;
    uint32_t subdiv_count2 = magic_lamp_effect->subdiv_count2;
    quad window_quad = { { entry->window_x, entry->window_y, 0.0f, 0.0f },
                         { entry->window_x + entry->window_width, entry->window_y, 1.0f, 0.0f },
                         { entry->window_x + entry->window_width,
                           entry->window_y + entry->window_height, 1.0f, 1.0f },
                         { entry->window_x, entry->window_y + entry->window_height, 0.0f, 1.0f } };
    switch (entry->spout_location) {
    case SPOUT_LOCATION_BOTTOM:
    case SPOUT_LOCATION_TOP:
        subdivide_quad(window_quad, subdiv_count2, subdiv_count, entry->subquads_cache);
        break;
    case SPOUT_LOCATION_LEFT:
    case SPOUT_LOCATION_RIGHT:
        subdivide_quad(window_quad, subdiv_count, subdiv_count2, entry->subquads_cache);
        break;
    }
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

    // alloc buffer
    entry->subquads_size = magic_lamp_effect->subdiv_count * magic_lamp_effect->subdiv_count2;
    entry->subquads = malloc(entry->subquads_size * sizeof(quad));
    entry->subquads_cache = malloc(entry->subquads_size * sizeof(quad));
    entry->vertices_size = entry->subquads_size * 6;
    entry->vertices = malloc(entry->vertices_size * sizeof(struct vertex));

    wl_list_init(&entry->thumbnail_update.link);
    wl_list_init(&entry->thumbnail_destroy.link);

    // screenshot window view
    entry->thumbnail = thumbnail;
    entry->thumbnail_update.notify = handle_thumbnail_update;
    thumbnail_add_update_listener(entry->thumbnail, &entry->thumbnail_update);
    entry->thumbnail_destroy.notify = handle_thumbnail_destroy;
    thumbnail_add_destroy_listener(entry->thumbnail, &entry->thumbnail_destroy);
    thumbnail_update(entry->thumbnail);

    entry->overtime = false;
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
    subdivide_window_quad(entry);

    entry->start_time = current_time_msec();

    return true;
}
