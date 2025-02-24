// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <linux/input-event-codes.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>

#include "input/cursor.h"
#include "output.h"
#include "scene/surface.h"
#include "view/view.h"
#include "xwayland_p.h"

static void handle_cursor_motion(struct wl_listener *listener, void *data) {}

static void handle_cursor_button(struct wl_listener *listener, void *data)
{
    struct xwayland_drag_x11 *drag_x11 = wl_container_of(listener, drag_x11, cursor_button);
    // default left button drag, TODO: other button drag
    struct wlr_pointer_button_event *event = data;
    if (event->button != BTN_LEFT || event->state != WLR_BUTTON_RELEASED) {
        return;
    }

    struct xwayland_server *xwyaland = drag_x11->xwayland;
    xwayland_end_drag_x11(xwyaland);
}

static void handle_drag_x11_surface_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_drag_x11 *drag_x11 = wl_container_of(listener, drag_x11, surface_destroy);

    wl_list_remove(&drag_x11->surface_destroy.link);
    wl_list_init(&drag_x11->surface_destroy.link);
    drag_x11->hovered_surface = NULL;
}

bool xwayland_is_dragging_x11(struct xwayland_server *xwayland)
{
    return xwayland->drag_x11;
}

bool drag_x11_has_data_source(struct xwayland_drag_x11 *drag_x11)
{
    return drag_x11->data_source;
}

void xwayland_end_drag_x11(struct xwayland_server *xwayland)
{
    if (!xwayland_is_dragging_x11(xwayland)) {
        return;
    }

    kywc_log(KYWC_DEBUG, "end drag X11 ");
    wl_list_remove(&xwayland->drag_x11->cursor_motion.link);
    wl_list_remove(&xwayland->drag_x11->cursor_button.link);
    wl_list_remove(&xwayland->drag_x11->surface_destroy.link);

    free(xwayland->drag_x11);
    xwayland->drag_x11 = NULL;
}

bool xwayland_start_drag_x11(struct xwayland_server *xwayland, xcb_window_t source_window)
{
    if (xwayland_is_dragging_x11(xwayland)) {
        if (xwayland->drag_x11->source_window != source_window) {
            kywc_log(KYWC_WARN, "dnd change owner?");
        }
        return false;
    }

    struct seat *seat = seat_from_wlr_seat(xwayland->wlr_xwayland->seat);
    struct wlr_surface *current_surface = wlr_surface_try_from_node(seat->cursor->hover.node);
    struct view *current_view = current_surface ? view_try_from_wlr_surface(current_surface) : NULL;
    if (!current_view) {
        return false;
    }
    /** in wlroots, the window is xwm.dnd_selection.window, be created for wayland to x11
     * TODO: use window id to compare
     */
    if (current_view && !xwayland_check_view(current_view)) {
        return false;
    }

    kywc_log(KYWC_DEBUG, "start drag X11");
    struct xwayland_drag_x11 *drag_x11 = calloc(1, sizeof(struct xwayland_drag_x11));
    if (!drag_x11) {
        return false;
    }

    drag_x11->source_window = source_window;
    drag_x11->hovered_surface = current_surface;
    wl_signal_add(&drag_x11->hovered_surface->events.destroy, &drag_x11->surface_destroy);
    drag_x11->surface_destroy.notify = handle_drag_x11_surface_destroy;

    drag_x11->cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&seat->events.cursor_motion, &drag_x11->cursor_motion);
    drag_x11->cursor_button.notify = handle_cursor_button;
    wl_signal_add(&seat->cursor->wlr_cursor->events.button, &drag_x11->cursor_button);

    drag_x11->xwayland = xwayland;
    xwayland->drag_x11 = drag_x11;

    xwayland_map_selection_window(xwayland, xwayland->window_catcher, true);
    return true;
}
