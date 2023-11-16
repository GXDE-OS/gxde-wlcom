// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _XWAYLAND_P_H_
#define _XWAYLAND_P_H_

#include <wlr/xwayland.h>

#include <kywc/log.h>

#include "xwayland.h"

/**
 * window type that for windows not OR
 * https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html
 */
enum atom_name {
    NET_WM_WINDOW_TYPE_DESKTOP,
    NET_WM_WINDOW_TYPE_DOCK,
    NET_WM_WINDOW_TYPE_TOOLBAR,
    NET_WM_WINDOW_TYPE_MENU,
    NET_WM_WINDOW_TYPE_UTILITY,
    NET_WM_WINDOW_TYPE_SPLASH,
    NET_WM_WINDOW_TYPE_DIALOG,
    NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
    NET_WM_WINDOW_TYPE_POPUP_MENU,
    NET_WM_WINDOW_TYPE_TOOLTIP,
    NET_WM_WINDOW_TYPE_NOTIFICATION,
    NET_WM_WINDOW_TYPE_NORMAL,
    ATOM_LAST,
};

struct xwayland_server {
    struct wlr_xwayland *wlr_xwayland;
    struct wl_list surfaces;
    struct wl_list unmanaged_surfaces;

    struct wl_listener xwayland_ready;
    struct wl_listener new_xwayland_surface;
    struct wl_listener server_destroy;
    struct wl_listener output_configured;

    xcb_atom_t atoms[ATOM_LAST];

    xcb_connection_t *xcb_conn;

    float scale;
};

void xwayland_view_create(struct xwayland_server *xwayland,
                          struct wlr_xwayland_surface *wlr_xwayland_surface);

void xwayland_unmanaged_create(struct xwayland_server *xwayland,
                               struct wlr_xwayland_surface *wlr_xwayland_surface);

void xwayland_restack_unmanaged(struct xwayland_server *xwayland);

bool xwayland_surface_has_type(struct wlr_xwayland_surface *wlr_xwayland_surface, int type);

#endif /* _XWAYLAND_P_H_ */
