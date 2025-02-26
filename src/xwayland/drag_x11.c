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

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
    struct xwayland_drag_x11 *drag_x11 = wl_container_of(listener, drag_x11, cursor_motion);
    struct seat_cursor_motion_event *event = data;
    struct xwayland_server *xwayland = drag_x11->xwayland;
    struct seat *seat = seat_from_wlr_seat(drag_x11->xwayland->wlr_xwayland->seat);
    /* send wayland motion */
    if (drag_x11->hovered_surface != NULL && drag_x11->hovered_client != NULL) {
        struct wl_resource *resource;
        wl_resource_for_each(resource, &drag_x11->hovered_client->data_devices) {
            wl_data_device_send_motion(resource, event->time_msec,
                                       wl_fixed_from_double(seat->cursor->sx),
                                       wl_fixed_from_double(seat->cursor->sy));
        }
    } else if (!drag_x11->hovered_surface) {
        // send no action to source window when hover NULL
        xwayland_send_dnd_status(xwayland, xwayland->window_catcher, drag_x11->source_window, 0);
    }

    struct wlr_surface *old_surface = drag_x11->hovered_surface;
    struct wlr_surface *new_surface =
        seat->cursor->hover.node ? wlr_surface_try_from_node(seat->cursor->hover.node) : NULL;
    struct view *old_view = old_surface ? view_try_from_wlr_surface(old_surface) : NULL;
    struct view *new_view = new_surface ? view_try_from_wlr_surface(new_surface) : NULL;
    if (old_surface == new_surface) {
        return;
    }

    if (old_view && !xwayland_check_view(old_view)) {
        kywc_log(KYWC_DEBUG, "leave wayland surface");
        xwayland_map_selection_window(xwayland, xwayland->window_catcher, NULL, false);
    }
    if (new_view && !xwayland_check_view(new_view)) {
        kywc_log(KYWC_DEBUG, "enter wayalnd surface");
        int width, height;
        output_layout_get_size(&width, &height);
        struct kywc_box box = { .x = 0, .y = 0, .width = width, .height = height };
        xwayland_map_selection_window(xwayland, xwayland->window_catcher, &box, true);
    }
    drag_set_focus(drag_x11, new_surface, seat->cursor->sx, seat->cursor->sy);
}

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
    drag_set_focus(drag_x11, NULL, 0, 0);
}

static void drag_handle_seat_client_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_drag_x11 *drag_x11 = wl_container_of(listener, drag_x11, seat_client_destroy);

    drag_x11->hovered_client = NULL;
    wl_list_remove(&drag_x11->seat_client_destroy.link);
}

bool xwayland_is_dragging_x11(struct xwayland_server *xwayland)
{
    return xwayland->drag_x11;
}

bool drag_x11_has_data_source(struct xwayland_drag_x11 *drag_x11)
{
    return drag_x11->data_source;
}

void drag_set_focus(struct xwayland_drag_x11 *drag, struct wlr_surface *surface, double sx,
                    double sy)
{
    if (drag->hovered_surface == surface) {
        return;
    }

    if (drag->hovered_client) {
        wl_list_remove(&drag->seat_client_destroy.link);
        wl_list_init(&drag->seat_client_destroy.link);

        // If we're switching focus to another client, we want to destroy all
        // offers without destroying the source. If the drag operation ends, we
        // want to keep the offer around for the data transfer.
        struct wlr_data_offer *offer, *tmp;
        wl_list_for_each_safe(offer, tmp, &drag->hovered_client->seat->drag_offers, link) {
            struct wl_client *client = wl_resource_get_client(offer->resource);
            if (offer->source == &drag->data_source->base &&
                client == drag->hovered_client->client) {
                offer->source = NULL;
                data_offer_destroy(offer);
            }
        }

        struct wl_resource *resource;
        wl_resource_for_each(resource, &drag->hovered_client->data_devices) {
            wl_data_device_send_leave(resource);
        }

        drag->hovered_client = NULL;
    }

    wl_list_remove(&drag->surface_destroy.link);
    wl_list_init(&drag->surface_destroy.link);
    drag->hovered_surface = NULL;

    if (!surface) {
        return;
    }

    struct wlr_seat *seat = drag->xwayland->wlr_xwayland->seat;
    struct wlr_seat_client *focus_client =
        wlr_seat_client_for_wl_client(seat, wl_resource_get_client(surface->resource));
    if (!focus_client) {
        return;
    }

    if (drag->data_source != NULL) {
        drag->data_source->base.accepted = false;

        uint32_t serial = wl_display_next_serial(seat->display);

        struct wl_resource *device_resource;
        wl_resource_for_each(device_resource, &focus_client->data_devices) {
            struct wlr_data_offer *offer =
                data_offer_create(device_resource, &drag->data_source->base, WLR_DATA_OFFER_DRAG);
            if (offer == NULL) {
                wl_resource_post_no_memory(device_resource);
                return;
            }

            data_offer_update_action(offer);

            if (wl_resource_get_version(offer->resource) >=
                WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
                wl_data_offer_send_source_actions(offer->resource, drag->data_source->base.actions);
            }

            wl_data_device_send_enter(device_resource, serial, surface->resource,
                                      wl_fixed_from_double(sx), wl_fixed_from_double(sy),
                                      offer->resource);
        }
    }

    drag->hovered_surface = surface;
    drag->hovered_client = focus_client;
    wl_signal_add(&surface->events.destroy, &drag->surface_destroy);
    drag->seat_client_destroy.notify = drag_handle_seat_client_destroy;
    wl_signal_add(&focus_client->events.destroy, &drag->seat_client_destroy);
}

void xwayland_end_drag_x11(struct xwayland_server *xwayland)
{
    if (!xwayland_is_dragging_x11(xwayland)) {
        return;
    }

    kywc_log(KYWC_DEBUG, "end drag X11 ");
    struct xwayland_drag_x11 *drag_x11 = xwayland->drag_x11;
    wlr_data_source_destroy(&drag_x11->data_source->base);
    wl_array_release(&drag_x11->data_source->mime_types_atoms);

    wl_list_remove(&drag_x11->cursor_motion.link);
    wl_list_remove(&drag_x11->cursor_button.link);
    wl_list_remove(&drag_x11->surface_destroy.link);
    wl_list_remove(&drag_x11->seat_client_destroy.link);

    struct xwayland_data_transfer *transfer;
    wl_list_for_each(transfer, &drag_x11->transfers, link) {
        xwayland_data_transfer_destroy(transfer);
    }

    free(drag_x11);
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
    wl_list_init(&drag_x11->seat_client_destroy.link);
    wl_signal_add(&drag_x11->hovered_surface->events.destroy, &drag_x11->surface_destroy);
    drag_x11->surface_destroy.notify = handle_drag_x11_surface_destroy;

    drag_x11->cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&seat->events.cursor_motion, &drag_x11->cursor_motion);
    drag_x11->cursor_button.notify = handle_cursor_button;
    wl_signal_add(&seat->cursor->wlr_cursor->events.button, &drag_x11->cursor_button);

    wl_list_init(&drag_x11->transfers);
    drag_x11->xwayland = xwayland;
    xwayland->drag_x11 = drag_x11;

    int width, height;
    output_layout_get_size(&width, &height);
    struct kywc_box box = { .x = 0, .y = 0, .width = width, .height = height };
    xwayland_map_selection_window(xwayland, xwayland->window_catcher, &box, true);
    return true;
}
