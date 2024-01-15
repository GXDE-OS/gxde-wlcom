// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include "scene_p.h"

#if HAVE_WLR_SCENE

struct ky_scene *ky_scene_from_node(struct ky_scene_node *node)
{
    struct ky_scene_tree *tree;
    if (node->type == WLR_SCENE_NODE_TREE) {
        tree = wl_container_of(node, tree, node);
    } else {
        tree = node->parent;
    }

    while (tree->node.parent != NULL) {
        tree = tree->node.parent;
    }
    return (struct ky_scene *)tree;
}

#else

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#include <wlr/types/wlr_presentation_time.h>

#include <kywc/log.h>

/**
 * basic scene node
 */

static struct ky_scene_node *node_accpet_input(struct ky_scene_node *node, int lx, int ly,
                                               double px, double py, double *rx, double *ry)
{
    kywc_log(KYWC_ERROR, "Need to implement accpet_input interface!");
    assert(false);
    return NULL;
}

static void node_update_outputs(struct ky_scene_node *node, int lx, int ly, struct wl_list *outputs,
                                struct ky_scene_output *ignore, struct ky_scene_output *force)
{
    kywc_log(KYWC_ERROR, "Need to implement update_outputs interface!");
    assert(false);
}

static void node_collect_damage(struct ky_scene_node *node, int lx, int ly, bool parent_enabled,
                                bool damage_all, pixman_region32_t *damage,
                                pixman_region32_t *invisible)
{
    kywc_log(KYWC_ERROR, "Need to implement collect_damage interface!");
    assert(false);
}

static void node_render(struct ky_scene_node *node, int lx, int ly,
                        struct ky_scene_render_target *target)
{
    kywc_log(KYWC_ERROR, "Need to implement generate_render_interface!");
    assert(false);
}

static void node_destroy(struct ky_scene_node *node)
{
    wlr_addon_set_finish(&node->addons);
    ky_scene_node_set_enabled(node, false);

    wl_list_remove(&node->link);
    free(node);
}

void ky_scene_node_init(struct ky_scene_node *node, struct ky_scene_tree *parent)
{
    *node = (struct ky_scene_node){
        .parent = parent,
        .enabled = true,
        .impl = {
            .accpet_input = node_accpet_input,
            .update_outputs = node_update_outputs,
            .collect_damage = node_collect_damage,
            .render = node_render,
            .destroy = node_destroy,
        },
    };

    wl_list_init(&node->link);
    wl_signal_init(&node->events.destroy);

    if (parent != NULL) {
        wl_list_insert(parent->children.prev, &node->link);
    }

    wlr_addon_set_init(&node->addons);
}

/**
 * scene tree
 */

struct ky_scene_tree *ky_scene_tree_from_node(struct ky_scene_node *node)
{
    struct ky_scene_tree *tree = wl_container_of(node, tree, node);
    return tree;
}

static struct ky_scene_node *tree_accpet_input(struct ky_scene_node *node, int lx, int ly,
                                               double px, double py, double *rx, double *ry)
{
    /* skip disabled or input bypassed nodes */
    if (!node->enabled || node->bypassed) {
        return NULL;
    }

    struct ky_scene_tree *scene_tree = ky_scene_tree_from_node(node);
    struct ky_scene_node *child, *found;
    double nx, ny;

    wl_list_for_each_reverse(child, &scene_tree->children, link) {
        found = child->impl.accpet_input(child, lx + child->x, ly + child->y, px, py, &nx, &ny);
        if (found) {
            *rx = nx;
            *ry = ny;
            return found;
        }
    }

    return NULL;
}

static void tree_update_outputs(struct ky_scene_node *node, int lx, int ly, struct wl_list *outputs,
                                struct ky_scene_output *ignore, struct ky_scene_output *force)
{
    // skip node enabled check
    struct ky_scene_tree *scene_tree = ky_scene_tree_from_node(node);
    struct ky_scene_node *child;

    wl_list_for_each(child, &scene_tree->children, link) {
        child->impl.update_outputs(child, lx + child->x, ly + child->y, outputs, ignore, force);
    }
}

static void tree_collect_damage(struct ky_scene_node *node, int lx, int ly, bool parent_enabled,
                                bool damage_all, pixman_region32_t *damage,
                                pixman_region32_t *invisible)
{
    bool node_enabled = parent_enabled && node->enabled;
    /* node is still disabled, skip it */
    if (!node_enabled && !node->last_enabled) {
        node->update_mask = KY_SCENE_NODE_UPDATE_NONE;
        return;
    }

    struct ky_scene_tree *scene_tree = ky_scene_tree_from_node(node);
    /* should damage all if position or layer changed */
    bool node_damage_all =
        damage_all || (node->last_enabled != node_enabled) ||
        (node->update_mask & (KY_SCENE_NODE_UPDATE_POSITION | KY_SCENE_NODE_UPDATE_LAYER));

    struct ky_scene_node *child;
    wl_list_for_each_reverse(child, &scene_tree->children, link) {
        child->impl.collect_damage(child, lx + child->x, ly + child->y, node_enabled,
                                   node_damage_all, damage, invisible);
    }

    node->last_enabled = node_enabled;
    node->update_mask = KY_SCENE_NODE_UPDATE_NONE;
}

static void tree_render(struct ky_scene_node *node, int lx, int ly,
                        struct ky_scene_render_target *target)
{
    if (!node->enabled) {
        return;
    }

    struct ky_scene_tree *scene_tree = ky_scene_tree_from_node(node);
    struct ky_scene_node *child;

    /* from bottom to top */
    wl_list_for_each(child, &scene_tree->children, link) {
        child->impl.render(child, lx + child->x, ly + child->y, target);
    }
}

static void tree_destroy(struct ky_scene_node *node)
{
    struct ky_scene_tree *tree = ky_scene_tree_from_node(node);

    struct ky_scene_node *child, *child_tmp;
    wl_list_for_each_safe(child, child_tmp, &tree->children, link) {
        ky_scene_node_destroy(child);
    }

    /* just call node_destroy, we not save destroy_func in scene_tree */
    node_destroy(node);
}

static void ky_scene_tree_init(struct ky_scene_tree *tree, struct ky_scene_tree *parent)
{
    *tree = (struct ky_scene_tree){ 0 };
    ky_scene_node_init(&tree->node, parent);

    tree->node.type = KY_SCENE_NODE_TREE;

    /* override node interface */
    tree->node.impl.accpet_input = tree_accpet_input;
    tree->node.impl.update_outputs = tree_update_outputs;
    tree->node.impl.collect_damage = tree_collect_damage;
    tree->node.impl.render = tree_render;
    tree->node.impl.destroy = tree_destroy;

    wl_list_init(&tree->children);
}

/**
 * scene root based on tree
 */

static void scene_destroy(struct ky_scene_node *node)
{
    struct ky_scene *scene = ky_scene_from_node(node);
    wl_list_remove(&scene->presentation_destroy.link);
    pixman_region32_fini(&scene->pending_damage);

    struct ky_scene_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &scene->outputs, link) {
        ky_scene_output_destroy(output);
    }

    scene->tree_destroy(node);
}

struct ky_scene *ky_scene_create(void)
{
    struct ky_scene *scene = calloc(1, sizeof(struct ky_scene));
    if (!scene) {
        return NULL;
    }

    ky_scene_tree_init(&scene->tree, NULL);
    scene->tree.node.role = KY_SCENE_NODE_ROOT;

    scene->tree_destroy = scene->tree.node.impl.destroy;
    scene->tree.node.impl.destroy = scene_destroy;

    wl_list_init(&scene->outputs);
    wl_list_init(&scene->presentation_destroy.link);
    pixman_region32_init(&scene->pending_damage);

    return scene;
}

struct ky_scene *ky_scene_from_node(struct ky_scene_node *node)
{
    struct ky_scene_tree *tree = node->parent;
    if (!tree) {
        tree = ky_scene_tree_from_node(node);
    } else {
        while (tree->node.parent != NULL) {
            tree = tree->node.parent;
        }
    }
    struct ky_scene *scene = wl_container_of(tree, scene, tree);
    return scene;
}

struct ky_scene_tree *ky_scene_tree_create(struct ky_scene_tree *parent)
{
    assert(parent);
    struct ky_scene_tree *tree = calloc(1, sizeof(struct ky_scene_tree));
    if (!tree) {
        return NULL;
    }

    ky_scene_tree_init(tree, parent);
    return tree;
}

static void scene_handle_presentation_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene *scene = wl_container_of(listener, scene, presentation_destroy);
    wl_list_remove(&scene->presentation_destroy.link);
    wl_list_init(&scene->presentation_destroy.link);
    scene->presentation = NULL;
}

void ky_scene_set_presentation(struct ky_scene *scene, struct wlr_presentation *presentation)
{
    assert(scene->presentation == NULL);
    scene->presentation = presentation;
    scene->presentation_destroy.notify = scene_handle_presentation_destroy;
    wl_signal_add(&presentation->events.destroy, &scene->presentation_destroy);
}

void ky_scene_collect_damage_in_box(struct ky_scene *scene, struct wlr_box *box,
                                    pixman_region32_t *damage)
{
    pixman_region32_t invisible;
    pixman_region32_init(&invisible);

    // get current damage in all scene nodes
    struct ky_scene_node *root = &scene->tree.node;
    root->impl.collect_damage(root, root->x, root->y, true, false, &scene->pending_damage,
                              &invisible);
    pixman_region32_fini(&invisible);

    pixman_region32_intersect_rect(damage, &scene->pending_damage, box->x, box->y, box->width,
                                   box->height);
    pixman_region32_subtract(&scene->pending_damage, &scene->pending_damage, damage);
}

/**
 * scene node operations
 */

void ky_scene_node_destroy(struct ky_scene_node *node)
{
    if (!node) {
        return;
    }
    /**
     * Destroy node must emit signal first, the destroy_node doesn't emit destroy signal.
     */
    wl_signal_emit_mutable(&node->events.destroy, NULL);
    node->impl.destroy(node);
}

void ky_scene_node_set_enabled(struct ky_scene_node *node, bool enabled)
{
    if (node->enabled == enabled) {
        return;
    }

    node->enabled = enabled;
}

void ky_scene_node_set_position(struct ky_scene_node *node, int x, int y)
{
    if (node->x == x && node->y == y) {
        return;
    }

    node->x = x;
    node->y = y;

    node->update_mask |= KY_SCENE_NODE_UPDATE_POSITION;
    ky_scene_node_update_outputs(node, NULL, NULL, NULL);
}

void ky_scene_node_place_above(struct ky_scene_node *node, struct ky_scene_node *sibling)
{
    assert(node != sibling);
    assert(node->parent == sibling->parent);
    if (node->link.prev == &sibling->link) {
        return;
    }

    wl_list_remove(&node->link);
    wl_list_insert(&sibling->link, &node->link);

    node->update_mask |= KY_SCENE_NODE_UPDATE_LAYER;
}

void ky_scene_node_place_below(struct ky_scene_node *node, struct ky_scene_node *sibling)
{
    assert(node != sibling);
    assert(node->parent == sibling->parent);
    if (node->link.next == &sibling->link) {
        return;
    }

    wl_list_remove(&node->link);
    wl_list_insert(sibling->link.prev, &node->link);

    node->update_mask |= KY_SCENE_NODE_UPDATE_LAYER;
}

void ky_scene_node_raise_to_top(struct ky_scene_node *node)
{
    struct ky_scene_node *current_top =
        wl_container_of(node->parent->children.prev, current_top, link);
    if (node == current_top) {
        return;
    }

    ky_scene_node_place_above(node, current_top);
}

void ky_scene_node_lower_to_bottom(struct ky_scene_node *node)
{
    struct ky_scene_node *current_bottom =
        wl_container_of(node->parent->children.next, current_bottom, link);
    if (node == current_bottom) {
        return;
    }

    ky_scene_node_place_below(node, current_bottom);
}

void ky_scene_node_reparent(struct ky_scene_node *node, struct ky_scene_tree *new_parent)
{
    assert(new_parent != NULL);
    if (node->parent == new_parent) {
        return;
    }

    /* Ensure that a node cannot become its own ancestor. */
    for (struct ky_scene_tree *ancestor = new_parent; ancestor != NULL;
         ancestor = ancestor->node.parent) {
        assert(&ancestor->node != node);
    }

    wl_list_remove(&node->link);
    node->parent = new_parent;
    wl_list_insert(new_parent->children.prev, &node->link);

    node->update_mask |= (KY_SCENE_NODE_UPDATE_POSITION | KY_SCENE_NODE_UPDATE_LAYER);
    ky_scene_node_update_outputs(node, NULL, NULL, NULL);
}

bool ky_scene_node_coords(struct ky_scene_node *node, int *lx_ptr, int *ly_ptr)
{
    assert(node);

    *lx_ptr = 0, *ly_ptr = 0;
    bool enabled = true;

    while (true) {
        enabled = enabled && node->enabled;
        *lx_ptr += node->x;
        *ly_ptr += node->y;
        if (!node->parent) {
            break;
        }
        node = &node->parent->node;
    }

    return enabled;
}

struct ky_scene_node *ky_scene_node_at(struct ky_scene_node *node, double lx, double ly, double *nx,
                                       double *ny)
{
    struct ky_scene_node *found;
    double sx, sy;
    int x, y;

    ky_scene_node_coords(node, &x, &y);
    found = node->impl.accpet_input(node, x, y, lx, ly, &sx, &sy);
    if (!found) {
        return NULL;
    }

    if (nx) {
        *nx = sx;
    }
    if (ny) {
        *ny = sy;
    }
    return found;
}

void ky_scene_log_region(enum kywc_log_level level, pixman_region32_t *region)
{
    int rects_len;
    const pixman_box32_t *rects = pixman_region32_rectangles(region, &rects_len);

    kywc_log(level, "region dump: %d nrects with extend (%d, %d) (%d, %d)", rects_len,
             region->extents.x1, region->extents.y1, region->extents.x2, region->extents.y2);

    for (int i = 0; i < rects_len; i++) {
        const pixman_box32_t *rect = &rects[i];
        kywc_log(level, "\trect[%d]: (%d, %d) (%d, %d)", i, rect->x1, rect->y1, rect->x2, rect->y2);
    }
}

#endif
