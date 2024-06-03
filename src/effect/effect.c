// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <stdlib.h>

#include <kywc/identifier.h>
#include <kywc/log.h>

#include "effect_p.h"
#include "scene/animation.h"
#include "scene/thumbnail.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

enum effect_type {
    EFFECT_TYPE_NONE = 0,
    EFFECT_TYPE_NODE = 1 << 0,
    EFFECT_TYPE_VIEW = 1 << 1,
    EFFECT_TYPE_SCENE = 1 << 2,
};

static struct effect_manager *manager = NULL;
static const struct wlr_addon_interface effect_addon_impl;

static uint32_t get_effect_types(const struct effect_interface *impl)
{
    uint32_t types = 0;
    if (!impl) {
        return 0;
    }
    if (impl->frame_render || impl->frame_render_pre || impl->frame_render_begin ||
        impl->frame_render_end || impl->frame_render_post) {
        types |= EFFECT_TYPE_SCENE;
    }
    if (impl->node_render) {
        types |= EFFECT_TYPE_NODE;
    }

    return types;
}

static void entities_clip_region(struct node_effect_chain *chain, pixman_region32_t *region)
{
    if (wl_list_empty(&chain->base.slots)) {
        return;
    }

    pixman_region32_clear(region);
    pixman_region32_t clip_region;
    pixman_region32_init(&clip_region);

    struct effect_entity *entity;
    struct effect_slot *slot;
    wl_list_for_each_reverse(slot, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, slot);
        if (!entity->effect->impl->entity_clip_region) {
            continue;
        }
        bool ret = entity->effect->impl->entity_clip_region(entity, &clip_region);
        pixman_region32_union(region, region, &clip_region);
        if (!ret) {
            break;
        }
    }

    pixman_region32_fini(&clip_region);
}

static void entities_opaque_region(struct node_effect_chain *chain, pixman_region32_t *region)
{
    if (wl_list_empty(&chain->base.slots)) {
        return;
    }

    pixman_region32_clear(region);
    pixman_region32_t opaque_region;
    pixman_region32_init(&opaque_region);

    struct effect_entity *entity;
    struct effect_slot *slot;
    wl_list_for_each_reverse(slot, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, slot);
        if (!entity->effect->impl->entity_opaque_region) {
            continue;
        }
        bool ret = entity->effect->impl->entity_opaque_region(entity, &opaque_region);
        pixman_region32_union(region, region, &opaque_region);
        if (!ret) {
            break;
        }
    }

    pixman_region32_fini(&opaque_region);
}

static bool entities_bounding_box(struct node_effect_chain *chain, struct wlr_box *box)
{
    bool valid_box = false;
    if (wl_list_empty(&chain->base.slots)) {
        *box = (struct wlr_box){ 0 };
        return valid_box;
    }

    int min_x1 = INT_MAX, min_y1 = INT_MAX;
    int max_x2 = INT_MIN, max_y2 = INT_MIN;

    struct kywc_box child_box;
    struct effect_entity *entity;
    struct effect_slot *slot;
    wl_list_for_each_reverse(slot, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, slot);
        if (!entity->effect->impl->entity_bounding_box) {
            continue;
        }
        valid_box = true;
        bool ret = entity->effect->impl->entity_bounding_box(entity, &child_box);
        min_x1 = MIN(min_x1, child_box.x);
        min_y1 = MIN(min_y1, child_box.y);
        max_x2 = MAX(max_x2, child_box.x + child_box.width);
        max_y2 = MAX(max_y2, child_box.y + child_box.height);
        if (!ret) {
            break;
        }
    }

    box->x = min_x1 == INT_MAX ? 0 : min_x1;
    box->y = min_y1 == INT_MAX ? 0 : min_y1;
    box->width = max_x2 == INT_MIN ? 0 : max_x2 - min_x1;
    box->height = max_y2 == INT_MIN ? 0 : max_y2 - min_y1;
    return valid_box;
}

static void entities_collect_damage(struct node_effect_chain *chain, int lx, int ly,
                                    bool parent_enabled, uint32_t damage_type,
                                    pixman_region32_t *damage, pixman_region32_t *invisible,
                                    pixman_region32_t *affected)
{
    struct wlr_box box;
    struct ky_scene_node *node = chain->node;
    /* node_chain_get_bounding_box */
    node->impl.get_bounding_box(node, &box);

    /* if node state is changed, it must in the affected region */
    if (box.width > 0 && box.height > 0 &&
        pixman_region32_contains_rectangle(
            affected, &(pixman_box32_t){ lx + box.x, ly + box.y, lx + box.x + box.width,
                                         ly + box.y + box.height }) == PIXMAN_REGION_OUT) {
        return;
    }

    bool node_enabled = parent_enabled && node->enabled;
    bool no_damage = node->last_enabled && node_enabled && damage_type == KY_SCENE_DAMAGE_NONE;
    if (!no_damage) {
        /* when node disable or set position, node last visible region is added to damgae */
        if (node->last_enabled && (!node_enabled || (damage_type & KY_SCENE_DAMAGE_HARMFUL))) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }
    }

    pixman_region32_clear(&node->visible_region);
    pixman_region32_t opaque_region, clip_region;
    pixman_region32_init(&opaque_region);
    pixman_region32_init(&clip_region);
    bool visible = node_enabled;
    if (visible) {
        entities_clip_region(chain, &clip_region);
        bool has_clip_region = pixman_region32_not_empty(&clip_region);
        if (has_clip_region) {
            pixman_region32_intersect_rect(&node->visible_region, &clip_region, 0, 0, box.width,
                                           box.height);
            pixman_region32_translate(&node->visible_region, lx, ly);
        } else {
            pixman_region32_init_rect(&node->visible_region, lx + box.x, ly + box.y, box.width,
                                      box.height);
        }
        pixman_region32_subtract(&node->visible_region, &node->visible_region, invisible);
        if (!no_damage) {
            pixman_region32_union(damage, damage, &node->visible_region);
        }

        pixman_region32_t region;
        pixman_region32_init(&region);
        entities_opaque_region(chain, &opaque_region);
        if (has_clip_region) {
            pixman_region32_intersect(&region, &opaque_region, &clip_region);
        }
        pixman_region32_translate(&region, lx, ly);
        pixman_region32_union(invisible, invisible, &region);
        pixman_region32_fini(&region);
    }

    pixman_region32_fini(&opaque_region);
    pixman_region32_fini(&clip_region);

    node->last_enabled = node_enabled;
    node->damage_type = KY_SCENE_DAMAGE_NONE;
}

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
    bool is_root = node->parent == NULL;
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    if (wl_list_empty(&chain->base.slots) || is_root) {
        chain->impl.collect_damage(node, lx, ly, parent_enabled, damage_type, damage, invisible,
                                   affected);
        return;
    }
    entities_collect_damage(chain, lx, ly, parent_enabled, damage_type, damage, invisible,
                            affected);
}

static void node_chain_push_damage(struct ky_scene_node *node, struct ky_scene_node *damage_node,
                                   uint32_t damage_type, pixman_region32_t *damage)
{
    if (!node->enabled && !node->force_damage_event) {
        return;
    }

    bool is_root = node->parent == NULL;
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    if (wl_list_empty(&chain->base.slots) || is_root) {
        chain->impl.push_damage(node, damage_node, damage_type, damage);
        return;
    }

    /* emit damage when node content damaged or children damaged */
    if ((node == damage_node && node->damage_type & KY_SCENE_DAMAGE_HARMLESS) ||
        node != damage_node) {
        wl_signal_emit_mutable(&node->events.damage, NULL);
    }

    if (!node->enabled) {
        return;
    }

    damage_type |= node->damage_type;

    struct effect_entity *entity;
    struct effect_slot *slot, *tmp;
    wl_list_for_each_reverse_safe(slot, tmp, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, slot);
        if (entity->effect->impl->node_push_damage &&
            !entity->effect->impl->node_push_damage(entity, damage_node, &damage_type, damage)) {
            break;
        }
    }

    pixman_region32_translate(damage, node->x, node->y);
    node->parent->node.impl.push_damage(&node->parent->node, damage_node, damage_type, damage);
}

static void node_chain_get_bounding_box(struct ky_scene_node *node, struct wlr_box *box)
{
    if (!node->enabled) {
        *box = (struct wlr_box){ 0 };
        return;
    }

    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    if (!entities_bounding_box(chain, box)) {
        chain->impl.get_bounding_box(node, box);
    }
}

static void node_chain_render(struct ky_scene_node *node, int lx, int ly,
                              struct ky_scene_render_target *target)
{
    if (!node->enabled) {
        return;
    }

    bool skip_node_effect =
        (node->parent == NULL) || (target->options & KY_SCENE_RENDER_DISABLE_EFFECT);
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    if (wl_list_empty(&chain->base.slots) || skip_node_effect) {
        chain->impl.render(node, lx, ly, target);
        return;
    }

    struct effect_entity *entity;
    struct effect_slot *slot, *temp_slot;
    wl_list_for_each_reverse_safe(slot, temp_slot, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, slot);
        if (entity->effect->impl->node_render &&
            !entity->effect->impl->node_render(entity, lx, ly, target)) {
            return;
        }
    }
    chain->impl.render(node, lx, ly, target);
}

static void node_chain_destroy(struct ky_scene_node *node)
{
    struct node_effect_chain *chain = node_effect_chain_from_node(node);

    ky_scene_add_damage(ky_scene_from_node(node), &node->visible_region);

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
    wl_list_remove(&entity->frame_slot.link);
    wl_list_remove(&entity->frame_slot.chain_destroy.link);

    wl_list_remove(&entity->effect_link);
    wl_list_remove(&entity->effect_enable.link);
    wl_list_remove(&entity->effect_disable.link);
    wl_list_remove(&entity->effect_destroy.link);

    struct effect_chain *chain = entity->slot.chain;
    if (chain) {
        struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
        /**
         * add the region which include effect entity region to collected damage,
         * otherwise the effect entity region will destroy.
         */
        ky_scene_add_damage(ky_scene_from_node(node_chain->node),
                            &node_chain->node->visible_region);
        /**
         * if node is tree, when effect added again visible should be collect again.
         * if node is rect/buffer, visible region will calc in the next frame,
         * if rect/buffer node added effect again before next frame, visible region
         * already added to collect damage, so can clear visible region.
         */
        pixman_region32_clear(&node_chain->node->visible_region);
        /* after removing effect entity, the node may cause damage in other locations */
        ky_scene_node_push_damage(node_chain->node, KY_SCENE_DAMAGE_BOTH, NULL);
    }

    /* if effects just use on scene root node, damage added in entity destroy function */
    if (entity->effect->impl->entity_destroy) {
        entity->effect->impl->entity_destroy(entity);
    }

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

static struct node_effect_chain *node_effect_chain_create(struct ky_scene_node *node)
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

static void frame_entity_handle_chain_destroy(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity = wl_container_of(listener, entity, frame_slot.chain_destroy);
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
    wl_list_remove(&entity->frame_slot.link);
    wl_list_init(&entity->frame_slot.link);
}

static struct wl_list *find_insertion_location(struct effect_entity *entity, bool frame)
{
    struct effect_chain *chain = frame ? entity->frame_slot.chain : entity->slot.chain;
    struct wl_list *list = &chain->slots;

    struct effect_entity *_entity;
    struct effect_slot *slot;
    wl_list_for_each(slot, &chain->slots, link) {
        _entity = frame ? wl_container_of(slot, _entity, frame_slot)
                        : wl_container_of(slot, _entity, slot);
        if (_entity->effect->priority < entity->effect->priority) {
            break;
        }
        list = &slot->link;
    }

    return list;
}

static void entity_handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity = wl_container_of(listener, entity, effect_enable);
    struct wl_list *list;

    if (entity->slot.chain) {
        list = find_insertion_location(entity, false);
        wl_list_insert(list, &entity->slot.link);
    }

    if (entity->frame_slot.chain) {
        list = find_insertion_location(entity, true);
        wl_list_insert(list, &entity->frame_slot.link);
    }
}

static void node_collect_visible(struct ky_scene_node *node, pixman_region32_t *visible)
{
    if (node->type == KY_SCENE_NODE_TREE) {
        struct ky_scene_tree *tree = ky_scene_tree_from_node(node);
        struct ky_scene_node *child_node;
        wl_list_for_each(child_node, &tree->children, link) {
            node_collect_visible(child_node, visible);
        }
    }

    if (pixman_region32_not_empty(&node->visible_region) && visible != &node->visible_region) {
        pixman_region32_union(visible, visible, &node->visible_region);
    }
}

struct effect_entity *ky_scene_node_find_effect_entity(struct ky_scene_node *node,
                                                       struct effect *effect)
{
    bool is_root = node->parent == NULL;
    struct wlr_addon *addon = wlr_addon_find(&node->addons, node, &effect_addon_impl);
    if (!addon) {
        return NULL;
    }
    struct node_effect_chain *chain = wl_container_of(addon, chain, addon);

    struct effect_entity *entity;
    struct effect_slot *slot;
    wl_list_for_each(slot, &chain->base.slots, link) {
        entity = is_root ? wl_container_of(slot, entity, frame_slot)
                         : wl_container_of(slot, entity, slot);
        if (entity->effect == effect) {
            return entity;
        }
    }

    return NULL;
}

struct effect_entity *ky_scene_node_add_effect(struct ky_scene_node *node, struct effect *effect)
{
    bool is_root = node->parent == NULL;
    if (is_root && !(effect->types & EFFECT_TYPE_SCENE)) {
        return NULL;
    } else if (!is_root && !(effect->types & EFFECT_TYPE_NODE)) {
        return NULL;
    }

    struct wlr_addon *addon = wlr_addon_find(&node->addons, node, &effect_addon_impl);
    struct node_effect_chain *chain =
        addon ? wl_container_of(addon, chain, addon) : node_effect_chain_create(node);
    if (!chain) {
        return NULL;
    }

    /* find a list to insert */
    struct wl_list *list = &chain->base.slots;
    struct effect_entity *entity;

    struct effect_slot *slot;
    wl_list_for_each(slot, &chain->base.slots, link) {
        entity = is_root ? wl_container_of(slot, entity, frame_slot)
                         : wl_container_of(slot, entity, slot);
        if (entity->effect == effect && !is_root) {
            kywc_log(KYWC_DEBUG, "effect %s is already added", effect->name);
            return entity;
        }
        /* update list by priority */
        if (entity->effect->priority < effect->priority) {
            break;
        }
        list = &slot->link;
    }

    /**
     * add effects through general nodes. usually, occur when both the
     * scene effects interface and the node effects interface are implemented.
     */
    if ((effect->types & EFFECT_TYPE_SCENE) && !is_root) {
        struct ky_scene *scene = ky_scene_from_node(node);
        entity = ky_scene_add_effect(scene, effect);
        if (!entity) {
            return NULL;
        }
        if (effect->types & EFFECT_TYPE_NODE) {
            entity->slot.chain = &chain->base;
            wl_list_insert(list, &entity->slot.link);
            entity->slot.chain_destroy.notify = entity_handle_chain_destroy;
            wl_signal_add(&chain->base.events.destroy, &entity->slot.chain_destroy);

            if (!pixman_region32_not_empty(&node->visible_region)) {
                node_collect_visible(node, &node->visible_region);
            }
        }
        return entity;
    }

    entity = calloc(1, sizeof(*entity));
    if (!entity) {
        return NULL;
    }

    if (!is_root) {
        entity->slot.chain = &chain->base;
        wl_list_insert(list, &entity->slot.link);
        entity->slot.chain_destroy.notify = entity_handle_chain_destroy;
        wl_signal_add(&chain->base.events.destroy, &entity->slot.chain_destroy);
        wl_list_init(&entity->frame_slot.link);
        wl_list_init(&entity->frame_slot.chain_destroy.link);

        if (!pixman_region32_not_empty(&node->visible_region)) {
            node_collect_visible(node, &node->visible_region);
        }
    } else {
        wl_list_init(&entity->slot.link);
        wl_list_init(&entity->slot.chain_destroy.link);
        entity->frame_slot.chain = &chain->base;
        wl_list_insert(list, &entity->frame_slot.link);
        entity->frame_slot.chain_destroy.notify = frame_entity_handle_chain_destroy;
        wl_signal_add(&chain->base.events.destroy, &entity->frame_slot.chain_destroy);
    }

    entity->effect = effect;
    wl_list_insert(&effect->entities, &entity->effect_link);
    entity->effect_enable.notify = entity_handle_effect_enable;
    wl_signal_add(&effect->events.enable, &entity->effect_enable);
    entity->effect_disable.notify = entity_handle_effect_disable;
    wl_signal_add(&effect->events.disable, &entity->effect_disable);
    entity->effect_destroy.notify = entity_handle_effect_destroy;
    wl_signal_add(&effect->events.destroy, &entity->effect_destroy);

    if (effect->impl->entity_create) {
        effect->impl->entity_create(entity);
    }

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
    effect->types = get_effect_types(impl);

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
    blur_effect_create(manager);
    screenshot_effect_create(manager);
    watermark_effect_create(manager);
    move_effect_create(manager);
    scale_effect_create(manager);
    soft_gamma_effect_create(manager);
    touchclick_effect_create(manager);
    touchtrail_effect_create(manager);
    long_touch_effect_create(manager);
    fade_effect_create(manager);
    slide_effect_create(manager);

    return true;
}

struct effect_entity *ky_scene_add_effect(struct ky_scene *scene, struct effect *effect)
{
    return ky_scene_node_add_effect(&scene->tree.node, effect);
}

enum interface_name {
    RENDER_PRE,
    RENDER_BEGIN,
    RENDER,
    RENDER_END,
    RENDER_POST,
};

#define scene_effect_run(entity, target, interface_name)                                           \
    if (entity->effect->impl->frame_##interface_name &&                                            \
        !entity->effect->impl->frame_##interface_name(entity, target)) {                           \
        return;                                                                                    \
    }

static void scene_output_run_effect(struct ky_scene_output *scene_output, enum interface_name name,
                                    struct ky_scene_render_target *target)
{
    struct ky_scene *scene = scene_output->scene;
    struct ky_scene_node *node = &scene->tree.node;
    struct wlr_addon *addon = wlr_addon_find(&node->addons, node, &effect_addon_impl);
    if (!addon) {
        return;
    }

    struct node_effect_chain *chain = wl_container_of(addon, chain, addon);

    struct effect_entity *entity;
    struct effect_slot *slot, *temp_slot;
    wl_list_for_each_reverse_safe(slot, temp_slot, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, frame_slot);
        switch (name) {
        case RENDER_PRE:
            if (entity->effect->impl->frame_render_pre &&
                !entity->effect->impl->frame_render_pre(entity, scene_output)) {
                return;
            }
            break;
        case RENDER_BEGIN:
            scene_effect_run(entity, target, render_begin);
            break;
        case RENDER:
            break;
        case RENDER_END:
            scene_effect_run(entity, target, render_end);
            break;
        case RENDER_POST:
            scene_effect_run(entity, target, render_post);
            break;
        default:
            break;
        }
    }
}

void ky_scene_output_render_pre(struct ky_scene_output *scene_output)
{
    scene_output_run_effect(scene_output, RENDER_PRE, NULL);
}

void ky_scene_output_render_begin(struct ky_scene_render_target *target)
{
    scene_output_run_effect(target->output, RENDER_BEGIN, target);
}

bool ky_scene_output_render(struct ky_scene_render_target *target)
{
    struct ky_scene *scene = target->output->scene;
    struct ky_scene_node *node = &scene->tree.node;
    struct wlr_addon *addon = wlr_addon_find(&node->addons, node, &effect_addon_impl);
    if (!addon) {
        return false;
    }

    struct node_effect_chain *chain = wl_container_of(addon, chain, addon);
    bool has_rendered = false;

    struct effect_entity *entity;
    struct effect_slot *slot, *temp_slot;
    wl_list_for_each_reverse_safe(slot, temp_slot, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, frame_slot);
        if (entity->effect->impl->frame_render) {
            has_rendered = true;
            entity->effect->impl->frame_render(entity, target);
            return has_rendered;
        }
    }
    return has_rendered;
}

void ky_scene_output_render_end(struct ky_scene_render_target *target)
{
    scene_output_run_effect(target->output, RENDER_END, target);
}

void ky_scene_output_render_post(struct ky_scene_render_target *target)
{
    scene_output_run_effect(target->output, RENDER_POST, target);
}
