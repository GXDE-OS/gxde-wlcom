#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/xwayland.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "scene/surface.h"
#include "view/xwayland.h"
#include "view_p.h"

struct xwayland_server {
    struct wlr_xwayland *wlr_xwayland;

    struct wl_listener xwayland_ready;
    struct wl_listener new_xwayland_surface;
    struct wl_listener server_destroy;
};

struct xwayland_view {
    struct view view;
    struct wlr_xwayland_surface *wlr_xwayland_surface;
    struct ky_scene_tree *surface_tree;

    struct wl_listener commit;

    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;

    struct wl_listener request_configure;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_minimize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_activate;

    struct wl_listener set_title;
    struct wl_listener set_class;
    // struct wl_signal set_role;
    struct wl_listener set_parent;
    // struct wl_signal set_pid;
    // struct wl_signal set_startup_id;
    // struct wl_signal set_window_type;
    // struct wl_listener set_hints;
    struct wl_listener set_decorations;
    // struct wl_signal set_strut_partial;
    struct wl_listener set_override_redirect;
    // struct wl_listener set_geometry;
};

struct xwayland_unmanaged {
    struct wlr_xwayland_surface *wlr_xwayland_surface;
    struct ky_scene_node *surface_node;

    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;

    struct wl_listener request_activate;
    struct wl_listener request_configure;
    // struct wl_listener request_fullscreen;

    struct wl_listener set_geometry;
    struct wl_listener set_override_redirect;
};

static struct xwayland_server *xwayland = NULL;

static bool xwayland_unmanaged_hover(struct seat *seat, struct ky_scene_node *node, double x,
                                     double y, uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    struct wlr_xwayland *wlr_xwayland = xwayland->wlr_xwayland;

    if (wlr_xwayland->seat != seat->wlr_seat) {
        wlr_xwayland_set_seat(wlr_xwayland, seat->wlr_seat);
    }

    seat_notify_motion(seat, surface, time, x, y, first);
    return false;
}

static void xwayland_unmanaged_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                                     bool pressed, uint32_t time, bool dual, void *data)
{
    struct wlr_xwayland *wlr_xwayland = xwayland->wlr_xwayland;
    if (wlr_xwayland->seat != seat->wlr_seat) {
        wlr_xwayland_set_seat(wlr_xwayland, seat->wlr_seat);
    }

    seat_notify_button(seat, time, button, pressed);

    /* only do activated when button pressed */
    if (!pressed) {
        return;
    }

    /* only activate and focus top surface */
    struct xwayland_unmanaged *unmanaged = data;
    seat_focus_surface(seat, unmanaged->wlr_xwayland_surface->surface);
}

static void xwayland_unmanaged_leave(struct seat *seat, struct ky_scene_node *node, bool last,
                                     void *data)
{
    /* so surface will call set_cursor when enter again */
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_leave(seat, surface);
}

static struct ky_scene_node *xwayland_unmanaged_get_root(void *data)
{
    struct xwayland_unmanaged *unmanaged = data;
    return unmanaged->surface_node;
}

static const struct input_event_node_impl xwayland_unmanaged_event_node_impl = {
    .hover = xwayland_unmanaged_hover,
    .click = xwayland_unmanaged_click,
    .leave = xwayland_unmanaged_leave,
};

static void unmanaged_handle_request_activate(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, request_activate);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;
    struct seat *seat = seat_from_wlr_seat(xwayland->wlr_xwayland->seat);
    seat_focus_surface(seat, wlr_xwayland_surface->surface);
}

static void unmanaged_handle_request_configure(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, request_configure);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;
    struct wlr_xwayland_surface_configure_event *event = data;
    wlr_xwayland_surface_configure(wlr_xwayland_surface, event->x, event->y, event->width,
                                   event->height);
    if (unmanaged->surface_node) {
        ky_scene_node_set_position(unmanaged->surface_node, event->x, event->y);
    }
}

static void unmanaged_handle_set_geometry(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, set_geometry);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;
    if (unmanaged->surface_node) {
        ky_scene_node_set_position(unmanaged->surface_node, wlr_xwayland_surface->x,
                                   wlr_xwayland_surface->y);
    }
}

static void unmanaged_handle_map(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, map);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    /* Stack new surface on top */
    wlr_xwayland_surface_restack(wlr_xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
    if (wlr_xwayland_or_surface_wants_focus(wlr_xwayland_surface)) {
        struct seat *seat = seat_from_wlr_seat(xwayland->wlr_xwayland->seat);
        seat_focus_surface(seat, wlr_xwayland_surface->surface);
    }

    unmanaged->set_geometry.notify = unmanaged_handle_set_geometry;
    wl_signal_add(&wlr_xwayland_surface->events.set_geometry, &unmanaged->set_geometry);

    struct view_layer *layer = view_manager_get_layer(LAYER_UNMANAGED, false);
    struct ky_scene_surface *scene_surface =
        ky_scene_surface_create(layer->tree, wlr_xwayland_surface->surface);
    unmanaged->surface_node = ky_scene_node_from_buffer(scene_surface->buffer);

    ky_scene_node_set_position(unmanaged->surface_node, wlr_xwayland_surface->x,
                               wlr_xwayland_surface->y);

    input_event_node_create(unmanaged->surface_node, &xwayland_unmanaged_event_node_impl,
                            xwayland_unmanaged_get_root, unmanaged);
}

static void unmanaged_handle_unmap(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, unmap);

    wl_list_remove(&unmanaged->set_geometry.link);
    ky_scene_node_set_enabled(unmanaged->surface_node, false);
    /* surface_node will be destroyed by scene surface */
    unmanaged->surface_node = NULL;
}

static void unmanaged_handle_associate(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, associate);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    unmanaged->map.notify = unmanaged_handle_map;
    wl_signal_add(&wlr_xwayland_surface->surface->events.map, &unmanaged->map);
    unmanaged->unmap.notify = unmanaged_handle_unmap;
    wl_signal_add(&wlr_xwayland_surface->surface->events.unmap, &unmanaged->unmap);
}

static void unmanaged_handle_dissociate(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, dissociate);

    wl_list_remove(&unmanaged->map.link);
    wl_list_remove(&unmanaged->unmap.link);
}

static void unmanaged_handle_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, destroy);

    wl_list_remove(&unmanaged->request_configure.link);
    wl_list_remove(&unmanaged->set_override_redirect.link);
    wl_list_remove(&unmanaged->request_activate.link);
    wl_list_remove(&unmanaged->associate.link);
    wl_list_remove(&unmanaged->dissociate.link);
    wl_list_remove(&unmanaged->destroy.link);

    free(unmanaged);
}

static void xwayland_view_create(struct wlr_xwayland_surface *wlr_xwayland_surface);
static void unmanaged_handle_set_override_redirect(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged =
        wl_container_of(listener, unmanaged, set_override_redirect);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    bool mapped = wlr_xwayland_surface->surface != NULL && wlr_xwayland_surface->surface->mapped;
    if (mapped) {
        unmanaged_handle_unmap(&unmanaged->unmap, NULL);
    }
    unmanaged_handle_destroy(&unmanaged->destroy, NULL);

    xwayland_view_create(wlr_xwayland_surface);
}

static void xwayland_unmanaged_create(struct wlr_xwayland_surface *wlr_xwayland_surface)
{
    struct xwayland_unmanaged *unmanaged = calloc(1, sizeof(struct xwayland_unmanaged));
    if (!unmanaged) {
        return;
    }

    unmanaged->wlr_xwayland_surface = wlr_xwayland_surface;

    unmanaged->associate.notify = unmanaged_handle_associate;
    wl_signal_add(&wlr_xwayland_surface->events.associate, &unmanaged->associate);
    unmanaged->dissociate.notify = unmanaged_handle_dissociate;
    wl_signal_add(&wlr_xwayland_surface->events.dissociate, &unmanaged->dissociate);
    unmanaged->destroy.notify = unmanaged_handle_destroy;
    wl_signal_add(&wlr_xwayland_surface->events.destroy, &unmanaged->destroy);

    unmanaged->request_activate.notify = unmanaged_handle_request_activate;
    wl_signal_add(&wlr_xwayland_surface->events.request_activate, &unmanaged->request_activate);
    unmanaged->request_configure.notify = unmanaged_handle_request_configure;
    wl_signal_add(&wlr_xwayland_surface->events.request_configure, &unmanaged->request_configure);

    unmanaged->set_override_redirect.notify = unmanaged_handle_set_override_redirect;
    wl_signal_add(&wlr_xwayland_surface->events.set_override_redirect,
                  &unmanaged->set_override_redirect);

    bool mapped = wlr_xwayland_surface->surface != NULL && wlr_xwayland_surface->surface->mapped;
    if (mapped) {
        unmanaged_handle_map(&unmanaged->map, NULL);
    }
}

static bool xwayland_view_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                                uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    struct xwayland_view *xwayland_view = data;
    struct wlr_xwayland *wlr_xwayland = xwayland->wlr_xwayland;

    if (wlr_xwayland->seat != seat->wlr_seat) {
        wlr_xwayland_set_seat(wlr_xwayland, seat->wlr_seat);
    }

    if (!hold) {
        seat_notify_motion(seat, surface, time, x, y, first);
        return false;
    }

    double sx = x - xwayland_view->view.base.geometry.x;
    double sy = y - xwayland_view->view.base.geometry.y;
    seat_notify_motion(seat, surface, time, sx, sy, first);
    return true;
}

static void xwayland_view_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                                bool pressed, uint32_t time, bool dual, void *data)
{
    struct wlr_xwayland *wlr_xwayland = xwayland->wlr_xwayland;

    if (wlr_xwayland->seat != seat->wlr_seat) {
        wlr_xwayland_set_seat(wlr_xwayland, seat->wlr_seat);
    }

    seat_notify_button(seat, time, button, pressed);

    /* only do activated when button pressed */
    if (!pressed) {
        return;
    }

    /* only activate and focus top surface */
    struct xwayland_view *xwayland_view = data;
    kywc_view_activate(&xwayland_view->view.base);
    seat_focus_surface(seat, xwayland_view->wlr_xwayland_surface->surface);
}

static void xwayland_view_leave(struct seat *seat, struct ky_scene_node *node, bool last,
                                void *data)
{
    /* so surface will call set_cursor when enter again */
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_leave(seat, surface);
}

static struct ky_scene_node *xwayland_view_get_root(void *data)
{
    struct xwayland_view *xwayland_view = data;
    return ky_scene_node_from_tree(xwayland_view->view.tree);
}

static const struct input_event_node_impl xwayland_view_event_node_impl = {
    .hover = xwayland_view_hover,
    .click = xwayland_view_click,
    .leave = xwayland_view_leave,
};

static struct xwayland_view *xwayland_view_from_view(struct view *view)
{
    struct xwayland_view *xwayland_view = wl_container_of(view, xwayland_view, view);
    return xwayland_view;
}

static void xwayland_view_close(struct view *view)
{
    struct xwayland_view *xwayland_view = xwayland_view_from_view(view);
    wlr_xwayland_surface_close(xwayland_view->wlr_xwayland_surface);
}

static void xwayland_view_destroy(struct view *view)
{
    struct xwayland_view *xwayland_view = xwayland_view_from_view(view);
    free(xwayland_view);
}

static void xwayland_view_configure(struct view *view)
{
    struct xwayland_view *xwayland_view = xwayland_view_from_view(view);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;
    struct kywc_view *kywc_view = &xwayland_view->view.base;

    if (view->pending.action & VIEW_ACTION_MINIMIZE) {
        wlr_xwayland_surface_set_minimized(wlr_xwayland_surface, kywc_view->minimized);
        view->pending.action &= ~VIEW_ACTION_MINIMIZE;
    }

    if (view->pending.action == VIEW_ACTION_NOP) {
        return;
    }

    if (view->pending.action & VIEW_ACTION_ACTIVATE) {
        if (kywc_view->activated && wlr_xwayland_surface->minimized) {
            wlr_xwayland_surface_set_minimized(wlr_xwayland_surface, false);
        }
        if (kywc_view->activated) {
            wlr_xwayland_surface_restack(wlr_xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
        }
        wlr_xwayland_surface_activate(wlr_xwayland_surface, kywc_view->activated);
    }

    if (view->pending.action & VIEW_ACTION_FULLSCREEN) {
        wlr_xwayland_surface_set_fullscreen(wlr_xwayland_surface, kywc_view->fullscreen);
    }

    if (view->pending.action & VIEW_ACTION_MAXIMIZE) {
        wlr_xwayland_surface_set_maximized(wlr_xwayland_surface, kywc_view->maximized);
    }

    /* no need to resize surface when activate only */
    if (view->pending.action == VIEW_ACTION_ACTIVATE) {
        view_configured(view);
        return;
    }

    struct kywc_box *current = &view->base.geometry;
    struct kywc_box *pending = &view->pending.geometry;

    wlr_xwayland_surface_configure(wlr_xwayland_surface, pending->x, pending->y, pending->width,
                                   pending->height);

    /* If no need to resizing, process the move immediately */
    if (current->width == pending->width && current->height == pending->height) {
        view_helper_move(view, pending->x, pending->y);
    }

    view_configured(view);
}

static const struct view_impl xwl_surface_impl = {
    .configure = xwayland_view_configure,
    .close = xwayland_view_close,
    .destroy = xwayland_view_destroy,
};

static bool xwayland_view_update_geometry(struct xwayland_view *xwayland_view)
{
    struct wlr_surface_state *state = &xwayland_view->wlr_xwayland_surface->surface->current;
    xcb_size_hints_t *size_hints = xwayland_view->wlr_xwayland_surface->size_hints;
    struct kywc_box *current = &xwayland_view->view.base.geometry;

    bool new_size = current->width != state->width || current->height != state->height;

    if (!size_hints) {
        view_update_size(&xwayland_view->view, state->width, state->height, 0, 0, 0, 0);
    } else {
        /* convert -1 to zero followed by xdg-shell */
        view_update_size(&xwayland_view->view, state->width, state->height,
                         size_hints->min_width < 0 ? 0 : size_hints->min_width,
                         size_hints->min_height < 0 ? 0 : size_hints->min_height,
                         size_hints->max_width < 0 ? 0 : size_hints->max_width,
                         size_hints->max_height < 0 ? 0 : size_hints->max_height);
    }

    return new_size;
}

static void xwayland_view_handle_commit(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, commit);
    struct kywc_box *current = &xwayland_view->view.base.geometry;

    if (!xwayland_view_update_geometry(xwayland_view)) {
        return;
    }

    struct kywc_box *pending = &xwayland_view->view.pending.geometry;
    int x = pending->x, y = pending->y;

    if (current->x != pending->x) {
        x = pending->x + pending->width - current->width;
    }
    if (current->y != pending->y) {
        y = pending->y + pending->height - current->height;
    }

    view_helper_move(&xwayland_view->view, x, y);
}

static void xwayland_view_handle_request_move(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, request_move);

    struct seat *seat = seat_from_wlr_seat(xwayland->wlr_xwayland->seat);
    interactive_begin_move(&xwayland_view->view, seat);
}

static void xwayland_view_handle_request_resize(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, request_resize);
    struct wlr_xwayland_resize_event *event = data;

    struct seat *seat = seat_from_wlr_seat(xwayland->wlr_xwayland->seat);
    interactive_begin_resize(&xwayland_view->view, event->edges, seat);
}

static void xwayland_view_handle_request_minimize(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_minimize);
    struct wlr_xwayland_minimize_event *event = data;

    kywc_view_set_minimized(&xwayland_view->view.base, event->minimize);
}

static void xwayland_view_handle_request_maximize(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_maximize);

    bool maximized = xwayland_view->wlr_xwayland_surface->maximized_horz &&
                     xwayland_view->wlr_xwayland_surface->maximized_vert;
    kywc_view_set_maximized(&xwayland_view->view.base, maximized);
}

static void xwayland_view_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_fullscreen);

    bool fullscreen = xwayland_view->wlr_xwayland_surface->fullscreen;
    kywc_view_set_fullscreen(&xwayland_view->view.base, fullscreen);
}

static void xwayland_view_handle_request_activate(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_activate);

    kywc_view_activate(&xwayland_view->view.base);

    struct wlr_seat *wlr_seat = xwayland->wlr_xwayland->seat;
    struct seat *seat = wlr_seat ? seat_from_wlr_seat(wlr_seat) : input_manager_get_default_seat();
    seat_focus_surface(seat, xwayland_view->wlr_xwayland_surface->surface);
}

static void xwayland_view_handle_set_title(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, set_title);

    view_set_title(&xwayland_view->view, xwayland_view->wlr_xwayland_surface->title);
}

static void xwayland_view_handle_set_class(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, set_class);

    view_set_app_id(&xwayland_view->view, xwayland_view->wlr_xwayland_surface->class);
}

static void xwayland_view_handle_set_parent(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, set_parent);
    struct wlr_xwayland_surface *parent = xwayland_view->wlr_xwayland_surface->parent;
    struct xwayland_view *parent_xwayland_view = parent ? parent->data : NULL;
    struct view *parent_view = parent_xwayland_view ? &parent_xwayland_view->view : NULL;

    view_set_parent(&xwayland_view->view, parent_view);
}

static void xwayland_view_handle_set_decorations(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, set_decorations);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    bool use_ssd = wlr_xwayland_surface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
    view_set_decoration(&xwayland_view->view, use_ssd);
}

static void xwayland_view_handle_map(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, map);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    xwayland_view_update_geometry(xwayland_view);

    view_set_app_id(&xwayland_view->view, wlr_xwayland_surface->class);
    view_set_title(&xwayland_view->view, wlr_xwayland_surface->title);
    xwayland_view_handle_set_parent(&xwayland_view->set_parent, NULL);

    kywc_view_set_minimized(&xwayland_view->view.base, wlr_xwayland_surface->minimized);
    xwayland_view_handle_request_maximize(&xwayland_view->request_maximize, NULL);
    xwayland_view_handle_request_fullscreen(&xwayland_view->request_fullscreen, NULL);

    assert(wlr_xwayland_surface->surface == xwayland_view->view.surface);
    xwayland_view->commit.notify = xwayland_view_handle_commit;
    wl_signal_add(&wlr_xwayland_surface->surface->events.commit, &xwayland_view->commit);

    xwayland_view->surface_tree =
        ky_scene_subsurface_tree_create(xwayland_view->view.tree, wlr_xwayland_surface->surface);
    /* event node will be destroyed when surface_tree destroy */
    input_event_node_create(ky_scene_node_from_tree(xwayland_view->surface_tree),
                            &xwayland_view_event_node_impl, xwayland_view_get_root, xwayland_view);
    view_map(&xwayland_view->view);

    /* apply the position in size_hints */
    xcb_size_hints_t *size_hints = wlr_xwayland_surface->size_hints;
    if (size_hints &&
        size_hints->flags & (XCB_ICCCM_SIZE_HINT_US_POSITION | XCB_ICCCM_SIZE_HINT_P_POSITION)) {
        view_helper_move(&xwayland_view->view, wlr_xwayland_surface->x, wlr_xwayland_surface->y);
    }
}

static void xwayland_view_handle_unmap(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, unmap);

    wl_list_remove(&xwayland_view->commit.link);
    /* surface_tree is destroyed by scene subsurface */
    view_unmap(&xwayland_view->view);
}

static void xwayland_view_handle_associate(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, associate);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    xwayland_view->view.surface = wlr_xwayland_surface->surface;

    xwayland_view->map.notify = xwayland_view_handle_map;
    wl_signal_add(&wlr_xwayland_surface->surface->events.map, &xwayland_view->map);
    xwayland_view->unmap.notify = xwayland_view_handle_unmap;
    wl_signal_add(&wlr_xwayland_surface->surface->events.unmap, &xwayland_view->unmap);
}

static void xwayland_view_handle_dissociate(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, dissociate);

    xwayland_view->view.surface = NULL;

    wl_list_remove(&xwayland_view->map.link);
    wl_list_remove(&xwayland_view->unmap.link);
}

static void xwayland_view_handle_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, destroy);

    wl_list_remove(&xwayland_view->destroy.link);
    wl_list_remove(&xwayland_view->associate.link);
    wl_list_remove(&xwayland_view->dissociate.link);

    wl_list_remove(&xwayland_view->request_configure.link);
    wl_list_remove(&xwayland_view->request_move.link);
    wl_list_remove(&xwayland_view->request_resize.link);
    wl_list_remove(&xwayland_view->request_minimize.link);
    wl_list_remove(&xwayland_view->request_maximize.link);
    wl_list_remove(&xwayland_view->request_fullscreen.link);
    wl_list_remove(&xwayland_view->request_activate.link);

    wl_list_remove(&xwayland_view->set_title.link);
    wl_list_remove(&xwayland_view->set_class.link);
    wl_list_remove(&xwayland_view->set_parent.link);
    wl_list_remove(&xwayland_view->set_decorations.link);
    wl_list_remove(&xwayland_view->set_override_redirect.link);

    view_destroy(&xwayland_view->view);
}

static void xwayland_view_handle_set_override_redirect(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, set_override_redirect);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    bool mapped = wlr_xwayland_surface->surface != NULL && wlr_xwayland_surface->surface->mapped;
    if (mapped) {
        xwayland_view_handle_unmap(&xwayland_view->unmap, NULL);
    }

    xwayland_view_handle_destroy(&xwayland_view->destroy, NULL);
    wlr_xwayland_surface->data = NULL;

    xwayland_unmanaged_create(wlr_xwayland_surface);
}

static void xwayland_view_handle_request_configure(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;

    struct kywc_box geo = { event->x, event->y, event->width, event->height };
    kywc_view_resize(&xwayland_view->view.base, &geo);
}

static void xwayland_view_create(struct wlr_xwayland_surface *wlr_xwayland_surface)
{
    struct xwayland_view *xwayland_view = calloc(1, sizeof(struct xwayland_view));
    if (!xwayland_view) {
        return;
    }

    view_init(&xwayland_view->view, &xwl_surface_impl, xwayland_view);

    xwayland_view->wlr_xwayland_surface = wlr_xwayland_surface;
    wlr_xwayland_surface->data = xwayland_view;

    xwayland_view->associate.notify = xwayland_view_handle_associate;
    wl_signal_add(&wlr_xwayland_surface->events.associate, &xwayland_view->associate);
    xwayland_view->dissociate.notify = xwayland_view_handle_dissociate;
    wl_signal_add(&wlr_xwayland_surface->events.dissociate, &xwayland_view->dissociate);
    xwayland_view->destroy.notify = xwayland_view_handle_destroy;
    wl_signal_add(&wlr_xwayland_surface->events.destroy, &xwayland_view->destroy);

    xwayland_view->request_configure.notify = xwayland_view_handle_request_configure;
    wl_signal_add(&wlr_xwayland_surface->events.request_configure,
                  &xwayland_view->request_configure);

    xwayland_view->request_move.notify = xwayland_view_handle_request_move;
    wl_signal_add(&wlr_xwayland_surface->events.request_move, &xwayland_view->request_move);
    xwayland_view->request_resize.notify = xwayland_view_handle_request_resize;
    wl_signal_add(&wlr_xwayland_surface->events.request_resize, &xwayland_view->request_resize);

    xwayland_view->request_minimize.notify = xwayland_view_handle_request_minimize;
    wl_signal_add(&wlr_xwayland_surface->events.request_minimize, &xwayland_view->request_minimize);
    xwayland_view->request_maximize.notify = xwayland_view_handle_request_maximize;
    wl_signal_add(&wlr_xwayland_surface->events.request_maximize, &xwayland_view->request_maximize);
    xwayland_view->request_fullscreen.notify = xwayland_view_handle_request_fullscreen;
    wl_signal_add(&wlr_xwayland_surface->events.request_fullscreen,
                  &xwayland_view->request_fullscreen);
    xwayland_view->request_activate.notify = xwayland_view_handle_request_activate;
    wl_signal_add(&wlr_xwayland_surface->events.request_activate, &xwayland_view->request_activate);

    xwayland_view->set_title.notify = xwayland_view_handle_set_title;
    wl_signal_add(&wlr_xwayland_surface->events.set_title, &xwayland_view->set_title);
    xwayland_view->set_class.notify = xwayland_view_handle_set_class;
    wl_signal_add(&wlr_xwayland_surface->events.set_class, &xwayland_view->set_class);
    xwayland_view->set_parent.notify = xwayland_view_handle_set_parent;
    wl_signal_add(&wlr_xwayland_surface->events.set_parent, &xwayland_view->set_parent);

    xwayland_view->set_decorations.notify = xwayland_view_handle_set_decorations;
    wl_signal_add(&wlr_xwayland_surface->events.set_decorations, &xwayland_view->set_decorations);

    xwayland_view->set_override_redirect.notify = xwayland_view_handle_set_override_redirect;
    wl_signal_add(&wlr_xwayland_surface->events.set_override_redirect,
                  &xwayland_view->set_override_redirect);

    bool mapped = wlr_xwayland_surface->surface != NULL && wlr_xwayland_surface->surface->mapped;
    if (mapped) {
        xwayland_view_handle_map(&xwayland_view->map, NULL);
    }
}

static void handle_new_xwayland_surface(struct wl_listener *listener, void *data)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = data;
    wlr_xwayland_surface_ping(wlr_xwayland_surface);

    if (wlr_xwayland_surface->override_redirect) {
        xwayland_unmanaged_create(wlr_xwayland_surface);
        return;
    }

    xwayland_view_create(wlr_xwayland_surface);
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data)
{
    kywc_log(KYWC_INFO, "xwayland is ready");
    struct seat *seat = input_manager_get_default_seat();
    wlr_xwayland_set_seat(xwayland->wlr_xwayland, seat->wlr_seat);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&xwayland->server_destroy.link);
    free(xwayland);
    xwayland = NULL;
}

bool xwayland_server_create(struct server *server)
{
    if (!server->options.enable_xwayland) {
        kywc_log(KYWC_INFO, "xwayland is disabled by cmdline");
        return false;
    }

    xwayland = calloc(1, sizeof(struct xwayland_server));
    if (!xwayland) {
        kywc_log(KYWC_ERROR, "cannot create xwayland server");
        return false;
    }

    xwayland->wlr_xwayland = wlr_xwayland_create(server->display, server->compositor, true);
    if (!xwayland->wlr_xwayland) {
        kywc_log(KYWC_ERROR, "cannot create wlroots xwayland server");
        free(xwayland);
        xwayland = NULL;
        return false;
    }

    xwayland->new_xwayland_surface.notify = handle_new_xwayland_surface;
    wl_signal_add(&xwayland->wlr_xwayland->events.new_surface, &xwayland->new_xwayland_surface);
    xwayland->xwayland_ready.notify = handle_xwayland_ready;
    wl_signal_add(&xwayland->wlr_xwayland->events.ready, &xwayland->xwayland_ready);
    xwayland->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &xwayland->server_destroy);

    setenv("DISPLAY", xwayland->wlr_xwayland->display_name, true);
    kywc_log(KYWC_INFO, "xwayland is running on display %s", xwayland->wlr_xwayland->display_name);

    /* set xwayland cursor, use the default seat0 */
    struct seat *seat = input_manager_get_default_seat();
    struct wlr_xcursor *xcursor =
        wlr_xcursor_manager_get_xcursor(seat->cursor->xcursor_manager, "left_ptr", 1);
    if (xcursor) {
        struct wlr_xcursor_image *image = xcursor->images[0];
        wlr_xwayland_set_cursor(xwayland->wlr_xwayland, image->buffer, image->width * 4,
                                image->width, image->height, image->hotspot_x, image->hotspot_y);
    }

    return true;
}

void xwayland_server_destroy(void)
{
    if (xwayland) {
        wlr_xwayland_destroy(xwayland->wlr_xwayland);
    }
}
