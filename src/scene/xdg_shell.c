#include <stdlib.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "scene/xdg_shell.h"

struct ky_scene_xdg_surface {
    struct ky_scene_tree *tree;
    struct wlr_xdg_surface *xdg_surface;
    struct ky_scene_tree *surface_tree;

    struct wl_listener tree_destroy;
    struct wl_listener xdg_surface_destroy;
    struct wl_listener xdg_surface_map;
    struct wl_listener xdg_surface_unmap;
    struct wl_listener xdg_surface_commit;
};

static void scene_xdg_surface_handle_tree_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_xdg_surface *scene_xdg_surface =
        wl_container_of(listener, scene_xdg_surface, tree_destroy);
    // tree and surface_node will be cleaned up by scene_node_finish
    wl_list_remove(&scene_xdg_surface->tree_destroy.link);
    wl_list_remove(&scene_xdg_surface->xdg_surface_destroy.link);
    wl_list_remove(&scene_xdg_surface->xdg_surface_map.link);
    wl_list_remove(&scene_xdg_surface->xdg_surface_unmap.link);
    wl_list_remove(&scene_xdg_surface->xdg_surface_commit.link);
    free(scene_xdg_surface);
}

static void scene_xdg_surface_handle_xdg_surface_destroy(struct wl_listener *listener, void *data)
{
    struct ky_scene_xdg_surface *scene_xdg_surface =
        wl_container_of(listener, scene_xdg_surface, xdg_surface_destroy);
    ky_scene_node_destroy(ky_scene_node_from_tree(scene_xdg_surface->tree));
}

static void scene_xdg_surface_handle_xdg_surface_map(struct wl_listener *listener, void *data)
{
    struct ky_scene_xdg_surface *scene_xdg_surface =
        wl_container_of(listener, scene_xdg_surface, xdg_surface_map);
    ky_scene_node_set_enabled(ky_scene_node_from_tree(scene_xdg_surface->tree), true);
}

static void scene_xdg_surface_handle_xdg_surface_unmap(struct wl_listener *listener, void *data)
{
    struct ky_scene_xdg_surface *scene_xdg_surface =
        wl_container_of(listener, scene_xdg_surface, xdg_surface_unmap);
    ky_scene_node_set_enabled(ky_scene_node_from_tree(scene_xdg_surface->tree), false);
}

static void scene_xdg_surface_update_position(struct ky_scene_xdg_surface *scene_xdg_surface)
{
    struct wlr_xdg_surface *xdg_surface = scene_xdg_surface->xdg_surface;

    struct wlr_box geo = { 0 };
    wlr_xdg_surface_get_geometry(xdg_surface, &geo);
    ky_scene_node_set_position(ky_scene_node_from_tree(scene_xdg_surface->surface_tree), -geo.x,
                               -geo.y);

    if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        struct wlr_xdg_popup *popup = xdg_surface->popup;
        ky_scene_node_set_position(ky_scene_node_from_tree(scene_xdg_surface->tree),
                                   popup->current.geometry.x, popup->current.geometry.y);
    }
}

static void scene_xdg_surface_handle_xdg_surface_commit(struct wl_listener *listener, void *data)
{
    struct ky_scene_xdg_surface *scene_xdg_surface =
        wl_container_of(listener, scene_xdg_surface, xdg_surface_commit);
    scene_xdg_surface_update_position(scene_xdg_surface);
}

struct ky_scene_tree *ky_scene_xdg_surface_create(struct ky_scene_tree *parent,
                                                  struct wlr_xdg_surface *xdg_surface)
{
    struct ky_scene_xdg_surface *scene_xdg_surface = calloc(1, sizeof(*scene_xdg_surface));
    if (scene_xdg_surface == NULL) {
        return NULL;
    }

    scene_xdg_surface->xdg_surface = xdg_surface;

    scene_xdg_surface->tree = ky_scene_tree_create(parent);
    if (scene_xdg_surface->tree == NULL) {
        free(scene_xdg_surface);
        return NULL;
    }

    scene_xdg_surface->surface_tree =
        ky_scene_subsurface_tree_create(scene_xdg_surface->tree, xdg_surface->surface);
    struct ky_scene_node *node = ky_scene_node_from_tree(scene_xdg_surface->tree);
    if (scene_xdg_surface->surface_tree == NULL) {
        ky_scene_node_destroy(node);
        free(scene_xdg_surface);
        return NULL;
    }

    scene_xdg_surface->tree_destroy.notify = scene_xdg_surface_handle_tree_destroy;
    ky_scene_node_add_destroy_listener(node, &scene_xdg_surface->tree_destroy);

    scene_xdg_surface->xdg_surface_destroy.notify = scene_xdg_surface_handle_xdg_surface_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &scene_xdg_surface->xdg_surface_destroy);

    scene_xdg_surface->xdg_surface_map.notify = scene_xdg_surface_handle_xdg_surface_map;
    wl_signal_add(&xdg_surface->events.map, &scene_xdg_surface->xdg_surface_map);

    scene_xdg_surface->xdg_surface_unmap.notify = scene_xdg_surface_handle_xdg_surface_unmap;
    wl_signal_add(&xdg_surface->events.unmap, &scene_xdg_surface->xdg_surface_unmap);

    scene_xdg_surface->xdg_surface_commit.notify = scene_xdg_surface_handle_xdg_surface_commit;
    wl_signal_add(&xdg_surface->surface->events.commit, &scene_xdg_surface->xdg_surface_commit);

    ky_scene_node_set_enabled(node, xdg_surface->mapped);
    scene_xdg_surface_update_position(scene_xdg_surface);

    return scene_xdg_surface->tree;
}
