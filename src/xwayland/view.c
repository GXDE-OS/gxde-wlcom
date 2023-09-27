// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>

#include "input/event.h"
#include "input/seat.h"
#include "output.h"
#include "scene/surface.h"
#include "view/action.h"
#include "xwayland_p.h"

struct xwayland_view {
    struct view view;
    struct xwayland_server *xwayland;
    struct wlr_xwayland_surface *wlr_xwayland_surface;
    struct ky_scene_node *surface_node;
    struct wl_listener node_destroy;

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
    // struct wl_listener set_role;
    struct wl_listener set_parent;
    // struct wl_listener set_pid;
    // struct wl_listener set_startup_id;
    // struct wl_listener set_window_type;
    struct wl_listener set_hints;
    struct wl_listener set_decorations;
    struct wl_listener set_strut_partial;
    struct wl_listener set_override_redirect;
    // struct wl_listener set_geometry;

    // TODO: output changed
    struct wl_listener output_update_usable_area;
};

static bool xwayland_view_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                                uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    struct xwayland_view *xwayland_view = data;
    struct wlr_xwayland *wlr_xwayland = xwayland_view->xwayland->wlr_xwayland;

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
    struct xwayland_view *xwayland_view = data;
    struct wlr_xwayland *wlr_xwayland = xwayland_view->xwayland->wlr_xwayland;

    if (wlr_xwayland->seat != seat->wlr_seat) {
        wlr_xwayland_set_seat(wlr_xwayland, seat->wlr_seat);
    }

    seat_notify_button(seat, time, button, pressed);

    /* only do activated when button pressed */
    if (!pressed) {
        return;
    }

    /* only activate and focus top surface */
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

static struct wlr_surface *xwayland_view_get_toplevel(void *data)
{
    struct xwayland_view *xwayland_view = data;
    return xwayland_view->view.surface;
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

static void xwayland_view_move(struct xwayland_view *xwayland_view, int x, int y)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;
    struct view *view = &xwayland_view->view;

    /* xwayland views need always sync position */
    wlr_xwayland_surface_configure(wlr_xwayland_surface, x, y, wlr_xwayland_surface->width,
                                   wlr_xwayland_surface->height);
    view_helper_move(view, x, y);
}

static void xwayland_view_configure(struct view *view)
{
    struct xwayland_view *xwayland_view = xwayland_view_from_view(view);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;
    struct kywc_view *kywc_view = &xwayland_view->view.base;

    if (view->pending.action & VIEW_ACTION_MINIMIZE) {
        view->pending.action &= ~VIEW_ACTION_MINIMIZE;
        wlr_xwayland_surface_set_minimized(wlr_xwayland_surface, kywc_view->minimized);
    }

    if (view->pending.action & VIEW_ACTION_ACTIVATE) {
        view->pending.action &= ~VIEW_ACTION_ACTIVATE;
        if (kywc_view->activated && wlr_xwayland_surface->minimized) {
            wlr_xwayland_surface_set_minimized(wlr_xwayland_surface, false);
        }
        wlr_xwayland_surface_activate(wlr_xwayland_surface, kywc_view->activated);
        if (kywc_view->activated) {
            wlr_xwayland_surface_restack(wlr_xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
            xwayland_restack_unmanaged(xwayland_view->xwayland);
        }
    }

    /* direct move when not changed size */
    if (view->pending.action & VIEW_ACTION_MOVE) {
        view->pending.action &= ~VIEW_ACTION_MOVE;
        if (!view_action_change_size(view->pending.action)) {
            xwayland_view_move(xwayland_view, view->pending.geometry.x, view->pending.geometry.y);
        } else {
            kywc_log(KYWC_DEBUG, "skip move when pending action 0x%x", view->pending.action);
        }
    }

    if (view->pending.action == VIEW_ACTION_NOP) {
        return;
    }

    /* now, only changed size action left */
    assert(view_action_change_size(view->pending.action));

    if (view->pending.action & VIEW_ACTION_FULLSCREEN) {
        wlr_xwayland_surface_set_fullscreen(wlr_xwayland_surface, kywc_view->fullscreen);
    }

    if (view->pending.action & VIEW_ACTION_MAXIMIZE) {
        wlr_xwayland_surface_set_maximized(wlr_xwayland_surface, kywc_view->maximized);
    }

    struct kywc_box *current = &view->base.geometry;
    struct kywc_box *pending = &view->pending.geometry;

    /* If no need to resizing, process the move immediately */
    if (current->width == pending->width && current->height == pending->height) {
        xwayland_view_move(xwayland_view, pending->x, pending->y);
        view_configured(&xwayland_view->view);
        return;
    }

    wlr_xwayland_surface_configure(wlr_xwayland_surface, pending->x, pending->y, pending->width,
                                   pending->height);
}

static const struct view_impl xwl_surface_impl = {
    .configure = xwayland_view_configure,
    .close = xwayland_view_close,
    .destroy = xwayland_view_destroy,
};

static void xwayland_view_update_geometry(struct xwayland_view *xwayland_view)
{
    struct wlr_surface_state *state = &xwayland_view->wlr_xwayland_surface->surface->current;
    xcb_size_hints_t *size_hints = xwayland_view->wlr_xwayland_surface->size_hints;

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
}

static void xwayland_view_handle_commit(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, commit);

    xwayland_view_update_geometry(xwayland_view);

    enum view_action pending_action = xwayland_view->view.pending.action;
    if (pending_action == VIEW_ACTION_NOP) {
        return;
    }

    assert(view_action_change_size(pending_action));

    struct kywc_box *current = &xwayland_view->view.base.geometry;
    struct kywc_box *pending = &xwayland_view->view.pending.geometry;
    int x = pending->x, y = pending->y;

    if (pending_action & VIEW_ACTION_RESIZE) {
        if (current->x != pending->x) {
            x = pending->x + pending->width - current->width;
        }
        if (current->y != pending->y) {
            y = pending->y + pending->height - current->height;
        }
    }

    xwayland_view_move(xwayland_view, x, y);
    view_configured(&xwayland_view->view);
}

static void xwayland_view_handle_request_move(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, request_move);
    struct seat *seat = seat_from_wlr_seat(xwayland_view->xwayland->wlr_xwayland->seat);
    window_begin_move(&xwayland_view->view, seat);
}

static void xwayland_view_handle_request_resize(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, request_resize);
    struct wlr_xwayland_resize_event *event = data;

    struct seat *seat = seat_from_wlr_seat(xwayland_view->xwayland->wlr_xwayland->seat);
    window_begin_resize(&xwayland_view->view, event->edges, seat);
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
    kywc_view_set_maximized(&xwayland_view->view.base, maximized, NULL);
}

static void xwayland_view_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_fullscreen);

    bool fullscreen = xwayland_view->wlr_xwayland_surface->fullscreen;
    kywc_view_set_fullscreen(&xwayland_view->view.base, fullscreen, NULL);
}

static void xwayland_view_handle_request_activate(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_activate);

    kywc_view_activate(&xwayland_view->view.base);

    struct wlr_seat *wlr_seat = xwayland_view->xwayland->wlr_xwayland->seat;
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

static void xwayland_view_handle_set_hints(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, set_hints);
    enum wlr_xwayland_icccm_input_model input_model =
        wlr_xwayland_icccm_input_model(xwayland_view->wlr_xwayland_surface);

    if (input_model == WLR_ICCCM_INPUT_MODEL_NONE || input_model == WLR_ICCCM_INPUT_MODEL_GLOBAL) {
        xwayland_view->view.base.focusable = false;
        xwayland_view->view.base.activatable = false;
    }
}

static void xwayland_view_handle_set_decorations(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, set_decorations);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;
    bool use_ssd = wlr_xwayland_surface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;

    for (size_t i = 0; i < wlr_xwayland_surface->window_type_len; ++i) {
        xcb_atom_t type = wlr_xwayland_surface->window_type[i];
        if (type == xwayland_view->xwayland->atoms[NET_WM_WINDOW_TYPE_DOCK]) {
            use_ssd = false;
            break;
        }
    }

    view_set_decoration(&xwayland_view->view, use_ssd);
}

static void xwayland_view_handle_output_update_usable_area(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, output_update_usable_area);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;
    struct kywc_box *usable_area = data;

    struct kywc_box geo;
    kywc_output_effective_geometry(xwayland_view->view.output, &geo);

    xcb_ewmh_wm_strut_partial_t *strut = wlr_xwayland_surface->strut_partial;
    if (strut->left_start_y != strut->left_end_y) {
        geo.x += strut->left;
        geo.width -= strut->left;
    }
    if (strut->right_start_y != strut->right_end_y) {
        geo.width = strut->right;
    }
    if (strut->top_start_x != strut->top_end_x) {
        geo.y += strut->top;
        geo.height -= strut->top;
    }
    if (strut->bottom_start_x != strut->bottom_end_x) {
        geo.height = strut->bottom;
    }

    /* intersect usable_area and geo */
    usable_area->x = geo.x > usable_area->x ? geo.x : usable_area->x;
    usable_area->y = geo.y > usable_area->y ? geo.y : usable_area->y;
    usable_area->width = geo.width < usable_area->width ? geo.width : usable_area->width;
    usable_area->height = geo.height < usable_area->height ? geo.height : usable_area->height;
}

// XXX: set enabled arg if we need update usable_area when minimize and unmap
static void xwayland_view_set_sruct_partial(struct xwayland_view *xwayland_view, bool enabled)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;
    xcb_ewmh_wm_strut_partial_t *strut = wlr_xwayland_surface->strut_partial;

    /* had reserved space before */
    bool had_area = !wl_list_empty(&xwayland_view->output_update_usable_area.link);
    bool has_area =
        enabled && strut &&
        (strut->left_start_y != strut->left_end_y || strut->right_start_y != strut->right_end_y ||
         strut->top_start_x != strut->top_end_x || strut->bottom_start_x != strut->bottom_end_x);

    if (!has_area) {
        if (had_area) {
            wl_list_remove(&xwayland_view->output_update_usable_area.link);
            wl_list_init(&xwayland_view->output_update_usable_area.link);
            kywc_output_update_usable_area(xwayland_view->view.output);
        }
        return;
    }

    if (!had_area) {
        xwayland_view->output_update_usable_area.notify =
            xwayland_view_handle_output_update_usable_area;
        output_add_update_usable_area_listener(xwayland_view->view.output,
                                               &xwayland_view->output_update_usable_area, true);
    }

    kywc_output_update_usable_area(xwayland_view->view.output);
}

static void xwayland_view_handle_set_strut_partial(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, set_strut_partial);
    xwayland_view_set_sruct_partial(xwayland_view, true);
}

static void xwayland_view_adjust_geometry(struct xwayland_view *xwayland_view, struct kywc_box *geo)
{
    struct kywc_view *kywc_view = &xwayland_view->view.base;
    struct kywc_output *kywc_output = kywc_output_at_point(geo->x, geo->y);

    if (kywc_view->fullscreen) {
        kywc_output_effective_geometry(kywc_output, geo);
        return;
    }

    /* update ssd and make sure ssd is visible */
    xwayland_view_handle_set_decorations(&xwayland_view->set_decorations, NULL);

    if (kywc_view->maximized) {
        view_get_tiled_geometry(&xwayland_view->view, geo, kywc_output, KYWC_TILE_ALL);
    } else if (kywc_view->tiled) {
        view_get_tiled_geometry(&xwayland_view->view, geo, kywc_output, kywc_view->tiled);
    } else {
        struct output *output = output_from_kywc_output(kywc_output);
        int min_x = output->usable_area.x + kywc_view->margin.off_x;
        int min_y = output->usable_area.y + kywc_view->margin.off_y;
        geo->x = geo->x <= min_x ? min_x : geo->x;
        geo->y = geo->y <= min_y ? min_y : geo->y;
    }
}

static void xwayland_view_handle_map(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, map);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    xwayland_view_update_geometry(xwayland_view);

    view_set_app_id(&xwayland_view->view, wlr_xwayland_surface->class);
    view_set_title(&xwayland_view->view, wlr_xwayland_surface->title);
    xwayland_view_handle_set_parent(&xwayland_view->set_parent, NULL);
    xwayland_view_handle_set_decorations(&xwayland_view->set_decorations, NULL);
    // TODO: set shadow to all xwayland view ?
    view_set_shadow(&xwayland_view->view, true);
    kywc_view_set_minimized(&xwayland_view->view.base, wlr_xwayland_surface->minimized);
    xwayland_view_handle_request_maximize(&xwayland_view->request_maximize, NULL);
    xwayland_view_handle_request_fullscreen(&xwayland_view->request_fullscreen, NULL);

    assert(wlr_xwayland_surface->surface == xwayland_view->view.surface);
    xwayland_view->commit.notify = xwayland_view_handle_commit;
    wl_signal_add(&wlr_xwayland_surface->surface->events.commit, &xwayland_view->commit);
    xwayland_view->set_strut_partial.notify = xwayland_view_handle_set_strut_partial;
    wl_signal_add(&wlr_xwayland_surface->events.set_strut_partial,
                  &xwayland_view->set_strut_partial);

    for (size_t i = 0; i < wlr_xwayland_surface->window_type_len; ++i) {
        xcb_atom_t type = wlr_xwayland_surface->window_type[i];
        if (type == xwayland_view->xwayland->atoms[NET_WM_WINDOW_TYPE_DOCK]) {
            xwayland_view->view.base.focusable = false;
            xwayland_view->view.base.activatable = false;
            /* reparent to dock layer and remove from workspace */
            struct view_layer *layer = view_manager_get_layer(LAYER_DOCK, false);
            ky_scene_node_reparent(ky_scene_node_from_tree(xwayland_view->view.tree), layer->tree);
            view_set_workspace(&xwayland_view->view, NULL);
            break;
        }
    }

    /* apply the position in size_hints */
    xcb_size_hints_t *size_hints = wlr_xwayland_surface->size_hints;
    if (size_hints &&
        size_hints->flags & (XCB_ICCCM_SIZE_HINT_US_POSITION | XCB_ICCCM_SIZE_HINT_P_POSITION) &&
        !xwayland_view->view.base.maximized && !xwayland_view->view.base.fullscreen &&
        !xwayland_view->view.base.tiled && !xwayland_view->view.base.has_initial_position) {
        xwayland_view->view.base.has_initial_position = true;
        struct kywc_box geo = {
            .x = size_hints->x,
            .y = size_hints->y,
        };
        xwayland_view_adjust_geometry(xwayland_view, &geo);
        kywc_view_move(&xwayland_view->view.base, geo.x, geo.y);
    }

    view_map(&xwayland_view->view);

    xwayland_view_set_sruct_partial(xwayland_view, true);
}

static void xwayland_view_handle_unmap(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, unmap);

    wl_list_remove(&xwayland_view->commit.link);
    wl_list_remove(&xwayland_view->set_strut_partial.link);
    /* surface_tree is destroyed by scene subsurface */
    view_unmap(&xwayland_view->view);
}

static void xwayalnd_view_handle_node_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, node_destroy);
    wl_list_remove(&xwayland_view->node_destroy.link);
    xwayland_view->surface_node = NULL;
}

static void xwayland_view_handle_associate(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, associate);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    xwayland_view->view.surface = wlr_xwayland_surface->surface;
    wlr_xwayland_surface->surface->data = xwayland_view;

    /* create scene tree here as we get surface here */
    struct ky_scene_tree *surface_tree =
        ky_scene_subsurface_tree_create(xwayland_view->view.content, wlr_xwayland_surface->surface);
    /* event node will be destroyed when surface_node destroy */
    xwayland_view->surface_node = ky_scene_node_from_tree(surface_tree);
    input_event_node_create(xwayland_view->surface_node, &xwayland_view_event_node_impl,
                            xwayland_view_get_root, xwayland_view_get_toplevel, xwayland_view);

    xwayland_view->map.notify = xwayland_view_handle_map;
    wl_signal_add(&wlr_xwayland_surface->surface->events.map, &xwayland_view->map);
    xwayland_view->unmap.notify = xwayland_view_handle_unmap;
    wl_signal_add(&wlr_xwayland_surface->surface->events.unmap, &xwayland_view->unmap);
    xwayland_view->node_destroy.notify = xwayalnd_view_handle_node_destroy;
    ky_scene_node_add_destroy_listener(xwayland_view->surface_node, &xwayland_view->node_destroy);
}

static void xwayland_view_handle_dissociate(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, dissociate);

    xwayland_view->wlr_xwayland_surface->surface->data = NULL;
    xwayland_view->view.surface = NULL;
    if (xwayland_view->surface_node) {
        ky_scene_node_destroy(xwayland_view->surface_node);
    }

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
    wl_list_remove(&xwayland_view->set_hints.link);
    wl_list_remove(&xwayland_view->set_decorations.link);
    wl_list_remove(&xwayland_view->set_override_redirect.link);
    wl_list_remove(&xwayland_view->output_update_usable_area.link);

    view_destroy(&xwayland_view->view);
}

static void xwayland_view_handle_set_override_redirect(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, set_override_redirect);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    if (wlr_xwayland_surface->surface && wlr_xwayland_surface->surface->mapped) {
        xwayland_view_handle_unmap(&xwayland_view->unmap, NULL);
        xwayland_view_handle_dissociate(&xwayland_view->dissociate, NULL);
    }
    xwayland_view_handle_destroy(&xwayland_view->destroy, NULL);
    wlr_xwayland_surface->data = NULL;

    xwayland_unmanaged_create(xwayland_view->xwayland, wlr_xwayland_surface);
}

static void xwayland_view_handle_request_configure(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    struct kywc_view *kywc_view = &xwayland_view->view.base;
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    struct kywc_box geo = { event->x, event->y, event->width, event->height };
    xwayland_view_adjust_geometry(xwayland_view, &geo);
    kywc_view_resize(kywc_view, &geo);

    if (!kywc_view->mapped) {
        wlr_xwayland_surface_configure(wlr_xwayland_surface, geo.x, geo.y, geo.width, geo.height);
        kywc_view->has_initial_position = true;
    }
}

void xwayland_view_create(struct xwayland_server *xwayland,
                          struct wlr_xwayland_surface *wlr_xwayland_surface)
{
    struct xwayland_view *xwayland_view = calloc(1, sizeof(struct xwayland_view));
    if (!xwayland_view) {
        return;
    }

    xwayland_view->xwayland = xwayland;
    view_init(&xwayland_view->view, &xwl_surface_impl, xwayland_view);

    xwayland_view->wlr_xwayland_surface = wlr_xwayland_surface;
    wlr_xwayland_surface->data = xwayland_view;
    wl_list_init(&xwayland_view->output_update_usable_area.link);

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

    xwayland_view->set_hints.notify = xwayland_view_handle_set_hints;
    wl_signal_add(&wlr_xwayland_surface->events.set_hints, &xwayland_view->set_hints);
    xwayland_view->set_decorations.notify = xwayland_view_handle_set_decorations;
    wl_signal_add(&wlr_xwayland_surface->events.set_decorations, &xwayland_view->set_decorations);

    xwayland_view->set_override_redirect.notify = xwayland_view_handle_set_override_redirect;
    wl_signal_add(&wlr_xwayland_surface->events.set_override_redirect,
                  &xwayland_view->set_override_redirect);

    if (wlr_xwayland_surface->surface && wlr_xwayland_surface->surface->mapped) {
        xwayland_view_handle_associate(&xwayland_view->associate, NULL);
        xwayland_view_handle_map(&xwayland_view->map, NULL);
    }
}
