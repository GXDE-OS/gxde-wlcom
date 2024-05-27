// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output.h>

#include "output.h"
#include "scene/animation.h"
#include "server.h"
#include "util/time.h"

#define POINTS (255)

struct point {
    float x, y;
};

enum animation_mask {
    ANIMATION_NONE = 0,
    ANIMATION_POSITION = 1 << 0,
    ANIMATION_SIZE = 1 << 1,
};

union state {
    struct {
        int x, y;
    };
    struct {
        int width, height;
    };
};

struct animation_state {
    struct animation *animation;
    uint32_t start;
    uint32_t duration; // ms
    union state src, dst;
};

struct animation_entity {
    struct ky_scene_node *node;
    struct wlr_addon addon;

    struct kywc_output *output;
    struct wl_listener output_frame;

    uint32_t mask;
    struct animation_state state[4];
};

struct animation {
    struct wl_list link;
    enum animation_type type;
    struct point p1, p2;
    struct point points[POINTS];
};

static struct animation_manager {
    struct wl_list animations;
    struct wl_listener destroy;
} *manager = NULL;

static const struct curve {
    struct point p1, p2;
} default_curves[] = {
    { { 0, 0 }, { 0, 0 } },    { { 0, 0 }, { 1, 1 } },    { { 0.25, 0.1 }, { 0.25, 1 } },
    { { 0.42, 0 }, { 1, 1 } }, { { 0, 0 }, { 0.58, 1 } }, { { 0.42, 0 }, { 0.58, 1 } },
};

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

    // kywc_log(KYWC_INFO, "animation value %d %d", lower, upper);

    struct point *p1 = &animation->points[lower];
    struct point *p2 = &animation->points[upper];
    float delta = (x - p1->x) / (p2->x - p1->x);

    if (isnan(delta) || isinf(delta)) {
        return 0.f;
    }

    return p1->y + (p2->y - p1->y) * delta;
}

void animation_destroy(struct animation *animation)
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

struct animation *animation_create(float p1x, float p1y, float p2x, float p2y)
{
    if (!manager) {
        return NULL;
    }
    return _animation_create(ANIMATION_TYPE_MOD, &(struct point){ p1x, p1y },
                             &(struct point){ p2x, p2y });
}

struct animation *animation_manager_get_default(enum animation_type type)
{
    if (!manager || type < ANIMATION_TYPE_LINER || type >= ANIMATION_TYPES) {
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

static void animation_entity_update_output(struct animation_entity *entity, bool clear)
{
    int lx = 0, ly = 0;
    if (clear || !ky_scene_node_coords(entity->node, &lx, &ly)) {
        wl_list_remove(&entity->output_frame.link);
        wl_list_init(&entity->output_frame.link);
        entity->output = NULL;
        return;
    }

    struct kywc_output *output = kywc_output_at_point(lx, ly);
    struct output *o = output_from_kywc_output(output);
    wlr_output_schedule_frame(o->wlr_output);

    if (output == entity->output) {
        return;
    }

    entity->output = output;
    // kywc_log(KYWC_INFO, "animation output to %s", output->name);
    wl_list_remove(&entity->output_frame.link);
    wl_signal_add(&o->scene_output->events.frame, &entity->output_frame);
}

static void entity_handle_output_frame(struct wl_listener *listener, void *data)
{
    struct animation_entity *entity = wl_container_of(listener, entity, output_frame);
    if (entity->mask == ANIMATION_NONE) {
        return;
    }

    uint32_t current = current_time_msec();
    // kywc_log(KYWC_INFO, "animation start at %d", current);

    if (entity->mask & ANIMATION_POSITION) {
        struct animation_state *state = &entity->state[0];
        uint32_t elapse = current - state->start;
        if (elapse >= state->duration) {
            ky_scene_node_set_position(entity->node, state->dst.x, state->dst.y);
            entity->mask &= ~ANIMATION_POSITION;
        } else {
            float percent = (float)elapse / state->duration;
            float value = animation_value(state->animation, percent);
            int delta_x = state->dst.x - state->src.x;
            int delta_y = state->dst.y - state->src.y;
            int x = state->src.x + value * delta_x;
            int y = state->src.y + value * delta_y;
            // kywc_log(KYWC_INFO, "animation set pos to (%d, %d) ", x, y);
            ky_scene_node_set_position(entity->node, x, y);
        }
    }

    if (entity->mask & ANIMATION_SIZE) {
        struct animation_state *state = &entity->state[1];
        uint32_t elapse = current - state->start;
        if (elapse >= state->duration) {
            ky_scene_rect_set_size(ky_scene_rect_from_node(entity->node), state->dst.width,
                                   state->dst.height);
            entity->mask &= ~ANIMATION_SIZE;
        } else {
            float percent = (float)elapse / state->duration;
            float value = animation_value(state->animation, percent);
            int delta_width = state->dst.width - state->src.width;
            int delta_height = state->dst.height - state->src.height;
            int width = state->src.width + value * delta_width;
            int height = state->src.height + value * delta_height;
            struct ky_scene_rect *rect = ky_scene_rect_from_node(entity->node);
            // kywc_log(KYWC_INFO, "animation set size to (%d, %d) ", width, height);
            ky_scene_rect_set_size(rect, width, height);
        }
    }

    animation_entity_update_output(entity, entity->mask == ANIMATION_NONE);
}

static void animation_entity_addon_destroy(struct wlr_addon *addon)
{
    struct animation_entity *entity = wl_container_of(addon, entity, addon);
    wl_list_remove(&entity->output_frame.link);
    wlr_addon_finish(addon);
    free(entity);
}

static const struct wlr_addon_interface animation_entity_addon_impl = {
    .name = "animaiton_entity",
    .destroy = animation_entity_addon_destroy,
};

static struct animation_entity *animation_entity_create(struct ky_scene_node *node)
{
    struct animation_entity *entity = calloc(1, sizeof(struct animation));
    if (!entity) {
        return NULL;
    }

    entity->node = node;
    wl_list_init(&entity->output_frame.link);
    entity->output_frame.notify = entity_handle_output_frame;

    wlr_addon_init(&entity->addon, &node->addons, node, &animation_entity_addon_impl);

    return entity;
}

static struct animation_entity *animation_entity_get(struct ky_scene_node *node)
{
    struct wlr_addon *addon = wlr_addon_find(&node->addons, node, &animation_entity_addon_impl);

    struct animation_entity *entity =
        addon ? wl_container_of(addon, entity, addon) : animation_entity_create(node);
    if (entity) {
        animation_entity_update_output(entity, false);
    }

    return entity;
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

bool animation_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(struct animation_manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->animations);

    /* create default animations */
    for (int i = ANIMATION_TYPE_LINER; i < ANIMATION_TYPES; i++) {
        _animation_create(i, &default_curves[i].p1, &default_curves[i].p2);
    }

    manager->destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->destroy);

    return true;
}

void ky_scene_node_set_position_with_animation(struct ky_scene_node *node, int x, int y,
                                               struct animation *animation, uint32_t duration)
{
    if (!manager || !animation) {
        ky_scene_node_set_position(node, x, y);
        return;
    }

    struct animation_entity *entity = animation_entity_get(node);
    if (!entity) {
        ky_scene_node_set_position(node, x, y);
        return;
    }

    struct animation_state *state = &entity->state[0];
    state->animation = animation;
    state->duration = duration;
    state->dst.x = x;
    state->dst.y = y;

    if (entity->mask & ANIMATION_POSITION) {
        return;
    }

    entity->mask |= ANIMATION_POSITION;
    state->start = current_time_msec();
    state->src.x = node->x;
    state->src.y = node->y;
}

void ky_scene_rect_set_size_with_animation(struct ky_scene_rect *rect, int width, int height,
                                           struct animation *animation, uint32_t duration)
{
    if (!manager || !animation) {
        ky_scene_rect_set_size(rect, width, height);
        return;
    }

    struct animation_entity *entity = animation_entity_get(&rect->node);
    if (!entity) {
        ky_scene_rect_set_size(rect, width, height);
        return;
    }

    struct animation_state *state = &entity->state[1];
    state->animation = animation;
    state->duration = duration;
    state->dst.width = width;
    state->dst.height = height;

    if (entity->mask & ANIMATION_SIZE) {
        return;
    }

    entity->mask |= ANIMATION_SIZE;
    state->start = current_time_msec();
    state->src.width = rect->width;
    state->src.height = rect->height;
}
