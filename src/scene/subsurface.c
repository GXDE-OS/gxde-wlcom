// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/util/addon.h>

#include "scene/surface.h"

/**
 * A tree for a surface and all of its child sub-surfaces.
 *
 * `tree` contains `scene_surface` and one node per sub-surface.
 */
struct ky_scene_subsurface_tree {
    struct ky_scene_tree *tree;
    struct wlr_surface *surface;
    struct ky_scene_surface *scene_surface;

    struct wl_listener tree_destroy;
    struct wl_listener surface_destroy;
    struct wl_listener surface_commit;
    struct wl_listener surface_map;
    struct wl_listener surface_unmap;
    struct wl_listener surface_new_subsurface;

    struct ky_scene_subsurface_tree *parent; // NULL for the top-level surface

    // Only valid if the surface is a sub-surface

    struct wlr_addon surface_addon;

    struct wl_listener subsurface_destroy;
};

static void subsurface_get_radius(struct wlr_subsurface *subsurface, int child_radius[4])
{
    memset(child_radius, 0, sizeof(int) * 4);
    struct wlr_surface *parent = subsurface->parent;
    if (!parent) {
        return;
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(parent);
    if (!buffer) {
        return;
    }

    int parent_radius[4] = { 0 };
    ky_scene_node_get_radius(&buffer->node, parent_radius);

    int x1 = 0, y1 = 0;
    int x2 = parent->current.width;
    int y2 = parent->current.height;
    struct {
        int x, y;
    } parent_vertex[4] = { { x2, y2 }, { x2, y1 }, { x1, y2 }, { x1, y1 } };

    x1 = subsurface->current.x;
    y1 = subsurface->current.y;
    x2 = x1 + subsurface->surface->current.width;
    y2 = y1 + subsurface->surface->current.height;
    struct {
        int x, y;
    } child_vertex[4] = { { x2, y2 }, { x2, y1 }, { x1, y2 }, { x1, y1 } };

    for (int i = 0; i < 4; ++i) {
        if (parent_radius[i] > 0 &&
            abs(parent_vertex[i].x - child_vertex[i].x) < parent_radius[i] &&
            abs(parent_vertex[i].y - child_vertex[i].y) < parent_radius[i]) {
            child_radius[i] = parent_radius[i];
        }
    }
}

static void subsurface_collect_damage(struct ky_scene_node *node, int lx, int ly,
                                      bool parent_enabled, uint32_t damage_type,
                                      pixman_region32_t *damage, pixman_region32_t *invisible,
                                      pixman_region32_t *affected)
{
    struct ky_scene_surface *scene_surface =
        ky_scene_surface_try_from_buffer(ky_scene_buffer_from_node(node));

    subsurface_get_radius(wlr_subsurface_try_from_wlr_surface(scene_surface->surface),
                          node->radius);

    scene_surface->buffer_default_impl.collect_damage(node, lx, ly, parent_enabled, damage_type,
                                                      damage, invisible, affected);
}

static void subsurface_tree_handle_tree_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, tree_destroy);
    // tree and scene_surface will be cleaned up by scene_node_finish
    if (subsurface_tree->parent) {
        wlr_addon_finish(&subsurface_tree->surface_addon);
        wl_list_remove(&subsurface_tree->subsurface_destroy.link);
    }
    wl_list_remove(&subsurface_tree->tree_destroy.link);
    wl_list_remove(&subsurface_tree->surface_destroy.link);
    wl_list_remove(&subsurface_tree->surface_commit.link);
    wl_list_remove(&subsurface_tree->surface_map.link);
    wl_list_remove(&subsurface_tree->surface_unmap.link);
    wl_list_remove(&subsurface_tree->surface_new_subsurface.link);
    free(subsurface_tree);
}

static const struct wlr_addon_interface subsurface_tree_addon_impl;

static struct ky_scene_subsurface_tree *
subsurface_tree_from_subsurface(struct ky_scene_subsurface_tree *parent,
                                struct wlr_subsurface *subsurface)
{
    struct wlr_addon *addon =
        wlr_addon_find(&subsurface->surface->addons, parent, &subsurface_tree_addon_impl);
    assert(addon != NULL);
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(addon, subsurface_tree, surface_addon);
    return subsurface_tree;
}

static void subsurface_tree_reconfigure(struct ky_scene_subsurface_tree *subsurface_tree)
{
    struct wlr_surface *surface = subsurface_tree->surface;

    struct ky_scene_node *prev = NULL;
    struct wlr_subsurface *subsurface;
    wl_list_for_each(subsurface, &surface->current.subsurfaces_below, current.link) {
        struct ky_scene_subsurface_tree *child =
            subsurface_tree_from_subsurface(subsurface_tree, subsurface);
        if (prev != NULL) {
            ky_scene_node_place_above(&child->tree->node, prev);
        }
        prev = &child->tree->node;

        ky_scene_node_set_position(&child->tree->node, subsurface->current.x,
                                   subsurface->current.y);
    }

    if (prev != NULL) {
        ky_scene_node_place_above(&subsurface_tree->scene_surface->buffer->node, prev);
    }
    prev = &subsurface_tree->scene_surface->buffer->node;

    wl_list_for_each(subsurface, &surface->current.subsurfaces_above, current.link) {
        struct ky_scene_subsurface_tree *child =
            subsurface_tree_from_subsurface(subsurface_tree, subsurface);
        ky_scene_node_place_above(&child->tree->node, prev);
        prev = &child->tree->node;

        ky_scene_node_set_position(&child->tree->node, subsurface->current.x,
                                   subsurface->current.y);
    }
}

static void subsurface_tree_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, surface_destroy);
    ky_scene_node_destroy(&subsurface_tree->tree->node);
}

static void subsurface_tree_handle_surface_commit(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, surface_commit);

    // TODO: only do this on subsurface order or position change
    subsurface_tree_reconfigure(subsurface_tree);
}

static void subsurface_tree_handle_subsurface_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, subsurface_destroy);
    ky_scene_node_destroy(&subsurface_tree->tree->node);
}

static void subsurface_tree_handle_surface_map(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, surface_map);

    ky_scene_node_set_enabled(&subsurface_tree->tree->node, true);
}

static void subsurface_tree_handle_surface_unmap(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, surface_unmap);

    ky_scene_node_set_enabled(&subsurface_tree->tree->node, false);
}

static void subsurface_tree_addon_destroy(struct wlr_addon *addon)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(addon, subsurface_tree, surface_addon);
    ky_scene_node_destroy(&subsurface_tree->tree->node);
}

static const struct wlr_addon_interface subsurface_tree_addon_impl = {
    .name = "ky_scene_subsurface_tree",
    .destroy = subsurface_tree_addon_destroy,
};

static struct ky_scene_subsurface_tree *scene_surface_tree_create(struct ky_scene_tree *parent,
                                                                  struct wlr_surface *surface);

static bool subsurface_tree_create_subsurface(struct ky_scene_subsurface_tree *parent,
                                              struct wlr_subsurface *subsurface)
{
    struct ky_scene_subsurface_tree *child =
        scene_surface_tree_create(parent->tree, subsurface->surface);
    if (child == NULL) {
        return false;
    }

    child->parent = parent;

    wlr_addon_init(&child->surface_addon, &subsurface->surface->addons, parent,
                   &subsurface_tree_addon_impl);

    child->subsurface_destroy.notify = subsurface_tree_handle_subsurface_destroy;
    wl_signal_add(&subsurface->events.destroy, &child->subsurface_destroy);

    return true;
}

static void subsurface_tree_handle_surface_new_subsurface(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, surface_new_subsurface);
    struct wlr_subsurface *subsurface = data;
    if (!subsurface_tree_create_subsurface(subsurface_tree, subsurface)) {
        wl_resource_post_no_memory(subsurface->resource);
    }
}

static struct ky_scene_subsurface_tree *scene_surface_tree_create(struct ky_scene_tree *parent,
                                                                  struct wlr_surface *surface)
{
    struct ky_scene_subsurface_tree *subsurface_tree = calloc(1, sizeof(*subsurface_tree));
    if (subsurface_tree == NULL) {
        return NULL;
    }

    subsurface_tree->tree = ky_scene_tree_create(parent);
    if (subsurface_tree->tree == NULL) {
        goto error_surface_tree;
    }

    subsurface_tree->scene_surface = ky_scene_surface_create(subsurface_tree->tree, surface);
    if (subsurface_tree->scene_surface == NULL) {
        goto error_scene_surface;
    }

    subsurface_tree->surface = surface;

    if (wlr_subsurface_try_from_wlr_surface(surface)) {
        subsurface_tree->scene_surface->buffer->node.impl.collect_damage =
            subsurface_collect_damage;
    }

    struct wlr_subsurface *subsurface;
    wl_list_for_each(subsurface, &surface->current.subsurfaces_below, current.link) {
        if (!subsurface_tree_create_subsurface(subsurface_tree, subsurface)) {
            goto error_scene_surface;
        }
    }
    wl_list_for_each(subsurface, &surface->current.subsurfaces_above, current.link) {
        if (!subsurface_tree_create_subsurface(subsurface_tree, subsurface)) {
            goto error_scene_surface;
        }
    }

    subsurface_tree_reconfigure(subsurface_tree);

    subsurface_tree->tree_destroy.notify = subsurface_tree_handle_tree_destroy;
    wl_signal_add(&subsurface_tree->tree->node.events.destroy, &subsurface_tree->tree_destroy);

    subsurface_tree->surface_destroy.notify = subsurface_tree_handle_surface_destroy;
    wl_signal_add(&surface->events.destroy, &subsurface_tree->surface_destroy);

    subsurface_tree->surface_commit.notify = subsurface_tree_handle_surface_commit;
    wl_signal_add(&surface->events.commit, &subsurface_tree->surface_commit);

    subsurface_tree->surface_map.notify = subsurface_tree_handle_surface_map;
    wl_signal_add(&surface->events.map, &subsurface_tree->surface_map);

    subsurface_tree->surface_unmap.notify = subsurface_tree_handle_surface_unmap;
    wl_signal_add(&surface->events.unmap, &subsurface_tree->surface_unmap);

    subsurface_tree->surface_new_subsurface.notify = subsurface_tree_handle_surface_new_subsurface;
    wl_signal_add(&surface->events.new_subsurface, &subsurface_tree->surface_new_subsurface);

    ky_scene_node_set_enabled(&subsurface_tree->tree->node, surface->mapped);

    return subsurface_tree;

error_scene_surface:
    ky_scene_node_destroy(&subsurface_tree->tree->node);
error_surface_tree:
    free(subsurface_tree);
    return NULL;
}

struct ky_scene_tree *ky_scene_subsurface_tree_create(struct ky_scene_tree *parent,
                                                      struct wlr_surface *surface)
{
    struct ky_scene_subsurface_tree *subsurface_tree = scene_surface_tree_create(parent, surface);
    if (subsurface_tree == NULL) {
        return NULL;
    }
    return subsurface_tree->tree;
}
