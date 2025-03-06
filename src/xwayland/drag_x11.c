// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _XOPEN_SOURCE 700
#include <assert.h>
#include <strings.h>
#include <unistd.h>

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

    /* some x11 source app do not send drop, so we end drag early if !accpepted */
    struct view *hover_view =
        drag_x11->hovered_surface ? view_try_from_wlr_surface(drag_x11->hovered_surface) : NULL;
    if (hover_view && !xwayland_check_view(hover_view) && drag_x11->data_source->base.accepted &&
        drag_x11->data_source->base.current_dnd_action) {
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

#define DATA_DEVICE_ALL_ACTIONS                                                                    \
    (WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |             \
     WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)

static const struct wl_data_offer_interface data_offer_impl;

static struct wlr_data_offer *data_offer_from_resource(struct wl_resource *resource)
{
    assert(wl_resource_instance_of(resource, &wl_data_offer_interface, &data_offer_impl));
    return wl_resource_get_user_data(resource);
}

static void data_offer_source_dnd_finish(struct wlr_data_offer *offer);

static void data_offer_destroy(struct wlr_data_offer *offer)
{
    if (offer == NULL) {
        return;
    }

    wl_list_remove(&offer->source_destroy.link);
    wl_list_remove(&offer->link);

    if (offer->type == WLR_DATA_OFFER_DRAG && offer->source) {
        // If the drag destination has version < 3, wl_data_offer.finish
        // won't be called, so do this here as a safety net, because
        // we still want the version >= 3 drag source to be happy.
        if (wl_resource_get_version(offer->resource) < WL_DATA_OFFER_ACTION_SINCE_VERSION) {
            data_offer_source_dnd_finish(offer);
        } else if (offer->source->impl->dnd_finish) {
            wlr_data_source_destroy(offer->source);
        }
    }

    // Make the resource inert
    wl_resource_set_user_data(offer->resource, NULL);

    free(offer);
}

static uint32_t data_offer_choose_action(struct wlr_data_offer *offer)
{
    uint32_t offer_actions, preferred_action = 0;
    if (wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_ACTION_SINCE_VERSION) {
        offer_actions = offer->actions;
        preferred_action = offer->preferred_action;
    } else {
        offer_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
    }

    uint32_t source_actions;
    if (offer->source->actions >= 0) {
        source_actions = offer->source->actions;
    } else {
        source_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
    }

    uint32_t available_actions = offer_actions & source_actions;
    if (!available_actions) {
        return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    }

    if (offer->source->compositor_action & available_actions) {
        return offer->source->compositor_action;
    }

    // If the dest side has a preferred DnD action, use it
    if ((preferred_action & available_actions) != 0) {
        return preferred_action;
    }

    // Use the first found action, in bit order
    return 1 << (ffs(available_actions) - 1);
}

static void data_offer_update_action(struct wlr_data_offer *offer)
{
    assert(offer->type == WLR_DATA_OFFER_DRAG);

    uint32_t action = data_offer_choose_action(offer);
    if (offer->source->current_dnd_action == action) {
        return;
    }
    offer->source->current_dnd_action = action;

    if (offer->in_ask) {
        return;
    }

    wlr_data_source_dnd_action(offer->source, action);

    if (wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_ACTION_SINCE_VERSION) {
        wl_data_offer_send_action(offer->resource, action);
    }
}

static void data_offer_handle_accept(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t serial, const char *mime_type)
{
    struct wlr_data_offer *offer = data_offer_from_resource(resource);
    if (offer == NULL) {
        return;
    }

    if (offer->type != WLR_DATA_OFFER_DRAG) {
        kywc_log(KYWC_DEBUG, "Ignoring wl_data_offer.accept request on a "
                             "non-drag-and-drop offer");
        return;
    }

    wlr_data_source_accept(offer->source, serial, mime_type);
}

static void data_offer_handle_receive(struct wl_client *client, struct wl_resource *resource,
                                      const char *mime_type, int32_t fd)
{
    struct wlr_data_offer *offer = data_offer_from_resource(resource);
    if (offer == NULL) {
        close(fd);
        return;
    }

    wlr_data_source_send(offer->source, mime_type, fd);
}

static void data_offer_source_dnd_finish(struct wlr_data_offer *offer)
{
    struct wlr_data_source *source = offer->source;
    if (source->actions < 0) {
        return;
    }

    if (offer->in_ask) {
        wlr_data_source_dnd_action(source, source->current_dnd_action);
    }

    wlr_data_source_dnd_finish(source);
}

static void data_offer_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void data_offer_handle_finish(struct wl_client *client, struct wl_resource *resource)
{
    struct wlr_data_offer *offer = data_offer_from_resource(resource);
    if (offer == NULL) {
        return;
    }

    // TODO: also fail while we have a drag-and-drop grab
    if (offer->type != WLR_DATA_OFFER_DRAG) {
        wl_resource_post_error(offer->resource, WL_DATA_OFFER_ERROR_INVALID_FINISH,
                               "Offer is not drag-and-drop");
        return;
    }
    if (!offer->source->accepted) {
        wl_resource_post_error(offer->resource, WL_DATA_OFFER_ERROR_INVALID_FINISH,
                               "Premature finish request");
        return;
    }
    enum wl_data_device_manager_dnd_action action = offer->source->current_dnd_action;
    if (action == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE ||
        action == WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) {
        wl_resource_post_error(offer->resource, WL_DATA_OFFER_ERROR_INVALID_FINISH,
                               "Offer finished with an invalid action");
        return;
    }

    data_offer_source_dnd_finish(offer);
    data_offer_destroy(offer);
}

static void data_offer_handle_set_actions(struct wl_client *client, struct wl_resource *resource,
                                          uint32_t actions, uint32_t preferred_action)
{
    struct wlr_data_offer *offer = data_offer_from_resource(resource);
    if (offer == NULL) {
        return;
    }

    if (actions & ~DATA_DEVICE_ALL_ACTIONS) {
        wl_resource_post_error(offer->resource, WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
                               "invalid action mask %x", actions);
        return;
    }

    if (preferred_action &&
        (!(preferred_action & actions) || __builtin_popcount(preferred_action) > 1)) {
        wl_resource_post_error(offer->resource, WL_DATA_OFFER_ERROR_INVALID_ACTION,
                               "invalid action %x", preferred_action);
        return;
    }

    if (offer->type != WLR_DATA_OFFER_DRAG) {
        wl_resource_post_error(offer->resource, WL_DATA_OFFER_ERROR_INVALID_OFFER,
                               "set_action can only be sent to drag-and-drop offers");
        return;
    }

    offer->actions = actions;
    offer->preferred_action = preferred_action;

    data_offer_update_action(offer);
}

static const struct wl_data_offer_interface data_offer_impl = {
    .accept = data_offer_handle_accept,
    .receive = data_offer_handle_receive,
    .destroy = data_offer_handle_destroy,
    .finish = data_offer_handle_finish,
    .set_actions = data_offer_handle_set_actions,
};

static void data_offer_handle_resource_destroy(struct wl_resource *resource)
{
    struct wlr_data_offer *offer = data_offer_from_resource(resource);
    data_offer_destroy(offer);
}

static void data_offer_handle_source_destroy(struct wl_listener *listener, void *data)
{
    struct wlr_data_offer *offer = wl_container_of(listener, offer, source_destroy);
    // Prevent data_offer_destroy from destroying the source again
    offer->source = NULL;
    data_offer_destroy(offer);
}

static struct wlr_data_offer *data_offer_create(struct wlr_seat *wlr_seat,
                                                struct wl_resource *device_resource,
                                                struct wlr_data_source *source,
                                                enum wlr_data_offer_type type)
{
    assert(wlr_seat != NULL);
    assert(source != NULL); // a NULL source means no selection

    struct wlr_data_offer *offer = calloc(1, sizeof(*offer));
    if (offer == NULL) {
        return NULL;
    }
    offer->source = source;
    offer->type = type;

    struct wl_client *client = wl_resource_get_client(device_resource);
    uint32_t version = wl_resource_get_version(device_resource);
    offer->resource = wl_resource_create(client, &wl_data_offer_interface, version, 0);
    if (offer->resource == NULL) {
        free(offer);
        return NULL;
    }
    wl_resource_set_implementation(offer->resource, &data_offer_impl, offer,
                                   data_offer_handle_resource_destroy);

    switch (type) {
    case WLR_DATA_OFFER_SELECTION:
        wl_list_insert(&wlr_seat->selection_offers, &offer->link);
        break;
    case WLR_DATA_OFFER_DRAG:
        wl_list_insert(&wlr_seat->drag_offers, &offer->link);
        break;
    }

    offer->source_destroy.notify = data_offer_handle_source_destroy;
    wl_signal_add(&source->events.destroy, &offer->source_destroy);

    wl_data_device_send_data_offer(device_resource, offer->resource);

    char **p;
    wl_array_for_each(p, &source->mime_types) {
        wl_data_offer_send_offer(offer->resource, *p);
    }

    return offer;
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
            struct wlr_data_offer *offer = data_offer_create(
                seat, device_resource, &drag->data_source->base, WLR_DATA_OFFER_DRAG);
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
    if (drag_x11->data_source) {
        // This will end the grab_x11
        wlr_data_source_destroy(&drag_x11->data_source->base);
        return;
    }

    wl_list_remove(&drag_x11->cursor_motion.link);
    wl_list_remove(&drag_x11->cursor_button.link);
    wl_list_remove(&drag_x11->surface_destroy.link);
    wl_list_remove(&drag_x11->seat_client_destroy.link);

    struct xwayland_data_transfer *transfer;
    wl_list_for_each(transfer, &drag_x11->transfers, link) {
        xwayland_data_transfer_destroy(transfer);
    }

    xwayland_map_selection_window(xwayland, xwayland->window_catcher, NULL, false);

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
