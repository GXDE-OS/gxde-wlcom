// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include "xwayland_p.h"

static void xwayland_data_source_send(struct wlr_data_source *wlr_source,
                                      const char *requested_mime_type, int32_t fd)
{
}

static void xwayland_data_source_finish(struct wlr_data_source *wlr_source) {}

static void xwayland_data_source_accept(struct wlr_data_source *wlr_source, uint32_t serial,
                                        const char *mime_type)
{
}

static const struct wlr_data_source_impl data_source_impl = {
    .send = xwayland_data_source_send,
    .accept = xwayland_data_source_accept,
    .dnd_finish = xwayland_data_source_finish,
    .destroy = NULL,
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

int xwayland_handle_dnd_message(struct xwayland_server *xwayland,
                                xcb_client_message_event_t *client_message)
{
    if (client_message->type == xwayland->atoms[DND_ENTER]) {
        kywc_log(KYWC_DEBUG, "dnd enter");
        return xwayland_handle_dnd_enter(xwayland, &client_message->data);
    } else if (client_message->type == xwayland->atoms[DND_POSITION]) {
        kywc_log(KYWC_DEBUG, "dnd position");
    } else if (client_message->type == xwayland->atoms[DND_DROP]) {
        kywc_log(KYWC_DEBUG, "dnd_drop");
    } else if (client_message->type == xwayland->atoms[DND_LEAVE]) {
        kywc_log(KYWC_DEBUG, "dnd_leave");
    }
    return 0;
}
