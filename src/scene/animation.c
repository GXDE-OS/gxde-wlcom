// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output.h>

#include "output.h"
#include "scene/animation.h"
#include "util/time.h"

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
    struct animation_entity *entity = calloc(1, sizeof(struct animation_entity));
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

void ky_scene_node_set_position_with_animation(struct ky_scene_node *node, int x, int y,
                                               struct animation *animation, uint32_t duration)
{
    if (!animation) {
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
    if (!animation) {
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
