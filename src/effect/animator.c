// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wayland-util.h>
#include <wlr/util/box.h>

#include <kywc/log.h>

#include "effect/animator.h"
#include "render/opengl.h"
#include "scene/render.h"
#include "server.h"
#include "util/time.h"

#define POINTS (255)

struct point {
    float x, y;
};

struct animation {
    struct wl_list link;
    enum animation_type type;
    struct point p1, p2;
    struct point points[POINTS];
};

static struct animation_manager {
    struct wl_list animations; // animation
    struct wl_listener destroy;
} *manager = NULL;

static const struct curve {
    struct point p1, p2;
} curves[ANIMATION_TYPES] = {
    { { 0, 0 }, { 0, 0 } },         { { 0, 0 }, { 1, 1 } },     { { 0.25, 0.1 }, { 0.25, 1 } },
    { { 0.42, 0 }, { 1, 1 } },      { { 0, 0 }, { 0.58, 1 } },  { { 0.42, 0 }, { 0.58, 1 } },
    { { 0.3, 0.15 }, { 0.1, 1 } },  { { 0, 0.4 }, { 0.2, 1 } }, { { 0.33, 0 }, { 1, 0.75 } },
    { { 0.3, 0.02 }, { 0.08, 1 } },
};

enum animator_mask {
    ANIMATOR_NONE = 0,
    ANIMATOR_ALPHA = 1 << 0,
    ANIMATOR_ANGLE = 1 << 1,
    ANIMATOR_GEOMETRY_X = 1 << 2,
    ANIMATOR_GEOMETRY_Y = 1 << 3,
    ANIMATOR_GEOMETRY_W = 1 << 4,
    ANIMATOR_GEOMETRY_H = 1 << 5,
    ANIMATOR_ALL = (1 << 6) - 1,
};

struct linear_function {
    float k;
    float b;
};

struct animation_entity {
    struct animation *animation;
    float value;
};

struct animator {
    int masks;

    int64_t start_time;
    int64_t current_time;
    int64_t end_time;

    struct animation_data current, end;

    struct animation_entity geometry;
    struct animation_entity alpha;
    struct animation_entity angle;

    struct linear_function angle_func;
    struct linear_function alpha_func;
    struct linear_function geometry_func[4];
};

static const float max_animation_value = 1;

/* copied from Hyprland */
float animation_value(struct animation *animation, float x)
{
    /* find x in points, then get y by x */
    if (x >= 1.0) {
        return 1.0f;
    }

    int upper = POINTS - 1;
    int lower = 0;
    int mid = upper / 2;

    while (abs(upper - lower) > 1) {
        if (animation->points[mid].x > x) {
            upper = mid;
        } else {
            lower = mid;
        }
        mid = (upper + lower) / 2;
    }

    struct point *p1 = &animation->points[lower];
    struct point *p2 = &animation->points[upper];
    float delta = (x - p1->x) / (p2->x - p1->x);

    if (isnan(delta) || isinf(delta)) {
        return 0.f;
    }

    return p1->y + (p2->y - p1->y) * delta;
}

static void animation_destroy(struct animation *animation)
{
    wl_list_remove(&animation->link);
    free(animation);
}

static struct animation *_animation_create(enum animation_type type, const struct point *p1,
                                           const struct point *p2)
{
    struct animation *animation = calloc(1, sizeof(struct animation));
    if (!animation) {
        return NULL;
    }

    animation->type = type;
    animation->p1 = *p1;
    animation->p2 = *p2;

    wl_list_insert(&manager->animations, &animation->link);
    float t, a1, a2, a3;
    for (int i = 0; i < POINTS; i++) {
        t = (i + 1) / (float)POINTS;
        a1 = 3 * t * pow(1 - t, 2);
        a2 = 3 * pow(t, 2) * (1 - t);
        a3 = pow(t, 3);
        animation->points[i].x = a1 * p1->x + a2 * p2->x + a3;
        animation->points[i].y = a1 * p1->y + a2 * p2->y + a3;
    }

    return animation;
}

static void linear_function_init(struct linear_function *func, float x1, float y1, float x2,
                                 float y2)
{
    float delta_x = x2 - x1;
    func->k = delta_x ? (y2 - y1) / delta_x : 0;

    func->b = y1 - func->k * x1;
}

struct animator *animator_create(struct animation_data *start, struct animation_type_group type,
                                 int64_t start_time, int64_t end_time)
{
    struct animator *animator = calloc(1, sizeof(*animator));
    if (!animator) {
        return NULL;
    }

    animator->geometry.animation = animation_manager_get(type.geometry);
    animator->alpha.animation = animation_manager_get(type.alpha);
    animator->angle.animation = animation_manager_get(type.angle);
    if (!animator->geometry.animation && !animator->alpha.animation && !animator->angle.animation) {
        free(animator);
        return NULL;
    }

    animator->masks = ANIMATOR_NONE;
    animator->current = *start;
    animator->end = *start;

    animator->start_time = start_time;
    animator->current_time = start_time;
    animator->end_time = end_time;

    return animator;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct animation *animation, *tmp;
    wl_list_for_each_safe(animation, tmp, &manager->animations, link) {
        animation_destroy(animation);
    }

    wl_list_remove(&manager->destroy.link);
    free(manager);
    manager = NULL;
}

struct animation *animation_manager_get(enum animation_type type)
{
    if (!manager || type <= ANIMATION_TYPE_NONE || type >= ANIMATION_TYPES) {
        return NULL;
    }

    struct animation *animation;
    wl_list_for_each(animation, &manager->animations, link) {
        if (animation->type == type) {
            return animation;
        }
    }
    return NULL;
}

bool animation_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct animation_manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->animations);

    /* create all animations */
    for (int i = ANIMATION_TYPE_LINER; i < ANIMATION_TYPES; i++) {
        _animation_create(i, &curves[i].p1, &curves[i].p2);
    }

    manager->destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->destroy);

    return true;
}

void animator_destroy(struct animator *animator)
{
    animator->geometry.animation = NULL;
    animator->angle.animation = NULL;
    animator->alpha.animation = NULL;
    free(animator);
}

static void animator_update_animation_value(struct animator *animator, int current_time)
{
    animator->current_time = current_time;
    int64_t delta_time = animator->current_time - animator->start_time;
    int64_t delta_max_time = animator->end_time - animator->start_time;
    float x = delta_time * 1.0f / delta_max_time;
    if (animator->geometry.animation) {
        animator->geometry.value = animation_value(animator->geometry.animation, x);
    }
    if (animator->alpha.animation) {
        animator->alpha.value = animation_value(animator->alpha.animation, x);
    }
    if (animator->angle.animation) {
        animator->angle.value = animation_value(animator->angle.animation, x);
    }
}

void animator_set_time(struct animator *animator, int64_t end_time)
{
    int64_t current_time = current_time_msec();
    if (end_time < current_time) {
        return;
    }
    animator->end_time = end_time;
    animator_update_animation_value(animator, current_time);
}

/**
 * reset start time, the start animator data is current animator data.
 * used to synchronize time and data with other animators.
 */
void animator_set_time_ex(struct animator *animator, int64_t start_time, int64_t end_time)
{
    if (end_time < start_time) {
        return;
    }
    animator->start_time = start_time;
    animator->end_time = end_time;
    animator_update_animation_value(animator, start_time);
}

void animator_set_angle(struct animator *animator, float end_angle)
{
    if (!animator->angle.animation) {
        return;
    }
    if (end_angle == animator->end.angle) {
        return;
    }
    animator->masks |= ANIMATOR_ANGLE;
    animator->end.angle = end_angle;
    linear_function_init(&animator->angle_func, animator->angle.value, animator->current.angle,
                         max_animation_value, end_angle);
}

void animator_set_alpha(struct animator *animator, float end_alpha)
{
    if (!animator->alpha.animation) {
        return;
    }
    if (end_alpha == animator->end.alpha) {
        return;
    }
    animator->masks |= ANIMATOR_ALPHA;
    animator->end.alpha = end_alpha;
    linear_function_init(&animator->alpha_func, animator->alpha.value, animator->current.alpha,
                         max_animation_value, end_alpha);
}

void animator_set_position(struct animator *animator, int end_x, int end_y)
{
    if (!animator->geometry.animation) {
        return;
    }
    if (end_x != animator->end.geometry.x) {
        animator->masks |= ANIMATOR_GEOMETRY_X;
        animator->end.geometry.x = end_x;
        linear_function_init(&animator->geometry_func[0], animator->geometry.value,
                             animator->current.geometry.x, max_animation_value, end_x);
    }

    if (end_y != animator->end.geometry.y) {
        animator->masks |= ANIMATOR_GEOMETRY_Y;
        animator->end.geometry.y = end_y;
        linear_function_init(&animator->geometry_func[1], animator->geometry.value,
                             animator->current.geometry.y, max_animation_value, end_y);
    }
}

void animator_set_size(struct animator *animator, int end_width, int end_height)
{
    if (!animator->geometry.animation) {
        return;
    }
    if (end_width != animator->end.geometry.width) {
        animator->masks |= ANIMATOR_GEOMETRY_W;
        animator->end.geometry.width = end_width;
        linear_function_init(&animator->geometry_func[2], animator->geometry.value,
                             animator->current.geometry.width, max_animation_value, end_width);
    }
    if (end_height != animator->end.geometry.height) {
        animator->masks |= ANIMATOR_GEOMETRY_H;
        animator->end.geometry.height = end_height;
        linear_function_init(&animator->geometry_func[3], animator->geometry.value,
                             animator->current.geometry.height, max_animation_value, end_height);
    }
}

const struct animation_data *animator_value(struct animator *animator, int64_t current_time)
{
    animator_update_animation_value(animator, current_time);
    float geometry_value = animator->geometry.value;
    float alpha_value = animator->alpha.value;
    float angle_value = animator->angle.value;
    if (animator->masks & ANIMATOR_ANGLE) {
        animator->current.angle = angle_value * animator->angle_func.k + animator->angle_func.b;
    }
    if (animator->masks & ANIMATOR_ALPHA) {
        animator->current.alpha = alpha_value * animator->alpha_func.k + animator->alpha_func.b;
    }
    if (animator->masks & ANIMATOR_GEOMETRY_X) {
        animator->current.geometry.x =
            geometry_value * animator->geometry_func[0].k + animator->geometry_func[0].b;
    }
    if (animator->masks & ANIMATOR_GEOMETRY_Y) {
        animator->current.geometry.y =
            geometry_value * (animator->geometry_func[1].k) + animator->geometry_func[1].b;
    }
    if (animator->masks & ANIMATOR_GEOMETRY_W) {
        animator->current.geometry.width =
            geometry_value * animator->geometry_func[2].k + animator->geometry_func[2].b;
    }
    if (animator->masks & ANIMATOR_GEOMETRY_H) {
        animator->current.geometry.height =
            geometry_value * animator->geometry_func[3].k + animator->geometry_func[3].b;
    }
    return &animator->current;
}

void animator_render_texture(struct animation_data *animation_data,
                             struct ky_scene_render_target *target, struct wlr_texture *texture)
{
    struct wlr_box dst_box = {
        .x = animation_data->geometry.x - target->logical.x,
        .y = animation_data->geometry.y - target->logical.y,
        .width = animation_data->geometry.width,
        .height = animation_data->geometry.height,
    };
    ky_scene_render_box(&dst_box, target);

    pixman_region32_t render_region;
    pixman_region32_init(&render_region);
    pixman_region32_copy(&render_region, &target->damage);
    pixman_region32_translate(&render_region, -target->logical.x, -target->logical.y);
    ky_scene_render_region(&render_region, target);

    pixman_region32_intersect_rect(&render_region, &render_region, dst_box.x, dst_box.y,
                                   dst_box.width, dst_box.height);
    if (!pixman_region32_not_empty(&render_region)) {
        pixman_region32_fini(&render_region);
        return;
    }

    struct ky_render_texture_options options = {
        .base = {
            .texture = texture,
            .dst_box = dst_box,
            .transform = target->transform,
            .alpha = &animation_data->alpha,
            .clip =  &render_region,
        },
        .rotation_angle = animation_data->angle,
    };
    ky_render_pass_add_texture(target->render_pass, &options);
    pixman_region32_fini(&render_region);
}
