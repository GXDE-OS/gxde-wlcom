// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <xcb/xfixes.h>

#include "xwayland_p.h"

#define XDND_VERSION 5

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

    } else if (response_type == XCB_PROPERTY_NOTIFY) {

    } else if (xwayland->xfixes && response_type == xwayland->xfixes->first_event +
                                                        XCB_XFIXES_SELECTION_NOTIFY) { // xfixes
        return xwayland_handle_xfixes_selection_notify(
            xwayland, (xcb_xfixes_selection_notify_event_t *)event);
    }

    return 0;
}
