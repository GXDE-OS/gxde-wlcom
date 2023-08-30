// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>
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
        struct ky_scene_node *tree_node = ky_scene_node_from_tree(child->tree);
        if (prev != NULL) {
            ky_scene_node_place_above(tree_node, prev);
        }
        prev = tree_node;

        ky_scene_node_set_position(tree_node, subsurface->current.x, subsurface->current.y);
    }

    struct ky_scene_node *buffer_node =
        ky_scene_node_from_buffer(subsurface_tree->scene_surface->buffer);
    if (prev != NULL) {
        ky_scene_node_place_above(buffer_node, prev);
    }
    prev = buffer_node;

    wl_list_for_each(subsurface, &surface->current.subsurfaces_above, current.link) {
        struct ky_scene_subsurface_tree *child =
            subsurface_tree_from_subsurface(subsurface_tree, subsurface);
        struct ky_scene_node *tree_node = ky_scene_node_from_tree(child->tree);
        ky_scene_node_place_above(tree_node, prev);
        prev = tree_node;

        ky_scene_node_set_position(tree_node, subsurface->current.x, subsurface->current.y);
    }
}

static void subsurface_tree_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, surface_destroy);
    ky_scene_node_destroy(ky_scene_node_from_tree(subsurface_tree->tree));
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
    ky_scene_node_destroy(ky_scene_node_from_tree(subsurface_tree->tree));
}

static void subsurface_tree_handle_surface_map(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, surface_map);

    ky_scene_node_set_enabled(ky_scene_node_from_tree(subsurface_tree->tree), true);
}

static void subsurface_tree_handle_surface_unmap(struct wl_listener *listener, void *data)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(listener, subsurface_tree, surface_unmap);

    ky_scene_node_set_enabled(ky_scene_node_from_tree(subsurface_tree->tree), false);
}

static void subsurface_tree_addon_destroy(struct wlr_addon *addon)
{
    struct ky_scene_subsurface_tree *subsurface_tree =
        wl_container_of(addon, subsurface_tree, surface_addon);
    ky_scene_node_destroy(ky_scene_node_from_tree(subsurface_tree->tree));
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
    ky_scene_node_add_destroy_listener(ky_scene_node_from_tree(subsurface_tree->tree),
                                       &subsurface_tree->tree_destroy);

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

    ky_scene_node_set_enabled(ky_scene_node_from_tree(subsurface_tree->tree), surface->mapped);

    return subsurface_tree;

error_scene_surface:
    ky_scene_node_destroy(ky_scene_node_from_tree(subsurface_tree->tree));
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
