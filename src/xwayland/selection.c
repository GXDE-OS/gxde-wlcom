// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <unistd.h>

#include <xcb/xfixes.h>

#include "kywc/boxes.h"
#include "xwayland_p.h"

#define XDND_VERSION 5

static int write_selection_property_to_wl_client(int fd, uint32_t mask, void *data)
{
    struct xwayland_data_transfer *transfer = data;

    char *property = xcb_get_property_value(transfer->property_reply);
    int remainder =
        xcb_get_property_value_length(transfer->property_reply) - transfer->property_start;

    ssize_t len = write(fd, property + transfer->property_start, remainder);
    if (len == -1) {
        kywc_log(KYWC_ERROR, "write error to target fd %d", fd);
        xwayland_data_transfer_destroy(transfer);
        return 0;
    }

    kywc_log(KYWC_DEBUG, "wrote %zd (total %zd, remaining %d) of %d bytes to fd %d", len,
             transfer->property_start + len, remainder,
             xcb_get_property_value_length(transfer->property_reply), fd);

    if (len < remainder) {
        transfer->property_start += len;
        return 1;
    }

    xwayland_data_transfer_destroy(transfer);

    return 0;
}

static int xwayland_handle_selection_notify(struct xwayland_server *xwayland,
                                            xcb_selection_notify_event_t *event)
{
    if (!xwayland_is_dragging_x11(xwayland)) {
        return 0;
    }

    if (event->requestor != xwayland->window_catcher) {
        return 0;
    }

    if (event->property == XCB_ATOM_NONE) {
        kywc_log(KYWC_ERROR, "convert selection failed");
        return 0;
    }

    char *mime_type = xwayland_mime_type_from_atom(event->target);
    struct xwayland_data_transfer *transfer =
        xwayland_data_transfer_find_by_type(xwayland->drag_x11, mime_type);
    free(mime_type);
    if (!transfer) {
        kywc_log(KYWC_DEBUG, "cannot find mime_type transfer");
        return 0;
    }

    xcb_window_t window = xwayland->window_catcher;
    xcb_get_property_cookie_t cookie = xcb_get_property(
        xwayland->xcb_conn, true, window, xwayland->atoms[WL_SELECTION], XCB_GET_PROPERTY_TYPE_ANY,
        0,         // offset
        0x1fffffff // length
    );

    transfer->property_start = 0;
    transfer->property_reply = xcb_get_property_reply(xwayland->xcb_conn, cookie, NULL);

    if (!transfer->property_reply) {
        kywc_log(KYWC_ERROR, "cannot get selection property");
        return 0;
    }

    if (transfer->property_reply->type == xwayland->atoms[INCR]) {
        kywc_log(KYWC_INFO, "incr start !");
        // TODO: handle incr
        xwayland_data_transfer_destroy(transfer);
        return 0;
    }

    /* write selection preoperty to client */
    int need_continue =
        write_selection_property_to_wl_client(transfer->wl_client_fd, WL_EVENT_WRITABLE, transfer);

    if (need_continue) {
        kywc_log(KYWC_DEBUG, "continue reading in loop");
        // continue reading in loop
        struct wl_event_loop *loop = wl_display_get_event_loop(xwayland->wlr_xwayland->wl_display);
        transfer->event_source =
            wl_event_loop_add_fd(loop, transfer->wl_client_fd, WL_EVENT_WRITABLE,
                                 write_selection_property_to_wl_client, transfer);
    }

    return 1;
}

static int xwayland_handle_xfixes_selection_notify(struct xwayland_server *xwayland,
                                                   xcb_xfixes_selection_notify_event_t *event)
{
    if (event->selection == xwayland->atoms[DND_SELECTION]) {
        if (event->owner != XCB_ATOM_NONE) {
            return xwayland_start_drag_x11(xwayland, event->owner);
        } else {
            xwayland_end_drag_x11(xwayland);
        }
        return 1;
    }

    return 0;
}

void xwayland_create_seletion_window(struct xwayland_server *xwayland, xcb_window_t *window,
                                     int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    *window = xcb_generate_id(xwayland->xcb_conn);
    xcb_create_window(
        xwayland->xcb_conn, XCB_COPY_FROM_PARENT, *window, xwayland->screen->root, x, y, width,
        height, 0, XCB_WINDOW_CLASS_INPUT_ONLY, xwayland->screen->root_visual, XCB_CW_EVENT_MASK,
        (uint32_t[]){ XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE });
    xcb_change_property(xwayland->xcb_conn, XCB_PROP_MODE_REPLACE, *window,
                        xwayland->atoms[DND_AWARE], XCB_ATOM_ATOM,
                        32, // format
                        1, &(uint32_t){ XDND_VERSION });
    xcb_unmap_window(xwayland->xcb_conn, *window);
    uint32_t values[] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(xwayland->xcb_conn, *window, XCB_CONFIG_WINDOW_STACK_MODE, values);
    xcb_flush(xwayland->xcb_conn);
}

int xwayland_handle_selection_event(struct xwayland_server *xwayland, xcb_generic_event_t *event)
{
    if (xwayland->wlr_xwayland->seat == NULL) {
        kywc_log(KYWC_ERROR, "not handling selection events: no seat assigned to xwayland");
        return 0;
    }

    const uint8_t response_type = event->response_type & 0x7f;
    if (response_type == XCB_SELECTION_NOTIFY) {
        return xwayland_handle_selection_notify(xwayland, (xcb_selection_notify_event_t *)event);
    } else if (response_type == XCB_PROPERTY_NOTIFY) {
        // XCB_PROPERTY_NOTIFY
    } else if (xwayland->xfixes && response_type == xwayland->xfixes->first_event +
                                                        XCB_XFIXES_SELECTION_NOTIFY) { // xfixes
        return xwayland_handle_xfixes_selection_notify(
            xwayland, (xcb_xfixes_selection_notify_event_t *)event);
    }

    return 0;
}

void xwayland_map_selection_window(struct xwayland_server *xwayland, xcb_window_t window,
                                   struct kywc_box *box, bool map)
{
    if (map) {
        xcb_map_window(xwayland->xcb_conn, window);
        uint16_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                              XCB_CONFIG_WINDOW_HEIGHT;
        uint32_t value_list[] = { box->x, box->y, box->width, box->height };
        xcb_configure_window(xwayland->xcb_conn, window, value_mask, value_list);
        uint32_t values[] = { XCB_STACK_MODE_ABOVE };
        xcb_configure_window(xwayland->xcb_conn, window, XCB_CONFIG_WINDOW_STACK_MODE, values);
    } else {
        xcb_unmap_window(xwayland->xcb_conn, window);
    }
    xcb_flush(xwayland->xcb_conn);
}

struct xwayland_data_transfer *xwayland_data_transfer_create(struct xwayland_drag_x11 *drag_x11,
                                                             const char *mime_type, int fd)
{
    struct xwayland_data_transfer *transfer = calloc(1, sizeof(struct xwayland_data_transfer));
    if (!transfer) {
        return NULL;
    }

    transfer->wl_client_fd = fd;
    transfer->mime_type = strdup(mime_type);
    if (!transfer->mime_type) {
        free(transfer);
        return NULL;
    }

    wl_list_insert(&drag_x11->transfers, &transfer->link);
    return transfer;
}

void xwayland_data_transfer_destroy(struct xwayland_data_transfer *transfer)
{
    if (transfer->event_source) {
        wl_event_source_remove(transfer->event_source);
    }
    wl_list_remove(&transfer->link);
    close(transfer->wl_client_fd);
    free(transfer->property_reply);
    free(transfer->mime_type);
    free(transfer);
}

struct xwayland_data_transfer *
xwayland_data_transfer_find_by_type(struct xwayland_drag_x11 *drag_x11, const char *mime_type)
{
    struct xwayland_data_transfer *transfer;
    wl_list_for_each(transfer, &drag_x11->transfers, link) {
        if (strcmp(mime_type, transfer->mime_type) == 0) {
            return transfer;
        }
    }
    return NULL;
}
