// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>

#include "effect/animator.h"
#include "effect/fade.h"
#include "effect/transform.h"
#include "effect_p.h"
#include "util/time.h"

struct fade_entity {
    struct transform *data;
    bool fade_in;
    int offset;
    float factor;

    bool sub_effect_permitted;

    struct ky_scene_node *node;
    struct kywc_box node_geometry;

    struct wl_listener destroy;
};

struct fade_effect {
    struct transform_effect *effect;
};

static struct fade_effect *fade_effect = NULL;

static void fade_calc_geometry(const struct kywc_box *node_geometry, int offset, float factor,
                               struct kywc_box *geometry)
{
    geometry->width = node_geometry->width * factor;
    geometry->height = node_geometry->height * factor;
    geometry->x = node_geometry->x + geometry->width * (1.0 - factor) / 2;
    geometry->y = node_geometry->y + geometry->height * (1.0 - factor) / 2 + offset;
}

static void fade_get_node_geometry(struct ky_scene_node *node, struct kywc_box *geometry)
{
    struct wlr_box box;
    node->impl.get_bounding_box(node, &box);
    ky_scene_node_coords(node, &geometry->x, &geometry->y);
    geometry->x += box.x;
    geometry->y += box.y;
    geometry->width = box.width;
    geometry->height = box.height;
}

static void fade_handle_transform_destroy(struct wl_listener *listener, void *data)
{
    struct fade_entity *fade_entity = wl_container_of(listener, fade_entity, destroy);
    wl_list_remove(&fade_entity->destroy.link);
    free(fade_entity);
}

static void fade_update_transform_options(struct transform_effect *effect,
                                          struct transform *transform,
                                          struct transform_options *options,
                                          struct animation_data *current, bool interrupted,
                                          void *data)
{
    struct fade_entity *fade_entity = transform_get_user_data(transform);
    if (interrupted) {
        options->start_time = current_time_msec();
        options->start.alpha = current->alpha;
        options->start.geometry = current->geometry;
        /**
         * Nested rendering of effects, the current geometry is wrong, need to update start geometry
         */
        if (fade_entity->sub_effect_permitted) {
            fade_entity->sub_effect_permitted = false;
            fade_calc_geometry(&fade_entity->node_geometry, fade_entity->offset,
                               fade_entity->factor, &options->start.geometry);
        }
        return;
    }

    if (!fade_entity->fade_in) {
        return;
    }

    struct kywc_box node_geometry;
    fade_get_node_geometry(fade_entity->node, &node_geometry);
    fade_calc_geometry(&node_geometry, fade_entity->offset, fade_entity->factor,
                       &options->start.geometry);
    options->end.geometry = node_geometry;
}

static void fade_effect_destroy(struct transform_effect *effect, void *data)
{
    struct fade_effect *fade_effect = data;
    free(fade_effect);
    fade_effect = NULL;
}

struct transform_effect_interface fade_impl = {
    .update_transform_options = fade_update_transform_options,
    .destroy = fade_effect_destroy,
};

bool fade_effect_create(struct effect_manager *manager)
{
    fade_effect = calloc(1, sizeof(*fade_effect));
    if (!fade_effect) {
        return false;
    }

    fade_effect->effect = transform_effect_create(manager, &fade_impl, "fade", 10, NULL);
    if (!fade_effect->effect) {
        free(fade_effect);
        return false;
    }

    return true;
}

static void fade_entity_create_transform(struct fade_entity *fade_entity,
                                         struct transform_effect *effect,
                                         struct transform_options *options,
                                         struct ky_scene_node *node)
{
    fade_entity->data =
        transform_effect_get_or_create_transform(effect, options, node, fade_entity);
    if (!fade_entity->data) {
        free(fade_entity);
        return;
    }

    /* prohibit updating thumbnail when fade out */
    if (!fade_entity->fade_in) {
        transform_block_source_update(fade_entity->data, true);
    }

    fade_entity->destroy.notify = fade_handle_transform_destroy;
    transform_add_destroy_listener(fade_entity->data, &fade_entity->destroy);
}

bool view_add_fade_effect(struct view *view, enum fade_action action)
{
    if (!fade_effect) {
        return false;
    }

    struct fade_entity *fade_entity = calloc(1, sizeof(*fade_entity));
    if (!fade_entity) {
        return false;
    }

    struct transform_options options = { 0 };
    options.type.alpha = ANIMATION_TYPE_EASE;
    options.type.geometry = ANIMATION_TYPE_EASE;
    options.scale = view->surface ? view->surface->current.scale : 1.0f;
    options.start_time = current_time_msec();
    struct ky_scene_node *node = &view->tree->node;
    fade_entity->node = node;
    if (action == FADE_IN) {
        fade_entity->fade_in = true;
        fade_entity->factor = 0.9;
        options.duration = 300;
        options.start.alpha = 0;
        options.end.alpha = 1.0;
        fade_get_node_geometry(node, &options.end.geometry);
        fade_calc_geometry(&options.end.geometry, fade_entity->offset, fade_entity->factor,
                           &options.start.geometry);
    } else {
        fade_entity->fade_in = false;
        fade_entity->factor = 0.8;
        options.duration = 200;
        options.start.alpha = 1.0;
        options.end.alpha = 0;
        fade_get_node_geometry(node, &options.start.geometry);
        fade_calc_geometry(&options.start.geometry, fade_entity->offset, fade_entity->factor,
                           &options.end.geometry);
    }

    fade_entity_create_transform(fade_entity, fade_effect->effect, &options, node);

    return true;
}

bool popup_add_fade_effect(struct ky_scene_node *node, enum fade_action action, bool topmost,
                           bool seat, float scale)
{
    if (!fade_effect) {
        return false;
    }

    struct fade_entity *fade_entity = calloc(1, sizeof(*fade_entity));
    if (!fade_entity) {
        return false;
    }

    struct transform_options options = { 0 };
    options.scale = scale;
    options.start_time = current_time_msec();
    fade_entity->node = node;
    if (action == FADE_IN) {
        if (topmost && seat) {
            fade_entity->factor = 1.0;
            options.duration = 220;
        } else if (topmost && !seat) {
            fade_entity->factor = 0.85;
            options.duration = 200;
        } else if (!topmost) {
            fade_entity->factor = 1.0;
            fade_entity->offset = -4;
            options.duration = 220;
        }
        fade_entity->fade_in = true;
        options.start.alpha = 0;
        options.end.alpha = 1.0;
        fade_get_node_geometry(node, &options.end.geometry);
        fade_calc_geometry(&options.end.geometry, fade_entity->offset, fade_entity->factor,
                           &options.start.geometry);
        fade_entity->node_geometry = options.end.geometry;
    } else {
        if (topmost && seat) {
            fade_entity->factor = 1.0;
            options.duration = 180;
            fade_entity->sub_effect_permitted = true;
        } else if (topmost && !seat) {
            fade_entity->factor = 0.85;
            options.duration = 150;
        } else if (!topmost) {
            fade_entity->factor = 1.0;
            fade_entity->offset = 4;
            options.duration = 180;
        }
        fade_entity->fade_in = false;
        options.start.alpha = 1.0;
        options.end.alpha = 0;
        fade_get_node_geometry(node, &options.start.geometry);
        fade_calc_geometry(&options.start.geometry, fade_entity->offset, fade_entity->factor,
                           &options.end.geometry);
        fade_entity->node_geometry = options.start.geometry;
    }

    if (topmost && seat) {
        options.type.alpha = ANIMATION_TYPE_30_2_8_100;
        options.type.geometry = ANIMATION_TYPE_30_2_8_100;
    } else {
        options.type.alpha = ANIMATION_TYPE_EASE;
        options.type.geometry = ANIMATION_TYPE_EASE;
    }

    node->role = KY_SCENE_NODE_POPUP;

    fade_entity_create_transform(fade_entity, fade_effect->effect, &options, node);

    return true;
}
