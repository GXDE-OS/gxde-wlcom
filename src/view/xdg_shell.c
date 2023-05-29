#include <stdlib.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "scene/xdg_shell.h"
#include "view_p.h"

struct xdg_view {
    struct view view;
    struct wlr_xdg_surface *wlr_xdg_surface;
    struct ky_scene_tree *surface_tree;

    struct wl_listener commit;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener new_popup;

    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_minimize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_show_window_menu;

    struct wl_listener set_parent;
    struct wl_listener set_title;
    struct wl_listener set_app_id;

    struct kywc_box pending;
    uint32_t configure_serial;
};

static struct xdg_view *xdg_view_from_view(struct view *view)
{
    struct xdg_view *xdg_view = wl_container_of(view, xdg_view, view);
    return xdg_view;
}

static void xdg_view_close(struct view *view)
{
    struct xdg_view *xdg_view = xdg_view_from_view(view);
    wlr_xdg_toplevel_send_close(xdg_view->wlr_xdg_surface->toplevel);
}

static void xdg_view_configure(struct view *view, struct kywc_box *pending)
{
    struct xdg_view *xdg_view = xdg_view_from_view(view);
    struct wlr_xdg_surface *wlr_xdg_surface = xdg_view->wlr_xdg_surface;
    struct wlr_xdg_toplevel *wlr_xdg_toplevel = wlr_xdg_surface->toplevel;
    struct kywc_view *kywc_view = &view->base;

    wlr_xdg_toplevel_set_maximized(wlr_xdg_toplevel, kywc_view->maximized);
    wlr_xdg_toplevel_set_fullscreen(wlr_xdg_toplevel, kywc_view->fullscreen);
    wlr_xdg_toplevel_set_activated(wlr_xdg_toplevel, kywc_view->activated);
    wlr_xdg_toplevel_set_resizing(wlr_xdg_toplevel, kywc_view->resizing);
    wlr_xdg_toplevel_set_tiled(wlr_xdg_toplevel, kywc_view->tiled_edges);

    /* If not resizing, process the move immediately */
    if (kywc_view->geometry.width == pending->width &&
        kywc_view->geometry.height == pending->height) {
        // xdg_view->pending_action = VIEW_NOP;
        kywc_view_move(kywc_view, pending->x, pending->y);
        return;
    }

    xdg_view->pending = *pending;
    xdg_view->configure_serial =
        wlr_xdg_toplevel_set_size(wlr_xdg_toplevel, pending->width, pending->height);
}

static void xdg_view_destroy(struct view *view)
{
    struct xdg_view *xdg_view = xdg_view_from_view(view);
    free(xdg_view);
}

static const struct view_impl xdg_surface_impl = {
    .type = KYWC_VIEW_XDG_SHELL,
    .configure = xdg_view_configure,
    .close = xdg_view_close,
    .destroy = xdg_view_destroy,
};

static void xdg_view_update_geometry(struct xdg_view *xdg_view)
{
    struct wlr_xdg_surface *wlr_xdg_surface = xdg_view->wlr_xdg_surface;
    struct wlr_xdg_toplevel *wlr_xdg_toplevel = wlr_xdg_surface->toplevel;
    struct wlr_surface *wlr_surface = wlr_xdg_surface->surface;
    struct kywc_view *kywc_view = &xdg_view->view.base;

    kywc_view->min_width = wlr_xdg_toplevel->current.min_width;
    kywc_view->min_height = wlr_xdg_toplevel->current.min_height;
    kywc_view->max_width = wlr_xdg_toplevel->current.max_width;
    kywc_view->max_height = wlr_xdg_toplevel->current.max_height;

    /* update kywc_view current size */
    struct wlr_box geo = wlr_xdg_surface->current.geometry;
    if (!geo.width && !geo.height) {
        geo.width = wlr_surface->current.width;
        geo.height = wlr_surface->current.height;
    }
    kywc_view->geometry.width = geo.width;
    kywc_view->geometry.height = geo.height;

    /* padding if used CSD with drop-shadow */
    kywc_view->padding.left = geo.x;
    kywc_view->padding.right = wlr_surface->current.width - geo.x - geo.width;
    kywc_view->padding.top = geo.y;
    kywc_view->padding.bottom = wlr_surface->current.height - geo.y - geo.height;

    kywc_log(KYWC_DEBUG, "kywc_view %p size: (%d x %d) range: (%d x %d) to (%d x %d)", kywc_view,
             kywc_view->geometry.width, kywc_view->geometry.height, kywc_view->min_width,
             kywc_view->min_height, kywc_view->max_width, kywc_view->max_height);
    kywc_log(KYWC_DEBUG, "kywc_view %p padding: ← %d↑ %d→ %d↓ %d", kywc_view,
             kywc_view->padding.left, kywc_view->padding.top, kywc_view->padding.right,
             kywc_view->padding.bottom);
}

static void xdg_view_handle_commit(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, commit);

    // TODO:how to signal activate, maximize, minimize, fullscreen ...
    xdg_view_update_geometry(xdg_view);
    view_commit(&xdg_view->view);
}

static void xdg_view_handle_map(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, map);
    struct wlr_xdg_surface *wlr_xdg_surface = xdg_view->wlr_xdg_surface;
    struct wlr_surface *wlr_surface = wlr_xdg_surface->surface;

    xdg_view_update_geometry(xdg_view);

    /* create tree for surface and all sub-surfaces */
    xdg_view->surface_tree = ky_scene_xdg_surface_create(xdg_view->view.tree, wlr_xdg_surface);

    xdg_view->commit.notify = xdg_view_handle_commit;
    wl_signal_add(&wlr_surface->events.commit, &xdg_view->commit);

    view_map(&xdg_view->view);
}

static void xdg_view_handle_unmap(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, unmap);

    wl_list_remove(&xdg_view->commit.link);
    view_unmap(&xdg_view->view);
}

static void xdg_view_handle_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, destroy);

    wl_list_remove(&xdg_view->destroy.link);
    wl_list_remove(&xdg_view->map.link);
    wl_list_remove(&xdg_view->unmap.link);
    wl_list_remove(&xdg_view->new_popup.link);
    wl_list_remove(&xdg_view->request_move.link);
    wl_list_remove(&xdg_view->request_minimize.link);
    wl_list_remove(&xdg_view->request_maximize.link);
    wl_list_remove(&xdg_view->request_fullscreen.link);
    wl_list_remove(&xdg_view->request_resize.link);
    wl_list_remove(&xdg_view->request_show_window_menu.link);
    wl_list_remove(&xdg_view->set_parent.link);
    wl_list_remove(&xdg_view->set_title.link);
    wl_list_remove(&xdg_view->set_app_id.link);

    view_destroy(&xdg_view->view);
}

static void xdg_view_handle_new_popup(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, new_popup);
}

static void xdg_view_handle_request_move(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_move);
}

static void xdg_view_handle_request_resize(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_resize);
}

static void xdg_view_handle_request_minimize(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_minimize);
}

static void xdg_view_handle_request_maximize(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_maximize);
}

static void xdg_view_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_fullscreen);
}

static void xdg_view_handle_show_window_menu(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_show_window_menu);
}

static void xdg_view_handle_set_parent(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, set_parent);
    struct wlr_xdg_toplevel *parent = xdg_view->wlr_xdg_surface->toplevel->parent;
    struct xdg_view *parent_xdg_view = parent ? parent->base->data : NULL;
    struct view *parent_view = parent_xdg_view ? &parent_xdg_view->view : NULL;

    view_set_parent(&xdg_view->view, parent_view);
}

static void xdg_view_handle_set_title(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, set_title);
    const char *title = xdg_view->wlr_xdg_surface->toplevel->title;

    view_set_title(&xdg_view->view, title);
}

static void xdg_view_handle_set_app_id(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, set_app_id);
    const char *app_id = xdg_view->wlr_xdg_surface->toplevel->app_id;

    view_set_app_id(&xdg_view->view, app_id);
}

static void handle_new_xdg_surface(struct wl_listener *listener, void *data)
{
    struct wlr_xdg_surface *wlr_xdg_surface = data;

    /* popup is handled in surface new_popup listener */
    if (wlr_xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return;
    }

    struct xdg_view *xdg_view = calloc(1, sizeof(struct xdg_view));
    if (!xdg_view) {
        wl_resource_post_no_memory(wlr_xdg_surface->surface->resource);
        return;
    }

    // TODO: add arg layer to move tree_create to view_init
    view_init(&xdg_view->view, &xdg_surface_impl, xdg_view);

    // TODO: move tree_create to view_map ?
    /* create view tree and disable it */
    struct view_layer *layer = view_manager_get_layer(LAYER_NORMAL);
    xdg_view->view.tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(ky_scene_node_from_tree(xdg_view->view.tree), false);

    xdg_view->wlr_xdg_surface = wlr_xdg_surface;
    wlr_xdg_surface->data = xdg_view;
    /* for decoration */
    wlr_xdg_surface->surface->data = &xdg_view->view;

    /* wlr_xdg_surface listeners */
    xdg_view->map.notify = xdg_view_handle_map;
    wl_signal_add(&wlr_xdg_surface->events.map, &xdg_view->map);
    xdg_view->unmap.notify = xdg_view_handle_unmap;
    wl_signal_add(&wlr_xdg_surface->events.unmap, &xdg_view->unmap);
    xdg_view->destroy.notify = xdg_view_handle_destroy;
    wl_signal_add(&wlr_xdg_surface->events.destroy, &xdg_view->destroy);
    xdg_view->new_popup.notify = xdg_view_handle_new_popup;
    wl_signal_add(&wlr_xdg_surface->events.new_popup, &xdg_view->new_popup);

    /* wlr_xdg_toplevel listeners */
    struct wlr_xdg_toplevel *toplevel = wlr_xdg_surface->toplevel;
    xdg_view->request_move.notify = xdg_view_handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &xdg_view->request_move);
    xdg_view->request_resize.notify = xdg_view_handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &xdg_view->request_resize);
    xdg_view->request_minimize.notify = xdg_view_handle_request_minimize;
    wl_signal_add(&toplevel->events.request_minimize, &xdg_view->request_minimize);
    xdg_view->request_maximize.notify = xdg_view_handle_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &xdg_view->request_maximize);
    xdg_view->request_fullscreen.notify = xdg_view_handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &xdg_view->request_fullscreen);
    xdg_view->request_show_window_menu.notify = xdg_view_handle_show_window_menu;
    wl_signal_add(&toplevel->events.request_show_window_menu, &xdg_view->request_show_window_menu);
    xdg_view->set_parent.notify = xdg_view_handle_set_parent;
    wl_signal_add(&toplevel->events.set_parent, &xdg_view->set_parent);
    xdg_view->set_title.notify = xdg_view_handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &xdg_view->set_title);
    xdg_view->set_app_id.notify = xdg_view_handle_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &xdg_view->set_app_id);
}

bool xdg_shell_init(struct view_manager *view_manager)
{
    struct server *server = view_manager->server;
    struct wlr_xdg_shell *xdg_shell = wlr_xdg_shell_create(server->display, 5);
    if (!xdg_shell) {
        kywc_log(KYWC_ERROR, "unable to create xdg shell");
        return false;
    }

    view_manager->new_xdg_surface.notify = handle_new_xdg_surface;
    wl_signal_add(&xdg_shell->events.new_surface, &view_manager->new_xdg_surface);

    return true;
}
