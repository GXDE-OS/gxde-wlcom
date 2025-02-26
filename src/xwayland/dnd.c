// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <fcntl.h>
#include <unistd.h>

#include "xwayland_p.h"

static void xwayland_data_source_send(struct wlr_data_source *wlr_source,
                                      const char *requested_mime_type, int32_t fd)
{
    struct xwayland_data_source *xwayland_data_source =
        wl_container_of(wlr_source, xwayland_data_source, base);
    struct xwayland_drag_x11 *drag_x11 = xwayland_data_source->drag_x11;
    struct xwayland_server *xwayland = drag_x11->xwayland;
    xcb_atom_t *atoms = xwayland_data_source->mime_types_atoms.data;

    bool found = false;
    xcb_atom_t mime_type_atom;
    char **mime_type_ptr;
    size_t i = 0;
    wl_array_for_each(mime_type_ptr, &wlr_source->mime_types) {
        char *mime_type = *mime_type_ptr;
        if (strcmp(mime_type, requested_mime_type) == 0) {
            found = true;
            mime_type_atom = atoms[i];
            break;
        }
        ++i;
    }
    if (!found) {
        kywc_log(KYWC_DEBUG, "Cannot send X11 selection to Wayland: "
                             "unsupported MIME type");
        close(fd);
        return;
    }

    fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
    if (!xwayland_data_transfer_create(drag_x11, requested_mime_type, fd)) {
        close(fd);
        return;
    }

    xcb_convert_selection(xwayland->xcb_conn, xwayland->window_catcher,
                          xwayland->atoms[DND_SELECTION], mime_type_atom,
                          xwayland->atoms[WL_SELECTION], XCB_TIME_CURRENT_TIME);
    xcb_flush(xwayland->xcb_conn);
}

static void xwayland_data_source_accept(struct wlr_data_source *wlr_source, uint32_t serial,
                                        const char *mime_type)
{
    struct xwayland_data_source *data_source = wl_container_of(wlr_source, data_source, base);
    enum wl_data_device_manager_dnd_action action =
        mime_type ? data_source->base.current_dnd_action : WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    struct xwayland_drag_x11 *drag_x11 = data_source->drag_x11;
    struct xwayland_server *xwayland = drag_x11->xwayland;
    xwayland_send_dnd_status(xwayland, xwayland->window_catcher, drag_x11->source_window, action);
}

static void xwayland_data_source_finish(struct wlr_data_source *wlr_source)
{
    struct xwayland_data_source *data_source = wl_container_of(wlr_source, data_source, base);
    struct xwayland_drag_x11 *drag_x11 = data_source->drag_x11;
    struct xwayland_server *xwayland = drag_x11->xwayland;
    bool accepted = wlr_source->accepted;
    enum wl_data_device_manager_dnd_action action = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    if (accepted) {
        action = data_device_manager_dnd_action_to_atom(wlr_source->actions);
    }
    xwayland_send_dnd_finish(xwayland, xwayland->window_catcher, drag_x11->source_window, accepted,
                             action);
}

static void xwayland_data_source_destroy(struct wlr_data_source *wlr_source)
{
    struct xwayland_data_source *data_source = wl_container_of(wlr_source, data_source, base);
    struct xwayland_server *xwayland = data_source->drag_x11->xwayland;
    wl_array_release(&data_source->mime_types_atoms);
    free(data_source);
    xwayland->drag_x11->data_source = NULL;
    xwayland_end_drag_x11(xwayland);
}

static const struct wlr_data_source_impl data_source_impl = {
    .send = xwayland_data_source_send,
    .accept = xwayland_data_source_accept,
    .dnd_finish = xwayland_data_source_finish,
    .destroy = xwayland_data_source_destroy,
};

static bool xwayland_add_atom_to_mime_types(struct wl_array *mime_types,
                                            struct wl_array *mime_types_atoms, xcb_atom_t atom)
{
    char *mime_type = xwayland_mime_type_from_atom(atom);
    if (mime_type == NULL) {
        return false;
    }

    char **mime_type_ptr = wl_array_add(mime_types, sizeof(*mime_type_ptr));
    if (mime_type_ptr == NULL) {
        return false;
    }
    *mime_type_ptr = mime_type;

    xcb_atom_t *mime_type_atom_ptr = wl_array_add(mime_types_atoms, sizeof(*mime_type_atom_ptr));
    if (mime_type_atom_ptr == NULL) {
        return false;
    }
    *mime_type_atom_ptr = atom;
    kywc_log(KYWC_DEBUG, "mime_type %s", mime_type);
    return true;
}

static bool xwayland_dnd_get_mime_types(struct xwayland_server *xwayland,
                                        struct wl_array *mime_types,
                                        struct wl_array *mime_types_atoms, xcb_window_t source)
{
    xcb_get_property_cookie_t cookie =
        xcb_get_property(xwayland->xcb_conn,
                         0, // delete
                         source, xwayland->atoms[DND_TYPE_LIST], XCB_GET_PROPERTY_TYPE_ANY,
                         0,         // offset
                         0x1fffffff // length
        );

    xcb_get_property_reply_t *reply = xcb_get_property_reply(xwayland->xcb_conn, cookie, NULL);
    if (reply == NULL) {
        return false;
    }
    if (reply->type != XCB_ATOM_ATOM || reply->value_len == 0) {
        kywc_log(KYWC_ERROR, "invalid XdndTypeList property");
        goto error;
    }

    xcb_atom_t *atoms = xcb_get_property_value(reply);
    for (uint32_t i = 0; i < reply->value_len; ++i) {
        if (!xwayland_add_atom_to_mime_types(mime_types, mime_types_atoms, atoms[i])) {
            kywc_log(KYWC_ERROR, "failed to add MIME type atom to list");
            goto error;
        }
    }

    free(reply);
    return true;

error:
    free(reply);
    return false;
}

static int xwayland_handle_dnd_enter(struct xwayland_server *xwayland,
                                     xcb_client_message_data_t *data)
{
    if (!xwayland_is_dragging_x11(xwayland)) {
        return 0;
    }

    // already has data source
    struct xwayland_drag_x11 *drag_x11 = xwayland->drag_x11;
    if (drag_x11_has_data_source(drag_x11)) {
        return 0;
    }

    xcb_window_t source_window = data->data32[0];
    if (source_window != drag_x11->source_window) {
        kywc_log(KYWC_ERROR, "ignoring XdndEnter client message because the source window "
                             "hasn't set the drag-and-drop selection");
        return 0;
    }

    // create data source
    drag_x11->data_source = calloc(1, sizeof(struct xwayland_data_source));
    if (!drag_x11->data_source) {
        return 0;
    }
    /* init data source */
    wlr_data_source_init(&drag_x11->data_source->base, &data_source_impl);
    wl_array_init(&drag_x11->data_source->mime_types_atoms);
    drag_x11->data_source->drag_x11 = drag_x11;

    struct xwayland_data_source *source = drag_x11->data_source;
    if ((data->data32[1] & 1) == 0) {
        // Less than 3 MIME types, those are in the message data
        for (size_t i = 0; i < 3; ++i) {
            xcb_atom_t atom = data->data32[2 + i];
            if (atom == XCB_ATOM_NONE) {
                break;
            }
            if (!xwayland_add_atom_to_mime_types(&source->base.mime_types,
                                                 &source->mime_types_atoms, atom)) {
                kywc_log(KYWC_ERROR, "failed to add MIME type atom to list");
                break;
            }
        }
    } else {
        if (!xwayland_dnd_get_mime_types(xwayland, &source->base.mime_types,
                                         &source->mime_types_atoms, source_window)) {
            kywc_log(KYWC_ERROR, "failed to add MIME type atom to list");
        }
    }

    // unmap the window catcher, mapping it when enter wayland window
    xwayland_map_selection_window(xwayland, xwayland->window_catcher, NULL, false);
    return 1;
}

static int xwayland_handle_dnd_position(struct xwayland_server *xwayland,
                                        xcb_client_message_data_t *data)
{
    if (xwayland->wlr_xwayland->seat == NULL) {
        kywc_log(KYWC_DEBUG, "ignoring XdndPosition client message because "
                             "there's no Xwayland seat");
        return 0;
    }

    if (!xwayland_is_dragging_x11(xwayland)) {
        kywc_log(KYWC_DEBUG, "ignoring XdndPosition client message because "
                             "no xwayland drag is being performed");
        return 0;
    }

    xcb_window_t source_window = data->data32[0];
    xcb_timestamp_t timestamp = data->data32[3];
    xcb_atom_t action_atom = data->data32[4];

    if (source_window != xwayland->drag_x11->source_window) {
        kywc_log(KYWC_DEBUG, "ignoring XdndPosition client message because "
                             "the source window hasn't set the drag-and-drop selection");
        return 0;
    }

    enum wl_data_device_manager_dnd_action action =
        data_device_manager_dnd_action_from_atom(action_atom);
    xwayland->drag_x11->data_source->base.actions = action;

    kywc_log(KYWC_DEBUG, "DND_POSITION window=%d timestamp=%u action=%d", source_window, timestamp,
             action);
    return 1;
}

static int xwayland_handle_dnd_drop(struct xwayland_server *xwayland,
                                    xcb_client_message_data_t *data)
{
    if (!xwayland_is_dragging_x11(xwayland)) {
        return 0;
    }

    xcb_window_t source_window = data->data32[0];
    if (source_window != xwayland->drag_x11->source_window) {
        kywc_log(KYWC_ERROR, "ignoring XdndDrop client message because the source window "
                             "hasn't set the drag-and-drop selection");
        return 0;
    }

    struct xwayland_drag_x11 *drag_x11 = xwayland->drag_x11;
    if (drag_x11->hovered_client && drag_x11->data_source->base.current_dnd_action &&
        drag_x11->data_source->base.accepted) {
        struct wl_resource *resource;
        wl_resource_for_each(resource, &drag_x11->hovered_client->data_devices) {
            wl_data_device_send_drop(resource);
        }
    } else {
        xwayland_end_drag_x11(xwayland);
    }

    return 1;
}

void xwayland_send_dnd_status(struct xwayland_server *xwayland, xcb_window_t requestor,
                              xcb_window_t window, uint32_t action)
{
    uint32_t flags = 0;
    if (action != WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) {
        flags = 1 << 1;  // Opt-in for XdndPosition messages
        flags |= 1 << 0; // We accept the drop
    }

    xcb_client_message_data_t data = { 0 };
    data.data32[0] = requestor;
    data.data32[1] = flags;
    data.data32[4] = data_device_manager_dnd_action_to_atom(action);

    xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .sequence = 0,
        .window = window,
        .type = xwayland->atoms[DND_STATUS],
        .data = data,
    };

    xcb_send_event(xwayland->xcb_conn, 0, window, XCB_EVENT_MASK_NO_EVENT, (char *)&event);
    xcb_flush(xwayland->xcb_conn);
}

void xwayland_send_dnd_finish(struct xwayland_server *xwayland, xcb_window_t requestor,
                              xcb_window_t window, bool accept, uint32_t action)
{
    xcb_client_message_data_t data = { 0 };
    data.data32[0] = requestor;
    data.data32[1] = accept;
    if (accept) {
        data.data32[2] = data_device_manager_dnd_action_to_atom(action);
    }

    xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .sequence = 0,
        .window = window,
        .type = xwayland->atoms[DND_FINISHED],
        .data = data,
    };
    xcb_send_event(xwayland->xcb_conn, 0, window, XCB_EVENT_MASK_NO_EVENT, (char *)&event);
    xcb_flush(xwayland->xcb_conn);
}

int xwayland_handle_dnd_message(struct xwayland_server *xwayland,
                                xcb_client_message_event_t *client_message)
{
    if (client_message->type == xwayland->atoms[DND_ENTER]) {
        kywc_log(KYWC_DEBUG, "dnd enter");
        return xwayland_handle_dnd_enter(xwayland, &client_message->data);
    } else if (client_message->type == xwayland->atoms[DND_POSITION]) {
        kywc_log(KYWC_DEBUG, "dnd position");
        return xwayland_handle_dnd_position(xwayland, &client_message->data);
    } else if (client_message->type == xwayland->atoms[DND_DROP]) {
        kywc_log(KYWC_DEBUG, "dnd_drop");
        return xwayland_handle_dnd_drop(xwayland, &client_message->data);
    } else if (client_message->type == xwayland->atoms[DND_LEAVE]) {
        kywc_log(KYWC_DEBUG, "dnd_leave");
    }
    return 0;
}
