// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <kywc/identifier.h>
#include <kywc/log.h>

#include "effect_p.h"
#include "scene/animation.h"
#include "scene/thumbnail.h"

static struct effect_manager *manager = NULL;
static const struct wlr_addon_interface effect_addon_impl;

static struct node_effect_chain *node_effect_chain_from_node(struct ky_scene_node *node)
{
    struct wlr_addon *addon = wlr_addon_find(&node->addons, node, &effect_addon_impl);
    struct node_effect_chain *chain = wl_container_of(addon, chain, addon);
    return chain;
}

static struct ky_scene_node *node_chain_accept_input(struct ky_scene_node *node, int lx, int ly,
                                                     double px, double py, double *rx, double *ry)
{
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    return chain->impl.accpet_input(node, lx, ly, px, py, rx, ry);
}

static void node_chain_update_outputs(struct ky_scene_node *node, int lx, int ly,
                                      struct wl_list *outputs, struct ky_scene_output *ignore,
                                      struct ky_scene_output *force)
{
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    chain->impl.update_outputs(node, lx, ly, outputs, ignore, force);
}

static void node_chain_collect_damage(struct ky_scene_node *node, int lx, int ly,
                                      bool parent_enabled, uint32_t damage_type,
                                      pixman_region32_t *damage, pixman_region32_t *invisible,
                                      pixman_region32_t *affected)
{
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    if (wl_list_empty(&chain->base.slots)) {
        chain->impl.collect_damage(node, lx, ly, parent_enabled, damage_type, damage, invisible,
                                   affected);
    }
}

static void node_chain_push_damage(struct ky_scene_node *node, struct ky_scene_node *damage_node,
                                   uint32_t damage_type, pixman_region32_t *damage)
{
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    if (wl_list_empty(&chain->base.slots)) {
        chain->impl.push_damage(node, damage_node, damage_type, damage);
    }
}

static void node_chain_get_bounding_box(struct ky_scene_node *node, struct wlr_box *box)
{
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    chain->impl.get_bounding_box(node, box);
}

static void node_chain_render(struct ky_scene_node *node, int lx, int ly,
                              struct ky_scene_render_target *target)
{
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    if (wl_list_empty(&chain->base.slots)) {
        chain->impl.render(node, lx, ly, target);
    }
}

static void node_chain_destroy(struct ky_scene_node *node)
{
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    chain->impl.destroy(node);
}

static const struct ky_scene_node_interface node_effect_impl = {
    .accpet_input = node_chain_accept_input,
    .update_outputs = node_chain_update_outputs,
    .collect_damage = node_chain_collect_damage,
    .render = node_chain_render,
    .get_bounding_box = node_chain_get_bounding_box,
    .push_damage = node_chain_push_damage,
    .destroy = node_chain_destroy,
};

void effect_entity_destroy(struct effect_entity *entity)
{
    wl_list_remove(&entity->slot.link);
    wl_list_remove(&entity->slot.chain_destroy.link);
    wl_list_remove(&entity->effect_link);
    wl_list_remove(&entity->effect_enable.link);
    wl_list_remove(&entity->effect_disable.link);
    wl_list_remove(&entity->effect_disable.link);

    free(entity);
}

static void node_effect_chain_addon_destroy(struct wlr_addon *addon)
{
    struct node_effect_chain *chain = wl_container_of(addon, chain, addon);
    wl_signal_emit(&chain->base.events.destroy, NULL);
    wlr_addon_finish(&chain->addon);
    chain->node->impl = chain->impl;
    free(chain);
}

static const struct wlr_addon_interface effect_addon_impl = {
    .name = "node_effect_chain",
    .destroy = node_effect_chain_addon_destroy,
};

static struct node_effect_chain *node_effec_chain_create(struct ky_scene_node *node)
{
    struct node_effect_chain *chain = calloc(1, sizeof(*chain));
    if (!chain) {
        return NULL;
    }

    wl_list_init(&chain->base.slots);
    wl_signal_init(&chain->base.events.destroy);

    chain->node = node;
    chain->impl = node->impl;
    node->impl = node_effect_impl;
    wlr_addon_init(&chain->addon, &node->addons, node, &effect_addon_impl);

    return chain;
}

static void entity_handle_chain_destroy(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity = wl_container_of(listener, entity, slot.chain_destroy);
    effect_entity_destroy(entity);
}

static void entity_handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity = wl_container_of(listener, entity, effect_destroy);
    effect_entity_destroy(entity);
}

static void entity_handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity = wl_container_of(listener, entity, effect_disable);
    wl_list_remove(&entity->slot.link);
    wl_list_init(&entity->slot.link);
}

static void entity_handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity = wl_container_of(listener, entity, effect_enable);
    struct wl_list *list = &entity->slot.chain->slots;

    struct effect_entity *_entity;
    struct effect_slot *slot;
    wl_list_for_each(slot, &entity->slot.chain->slots, link) {
        _entity = wl_container_of(slot, _entity, slot);
        if (_entity->effect->priority < entity->effect->priority) {
            break;
        }
        list = &slot->link;
    }

    wl_list_insert(list, &entity->slot.link);
}

struct effect_entity *ky_scene_node_add_effect(struct ky_scene_node *node, struct effect *effect)
{
    struct wlr_addon *addon = wlr_addon_find(&node->addons, node, &effect_addon_impl);
    struct node_effect_chain *chain =
        addon ? wl_container_of(addon, chain, addon) : node_effec_chain_create(node);
    if (!chain) {
        return NULL;
    }

    /* find a list to insert */
    struct wl_list *list = &chain->base.slots;
    struct effect_entity *entity;

    struct effect_slot *slot;
    wl_list_for_each(slot, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, slot);
        if (entity->effect == effect) {
            kywc_log(KYWC_WARN, "effect %s is already added", effect->name);
            return entity;
        }
        /* update list by priority */
        if (entity->effect->priority < effect->priority) {
            break;
        }
        list = &slot->link;
    }

    entity = calloc(1, sizeof(*entity));
    if (!entity) {
        return NULL;
    }

    entity->slot.chain = &chain->base;
    wl_list_insert(list, &entity->slot.link);
    entity->slot.chain_destroy.notify = entity_handle_chain_destroy;
    wl_signal_add(&chain->base.events.destroy, &entity->slot.chain_destroy);

    entity->effect = effect;
    wl_list_insert(&effect->entities, &entity->effect_link);
    entity->effect_enable.notify = entity_handle_effect_enable;
    wl_signal_add(&effect->events.enable, &entity->effect_enable);
    entity->effect_disable.notify = entity_handle_effect_disable;
    wl_signal_add(&effect->events.disable, &entity->effect_disable);
    entity->effect_destroy.notify = entity_handle_effect_destroy;
    wl_signal_add(&effect->events.destroy, &entity->effect_destroy);

    return entity;
}

void effect_set_enabled(struct effect *effect, bool enabled)
{
    if (effect->enabled == enabled) {
        return;
    }

    effect->enabled = enabled;

    if (enabled) {
        wl_signal_emit_mutable(&effect->events.enable, NULL);
    } else {
        wl_signal_emit_mutable(&effect->events.disable, NULL);
    }
}

void effect_destroy(struct effect *effect)
{
    if (effect->enabled) {
        wl_signal_emit_mutable(&effect->events.disable, NULL);
    }
    wl_signal_emit_mutable(&effect->events.destroy, NULL);
    // assert(wl_list_empty(&effect->entities));

    wl_list_remove(&effect->link);
    free((void *)effect->uuid);
    free((void *)effect->name);
    free(effect);
}

struct effect *effect_create(const char *name, int priority, bool enabled,
                             const struct effect_interface *impl)
{
    struct effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return NULL;
    }

    effect->uuid = kywc_identifier_uuid_generate();
    effect->name = strdup(name);
    effect->priority = priority;
    effect->impl = impl;
    effect->enabled = enabled;

    wl_list_init(&effect->entities);
    wl_signal_init(&effect->events.enable);
    wl_signal_init(&effect->events.disable);
    wl_signal_init(&effect->events.destroy);
    wl_list_insert(&manager->effects, &effect->link);

    return effect;
}

struct effect *effect_by_uuid(const char *uuid)
{
    struct effect *effect;
    wl_list_for_each(effect, &manager->effects, link) {
        if (strcmp(effect->uuid, uuid) == 0) {
            return effect;
        }
    }
    return NULL;
}

struct effect *effect_by_name(const char *name)
{
    struct effect *effect;
    wl_list_for_each(effect, &manager->effects, link) {
        if (strcmp(effect->name, name) == 0) {
            return effect;
        }
    }
    return NULL;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);

    struct effect *effect, *tmp;
    wl_list_for_each_safe(effect, tmp, &manager->effects, link) {
        effect_destroy(effect);
    }

    free(manager);
    manager = NULL;
}

bool effect_manager_create(struct server *server)
{
    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->effects);

    manager->server = server;
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    effect_manager_config_init(manager);

    animation_manager_create(server);
    thumbnail_manager_create(server);

    capture_manager_create(server);
    ky_capture_manager_create(server);

    /* builtin effects */
    showfps_effect_create(manager);

    return true;
}
