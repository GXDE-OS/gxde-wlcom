// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/util/region.h>

#include "output.h"
#include "scene_p.h"
#include "util/debug.h"

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/**
 * basic scene node
 */

static struct ky_scene_node *node_accept_input(struct ky_scene_node *node, int lx, int ly,
                                               double px, double py, double *rx, double *ry)
{
    kywc_log(KYWC_ERROR, "Need to implement accept_input interface!");
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
                                uint32_t update_mask, pixman_region32_t *damage,
                                pixman_region32_t *invisible, pixman_region32_t *affected)
{
    kywc_log(KYWC_ERROR, "Need to implement collect_damage interface!");
    assert(false);
}

static void node_render(struct ky_scene_node *node, int lx, int ly,
                        struct ky_scene_render_target *target)
{
    kywc_log(KYWC_ERROR, "Need to implement render interface!");
    assert(false);
}

static void node_get_bounding_box(struct ky_scene_node *node, struct wlr_box *box)
{
    kywc_log(KYWC_ERROR, "Need to implement get_bounding_box interface!");
    assert(false);
}

static void node_push_damage(struct ky_scene_node *node, struct ky_scene_node *damage_node,
                             uint32_t damage_type, pixman_region32_t *damage)
{
    if (!node->enabled && !node->force_damage_event) {
        return;
    }

    assert(damage != NULL);
    /* root node has own push_damage */
    assert(node->parent);

    /* emit damage when node content damaged or children damaged */
    if ((node == damage_node && node->damage_type & KY_SCENE_DAMAGE_HARMLESS) ||
        node != damage_node) {
        wl_signal_emit_mutable(&node->events.damage, NULL);
    }

    if (!node->enabled) {
        return;
    }

    pixman_region32_translate(damage, node->x, node->y);
    node->parent->node.impl.push_damage(&node->parent->node, damage_node,
                                        damage_type | node->damage_type, damage);
}

static void node_destroy(struct ky_scene_node *node)
{
    wlr_addon_set_finish(&node->addons);
    ky_scene_node_set_enabled(node, false);

    pixman_region32_fini(&node->visible_region);
    pixman_region32_fini(&node->input_region);
    pixman_region32_fini(&node->clip_region);
    pixman_region32_fini(&node->blur.region);

    wl_list_remove(&node->link);
    free(node);
}

void ky_scene_node_init(struct ky_scene_node *node, struct ky_scene_tree *parent)
{
    *node = (struct ky_scene_node){
        .parent = parent,
        .enabled = true,
        .damage_type = KY_SCENE_DAMAGE_HARMFUL,
        .blur.iterations = 3,
        .blur.offset = 2.6f,
        .impl = {
            .accept_input = node_accept_input,
            .update_outputs = node_update_outputs,
            .collect_damage = node_collect_damage,
            .render = node_render,
            .get_bounding_box = node_get_bounding_box,
            .push_damage = node_push_damage,
            .destroy = node_destroy,
        },
    };

    wl_list_init(&node->link);
    wl_signal_init(&node->events.damage);
    wl_signal_init(&node->events.destroy);
    pixman_region32_init(&node->visible_region);
    pixman_region32_init(&node->input_region);
    pixman_region32_init(&node->clip_region);
    pixman_region32_init(&node->blur.region);

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

static struct ky_scene_node *tree_accept_input(struct ky_scene_node *node, int lx, int ly,
                                               double px, double py, double *rx, double *ry)
{
    /* skip disabled or input bypassed nodes */
    if (!node->enabled || node->input_bypassed) {
        return NULL;
    }

    struct ky_scene_tree *scene_tree = ky_scene_tree_from_node(node);
    struct ky_scene_node *child, *found;
    double nx, ny;

    wl_list_for_each_reverse(child, &scene_tree->children, link) {
        found = child->impl.accept_input(child, lx + child->x, ly + child->y, px, py, &nx, &ny);
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
                                uint32_t damage_type, pixman_region32_t *damage,
                                pixman_region32_t *invisible, pixman_region32_t *affected)
{
    bool node_enabled = parent_enabled && node->enabled;
    struct ky_scene_tree *scene_tree = ky_scene_tree_from_node(node);

    struct ky_scene_node *child;
    wl_list_for_each_reverse(child, &scene_tree->children, link) {
        child->impl.collect_damage(child, lx + child->x, ly + child->y, node_enabled,
                                   damage_type | child->damage_type, damage, invisible, affected);
    }

    node->last_enabled = node_enabled;
    node->damage_type = KY_SCENE_DAMAGE_NONE;
}

static void tree_render(struct ky_scene_node *node, int lx, int ly,
                        struct ky_scene_render_target *target)
{
    struct ky_scene_tree *scene_tree = ky_scene_tree_from_node(node);
    if (!node->enabled || wl_list_empty(&scene_tree->children)) {
        return;
    }

    /* from bottom to top */
    struct ky_scene_node *child;
    wl_list_for_each(child, &scene_tree->children, link) {
        child->impl.render(child, lx + child->x, ly + child->y, target);
    }
}

static void tree_get_bounding_box(struct ky_scene_node *node, struct wlr_box *box)
{
    struct ky_scene_tree *tree = ky_scene_tree_from_node(node);
    if (wl_list_empty(&tree->children)) {
        *box = (struct wlr_box){ 0 };
        return;
    }

    int min_x1 = INT_MAX, min_y1 = INT_MAX;
    int max_x2 = INT_MIN, max_y2 = INT_MIN;

    struct wlr_box child_box;
    struct ky_scene_node *child;
    wl_list_for_each(child, &tree->children, link) {
        if (!ky_scene_node_is_visible(child)) {
            continue;
        }

        child->impl.get_bounding_box(child, &child_box);
        child_box.x += child->x;
        child_box.y += child->y;

        min_x1 = MIN(min_x1, child_box.x);
        min_y1 = MIN(min_y1, child_box.y);
        max_x2 = MAX(max_x2, child_box.x + child_box.width);
        max_y2 = MAX(max_y2, child_box.y + child_box.height);
    }

    box->x = min_x1 == INT_MAX ? 0 : min_x1;
    box->y = min_y1 == INT_MAX ? 0 : min_y1;
    box->width = max_x2 == INT_MIN ? 0 : max_x2 - min_x1;
    box->height = max_y2 == INT_MIN ? 0 : max_y2 - min_y1;
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
    tree->node.impl.accept_input = tree_accept_input;
    tree->node.impl.update_outputs = tree_update_outputs;
    tree->node.impl.collect_damage = tree_collect_damage;
    tree->node.impl.render = tree_render;
    tree->node.impl.get_bounding_box = tree_get_bounding_box;
    tree->node.impl.destroy = tree_destroy;

    wl_list_init(&tree->children);
}

/**
 * scene root based on tree
 */

static void scene_damage_outputs(struct ky_scene *scene, const pixman_region32_t *damage)
{
    struct ky_scene_output *scene_output;
    struct kywc_box geo = { 0 };
    wl_list_for_each(scene_output, &scene->outputs, link) {
        if (scene_output->viewport.has_src) {
            geo.x = scene_output->viewport.src.x;
            geo.y = scene_output->viewport.src.y;
            geo.width = scene_output->viewport.src.width;
            geo.height = scene_output->viewport.src.height;
        } else {
            struct output *output = output_from_wlr_output(scene_output->output);
            geo = output->geometry;
        }
        if (pixman_region32_contains_rectangle(
                damage, &(pixman_box32_t){ geo.x, geo.y, geo.x + geo.width, geo.y + geo.height }) !=
            PIXMAN_REGION_OUT) {
            output_schedule_frame(scene_output->output);
        }
    }
}

static void scene_push_damage(struct ky_scene_node *node, struct ky_scene_node *damage_node,
                              uint32_t damage_type, pixman_region32_t *damage)
{
    if (!node->enabled) {
        return;
    }

    assert(damage != NULL);
    pixman_region32_translate(damage, node->x, node->y);

    struct ky_scene *scene = ky_scene_from_node(node);
    damage_type |= node->damage_type;

    assert(damage_type != KY_SCENE_DAMAGE_NONE);
    if (damage_type == KY_SCENE_DAMAGE_HARMLESS && damage_node->last_enabled) {
        assert(damage_node->type == KY_SCENE_NODE_RECT ||
               damage_node->type == KY_SCENE_NODE_BUFFER);
        pixman_region32_intersect(damage, damage, &damage_node->visible_region);
        ky_scene_add_damage(scene, damage);
        return;
    }

    scene_damage_outputs(scene, damage);
    pixman_region32_union(&scene->pushed_damage, &scene->pushed_damage, damage);
}

static void scene_destroy(struct ky_scene_node *node)
{
    struct ky_scene *scene = ky_scene_from_node(node);
    wl_list_remove(&scene->presentation_destroy.link);

    struct ky_scene_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &scene->outputs, link) {
        ky_scene_output_destroy(output);
    }

    pixman_region32_fini(&scene->collected_damage);
    pixman_region32_fini(&scene->collected_invisible);
    pixman_region32_fini(&scene->pushed_damage);

    scene->tree_destroy(node);
}

struct ky_scene *ky_scene_create(void)
{
    struct ky_scene *scene = calloc(1, sizeof(struct ky_scene));
    if (!scene) {
        return NULL;
    }

    ky_scene_tree_init(&scene->tree, NULL);
    scene->tree.node.role.type = KY_SCENE_ROLE_ROOT;
    scene->tree.node.role.data = scene;

    scene->tree_destroy = scene->tree.node.impl.destroy;
    scene->tree.node.impl.destroy = scene_destroy;
    scene->tree.node.impl.push_damage = scene_push_damage;

    wl_list_init(&scene->outputs);
    wl_list_init(&scene->presentation_destroy.link);

    pixman_region32_init(&scene->collected_damage);
    pixman_region32_init(&scene->collected_invisible);
    pixman_region32_init(&scene->pushed_damage);

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

void ky_scene_damage_whole(struct ky_scene *scene)
{
    struct ky_scene_output *output;
    wl_list_for_each(output, &scene->outputs, link) {
        ky_scene_output_damage_whole(output);
    }
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

static void scene_handle_linux_dmabuf_v1_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene *scene = wl_container_of(listener, scene, linux_dmabuf_v1_destroy);
    wl_list_remove(&scene->linux_dmabuf_v1_destroy.link);
    wl_list_init(&scene->linux_dmabuf_v1_destroy.link);
    scene->linux_dmabuf_v1 = NULL;
}

void ky_scene_set_linux_dmabuf_v1(struct ky_scene *scene,
                                  struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1)
{
    assert(scene->linux_dmabuf_v1 == NULL);
    scene->linux_dmabuf_v1 = linux_dmabuf_v1;
    scene->linux_dmabuf_v1_destroy.notify = scene_handle_linux_dmabuf_v1_destroy;
    wl_signal_add(&linux_dmabuf_v1->events.destroy, &scene->linux_dmabuf_v1_destroy);
}

static void handle_tearing_control_v1_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene *scene = wl_container_of(listener, scene, tearing_control_v1_destroy);
    wl_list_remove(&scene->tearing_control_v1_destroy.link);
    scene->tearing_control_v1 = NULL;
}

void ky_scene_set_tearing_control_v1(struct ky_scene *scene,
                                     struct wlr_tearing_control_manager_v1 *tearing_control_v1)
{
    assert(scene->tearing_control_v1 == NULL);
    scene->tearing_control_v1 = tearing_control_v1;
    scene->tearing_control_v1_destroy.notify = handle_tearing_control_v1_destroy;
    wl_signal_add(&tearing_control_v1->events.destroy, &scene->tearing_control_v1_destroy);
}

bool ky_scene_surface_is_tearing_allowed(struct ky_scene *scene, struct wlr_surface *surface)
{
    if (!scene->tearing_control_v1 || !surface) {
        return false;
    }

    enum wp_tearing_control_v1_presentation_hint hint =
        wlr_tearing_control_manager_v1_surface_hint_from_surface(scene->tearing_control_v1,
                                                                 surface);
    kywc_log(KYWC_DEBUG, "get surface tearing hint: %d", hint);
    return hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
}

bool ky_scene_is_tearing_needed(struct ky_scene *scene)
{
    if (!scene->tearing_control_v1) {
        return false;
    }

    struct wlr_tearing_control_v1 *hint;
    wl_list_for_each(hint, &scene->tearing_control_v1->surface_hints, link) {
        if (hint->hint == WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC) {
            return true;
        }
    }

    return false;
}

void ky_scene_collect_damage(struct ky_scene *scene)
{
    KY_PROFILE_ZONE(zone, __func__);

    /* skip collect damage if no pushed_damage */
    if (pixman_region32_not_empty(&scene->pushed_damage)) {
        pixman_region32_t invisible;
        pixman_region32_init(&invisible);

        // invisible region in pushed_damage region will be recalculated
        pixman_region32_subtract(&scene->collected_invisible, &scene->collected_invisible,
                                 &scene->pushed_damage);

        // get current damage in all scene nodes
        struct ky_scene_node *root = &scene->tree.node;
        root->impl.collect_damage(root, root->x, root->y, true, false, &scene->collected_damage,
                                  &invisible, &scene->pushed_damage);

        pixman_region32_union(&scene->collected_invisible, &scene->collected_invisible, &invisible);
        pixman_region32_fini(&invisible);

        pixman_region32_clear(&scene->pushed_damage);
    }

    if (!pixman_region32_not_empty(&scene->collected_damage)) {
        KY_PROFILE_ZONE_END(zone);
        return;
    }

    /* distribute damage to outputs */
    struct wlr_box box = { 0 };
    pixman_region32_t region;

    struct ky_scene_output *output;
    wl_list_for_each(output, &scene->outputs, link) {
        if (output->viewport.has_src) {
            box = output->viewport.src;
        } else {
            box.x = output->x;
            box.y = output->y;
            wlr_output_effective_resolution(output->output, &box.width, &box.height);
        }
        pixman_region32_init_rect(&region, box.x, box.y, box.width, box.height);
        pixman_region32_intersect(&region, &region, &scene->collected_damage);
        pixman_region32_union(&output->collected_damage, &output->collected_damage, &region);
        pixman_region32_fini(&region);
    }

    pixman_region32_clear(&scene->collected_damage);

    KY_PROFILE_ZONE_END(zone);
}

void ky_scene_add_damage(struct ky_scene *scene, const pixman_region32_t *damage)
{
    if (!pixman_region32_not_empty(damage)) {
        return;
    }

    pixman_region32_union(&scene->collected_damage, &scene->collected_damage, damage);

    scene_damage_outputs(scene, damage);
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

    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    node->enabled = enabled;
    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
}

void ky_scene_node_set_input_bypassed(struct ky_scene_node *node, bool bypassed)
{
    node->input_bypassed = bypassed;
}

void ky_scene_node_force_damage_event(struct ky_scene_node *node, bool force)
{
    /**
     * damage event for this node is may still not emitted,
     * early returned in ky_scene_node_push_damage by node->enabled
     */
    node->force_damage_event = force;
}

void ky_scene_node_set_position(struct ky_scene_node *node, int x, int y)
{
    if (node->x == x && node->y == y) {
        return;
    }

    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    node->x = x;
    node->y = y;
    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);

    ky_scene_node_update_outputs(node, NULL, NULL, NULL);
}

void ky_scene_node_place_above(struct ky_scene_node *node, struct ky_scene_node *sibling)
{
    assert(node != sibling);
    assert(node->parent == sibling->parent);
    if (node->link.prev == &sibling->link) {
        return;
    }

    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    wl_list_remove(&node->link);
    wl_list_insert(&sibling->link, &node->link);
    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
}

void ky_scene_node_place_below(struct ky_scene_node *node, struct ky_scene_node *sibling)
{
    assert(node != sibling);
    assert(node->parent == sibling->parent);
    if (node->link.next == &sibling->link) {
        return;
    }

    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    wl_list_remove(&node->link);
    wl_list_insert(sibling->link.prev, &node->link);
    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
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

    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
    wl_list_remove(&node->link);
    node->parent = new_parent;
    wl_list_insert(new_parent->children.prev, &node->link);
    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);

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
    found = node->impl.accept_input(node, x, y, lx, ly, &sx, &sy);
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

void ky_scene_node_set_input_region(struct ky_scene_node *node, const pixman_region32_t *region)
{
    /* tree is not support currently */
    assert(node->type == KY_SCENE_NODE_RECT || node->type == KY_SCENE_NODE_BUFFER);
    if (pixman_region32_equal(&node->input_region, region)) {
        return;
    }
    pixman_region32_copy(&node->input_region, region);
}

void ky_scene_node_set_clip_region(struct ky_scene_node *node, const pixman_region32_t *region)
{
    /* tree is not support currently */
    assert(node->type == KY_SCENE_NODE_RECT || node->type == KY_SCENE_NODE_BUFFER);
    if (pixman_region32_equal(&node->clip_region, region)) {
        return;
    }
    pixman_region32_copy(&node->clip_region, region);
    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_BOTH, NULL);
}

void ky_scene_node_set_blur_region(struct ky_scene_node *node, const pixman_region32_t *region)
{
    /* tree is not support currently */
    assert(node->type == KY_SCENE_NODE_RECT || node->type == KY_SCENE_NODE_BUFFER);
    bool has_blur = region != NULL;
    /* early return if still has no blur */
    if (!has_blur && has_blur == node->has_blur) {
        return;
    }

    /* add blur or remove blur */
    if (node->has_blur != has_blur) {
        node->has_blur = has_blur;
        if (has_blur) {
            pixman_region32_copy(&node->blur.region, region);
        } else {
            pixman_region32_clear(&node->blur.region);
        }
    } else { /* change blur region */
        if (pixman_region32_equal(&node->blur.region, region)) {
            return;
        }
        pixman_region32_copy(&node->blur.region, region);
    }

    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, NULL);
}

void ky_scene_node_set_blur_level(struct ky_scene_node *node, uint32_t iterations, float offset)
{
    /* tree is not support currently */
    assert(node->type == KY_SCENE_NODE_RECT || node->type == KY_SCENE_NODE_BUFFER);
    if (node->blur.iterations == iterations && node->blur.offset == offset &&
        node->blur.custom_level) {
        return;
    }

    node->blur.iterations = iterations;
    node->blur.offset = offset;
    node->blur.custom_level = true;
    if (node->has_blur) {
        ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, &node->blur.region);
    }
}

void ky_scene_node_reset_blur_level(struct ky_scene_node *node)
{
    /* tree is not support currently */
    assert(node->type == KY_SCENE_NODE_RECT || node->type == KY_SCENE_NODE_BUFFER);
    if (!node->blur.custom_level) {
        return;
    }

    node->blur.iterations = 3;
    node->blur.offset = 2.6f;
    node->blur.custom_level = false;
    if (node->has_blur) {
        ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_HARMFUL, &node->blur.region);
    }
}

static void scene_node_get_blur_region(struct ky_scene_node *node, pixman_region32_t *region)
{
    /* tree is not support currently */
    assert(node->type == KY_SCENE_NODE_RECT || node->type == KY_SCENE_NODE_BUFFER);
    if (!node->has_blur) {
        pixman_region32_clear(region);
        return;
    }

    if (pixman_region32_not_empty(&node->blur.region)) {
        pixman_region32_copy(region, &node->blur.region);
    } else {
        struct wlr_box box;
        node->impl.get_bounding_box(node, &box);
        pixman_region32_fini(region);
        pixman_region32_init_rect(region, box.x, box.y, box.width, box.height);
    }
}

/* the x、y value is the offset relative to the first node */
static void node_for_each_enabled(struct ky_scene_node *node, int x, int y,
                                  void (*callback)(struct ky_scene_node *, int, int, void *),
                                  void *data)
{
    if (!node->enabled) {
        return;
    }

    x += node->x;
    y += node->y;
    callback(node, x, y, data);

    if (node->type != KY_SCENE_NODE_TREE) {
        return;
    }

    struct ky_scene_tree *tree = ky_scene_tree_from_node(node);
    struct ky_scene_node *pos;
    wl_list_for_each(pos, &tree->children, link) {
        node_for_each_enabled(pos, x, y, callback, data);
    }
}

static void get_node_blur_info(struct ky_scene_node *node, int x, int y, void *data)
{
    /* tree is not support currently */
    if (!node->has_blur ||
        (node->type != KY_SCENE_NODE_RECT && node->type != KY_SCENE_NODE_BUFFER)) {
        return;
    }

    pixman_region32_t blur_region;
    pixman_region32_init(&blur_region);
    scene_node_get_blur_region(node, &blur_region);
    pixman_region32_translate(&blur_region, x, y);

    struct blur_info *info = data;
    pixman_region32_union(&info->region, &info->region, &blur_region);
    pixman_region32_fini(&blur_region);

    info->iterations =
        info->iterations < node->blur.iterations ? node->blur.iterations : info->iterations;
    info->offset = info->offset < node->blur.offset ? node->blur.offset : info->offset;
    info->custom_level = info->custom_level || node->blur.custom_level;
}

void ky_scene_node_get_blur_info(struct ky_scene_node *node, struct blur_info *info)
{
    info->offset = 0;
    info->iterations = 0;
    info->custom_level = false;
    pixman_region32_clear(&info->region);
    node_for_each_enabled(node, -node->x, -node->y, get_node_blur_info, info);
}

void ky_scene_node_set_radius(struct ky_scene_node *node, const int radius[static 4])
{
    /* tree is not support currently */
    assert(node->type == KY_SCENE_NODE_RECT || node->type == KY_SCENE_NODE_BUFFER);
    if (memcmp(node->radius, radius, sizeof(node->radius)) == 0) {
        return;
    }

    memcpy(node->radius, radius, sizeof(node->radius));
    ky_scene_node_push_damage(node, KY_SCENE_DAMAGE_BOTH, NULL);
}

struct round_corner {
    int x, y;
    int radius;
};

static void get_node_radius(struct ky_scene_node *node, int x, int y, void *data)
{
    if (node->type != KY_SCENE_NODE_RECT && node->type != KY_SCENE_NODE_BUFFER) {
        return;
    }

    int x1 = x, y1 = y;
    int x2 = x1, y2 = y1;

    if (node->type == KY_SCENE_NODE_RECT) {
        struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
        x2 += rect->width;
        y2 += rect->height;
    } else if (node->type == KY_SCENE_NODE_BUFFER) {
        struct ky_scene_buffer *buffer = ky_scene_buffer_from_node(node);
        x2 += buffer->dst_width;
        y2 += buffer->dst_height;
    }

    /* don't include decoration radius, because decoration radius isn't in rect node radius */
    struct round_corner *corner = data;
    int index = KY_SCENE_ROUND_CORNER_RB;
    if (node->radius[index] > 0 && (corner[index].x < x2 || corner[index].y < y2)) {
        corner[index].x = x2;
        corner[index].y = y2;
        corner[index].radius = node->radius[index];
    }
    index = KY_SCENE_ROUND_CORNER_RT;
    if (node->radius[index] > 0 && (corner[index].x < x2 || corner[index].y > y1)) {
        corner[index].x = x2;
        corner[index].y = y1;
        corner[index].radius = node->radius[index];
    }
    index = KY_SCENE_ROUND_CORNER_LB;
    if (node->radius[index] > 0 && (corner[index].x > x1 || corner[index].y < y2)) {
        corner[index].x = x1;
        corner[index].y = y2;
        corner[index].radius = node->radius[index];
    }
    index = KY_SCENE_ROUND_CORNER_LT;
    if (node->radius[index] > 0 && (corner[index].x < x1 || corner[index].y < y1)) {
        corner[index].x = x1;
        corner[index].y = y1;
        corner[index].radius = node->radius[index];
    }
}

void ky_scene_node_get_radius(struct ky_scene_node *node, int radius[static 4])
{
    if (node->type == KY_SCENE_NODE_RECT || node->type == KY_SCENE_NODE_BUFFER) {
        memcpy(radius, node->radius, sizeof(node->radius));
        return;
    }

    /* 0 = right-bottom, 1 = right-top, 2 = left-bottom, 3 = left-top */
    struct round_corner corner[4] = { 0 };
    node_for_each_enabled(node, -node->x, -node->y, get_node_radius, &corner);

    struct wlr_box box = { 0 };
    node->impl.get_bounding_box(node, &box);

    int x1 = box.x, y1 = box.y;
    int x2 = box.x + box.width, y2 = box.y + box.height;

    struct point {
        int x, y;
    } points[4] = { { x2, y2 }, { x2, y1 }, { x1, y2 }, { x1, y1 } };

    for (int i = 0; i < 4; ++i) {
        if (corner[i].radius > 0 && corner[i].x == points[i].x && corner[i].y == points[i].y) {
            radius[i] = corner[i].radius;
        }
    }
}

bool ky_scene_node_is_visible(struct ky_scene_node *node)
{
    if (!node->enabled) {
        return false;
    }

    if (node->type == KY_SCENE_NODE_TREE) {
        struct ky_scene_tree *tree = ky_scene_tree_from_node(node);
        return !wl_list_empty(&tree->children);
    } else if (node->type == KY_SCENE_NODE_RECT) {
        struct ky_scene_rect *rect = ky_scene_rect_from_node(node);
        return rect->color[3] != 0;
    } else if (node->type == KY_SCENE_NODE_BUFFER) {
        struct ky_scene_buffer *buffer = ky_scene_buffer_from_node(node);
        return buffer->buffer && buffer->opacity != 0;
    }

    return false;
}

void ky_scene_node_push_damage(struct ky_scene_node *node, enum ky_scene_damage_type damage_type,
                               const pixman_region32_t *damage)
{
    if (!node->enabled) {
        return;
    }

    pixman_region32_t damage_region;
    pixman_region32_init(&damage_region);

    if (damage == NULL) {
        struct wlr_box box = { 0 };
        node->impl.get_bounding_box(node, &box);
        pixman_region32_init_rect(&damage_region, box.x, box.y, box.width, box.height);
    } else {
        pixman_region32_copy(&damage_region, damage);
    }

    if (!pixman_region32_not_empty(&damage_region)) {
        pixman_region32_fini(&damage_region);
        return;
    }

    node->damage_type |= damage_type;
    node->impl.push_damage(node, node, 0, &damage_region);

    pixman_region32_fini(&damage_region);
}

void ky_scene_corner_region(pixman_region32_t *region, int width, int height,
                            const int radius[static 4])
{
    int rb = radius[KY_SCENE_ROUND_CORNER_RB];
    int rt = radius[KY_SCENE_ROUND_CORNER_RT];
    int lb = radius[KY_SCENE_ROUND_CORNER_LB];
    int lt = radius[KY_SCENE_ROUND_CORNER_LT];

    if (rb > 0) {
        pixman_region32_union_rect(region, region, width - rb, height - rb, rb, rb);
    }
    if (rt > 0) {
        pixman_region32_union_rect(region, region, width - rt, 0, rt, rt);
    }
    if (lb > 0) {
        pixman_region32_union_rect(region, region, 0, height - lb, lb, lb);
    }
    if (lt > 0) {
        pixman_region32_union_rect(region, region, 0, 0, lt, lt);
    }
}

void ky_scene_log_region(enum kywc_log_level level, const char *desc,
                         const pixman_region32_t *region)
{
    int rects_len;
    const pixman_box32_t *rects = pixman_region32_rectangles(region, &rects_len);

    kywc_log(level, "%s region dump: %d nrects with extend (%d, %d) %d x %d", desc, rects_len,
             region->extents.x1, region->extents.y1, region->extents.x2 - region->extents.x1,
             region->extents.y2 - region->extents.y1);

    for (int i = 0; i < rects_len; i++) {
        const pixman_box32_t *rect = &rects[i];
        kywc_log(level, "\trect[%d]: (%d, %d) %d x %d", i, rect->x1, rect->y1, rect->x2 - rect->x1,
                 rect->y2 - rect->y1);
    }
}
