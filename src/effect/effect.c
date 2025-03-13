// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#include <kywc/log.h>

#include "effect/animator.h"
#include "effect_p.h"
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

static void wlr_box_to_kywc_box(struct wlr_box *box, struct kywc_box *kywc_box)
{
    kywc_box->x = box->x;
    kywc_box->y = box->y;
    kywc_box->width = box->width;
    kywc_box->height = box->height;
}

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
    if (impl->node_render || impl->node_push_damage || impl->entity_bounding_box ||
        impl->entity_clip_region || impl->entity_opaque_region) {
        types |= EFFECT_TYPE_NODE;
    }

    return types;
}

/* Do not trigger damage event that node with effect entity */
void effect_entity_push_damage(struct effect_entity *entity, uint32_t damage_type)
{
    struct kywc_box box = { 0 };
    if (entity->effect->impl->entity_bounding_box) {
        entity->effect->impl->entity_bounding_box(entity, &box);
    }

    pixman_region32_t damage_region;
    pixman_region32_init_rect(&damage_region, box.x, box.y, box.width, box.height);
    if (!pixman_region32_not_empty(&damage_region)) {
        pixman_region32_fini(&damage_region);
        return;
    }

    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);

    node_chain->damage_type |= damage_type;
    pixman_region32_translate(&damage_region, node_chain->node->x, node_chain->node->y);

    struct ky_scene_node *parent_node = &node_chain->node->parent->node;
    parent_node->impl.push_damage(parent_node, node_chain->node, node_chain->damage_type,
                                  &damage_region);

    pixman_region32_fini(&damage_region);
}

static void calc_bounding(int *min_x1, int *min_y1, int *max_x2, int *max_y2, struct kywc_box *box)
{
    *min_x1 = MIN(*min_x1, box->x);
    *min_y1 = MIN(*min_y1, box->y);
    *max_x2 = MAX(*max_x2, box->x + box->width);
    *max_y2 = MAX(*max_y2, box->y + box->height);
}

void effect_entity_get_bounding_box(struct effect_entity *entity, enum bounding_box_type type,
                                    struct kywc_box *box)
{
    assert(entity->effect->types & EFFECT_TYPE_NODE);

    struct effect_chain *chain = entity->slot.chain;
    struct node_effect_chain *node_chain = wl_container_of(chain, node_chain, base);
    struct ky_scene_node *node = node_chain->node;
    bool stash_enabled = node->enabled;

    struct wlr_box _box;
    if (type == BOUNDING_BOX_NO_EFFECT) {
        node->enabled = true;
        node_chain->impl.get_bounding_box(node, &_box);
        node->enabled = stash_enabled;
        wlr_box_to_kywc_box(&_box, box);
        return;
    }

    int min_x1 = INT_MAX, min_y1 = INT_MAX;
    int max_x2 = INT_MIN, max_y2 = INT_MIN;

    bool ret = false;
    struct kywc_box child_box;
    struct effect_entity *other_entity;
    struct effect_slot *slot;
    /* low priority to high priority */
    wl_list_for_each_reverse(slot, &node_chain->base.slots, link) {
        other_entity = wl_container_of(slot, other_entity, slot);
        /**
         * the higher the value, the lower the priority.
         * only getting bounding box for higher priority effects.
         */
        if ((other_entity->effect->priority >= entity->effect->priority &&
             other_entity != entity) ||
            !other_entity->effect->impl->entity_bounding_box) {
            continue;
        }

        ret = other_entity->effect->impl->entity_bounding_box(other_entity, &child_box);
        calc_bounding(&min_x1, &min_y1, &max_x2, &max_y2, &child_box);
        if (!ret) {
            break;
        }
    }

    if (!ret) {
        node_chain->impl.get_bounding_box(node, &_box);
        wlr_box_to_kywc_box(&_box, box);
        calc_bounding(&min_x1, &min_y1, &max_x2, &max_y2, box);
    }

    box->x = min_x1 == INT_MAX ? 0 : min_x1;
    box->y = min_y1 == INT_MAX ? 0 : min_y1;
    box->width = max_x2 == INT_MIN ? 0 : max_x2 - min_x1;
    box->height = max_y2 == INT_MIN ? 0 : max_y2 - min_y1;
}

static bool entities_enabled(struct node_effect_chain *chain)
{
    struct ky_scene_node *node = chain->node;
    if (!node) {
        return false;
    }

    struct effect_slot *slot;
    struct effect_entity *entity;
    bool enable = false, is_root = node->parent == NULL;
    wl_list_for_each(slot, &chain->base.slots, link) {
        entity = is_root ? wl_container_of(slot, entity, frame_slot)
                         : wl_container_of(slot, entity, slot);
        enable |= entity->enabled;
        if (enable) {
            break;
        }
    }

    return enable;
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

/* if return false, box value isn't valid */
static bool entities_bounding_box(struct node_effect_chain *chain, struct kywc_box *box)
{
    if (wl_list_empty(&chain->base.slots)) {
        *box = (struct kywc_box){ 0 };
        return false;
    }

    struct effect_slot *slot = wl_container_of(chain->base.slots.next, slot, link);
    struct effect_entity *first_entity = wl_container_of(slot, first_entity, slot);
    effect_entity_get_bounding_box(first_entity, BOUNDING_BOX_WITH_EFFECT, box);
    return true;
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
    return chain->impl.accept_input(node, lx, ly, px, py, rx, ry);
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
        pixman_region32_union(damage, damage, &chain->visible_region);
        pixman_region32_clear(&chain->visible_region);
        chain->impl.collect_damage(node, lx, ly, parent_enabled, damage_type, damage, invisible,
                                   affected);
        return;
    }

    bool chain_enabled = entities_enabled(chain);
    if (!chain->last_enabled && !chain_enabled) {
        return;
    }

    /**
     * if node state is changed, some entities maybe not in the affected region.
     * so it can't be skipped by affected region.
     */

    damage_type |= chain->damage_type;
    bool no_damage = chain_enabled == chain->last_enabled && damage_type == KY_SCENE_DAMAGE_NONE;
    if (!no_damage) {
        /**
         * if chain has KY_SCENE_DAMAGE_HARMFUL damage or chain changes to disabled,
         * last visible region is added to damgae.
         */
        if (chain->last_enabled && (!chain_enabled || damage_type & KY_SCENE_DAMAGE_HARMFUL)) {
            pixman_region32_union(damage, damage, &chain->visible_region);
        }
    }

    pixman_region32_clear(&chain->visible_region);

    if (!chain_enabled) {
        chain->last_enabled = chain_enabled;
        chain->damage_type = KY_SCENE_DAMAGE_NONE;
        node->damage_type = KY_SCENE_DAMAGE_NONE;
        return;
    }

    /* recalc visible region */
    struct kywc_box box;
    if (!entities_bounding_box(chain, &box)) {
        return;
    }

    pixman_region32_union_rect(&chain->visible_region, &chain->visible_region, box.x, box.y,
                               box.width, box.height);

    pixman_region32_t clip_region;
    pixman_region32_init(&clip_region);
    entities_clip_region(chain, &clip_region);
    bool has_clip_region = pixman_region32_not_empty(&clip_region);
    if (has_clip_region) {
        pixman_region32_intersect(&chain->visible_region, &chain->visible_region, &clip_region);
    }

    pixman_region32_translate(&chain->visible_region, lx, ly);
    pixman_region32_subtract(&chain->visible_region, &chain->visible_region, invisible);
    if (!no_damage) {
        pixman_region32_union(damage, damage, &chain->visible_region);
    }

    pixman_region32_t opaque_region;
    pixman_region32_init(&opaque_region);
    entities_opaque_region(chain, &opaque_region);
    if (has_clip_region) {
        pixman_region32_intersect(&opaque_region, &opaque_region, &clip_region);
    }
    pixman_region32_translate(&opaque_region, lx, ly);
    pixman_region32_subtract(invisible, invisible, &opaque_region);
    pixman_region32_fini(&opaque_region);
    pixman_region32_fini(&clip_region);

    chain->last_enabled = chain_enabled;
    chain->damage_type = KY_SCENE_DAMAGE_NONE;
    /**
     * although the node already has effect entities,
     * it's damage_type can still be modified by ky_scene_node_push_damage.
     */
    node->damage_type = KY_SCENE_DAMAGE_NONE;
}

static void node_chain_push_damage(struct ky_scene_node *node, struct ky_scene_node *damage_node,
                                   uint32_t damage_type, pixman_region32_t *damage)
{
    bool is_root = node->parent == NULL;
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    if (wl_list_empty(&chain->base.slots) || is_root) {
        chain->impl.push_damage(node, damage_node, damage_type, damage);
        return;
    }

    bool chain_enabled = entities_enabled(chain);
    bool enable = chain_enabled | node->enabled;
    if (!enable && !node->force_damage_event) {
        return;
    }

    damage_type |= chain->damage_type | node->damage_type;
    /* emit damage when node content damaged or children damaged */
    if ((node == damage_node && damage_type & KY_SCENE_DAMAGE_HARMLESS) || node != damage_node) {
        wl_signal_emit_mutable(&node->events.damage, NULL);
    }

    struct effect_entity *entity;
    struct effect_slot *slot, *tmp;
    uint32_t tmp_damage_type = damage_type;
    wl_list_for_each_reverse_safe(slot, tmp, &chain->base.slots, link) {
        entity = wl_container_of(slot, entity, slot);
        if (entity->effect->impl->node_push_damage &&
            !entity->effect->impl->node_push_damage(entity, damage_node, &tmp_damage_type,
                                                    damage)) {
            break;
        }
    }

    chain->damage_type = tmp_damage_type | damage_type;
    pixman_region32_translate(damage, node->x, node->y);
    node->parent->node.impl.push_damage(&node->parent->node, damage_node, chain->damage_type,
                                        damage);
}

static void node_chain_get_bounding_box(struct ky_scene_node *node, struct wlr_box *box)
{
    /* always use origin function */
    struct node_effect_chain *chain = node_effect_chain_from_node(node);
    chain->impl.get_bounding_box(node, box);
}

static void node_chain_render(struct ky_scene_node *node, int lx, int ly,
                              struct ky_scene_render_target *target)
{
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

    ky_scene_add_damage(ky_scene_from_node(node), &chain->visible_region);
    pixman_region32_fini(&chain->visible_region);

    chain->impl.destroy(node);
}

static const struct ky_scene_node_interface node_effect_impl = {
    .accept_input = node_chain_accept_input,
    .update_outputs = node_chain_update_outputs,
    .collect_damage = node_chain_collect_damage,
    .render = node_chain_render,
    .get_bounding_box = node_chain_get_bounding_box,
    .push_damage = node_chain_push_damage,
    .destroy = node_chain_destroy,
};

void effect_entity_destroy(struct effect_entity *entity)
{
    /* don't trigger node damage event */
    effect_entity_push_damage(entity, KY_SCENE_DAMAGE_BOTH);

    wl_list_remove(&entity->slot.link);
    wl_list_remove(&entity->slot.chain_destroy.link);
    wl_list_remove(&entity->frame_slot.link);
    wl_list_remove(&entity->frame_slot.chain_destroy.link);

    wl_list_remove(&entity->effect_link);
    wl_list_remove(&entity->effect_enable.link);
    wl_list_remove(&entity->effect_disable.link);
    wl_list_remove(&entity->effect_destroy.link);

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

static void node_effect_chain_set_visible_region(struct node_effect_chain *chain,
                                                 struct ky_scene_node *node)
{
    if (node->type == KY_SCENE_NODE_TREE) {
        struct ky_scene_tree *tree = ky_scene_tree_from_node(node);
        struct ky_scene_node *child_node;
        wl_list_for_each(child_node, &tree->children, link) {
            node_effect_chain_set_visible_region(chain, child_node);
        }
    }

    if (pixman_region32_not_empty(&node->visible_region)) {
        pixman_region32_union(&chain->visible_region, &chain->visible_region,
                              &node->visible_region);
    }
}

static struct node_effect_chain *node_effect_chain_create(struct ky_scene_node *node)
{
    struct node_effect_chain *chain = calloc(1, sizeof(*chain));
    if (!chain) {
        return NULL;
    }

    chain->last_enabled = false;
    chain->damage_type = node->damage_type;
    pixman_region32_init(&chain->visible_region);
    wl_signal_init(&chain->base.events.destroy);
    wl_list_init(&chain->base.slots);

    chain->node = node;
    chain->impl = node->impl;
    node->impl = node_effect_impl;
    wlr_addon_init(&chain->addon, &node->addons, node, &effect_addon_impl);

    /* node isn't root */
    if (node->parent) {
        node_effect_chain_set_visible_region(chain, node);
    }

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

static void entity_insert_to_chain(struct effect_entity *entity, bool frame)
{
    struct effect_chain *chain = frame ? entity->frame_slot.chain : entity->slot.chain;
    if (!chain) {
        return;
    }

    struct wl_list *list = &chain->slots;
    struct effect_entity *_entity;
    struct effect_slot *slot;
    wl_list_for_each(slot, &chain->slots, link) {
        _entity = frame ? wl_container_of(slot, _entity, frame_slot)
                        : wl_container_of(slot, _entity, slot);
        if (_entity->effect->priority > entity->effect->priority) {
            break;
        }
        list = &slot->link;
    }

    if (frame) {
        wl_list_insert(list, &entity->frame_slot.link);
    } else {
        struct node_effect_chain *node_chain = (struct node_effect_chain *)chain;
        if (wl_list_empty(&chain->slots) &&
            !pixman_region32_not_empty(&node_chain->visible_region)) {
            node_effect_chain_set_visible_region(node_chain, node_chain->node);
        }
        wl_list_insert(list, &entity->slot.link);
    }
}

static void entity_handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct effect_entity *entity = wl_container_of(listener, entity, effect_enable);

    if (entity->slot.chain) {
        entity_insert_to_chain(entity, false);
    }

    if (entity->frame_slot.chain) {
        entity_insert_to_chain(entity, true);
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

    /**
     * add effects through general nodes. usually, occur when both the
     * scene effects interface and the node effects interface are implemented.
     */
    if ((effect->types & EFFECT_TYPE_SCENE) && !is_root) {
        struct ky_scene *scene = ky_scene_from_node(node);
        struct effect_entity *entity = ky_scene_add_effect(scene, effect);
        if (!entity) {
            return NULL;
        }

        if (effect->types & EFFECT_TYPE_NODE) {
            entity->slot.chain = &chain->base;
            wl_list_init(&entity->slot.link);
            entity->slot.chain_destroy.notify = entity_handle_chain_destroy;
            wl_signal_add(&chain->base.events.destroy, &entity->slot.chain_destroy);

            if (effect->enabled) {
                entity_insert_to_chain(entity, false);
            }
        }

        return entity;
    }

    struct effect_entity *entity = calloc(1, sizeof(*entity));
    if (!entity) {
        return NULL;
    }

    entity->enabled = true;

    if (!is_root) {
        entity->slot.chain = &chain->base;
        wl_list_init(&entity->slot.link);
        entity->slot.chain_destroy.notify = entity_handle_chain_destroy;
        wl_signal_add(&chain->base.events.destroy, &entity->slot.chain_destroy);
        wl_list_init(&entity->frame_slot.link);
        wl_list_init(&entity->frame_slot.chain_destroy.link);
    } else {
        wl_list_init(&entity->slot.link);
        wl_list_init(&entity->slot.chain_destroy.link);
        entity->frame_slot.chain = &chain->base;
        wl_list_init(&entity->frame_slot.link);
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

    if (effect->enabled) {
        entity_insert_to_chain(entity, is_root);
    }

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
    effect->destroying = true;

    if (effect->enabled) {
        wl_signal_emit_mutable(&effect->events.disable, NULL);
    }
    wl_signal_emit_mutable(&effect->events.destroy, NULL);
    // assert(wl_list_empty(&effect->entities));

    wl_list_remove(&effect->link);
    free((void *)effect->name);
    free(effect);
}

struct effect *effect_create(const char *name, int priority, bool enabled,
                             const struct effect_interface *impl, void *data)
{
    struct effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return NULL;
    }

    effect->name = strdup(name);
    effect->priority = priority;
    effect->impl = impl;
    effect->enabled = enabled;
    effect->types = get_effect_types(impl);
    effect->user_data = data;

    wl_list_init(&effect->entities);
    wl_signal_init(&effect->events.enable);
    wl_signal_init(&effect->events.disable);
    wl_signal_init(&effect->events.destroy);
    wl_list_insert(&manager->effects, &effect->link);

    effect->manager = manager;
    effect->enabled = effect_init_config(effect);

    return effect;
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

    animation_manager_create(server);
    thumbnail_manager_create(server);
    capture_manager_create(server);
    ky_capture_manager_create(server);

    manager->server = server;
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    effect_manager_config_init(manager);

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
    translation_effect_create(manager);
    output_transform_effect_create(manager);

    shake_cursor_effect_create(manager);
    shake_view_effect_create(manager);
    locate_pointer_effect_create(manager);
    magic_lamp_effect_create(manager);
    zoom_effect_create(manager);
    node_transform_effect_create(manager);

    return true;
}

struct effect_entity *ky_scene_find_effect_entity(struct ky_scene *scene, struct effect *effect)
{
    return ky_scene_node_find_effect_entity(&scene->tree.node, effect);
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
                !entity->effect->impl->frame_render_pre(entity, target)) {
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

void ky_scene_output_render_pre(struct ky_scene_render_target *target)
{
    scene_output_run_effect(target->output, RENDER_PRE, target);
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
