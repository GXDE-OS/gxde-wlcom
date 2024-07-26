// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include <wlr/render/wlr_texture.h>

#include <kywc/log.h>

#include "effect/animator.h"
#include "effect/fade.h"
#include "effect_p.h"
#include "render/renderer.h"
#include "scene/thumbnail.h"
#include "util/time.h"

struct fade_effect_data {
    struct animator *animator;
    struct animation_data current;

    struct kywc_box end_geometry;

    struct fade_options options;

    struct ky_scene_buffer *buffer;
    struct wl_listener buffer_destroy;

    struct thumbnail *thumbnail;
    struct wlr_texture *thumbnail_texture;
    struct wlr_buffer *thumbnail_buffer;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
};

struct fade_effect {
    struct effect *effect;
    struct effect_manager *manager;

    struct wl_listener destroy;
};

static struct fade_effect *fade = NULL;

static bool fade_data_create_animator(struct fade_effect_data *data, struct fade_options *options,
                                      struct animation_data *start, struct animation_data *end);

static struct fade_effect_data *fade_data_create(struct fade_options *options,
                                                 struct ky_scene_node *node);

static void fade_entity_push_damage(struct effect_entity *entity)
{
    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
    ky_scene_node_push_damage(node_chain->node, KY_SCENE_DAMAGE_BOTH, NULL);
}

static void calc_geometry(struct kywc_box *node_geometry, int offset, float factor,
                          struct kywc_box *geometry)
{
    geometry->width = node_geometry->width * factor;
    geometry->height = node_geometry->height * factor;
    geometry->x = node_geometry->x + geometry->width * (1.0 - factor) / 2;
    geometry->y = node_geometry->y + geometry->height * (1.0 - factor) / 2 + offset;
}

static void get_node_geometry(struct ky_scene_node *node, struct kywc_box *geometry)
{
    struct wlr_box box;
    node->impl.get_bounding_box(node, &box);
    ky_scene_node_coords(node, &geometry->x, &geometry->y);
    geometry->x += box.x;
    geometry->y += box.y;
    geometry->width = box.width;
    geometry->height = box.height;
}

static bool fade_frame_render_pre(struct effect_entity *entity,
                                  struct ky_scene_output *scene_output)
{
    struct fade_effect_data *data = entity->user_data;
    struct fade_options *options = &data->options;
    if (current_time_msec() > options->start_time + options->duration) {
        effect_entity_destroy(entity);
        return true;
    }

    if (options->action == FADE_IN) {
        struct effect_chain *chain = entity->slot.chain;
        struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
        struct animation_data start = { 0 }, end = { 0 };
        get_node_geometry(node_chain->node, &end.geometry);

        if (!kywc_box_equal(&data->end_geometry, &end.geometry)) {
            data->end_geometry = end.geometry;
            calc_geometry(&end.geometry, options->offset, options->factor, &start.geometry);

            if (data->animator) {
                animator_destroy(data->animator);
            }

            end.alpha = 1;
            if (!fade_data_create_animator(data, options, &start, &end)) {
                effect_entity_destroy(entity);
                return true;
            }
        }
    }

    const struct animation_data *animation_data =
        animator_value(data->animator, current_time_msec());
    data->current = *animation_data;
    fade_entity_push_damage(entity);

    return true;
}

static bool fade_entity_bouding_box(struct effect_entity *entity, struct kywc_box *box)
{
    struct fade_effect_data *fade_data = entity->user_data;
    *box = fade_data->current.geometry;

    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);

    if (box->width <= 0 || box->height <= 0) {
        struct wlr_box node_box;
        node_chain->impl.get_bounding_box(node_chain->node, &node_box);
        box->x = node_box.x;
        box->y = node_box.y;
        box->width = node_box.width;
        box->height = node_box.height;
        return false;
    }

    int lx, ly;
    ky_scene_node_coords(node_chain->node, &lx, &ly);
    box->x -= lx;
    box->y -= ly;

    return false;
}

static bool fade_node_push_damage(struct effect_entity *entity, struct ky_scene_node *damage_node,
                                  uint32_t *damage_type, pixman_region32_t *damage)
{
    struct kywc_box box;
    fade_entity_bouding_box(entity, &box);
    pixman_region32_union_rect(damage, damage, box.x, box.y, box.width, box.height);
    return false;
}

static void handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, thumbnail_update);
    struct thumbnail_update_event *event = data;
    if (!event->buffer_changed) {
        return;
    }

    if (fade_data->thumbnail_texture) {
        wlr_texture_destroy(fade_data->thumbnail_texture);
    }

    fade_data->thumbnail_buffer = event->buffer;
    fade_data->thumbnail_texture =
        wlr_texture_from_buffer(fade->manager->server->renderer, event->buffer);
}

static void handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, thumbnail_destroy);
    wl_list_remove(&fade_data->thumbnail_destroy.link);
    wl_list_remove(&fade_data->thumbnail_update.link);

    fade_data->thumbnail = NULL;
}

static bool fade_frame_render_post(struct effect_entity *entity,
                                   struct ky_scene_render_target *target)
{
    fade_entity_push_damage(entity);
    return true;
}

static void fade_data_destroy(struct fade_effect_data *data)
{
    if (data->buffer) {
        wl_list_remove(&data->buffer_destroy.link);
        ky_scene_node_destroy(&data->buffer->node);
    }
    if (data->thumbnail) {
        wl_list_remove(&data->thumbnail_update.link);
        wl_list_remove(&data->thumbnail_destroy.link);
        thumbnail_destroy(data->thumbnail);
    }
    if (data->thumbnail_texture) {
        wlr_texture_destroy(data->thumbnail_texture);
    }
    if (data->animator) {
        animator_destroy(data->animator);
    }
    free(data);
}

static void fade_entity_destroy(struct effect_entity *entity)
{
    struct fade_effect_data *data = entity->user_data;
    if (!data) {
        return;
    }

    fade_data_destroy(data);
}

static bool fade_node_render(struct effect_entity *entity, int lx, int ly,
                             struct ky_scene_render_target *target)
{
    struct fade_effect_data *data = entity->user_data;
    if (!target->output || !data->thumbnail_texture) {
        return false;
    }

    animator_render_texture(&data->current, target, data->thumbnail_texture);
    return false;
}

static const struct effect_interface fade_effect_impl = {
    .entity_destroy = fade_entity_destroy,
    .entity_bounding_box = fade_entity_bouding_box,
    .node_push_damage = fade_node_push_damage,
    .node_render = fade_node_render,
    .frame_render_pre = fade_frame_render_pre,
    .frame_render_post = fade_frame_render_post,
};

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity, *tmp;
    wl_list_for_each_safe(entity, tmp, &fade->effect->entities, effect_link) {
        effect_entity_destroy(entity);
    }

    wl_list_remove(&fade->destroy.link);
    free(fade);
    fade = NULL;
}

bool fade_effect_create(struct effect_manager *manager)
{
    fade = calloc(1, sizeof(*fade));
    if (!fade) {
        return false;
    }

    bool enabled = !ky_renderer_is_software(manager->server->renderer);
    fade->effect = effect_create("fade", 10, enabled, &fade_effect_impl);
    if (!fade->effect) {
        free(fade);
        fade = NULL;
        return false;
    }

    fade->manager = manager;
    fade->destroy.notify = handle_effect_destroy;
    wl_signal_add(&fade->effect->events.destroy, &fade->destroy);

    return true;
}

static void handle_buffer_destroy(struct wl_listener *listener, void *data)
{
    struct fade_effect_data *fade_data = wl_container_of(listener, fade_data, buffer_destroy);
    wl_list_remove(&fade_data->buffer_destroy.link);
    fade_data->buffer = NULL;
}

static struct ky_scene_tree *get_node_layer_tree(struct ky_scene_node *node)
{
    struct ky_scene_tree *tree = node->parent;

    int deep = 0;
    while (tree) {
        deep++;
        tree = tree->node.parent;
    }

    struct ky_scene_tree *view_parent = node->parent;
    view_parent = deep >= 4 ? view_parent->node.parent : view_parent;

    return view_parent;
}

static struct ky_scene_buffer *fade_create_scene_buffer(struct ky_scene_node *node,
                                                        struct fade_effect_data *data)
{
    struct ky_scene_tree *layer_tree;
    struct view_layer *layer;
    if (node->role == KY_SCENE_NODE_TOPLEVEL) {
        layer_tree = get_node_layer_tree(node);
    } else {
        layer = view_manager_get_layer(LAYER_POPUP, false);
        layer_tree = layer->tree;
    }
    if (!layer_tree) {
        kywc_log(KYWC_WARN, "node is not in layer");
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_create(layer_tree, data->thumbnail_buffer);
    if (!buffer) {
        return NULL;
    }

    ky_scene_node_raise_to_top(&buffer->node);
    ky_scene_node_set_input_bypassed(&buffer->node, true);

    return buffer;
}

static bool fade_data_create_animator(struct fade_effect_data *data, struct fade_options *options,
                                      struct animation_data *start, struct animation_data *end)
{
    data->animator = animator_create(start, options->type, options->start_time,
                                     options->start_time + options->duration);
    if (!data->animator) {
        return false;
    }

    animator_set_position(data->animator, end->geometry.x, end->geometry.y);
    animator_set_size(data->animator, end->geometry.width, end->geometry.height);
    animator_set_alpha(data->animator, end->alpha);
    return true;
}

static bool node_add_fade_out_effect(struct fade_options *options, struct ky_scene_node *node,
                                     struct animation_data *animation_data)
{
    struct fade_effect_data *data = fade_data_create(options, node);
    if (!data) {
        return false;
    }

    struct animation_data start = { 0 }, end = { 0 };
    if (animation_data) {
        start.alpha = animation_data->alpha;
        start.geometry = animation_data->geometry;
        end.geometry.width = animation_data->geometry.width * 0.8;
        end.geometry.height = animation_data->geometry.height * 0.8;
        end.geometry.x = animation_data->geometry.x + animation_data->geometry.width * 0.1;
        end.geometry.y = animation_data->geometry.y + animation_data->geometry.height * 0.1;
    } else {
        start.alpha = 1;
        get_node_geometry(node, &start.geometry);
        calc_geometry(&start.geometry, options->offset, options->factor, &end.geometry);
    }

    if (!fade_data_create_animator(data, options, &start, &end)) {
        fade_data_destroy(data);
        return false;
    }

    struct ky_scene_buffer *buffer = fade_create_scene_buffer(node, data);
    if (!buffer) {
        fade_data_destroy(data);
        return false;
    }

    struct effect_entity *entity = ky_scene_node_add_effect(&buffer->node, fade->effect);
    if (!entity) {
        fade_data_destroy(data);
        ky_scene_node_destroy(&buffer->node);
        return false;
    }

    data->buffer_destroy.notify = handle_buffer_destroy;
    wl_signal_add(&buffer->node.events.destroy, &data->buffer_destroy);

    data->buffer = buffer;
    entity->user_data = data;

    return true;
}

static struct fade_effect_data *fade_data_create(struct fade_options *options,
                                                 struct ky_scene_node *node)
{
    struct fade_effect_data *data = calloc(1, sizeof(*data));
    if (!data) {
        return NULL;
    }

    data->buffer = NULL;
    data->options = *options;
    data->thumbnail = thumbnail_create_from_node(node, 1.0f);
    if (!data->thumbnail) {
        free(data);
        return false;
    }

    data->thumbnail_update.notify = handle_thumbnail_update;
    thumbnail_add_update_listener(data->thumbnail, &data->thumbnail_update);
    data->thumbnail_destroy.notify = handle_thumbnail_destroy;
    thumbnail_add_destroy_listener(data->thumbnail, &data->thumbnail_destroy);

    if (data->options.action == FADE_IN) {
        return data;
    }

    thumbnail_update(data->thumbnail);
    if (!data->thumbnail_buffer) {
        fade_data_destroy(data);
        return NULL;
    }

    return data;
}

bool node_add_fade_effect(struct ky_scene_node *node, struct fade_options *options)
{
    struct fade_effect_data *fade_data = NULL;
    struct effect_entity *entity;
    if (options->action == FADE_OUT) {
        struct ky_scene_node *entity_node = options->entity_node ? options->entity_node : node;
        entity = ky_scene_node_find_effect_entity(entity_node, fade->effect);
        if (entity) {
            fade_data = entity->user_data;
            struct animation_data current = fade_data->current;

            effect_entity_destroy(entity);
            return node_add_fade_out_effect(options, node, &current);
        }
        return node_add_fade_out_effect(options, node, NULL);
    }

    fade_data = fade_data_create(options, node);
    if (!fade_data) {
        return false;
    }

    entity = ky_scene_node_add_effect(node, fade->effect);
    if (!entity) {
        fade_data_destroy(fade_data);
        return false;
    }

    entity->user_data = fade_data;

    return true;
}

bool view_add_fade_effect(struct view *view, enum fade_action action)
{
    if (!fade || !fade->effect->enabled) {
        return false;
    }

    struct fade_options options = { 0 };
    options.action = action;
    options.start_time = current_time_msec();
    options.duration = action ? 300 : 200;
    options.factor = action ? 0.9 : 0.8;
    options.entity_node = NULL;
    options.type.alpha = ANIMATION_TYPE_EASE;
    options.type.geometry = ANIMATION_TYPE_EASE;

    struct ky_scene_node *node = &view->tree->node;
    node->role = KY_SCENE_NODE_TOPLEVEL;
    node_add_fade_effect(node, &options);

    return true;
}

bool popup_add_fade_effect(struct ky_scene_node *entity_node, struct ky_scene_node *node,
                           enum fade_action action, bool topmost, bool seat)
{
    if (!fade || !fade->effect->enabled) {
        return false;
    }

    struct fade_options options = { 0 };
    options.action = action;
    options.start_time = current_time_msec();
    options.entity_node = entity_node;
    if (action == FADE_IN) {
        if (topmost && seat) {
            options.duration = 220;
            options.factor = 1.0;
        } else if (topmost && !seat) {
            options.duration = 200;
            options.factor = 0.85;
        } else if (!topmost) {
            options.duration = 220;
            options.offset = -4;
            options.factor = 1.0;
        }
    } else {
        if (topmost && seat) {
            options.duration = 180;
            options.factor = 1.0;
        } else if (topmost && !seat) {
            options.duration = 150;
            options.factor = 0.85;
        } else if (!topmost) {
            options.duration = 180;
            options.offset = 4;
            options.factor = 1.0;
        }
    }

    if (topmost && seat) {
        options.type.alpha = ANIMATION_TYPE_30_2_8_100;
        options.type.geometry = ANIMATION_TYPE_30_2_8_100;
    } else {
        options.type.alpha = ANIMATION_TYPE_EASE;
        options.type.geometry = ANIMATION_TYPE_EASE;
    }

    node->role = KY_SCENE_NODE_POPUP;
    node_add_fade_effect(node, &options);

    return true;
}
