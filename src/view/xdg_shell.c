// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "input/event.h"
#include "output.h"
#include "scene/xdg_shell.h"
#include "view/action.h"
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
};

static bool xdg_view_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                           uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    if (first) {
        kywc_log(KYWC_DEBUG, "first hover surface %p (%f %f)", surface, x, y);
    }

    if (!hold) {
        seat_notify_motion(seat, surface, time, x, y, first);
        return false;
    }

    struct xdg_view *xdg_view = data;
    struct kywc_box *geometry = &xdg_view->view.base.geometry;

    double sx = x - geometry->x;
    double sy = y - geometry->y;
    sx = sx < 0 ? 0 : (sx > geometry->width ? geometry->width : sx);
    sy = sy < 0 ? 0 : (sy > geometry->height ? geometry->height : sy);
    sx += xdg_view->wlr_xdg_surface->current.geometry.x;
    sy += xdg_view->wlr_xdg_surface->current.geometry.y;

    seat_notify_motion(seat, surface, time, sx, sy, first);
    return true;
}

static void xdg_view_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                           bool pressed, uint32_t time, enum click_state state, void *data)
{
    seat_notify_button(seat, time, button, pressed);

    /* only do activated when button pressed */
    if (!pressed) {
        return;
    }

    /* only activate and focus top surface */
    struct xdg_view *xdg_view = data;
    kywc_view_activate(&xdg_view->view.base);
    seat_focus_surface(seat, xdg_view->wlr_xdg_surface->surface);
}

static void xdg_view_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data)
{
    /* so surface will call set_cursor when enter again */
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_leave(seat, surface);
}

static struct ky_scene_node *xdg_view_get_root(void *data)
{
    struct xdg_view *xdg_view = data;
    return &xdg_view->view.tree->node;
}

static struct wlr_surface *xdg_view_get_toplevel(void *data)
{
    struct xdg_view *xdg_view = data;
    return xdg_view->view.surface;
}

static const struct input_event_node_impl xdg_view_event_node_impl = {
    .hover = xdg_view_hover,
    .click = xdg_view_click,
    .leave = xdg_view_leave,
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

static void xdg_view_configure(struct view *view)
{
    struct xdg_view *xdg_view = xdg_view_from_view(view);
    struct wlr_xdg_toplevel *wlr_xdg_toplevel = xdg_view->wlr_xdg_surface->toplevel;
    struct kywc_view *kywc_view = &xdg_view->view.base;

    /* do nothing when minimize */
    if (view->pending.action & VIEW_ACTION_MINIMIZE) {
        view->pending.action &= ~VIEW_ACTION_MINIMIZE;
    }

    /* ignore activate configure serial */
    if (view->pending.action & VIEW_ACTION_ACTIVATE) {
        view->pending.action &= ~VIEW_ACTION_ACTIVATE;
        wlr_xdg_toplevel_set_activated(wlr_xdg_toplevel, kywc_view->activated);
    }

    /* direct move when not changed size */
    if (view->pending.action & VIEW_ACTION_MOVE) {
        view->pending.action &= ~VIEW_ACTION_MOVE;
        if (!view_action_change_size(view->pending.configure_action)) {
            view_helper_move(view, view->pending.geometry.x, view->pending.geometry.y);
        } else {
            kywc_log(KYWC_DEBUG, "skip move when pending configure action 0x%x",
                     view->pending.configure_action);
        }
    }

    if (view->pending.action == VIEW_ACTION_NOP) {
        return;
    }

    /* now, only changed size action left */
    assert(view_action_change_size(view->pending.action));

    uint32_t serial = 0;
    if (view->pending.action & VIEW_ACTION_FULLSCREEN) {
        serial = wlr_xdg_toplevel_set_fullscreen(wlr_xdg_toplevel, kywc_view->fullscreen);
    }

    if (view->pending.action & VIEW_ACTION_MAXIMIZE) {
        serial = wlr_xdg_toplevel_set_maximized(wlr_xdg_toplevel, kywc_view->maximized);
    }

    if (view->pending.action & VIEW_ACTION_TILE) {
        serial = wlr_xdg_toplevel_set_tiled(wlr_xdg_toplevel, kywc_view->tiled ? 0xf : 0);
    }

    struct kywc_box *current = &view->base.geometry;
    struct kywc_box *pending = &view->pending.geometry;

    /* If no need to resizing, process the move immediately */
    if (view->pending.configure_serial == 0 && current->width == pending->width &&
        current->height == pending->height) {
        view->pending.action &= ~VIEW_ACTION_RESIZE;
        view_helper_move(view, pending->x, pending->y);
        view_configure(view, serial);
        return;
    }

    serial = wlr_xdg_toplevel_set_size(wlr_xdg_toplevel, pending->width, pending->height);

    view_configure(view, serial);
}

static void xdg_view_close_popups(struct view *view)
{
    struct xdg_view *xdg_view = xdg_view_from_view(view);

    struct wlr_xdg_popup *popup, *tmp;
    wl_list_for_each_safe(popup, tmp, &xdg_view->wlr_xdg_surface->popups, link) {
        wlr_xdg_popup_destroy(popup);
    }
}

static void xdg_view_destroy(struct view *view)
{
    struct xdg_view *xdg_view = xdg_view_from_view(view);
    free(xdg_view);
}

static const struct view_impl xdg_surface_impl = {
    .configure = xdg_view_configure,
    .close_popups = xdg_view_close_popups,
    .close = xdg_view_close,
    .destroy = xdg_view_destroy,
    .get_icon_buffer = NULL,
};

static void xdg_view_update_geometry(struct xdg_view *xdg_view)
{
    struct wlr_xdg_surface *wlr_xdg_surface = xdg_view->wlr_xdg_surface;
    struct wlr_surface *wlr_surface = wlr_xdg_surface->surface;

    struct wlr_box *geo = &wlr_xdg_surface->current.geometry;
    int width = geo->width, height = geo->height;

    if (!width || !height) {
        width = wlr_surface->current.width;
        height = wlr_surface->current.height;
    }

    struct wlr_xdg_toplevel_state *current = &wlr_xdg_surface->toplevel->current;
    view_update_size(&xdg_view->view, width, height, current->min_width, current->min_height,
                     current->max_width, current->max_height);

    struct kywc_view *kywc_view = &xdg_view->view.base;
    /* padding if used CSD with drop-shadow */
    kywc_view->padding.left = geo->x;
    kywc_view->padding.right = wlr_surface->current.width - geo->x - width;
    kywc_view->padding.top = geo->y;
    kywc_view->padding.bottom = wlr_surface->current.height - geo->y - height;

    kywc_log(KYWC_DEBUG, "kywc_view %p padding: ← %d↑ %d→ %d↓ %d", kywc_view,
             kywc_view->padding.left, kywc_view->padding.top, kywc_view->padding.right,
             kywc_view->padding.bottom);
}

static void xdg_view_handle_commit(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, commit);
    struct view *view = &xdg_view->view;

    xdg_view_update_geometry(xdg_view);

    enum view_action pending_action = view->pending.configure_action;
    uint32_t pending_serial = view->pending.configure_serial;
    if (pending_action == VIEW_ACTION_NOP && pending_serial == 0) {
        return;
    }

    assert(view_action_change_size(pending_action));

    struct kywc_box *current = &view->base.geometry;
    struct kywc_box *pending = &view->pending.configure_geometry;
    int x = pending->x, y = pending->y;

    uint32_t current_serial = xdg_view->wlr_xdg_surface->current.configure_serial;
    /* pending configure has not been acked yet, fix wobbling when resize */
    if (pending_serial >= current_serial && pending_action & VIEW_ACTION_RESIZE) {
        uint32_t resize_edges = xdg_view->view.current_resize_edges;
        if (resize_edges & KYWC_EDGE_LEFT) {
            x += pending->width - current->width;
        }
        if (resize_edges & KYWC_EDGE_TOP) {
            y += pending->height - current->height;
        }
        view_helper_move(view, x, y);
    }

    /* last configure has been acked */
    if (current_serial >= pending_serial) {
        view_helper_move(view, x, y);
        view_configured(&xdg_view->view);
    }
}

static void xdg_view_handle_new_popup(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, new_popup);
    struct wlr_xdg_popup *wlr_xdg_popup = data;

    struct kywc_view *kywc_view = &xdg_view->view.base;
    enum layer layer = LAYER_POPUP;
    if (kywc_view->role == KYWC_VIEW_ROLE_LOGOUT || kywc_view->role == KYWC_VIEW_ROLE_SCREENLOCK) {
        layer = LAYER_SCREEN_LOCK_NOTIFICATION;
    }
    struct view_layer *popup_layer = view_manager_get_layer(layer, false);
    xdg_popup_create(wlr_xdg_popup, xdg_view->surface_tree, popup_layer);
}

static void xdg_view_handle_request_move(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_move);
    struct wlr_xdg_toplevel_move_event *event = data;
    struct seat *seat = seat_from_wlr_seat(event->seat->seat);

    window_begin_move(&xdg_view->view, seat);
}

static void xdg_view_handle_request_resize(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_resize);
    struct wlr_xdg_toplevel_resize_event *event = data;
    struct seat *seat = seat_from_wlr_seat(event->seat->seat);

    window_begin_resize(&xdg_view->view, event->edges, seat);
}

static void xdg_view_handle_request_minimize(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_minimize);
    struct wlr_xdg_toplevel *toplevel = xdg_view->wlr_xdg_surface->toplevel;

    kywc_view_set_minimized(&xdg_view->view.base, toplevel->requested.minimized);
}

static void xdg_view_handle_request_maximize(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_maximize);
    struct wlr_xdg_toplevel *toplevel = xdg_view->wlr_xdg_surface->toplevel;

    kywc_view_set_maximized(&xdg_view->view.base, toplevel->requested.maximized, NULL);
}

static void xdg_view_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_fullscreen);
    struct wlr_xdg_toplevel *toplevel = xdg_view->wlr_xdg_surface->toplevel;
    struct wlr_xdg_toplevel_requested *requested = &toplevel->requested;
    struct kywc_view *kywc_view = &xdg_view->view.base;

    struct kywc_output *kywc_output = NULL;
    if (requested->fullscreen_output) {
        struct output *output = output_from_wlr_output(requested->fullscreen_output);
        if (output && !output->base.destroying && output->base.state.enabled) {
            kywc_output = &output->base;
        }
    }

    kywc_view_set_fullscreen(kywc_view, requested->fullscreen, kywc_output);
}

static void xdg_view_handle_show_window_menu(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, request_show_window_menu);
    struct wlr_xdg_toplevel_show_window_menu_event *event = data;
    struct kywc_view *kywc_view = &xdg_view->view.base;

    struct seat *seat = seat_from_wlr_seat(event->seat->seat);
    int off_x = kywc_view->geometry.x - xdg_view->wlr_xdg_surface->current.geometry.x;
    int off_y = kywc_view->geometry.y - xdg_view->wlr_xdg_surface->current.geometry.y;
    view_show_window_menu(&xdg_view->view, seat, off_x + event->x, off_y + event->y);
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

static void xdg_view_handle_map(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, map);
    struct wlr_xdg_surface *wlr_xdg_surface = xdg_view->wlr_xdg_surface;
    struct wlr_surface *wlr_surface = wlr_xdg_surface->surface;
    struct wlr_xdg_toplevel *toplevel = wlr_xdg_surface->toplevel;

    xdg_view_update_geometry(xdg_view);

    /* all states are ready when map */
    view_set_app_id(&xdg_view->view, toplevel->app_id);
    view_set_title(&xdg_view->view, toplevel->title);
    xdg_view_handle_set_parent(&xdg_view->set_parent, NULL);

    view_set_shaded(&xdg_view->view, xdg_view->view.base.ssd != KYWC_SSD_NONE);

    xdg_view_handle_request_minimize(&xdg_view->request_minimize, NULL);
    xdg_view_handle_request_maximize(&xdg_view->request_maximize, NULL);
    xdg_view_handle_request_fullscreen(&xdg_view->request_fullscreen, NULL);

    /* some requesets before map are cached in toplevel */
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

    xdg_view->new_popup.notify = xdg_view_handle_new_popup;
    wl_signal_add(&wlr_xdg_surface->events.new_popup, &xdg_view->new_popup);
    xdg_view->commit.notify = xdg_view_handle_commit;
    wl_signal_add(&wlr_surface->events.commit, &xdg_view->commit);

    struct wl_client *client = wl_resource_get_client(wlr_surface->resource);
    wl_client_get_credentials(client, &xdg_view->view.pid, NULL, NULL);
    view_map(&xdg_view->view);
}

static void xdg_view_handle_unmap(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, unmap);

    wl_list_remove(&xdg_view->commit.link);
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

    view_unmap(&xdg_view->view);
}

static void xdg_view_handle_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_view *xdg_view = wl_container_of(listener, xdg_view, destroy);

    wl_list_remove(&xdg_view->destroy.link);
    wl_list_remove(&xdg_view->map.link);
    wl_list_remove(&xdg_view->unmap.link);

    /* scene tree destroy will be called before by scene */
    view_destroy(&xdg_view->view);
}

static void handle_new_xdg_surface(struct wl_listener *listener, void *data)
{
    struct wlr_xdg_surface *wlr_xdg_surface = data;

    /* popup is handled in surface new_popup listener */
    if (wlr_xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return;
    }

    wlr_xdg_surface_ping(wlr_xdg_surface);

    struct xdg_view *xdg_view = calloc(1, sizeof(struct xdg_view));
    if (!xdg_view) {
        wl_resource_post_no_memory(wlr_xdg_surface->surface->resource);
        return;
    }

    xdg_view->view.surface = wlr_xdg_surface->surface;
    view_init(&xdg_view->view, &xdg_surface_impl, xdg_view);

    xdg_view->wlr_xdg_surface = wlr_xdg_surface;
    wlr_xdg_surface->data = xdg_view;
    wlr_xdg_surface->surface->data = &xdg_view->view;

    /* create tree for surface and all sub-surfaces */
    xdg_view->surface_tree = ky_scene_xdg_surface_create(xdg_view->view.content, wlr_xdg_surface);
    /* event node will be destroyed when xdg_surface destroy */
    input_event_node_create(&xdg_view->surface_tree->node, &xdg_view_event_node_impl,
                            xdg_view_get_root, xdg_view_get_toplevel, xdg_view);

    /* others will add in map and remove in unmap */
    xdg_view->map.notify = xdg_view_handle_map;
    wl_signal_add(&wlr_xdg_surface->surface->events.map, &xdg_view->map);
    xdg_view->unmap.notify = xdg_view_handle_unmap;
    wl_signal_add(&wlr_xdg_surface->surface->events.unmap, &xdg_view->unmap);
    xdg_view->destroy.notify = xdg_view_handle_destroy;
    wl_signal_add(&wlr_xdg_surface->events.destroy, &xdg_view->destroy);
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
