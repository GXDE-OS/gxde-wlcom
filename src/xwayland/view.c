// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <float.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/util/region.h>

#include "input/event.h"
#include "input/seat.h"
#include "output.h"
#include "painter.h"
#include "scene/surface.h"
#include "theme.h"
#include "view/action.h"
#include "view/workspace.h"
#include "xwayland_p.h"

struct xwayland_view {
    struct view view;
    struct wl_list link;
    struct xwayland_server *xwayland;
    struct wlr_xwayland_surface *wlr_xwayland_surface;
    struct wl_listener surface_tree_destroy;

    struct wl_listener precommit;
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
    // struct wl_listener set_startup_id;
    // struct wl_listener set_window_type;
    struct wl_listener set_hints;
    struct wl_listener set_decorations;
    struct wl_listener set_strut_partial;
    struct wl_listener set_override_redirect;
    // struct wl_listener set_geometry;

    // TODO: output changed
    struct wl_listener output_update_usable_area;

    struct wl_list net_wm_icons; // from net_wm_icon
};

struct net_wm_icon {
    struct wl_list link;

    uint32_t width, height;
    unsigned char *data;
    struct wlr_buffer *buffer;
};

static bool xwayland_view_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                                uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);

    xwayland_update_seat(seat);
    xwayland_update_hovered_surface(surface);

    if (!hold) {
        seat_notify_motion(seat, surface, time, xwayland_scale(x), xwayland_scale(y), first);
        return false;
    }

    struct xwayland_view *xwayland_view = data;
    double sx = x - xwayland_view->view.base.geometry.x;
    double sy = y - xwayland_view->view.base.geometry.y;
    seat_notify_motion(seat, surface, time, xwayland_scale(sx), xwayland_scale(sy), first);
    return true;
}

static void xwayland_view_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                                bool pressed, uint32_t time, enum click_state state, void *data)
{
    xwayland_update_seat(seat);

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
    return &xwayland_view->view.tree->node;
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
    wlr_xwayland_surface_configure(wlr_xwayland_surface, xwayland_scale(x), xwayland_scale(y),
                                   wlr_xwayland_surface->width, wlr_xwayland_surface->height);
    view_helper_move(view, x, y);
}

static void xwayland_restack_view(struct xwayland_view *xwayland_view)
{
    struct wlr_xwayland_surface *surface = xwayland_view->wlr_xwayland_surface;

    if (xwayland_view->view.base.kept_below) {
        wlr_xwayland_surface_restack(surface, NULL, XCB_STACK_MODE_BELOW);
        return;
    } else if (xwayland_view->view.base.kept_above) {
        wlr_xwayland_surface_restack(surface, NULL, XCB_STACK_MODE_ABOVE);
        xwayland_restack_unmanaged(xwayland_view->xwayland);
        return;
    }

    wlr_xwayland_surface_restack(surface, NULL, XCB_STACK_MODE_ABOVE);

    struct xwayland_view *view;
    wl_list_for_each(view, &xwayland_view->xwayland->surfaces, link) {
        surface = view->wlr_xwayland_surface;
        if (xwayland_view->view.base.kept_above) {
            wlr_xwayland_surface_restack(surface, NULL, XCB_STACK_MODE_ABOVE);
        }
    }

    xwayland_restack_unmanaged(xwayland_view->xwayland);
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
            xwayland_restack_view(xwayland_view);
        }
    }

    /* direct move when not changed size */
    if (view->pending.action & VIEW_ACTION_MOVE) {
        view->pending.action &= ~VIEW_ACTION_MOVE;
        if (!view_action_change_size(view->pending.configure_action)) {
            xwayland_view_move(xwayland_view, view->pending.geometry.x, view->pending.geometry.y);
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

    if (view->pending.action & VIEW_ACTION_FULLSCREEN) {
        wlr_xwayland_surface_set_fullscreen(wlr_xwayland_surface, kywc_view->fullscreen);
    }

    if (view->pending.action & VIEW_ACTION_MAXIMIZE) {
        wlr_xwayland_surface_set_maximized(wlr_xwayland_surface, kywc_view->maximized);
    }

    struct kywc_box *pending = &view->pending.geometry;
    /* there is no commit after map */
    if (view->base.has_initial_position) {
        view_helper_move(view, pending->x, pending->y);
    }

    wlr_xwayland_surface_configure(wlr_xwayland_surface, xwayland_scale(pending->x),
                                   xwayland_scale(pending->y), xwayland_scale(pending->width),
                                   xwayland_scale(pending->height));

    view_configure(&xwayland_view->view, 0);
}

static struct wlr_buffer *xwayland_view_get_wm_icon_buffer(struct view *view, float scale)
{
    struct xwayland_view *xwayland_view = xwayland_view_from_view(view);
    struct theme *theme = theme_manager_get_current();
    struct draw_info info = {
        .width = theme->icon_size,
        .height = theme->icon_size,
        .scale = scale,
        .svg = NULL,
        .png_path = NULL,
    };

    float scale_width = info.width * info.scale;
    float min_abs = FLT_MAX;
    float tmp_abs;
    struct net_wm_icon *icon_similar = NULL;
    struct net_wm_icon *icon;
    wl_list_for_each(icon, &xwayland_view->net_wm_icons, link) {
        tmp_abs = fabs(icon->width - scale_width);
        if (tmp_abs < min_abs) {
            min_abs = tmp_abs;
            icon_similar = icon;
        }
    }
    if (!icon_similar) {
        return NULL;
    }

    if (icon_similar->buffer) {
        return icon_similar->buffer;
    }

    info.pixel.width = icon_similar->width;
    info.pixel.height = icon_similar->height;
    info.pixel.data = icon_similar->data;
    icon_similar->buffer = painter_draw_buffer(&info);

    return icon_similar->buffer;
}

static const struct view_impl xwl_surface_impl = {
    .configure = xwayland_view_configure,
    .close = xwayland_view_close,
    .destroy = xwayland_view_destroy,
    .get_icon_buffer = xwayland_view_get_wm_icon_buffer,
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
                         size_hints->min_width < 0 ? 0 : xwayland_unscale(size_hints->min_width),
                         size_hints->min_height < 0 ? 0 : xwayland_unscale(size_hints->min_height),
                         size_hints->max_width < 0 ? 0 : xwayland_unscale(size_hints->max_width),
                         size_hints->max_height < 0 ? 0 : xwayland_unscale(size_hints->max_height));
    }
}

static void xwayland_view_handle_commit(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, commit);
    struct kywc_box geo = xwayland_view->view.base.geometry;
    struct kywc_box *current = &xwayland_view->view.base.geometry;
    uint32_t resize_edges = xwayland_view->view.current_resize_edges;

    xwayland_view_update_geometry(xwayland_view);

    enum view_action pending_action = xwayland_view->view.pending.configure_action;
    if (pending_action == VIEW_ACTION_NOP) {
        /* fix postion when resizing by left or top edges */
        int x = resize_edges & KYWC_EDGE_LEFT ? geo.x + geo.width - current->width : geo.x;
        int y = resize_edges & KYWC_EDGE_TOP ? geo.y + geo.height - current->height : geo.y;
        if (x != geo.x || y != geo.y) {
            xwayland_view_move(xwayland_view, x, y);
        }
        return;
    }

    assert(view_action_change_size(pending_action));

    struct kywc_box *pending = &xwayland_view->view.pending.configure_geometry;
    /* workaround: force check the size when maximize */
    if (pending_action == VIEW_ACTION_MAXIMIZE && current->width != pending->width &&
        current->height != pending->height) {
        return;
    }

    int x = pending->x, y = pending->y;
    if (pending_action & VIEW_ACTION_RESIZE) {
        if (resize_edges & KYWC_EDGE_LEFT) {
            x += pending->width - current->width;
        }
        if (resize_edges & KYWC_EDGE_TOP) {
            y += pending->height - current->height;
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

    if (input_model == WLR_ICCCM_INPUT_MODEL_NONE) {
        xwayland_view->view.base.focusable = false;
        xwayland_view->view.base.activatable = false;
    }
}

static void xwayland_view_handle_set_decorations(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, set_decorations);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    if (xwayland_surface_has_type(wlr_xwayland_surface, NET_WM_WINDOW_TYPE_DOCK) ||
        xwayland_surface_has_type(wlr_xwayland_surface, NET_WM_WINDOW_TYPE_SPLASH) ||
        xwayland_surface_has_type(wlr_xwayland_surface, KDE_NET_WM_WINDOW_TYPE_OVERRIDE)) {
        view_set_decoration(&xwayland_view->view, KYWC_SSD_NONE);
        return;
    }

    /* disable ssd if the window has clip region */
    if (xwayland_view->view.surface) {
        struct ky_scene_buffer *buffer =
            ky_scene_buffer_try_from_surface(xwayland_view->view.surface);
        if (pixman_region32_not_empty(&buffer->node.clip_region)) {
            view_set_decoration(&xwayland_view->view, KYWC_SSD_NONE);
            return;
        }
    }

    enum kywc_ssd ssd = KYWC_SSD_ALL;
    if (wlr_xwayland_surface->decorations & WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER) {
        ssd &= ~KYWC_SSD_BORDER;
    }
    if (wlr_xwayland_surface->decorations & WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE) {
        ssd &= ~KYWC_SSD_TITLE;
    }
    view_set_decoration(&xwayland_view->view, ssd);
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
        int scaled_left = xwayland_unscale(strut->left);
        geo.x += scaled_left;
        geo.width -= scaled_left;
    }
    if (strut->right_start_y != strut->right_end_y) {
        geo.width -= xwayland_unscale(strut->right);
    }
    if (strut->top_start_x != strut->top_end_x) {
        int scaled_top = xwayland_unscale(strut->top);
        geo.y += scaled_top;
        geo.height -= scaled_top;
    }
    if (strut->bottom_start_x != strut->bottom_end_x) {
        geo.height -= xwayland_unscale(strut->bottom);
    }

    /* intersect usable_area and geo */
    usable_area->x = geo.x > usable_area->x ? geo.x : usable_area->x;
    usable_area->y = geo.y > usable_area->y ? geo.y : usable_area->y;
    usable_area->width = geo.width < usable_area->width ? geo.width : usable_area->width;
    usable_area->height = geo.height < usable_area->height ? geo.height : usable_area->height;
}

// XXX: set enabled arg if we need update usable_area when minimize and unmap
static void xwayland_view_set_strut_partial(struct xwayland_view *xwayland_view, bool enabled)
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
            output_update_usable_area(xwayland_view->view.output);
            xwayland_view->view.base.movable = true;
        }
        return;
    }

    if (!had_area) {
        xwayland_view->output_update_usable_area.notify =
            xwayland_view_handle_output_update_usable_area;
        output_add_update_usable_area_listener(xwayland_view->view.output,
                                               &xwayland_view->output_update_usable_area, false);
        xwayland_view->view.base.movable = false;
    }

    output_update_usable_area(xwayland_view->view.output);
}

static void xwayland_view_handle_set_strut_partial(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, set_strut_partial);
    xwayland_view_set_strut_partial(xwayland_view, true);
}

static void xwayland_view_adjust_geometry(struct xwayland_view *xwayland_view, struct kywc_box *geo)
{
    struct kywc_view *kywc_view = &xwayland_view->view.base;
    struct kywc_output *kywc_output = kywc_output_at_point(geo->x, geo->y);

    if (kywc_view->fullscreen) {
        kywc_output_effective_geometry(kywc_output, geo);
        return;
    }

    if (kywc_view->maximized) {
        view_get_tiled_geometry(&xwayland_view->view, geo, kywc_output, KYWC_TILE_ALL);
    } else if (kywc_view->tiled) {
        view_get_tiled_geometry(&xwayland_view->view, geo, kywc_output, kywc_view->tiled);
    } else if (wl_list_empty(&xwayland_view->output_update_usable_area.link)) {
        struct output *output = output_from_kywc_output(kywc_output);
        if (kywc_view->has_initial_position) {
            int min_x = output->usable_area.x + kywc_view->margin.off_x;
            int min_y = output->usable_area.y + kywc_view->margin.off_y;
            geo->x = geo->x <= min_x ? min_x : geo->x;
            geo->y = geo->y <= min_y ? min_y : geo->y;
        } else {
            window_move_constraints(kywc_view, output, &geo->x, &geo->y);
        }
    }
}

static void xwayland_view_apply_type(struct xwayland_view *xwayland_view)
{
    struct wlr_xwayland_surface *surface = xwayland_view->wlr_xwayland_surface;
    struct view_layer *layer = NULL;
    bool removed_from_workspace = false;

    if (xwayland_surface_has_type(surface, NET_WM_WINDOW_TYPE_DESKTOP)) {
        layer = view_manager_get_layer(LAYER_DESKTOP, false);
        xwayland_view->view.base.resizable = false;
        xwayland_view->view.base.role = KYWC_VIEW_ROLE_DESKTOP;
        removed_from_workspace = true;
    } else if (xwayland_surface_has_type(surface, NET_WM_WINDOW_TYPE_DOCK)) {
        layer = view_manager_get_layer(LAYER_DOCK, false);
        xwayland_view->view.base.focusable = false;
        xwayland_view->view.base.activatable = false;
        xwayland_view->view.base.resizable = false;
        xwayland_view->view.base.role = KYWC_VIEW_ROLE_PANEL;
        removed_from_workspace = true;
    }

    if (removed_from_workspace) {
        view_unset_workspace(&xwayland_view->view, layer);
    }

    xwayland_view->view.base.has_round_corner =
        xwayland_surface_has_type(surface, NET_WM_WINDOW_TYPE_NORMAL) ||
        xwayland_surface_has_type(surface, NET_WM_WINDOW_TYPE_DIALOG);
}

void xwayland_view_set_above_or_below(struct wlr_xwayland_surface *surface, bool above_or_below,
                                      bool state, bool toggle)
{
    struct xwayland_view *xwayland_view = surface->data;
    if (!xwayland_view) {
        return;
    }

    bool new_state;
    if (above_or_below) {
        new_state = toggle ? !xwayland_view->view.base.kept_above : state;
        kywc_view_set_kept_above(&xwayland_view->view.base, new_state);
    } else {
        new_state = toggle ? !xwayland_view->view.base.kept_below : state;
        kywc_view_set_kept_below(&xwayland_view->view.base, new_state);
    }
}

void xwayland_view_set_skip_taskbar(struct wlr_xwayland_surface *surface, bool skip_taskbar)
{
    struct xwayland_view *xwayland_view = surface->data;
    if (!xwayland_view) {
        return;
    }

    if (xwayland_view->view.base.skip_taskbar != skip_taskbar) {
        xwayland_view->view.base.skip_taskbar = skip_taskbar;
        wl_signal_emit_mutable(&xwayland_view->view.base.events.capabilities, NULL);
    }
}

void xwayland_view_set_skip_switcher(struct wlr_xwayland_surface *surface, bool skip_switcher)
{
    struct xwayland_view *xwayland_view = surface->data;
    if (!xwayland_view) {
        return;
    }

    if (xwayland_view->view.base.skip_switcher != skip_switcher) {
        xwayland_view->view.base.skip_switcher = skip_switcher;
        wl_signal_emit_mutable(&xwayland_view->view.base.events.capabilities, NULL);
    }
}

static void xwayland_view_fixup_position(struct xwayland_view *xwayland_view)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;
    struct kywc_box geo = { 0 };

    if (wlr_xwayland_surface->x != 0 || wlr_xwayland_surface->y != 0) {
        geo.x = xwayland_unscale(wlr_xwayland_surface->x);
        geo.y = xwayland_unscale(wlr_xwayland_surface->y);
        xwayland_view->view.base.has_initial_position = true;
    } else {
        /* apply the position in size_hints */
        xcb_size_hints_t *size_hints = wlr_xwayland_surface->size_hints;
        if (size_hints && size_hints->flags &
                              (XCB_ICCCM_SIZE_HINT_US_POSITION | XCB_ICCCM_SIZE_HINT_P_POSITION)) {
            geo.x = xwayland_unscale(size_hints->x);
            geo.y = xwayland_unscale(size_hints->y);
            xwayland_view->view.base.has_initial_position = true;
        }
    }
    if (xwayland_view->view.base.has_initial_position) {
        xwayland_view_adjust_geometry(xwayland_view, &geo);
        kywc_view_move(&xwayland_view->view.base, geo.x, geo.y);
    }
}

static void xwayland_view_fixup_parent(struct xwayland_view *xwayland_view)
{
    struct workspace *workspace = workspace_manager_get_current();
    struct view_proxy *view_proxy;
    wl_list_for_each(view_proxy, &workspace->view_proxies, workspace_link) {
        /* skip views not mapped */
        if (!view_proxy->view->base.mapped) {
            continue;
        }
        if (view_proxy->view->pid == xwayland_view->view.pid) {
            view_set_parent(&xwayland_view->view, view_proxy->view);
        }
        break;
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
    xwayland_view_handle_request_maximize(&xwayland_view->request_maximize, NULL);
    xwayland_view_handle_request_fullscreen(&xwayland_view->request_fullscreen, NULL);
    xwayland_view_handle_set_hints(&xwayland_view->set_hints, NULL);

    assert(wlr_xwayland_surface->surface == xwayland_view->view.surface);
    xwayland_view->commit.notify = xwayland_view_handle_commit;
    wl_signal_add(&wlr_xwayland_surface->surface->events.commit, &xwayland_view->commit);
    xwayland_view->set_strut_partial.notify = xwayland_view_handle_set_strut_partial;
    wl_signal_add(&wlr_xwayland_surface->events.set_strut_partial,
                  &xwayland_view->set_strut_partial);

    xwayland_view->request_move.notify = xwayland_view_handle_request_move;
    wl_signal_add(&wlr_xwayland_surface->events.request_move, &xwayland_view->request_move);
    xwayland_view->request_resize.notify = xwayland_view_handle_request_resize;
    wl_signal_add(&wlr_xwayland_surface->events.request_resize, &xwayland_view->request_resize);

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

    xwayland_view_apply_type(xwayland_view);

    /* we should stack above the new window always */
    if (!view_is_activatable(&xwayland_view->view)) {
        xwayland_restack_view(xwayland_view);
    }

    /* fix postion if not special state */
    if (!xwayland_view->view.base.maximized && !xwayland_view->view.base.fullscreen &&
        !xwayland_view->view.base.tiled) {
        xwayland_view_fixup_position(xwayland_view);
    }

    xwayland_view->view.pid = wlr_xwayland_surface->pid;
    xwayland_view->view.base.modal = wlr_xwayland_surface->parent && wlr_xwayland_surface->modal;

    if (xwayland_view->view.base.modal && !wlr_xwayland_surface->parent->surface) {
        xwayland_view_fixup_parent(xwayland_view);
    }

    view_map(&xwayland_view->view);

    xwayland_view_set_strut_partial(xwayland_view, true);
}

static void xwayland_view_handle_unmap(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, unmap);

    wl_list_remove(&xwayland_view->commit.link);
    wl_list_remove(&xwayland_view->set_strut_partial.link);
    wl_list_remove(&xwayland_view->request_move.link);
    wl_list_remove(&xwayland_view->request_resize.link);
    wl_list_remove(&xwayland_view->request_maximize.link);
    wl_list_remove(&xwayland_view->request_fullscreen.link);
    wl_list_remove(&xwayland_view->request_activate.link);
    wl_list_remove(&xwayland_view->set_title.link);
    wl_list_remove(&xwayland_view->set_class.link);
    wl_list_remove(&xwayland_view->set_parent.link);
    wl_list_remove(&xwayland_view->set_hints.link);
    wl_list_remove(&xwayland_view->set_decorations.link);

    /* surface_tree is destroyed by scene subsurface */
    view_unmap(&xwayland_view->view);
}

static void xwayland_view_handle_surface_tree_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, surface_tree_destroy);
    wl_list_remove(&xwayland_view->surface_tree_destroy.link);
    xwayland_view->view.surface_tree = NULL;
}

static void xwayland_view_handle_precommit(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, precommit);
    if (xwayland_view->xwayland->scale == 1.0) {
        return;
    }

    struct wlr_surface_state *pending = data;
    pending->width = xwayland_unscale(pending->width);
    pending->height = xwayland_unscale(pending->height);

    float scale = 1.0 / xwayland_view->xwayland->scale;
    if (pending->committed & WLR_SURFACE_STATE_SURFACE_DAMAGE) {
        wlr_region_scale(&pending->surface_damage, &pending->surface_damage, scale);
    }
    if (pending->committed & WLR_SURFACE_STATE_OPAQUE_REGION) {
        wlr_region_scale(&pending->opaque, &pending->opaque, scale);
    }
    if (pending->committed & WLR_SURFACE_STATE_INPUT_REGION) {
        wlr_region_scale(&pending->input, &pending->input, scale);
    }
    if (pending->committed & WLR_SURFACE_STATE_OFFSET) {
        pending->dx = xwayland_unscale(pending->dx);
        pending->dy = xwayland_unscale(pending->dy);
    }
}

static void xwayland_view_handle_associate(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, associate);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;

    xwayland_view->view.surface = wlr_xwayland_surface->surface;
    wlr_xwayland_surface->surface->data = xwayland_view;

    xwayland_view->unmap.notify = xwayland_view_handle_unmap;
    wl_signal_add(&wlr_xwayland_surface->surface->events.unmap, &xwayland_view->unmap);

    /* create scene tree here as we get surface here */
    xwayland_view->view.surface_tree =
        ky_scene_subsurface_tree_create(xwayland_view->view.tree, wlr_xwayland_surface->surface);
    /* event node will be destroyed when surface_node destroy */
    input_event_node_create(&xwayland_view->view.surface_tree->node, &xwayland_view_event_node_impl,
                            xwayland_view_get_root, xwayland_view_get_toplevel, xwayland_view);

    xwayland_surface_shape_select_input(wlr_xwayland_surface, true);
    xwayland_surface_apply_shape_region(wlr_xwayland_surface);

    // read all surface properties
    xwayland_read_wm_state(wlr_xwayland_surface->window_id);
    xwayland_read_wm_icon(wlr_xwayland_surface->window_id);
    xwayland_read_wm_window_opacity(wlr_xwayland_surface->window_id);

    xwayland_view->precommit.notify = xwayland_view_handle_precommit;
    wl_signal_add(&wlr_xwayland_surface->surface->events.precommit, &xwayland_view->precommit);
    xwayland_view->map.notify = xwayland_view_handle_map;
    wl_signal_add(&wlr_xwayland_surface->surface->events.map, &xwayland_view->map);
    xwayland_view->surface_tree_destroy.notify = xwayland_view_handle_surface_tree_destroy;
    wl_signal_add(&xwayland_view->view.surface_tree->node.events.destroy,
                  &xwayland_view->surface_tree_destroy);
}

static void xwayland_view_handle_dissociate(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, dissociate);

    xwayland_view->wlr_xwayland_surface->surface->data = NULL;
    xwayland_view->view.surface = NULL;
    if (xwayland_view->view.surface_tree) {
        ky_scene_node_destroy(&xwayland_view->view.surface_tree->node);
    }

    wl_list_remove(&xwayland_view->precommit.link);
    wl_list_remove(&xwayland_view->map.link);
    wl_list_remove(&xwayland_view->unmap.link);
}

static void xwayland_view_icon_destroy(struct net_wm_icon *icon)
{
    wl_list_remove(&icon->link);
    wlr_buffer_drop(icon->buffer);
    free(icon->data);
    free(icon);
}

static void xwayland_view_handle_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view = wl_container_of(listener, xwayland_view, destroy);

    wl_list_remove(&xwayland_view->link);
    wl_list_remove(&xwayland_view->destroy.link);
    wl_list_remove(&xwayland_view->associate.link);
    wl_list_remove(&xwayland_view->dissociate.link);
    wl_list_remove(&xwayland_view->request_configure.link);
    wl_list_remove(&xwayland_view->request_minimize.link);
    wl_list_remove(&xwayland_view->set_override_redirect.link);
    wl_list_remove(&xwayland_view->output_update_usable_area.link);

    struct net_wm_icon *icon, *tmp;
    wl_list_for_each_safe(icon, tmp, &xwayland_view->net_wm_icons, link) {
        xwayland_view_icon_destroy(icon);
    }

    view_destroy(&xwayland_view->view);
}

static void xwayland_view_handle_set_override_redirect(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, set_override_redirect);
    struct wlr_xwayland_surface *wlr_xwayland_surface = xwayland_view->wlr_xwayland_surface;
    struct xwayland_server *xwayland = xwayland_view->xwayland;

    if (wlr_xwayland_surface->surface && wlr_xwayland_surface->surface->mapped) {
        xwayland_view_handle_unmap(&xwayland_view->unmap, NULL);
        xwayland_view_handle_dissociate(&xwayland_view->dissociate, NULL);
    }
    xwayland_view_handle_destroy(&xwayland_view->destroy, NULL);
    wlr_xwayland_surface->data = NULL;

    xwayland_unmanaged_create(xwayland, wlr_xwayland_surface);
}

static void xwayland_view_handle_request_configure(struct wl_listener *listener, void *data)
{
    struct xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_configure);
    struct kywc_view *kywc_view = &xwayland_view->view.base;

    /* skip configure when resizing by left or top edges */
    if (kywc_view->mapped &&
        xwayland_view->view.current_resize_edges & (KYWC_EDGE_LEFT | KYWC_EDGE_TOP)) {
        return;
    }

    struct wlr_xwayland_surface_configure_event *event = data;
    struct kywc_box geo = { xwayland_unscale(event->x), xwayland_unscale(event->y),
                            xwayland_unscale(event->width), xwayland_unscale(event->height) };

    if (!kywc_view->mapped) {
        wlr_xwayland_surface_configure(xwayland_view->wlr_xwayland_surface, event->x, event->y,
                                       event->width, event->height);
    } else {
        xwayland_view_adjust_geometry(xwayland_view, &geo);
    }

    kywc_view_resize(kywc_view, &geo);
}

void xwayland_view_create(struct xwayland_server *xwayland,
                          struct wlr_xwayland_surface *wlr_xwayland_surface)
{
    struct xwayland_view *xwayland_view = calloc(1, sizeof(struct xwayland_view));
    if (!xwayland_view) {
        return;
    }

    xwayland_view->xwayland = xwayland;
    wl_list_insert(&xwayland->surfaces, &xwayland_view->link);
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
    xwayland_view->request_minimize.notify = xwayland_view_handle_request_minimize;
    wl_signal_add(&wlr_xwayland_surface->events.request_minimize, &xwayland_view->request_minimize);
    xwayland_view->set_override_redirect.notify = xwayland_view_handle_set_override_redirect;
    wl_signal_add(&wlr_xwayland_surface->events.set_override_redirect,
                  &xwayland_view->set_override_redirect);

    wl_list_init(&xwayland_view->precommit.link);
    wl_list_init(&xwayland_view->map.link);
    wl_list_init(&xwayland_view->unmap.link);
    wl_list_init(&xwayland_view->net_wm_icons);

    if (wlr_xwayland_surface->surface && wlr_xwayland_surface->surface->mapped) {
        xwayland_view_handle_associate(&xwayland_view->associate, NULL);
        xwayland_view_handle_map(&xwayland_view->map, NULL);
    }
}

struct wlr_xwayland_surface *xwayland_view_look_surface(struct xwayland_server *xwayland,
                                                        xcb_window_t window_id)
{
    struct xwayland_view *xwayland_view;
    wl_list_for_each(xwayland_view, &xwayland->surfaces, link) {
        if (xwayland_view->wlr_xwayland_surface->window_id == window_id) {
            return xwayland_view->wlr_xwayland_surface;
        }
    }
    return NULL;
}

void xwayland_view_add_new_wm_icon(struct wlr_xwayland_surface *surface, uint32_t width,
                                   uint32_t height, uint32_t size, uint32_t *data)
{
    struct xwayland_view *xwayland_view = surface->data;
    if (!xwayland_view) {
        return;
    }

    struct net_wm_icon *old_icon, *tmp;
    wl_list_for_each_safe(old_icon, tmp, &xwayland_view->net_wm_icons, link) {
        if (old_icon->width == width && old_icon->height == height) {
            xwayland_view_icon_destroy(old_icon);
        }
    }

    struct net_wm_icon *icon = calloc(1, sizeof(struct net_wm_icon));
    if (!icon) {
        return;
    }
    wl_list_insert(&xwayland_view->net_wm_icons, &icon->link);
    icon->width = width;
    icon->height = height;
    icon->data = (unsigned char *)malloc(size);
    memcpy(icon->data, (unsigned char *)data, size);
}

bool xwayland_view_set_opacity(struct xwayland_server *xwayland, xcb_window_t window_id,
                               float opacity)
{
    struct wlr_xwayland_surface *surface = xwayland_view_look_surface(xwayland, window_id);
    if (!surface) {
        return false;
    }

    struct xwayland_view *xwayland_view = surface->data;
    if (xwayland_view->view.surface) {
        struct ky_scene_buffer *buffer =
            ky_scene_buffer_try_from_surface(xwayland_view->view.surface);
        ky_scene_buffer_set_opacity(buffer, opacity);
    }

    return true;
}

bool xwayland_view_set_shape_region(struct xwayland_server *xwayland, xcb_window_t window_id,
                                    xcb_shape_sk_t kind, const pixman_region32_t *region)
{
    struct wlr_xwayland_surface *surface = xwayland_view_look_surface(xwayland, window_id);
    if (!surface) {
        return false;
    }
    struct xwayland_view *xwayland_view = surface->data;
    if (!xwayland_view->view.surface) {
        return true;
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(xwayland_view->view.surface);
    if (kind == XCB_SHAPE_SK_BOUNDING || kind == XCB_SHAPE_SK_CLIP) {
        ky_scene_node_set_clip_region(&buffer->node, region);
    }
    if (kind == XCB_SHAPE_SK_BOUNDING || kind == XCB_SHAPE_SK_INPUT) {
        ky_scene_node_set_input_region(&buffer->node, region);
        /* empty input region means no input support */
        bool need_bypassed = kind == XCB_SHAPE_SK_INPUT && !pixman_region32_not_empty(region);
        ky_scene_node_set_input_bypassed(&buffer->node, need_bypassed);
    }

    return true;
}
