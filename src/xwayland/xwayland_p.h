// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _XWAYLAND_P_H_
#define _XWAYLAND_P_H_

#include <pixman.h>
#include <wlr/xwayland.h>
#include <xcb/shape.h>

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

    /* kde extensions */
    KDE_NET_WM_WINDOW_TYPE_OVERRIDE,

    /* ukui atoms */
    UKUI_NET_WM_WINDOW_TYPE_SYSTEMWINDOW,
    UKUI_NET_WM_WINDOW_TYPE_INPUTPANEL,
    UKUI_NET_WM_WINDOW_TYPE_LOGOUT,
    UKUI_NET_WM_WINDOW_TYPE_SCREENLOCK,
    UKUI_NET_WM_WINDOW_TYPE_SCREENLOCKNOTIFICATION,
    UKUI_NET_WM_WINDOW_TYPE_WATERMARK,

    NET_WM_STATE,
    NET_WM_STATE_ABOVE,
    NET_WM_STATE_BELOW,
    NET_WM_STATE_SKIP_TASKBAR,
    // KDE-specific atom
    KDE_NET_WM_STATE_SKIP_SWITCHER,

    NET_WM_ICON,
    NET_WM_WINDOW_OPACITY,

    UTF8_STRING,
    NET_WM_NAME,
    NET_SUPPORTING_WM_CHECK,

    ATOM_LAST,
};

struct xwayland_server {
    struct server *server;
    struct wlr_xwayland *wlr_xwayland;
    struct wl_list surfaces;
    struct wl_list unmanaged_surfaces;

    struct wl_listener xwayland_ready;
    struct wl_listener new_xwayland_surface;
    struct wl_listener server_destroy;
    struct wl_listener output_configured;

    xcb_atom_t atoms[ATOM_LAST];

    xcb_connection_t *xcb_conn;
    struct wl_event_source *event_source;
    const xcb_query_extension_reply_t *shape;

    struct wlr_surface *hoverd_surface;
    struct wl_listener surface_destroy;

    float scale;
};

void xwayland_view_create(struct xwayland_server *xwayland,
                          struct wlr_xwayland_surface *wlr_xwayland_surface);

void xwayland_unmanaged_create(struct xwayland_server *xwayland,
                               struct wlr_xwayland_surface *wlr_xwayland_surface);

void xwayland_restack_unmanaged(struct xwayland_server *xwayland);

bool xwayland_surface_has_type(struct wlr_xwayland_surface *wlr_xwayland_surface, int type);

bool xwayland_surface_has_input(struct wlr_xwayland_surface *wlr_xwayland_surface);

void xwayland_surface_shape_select_input(struct wlr_xwayland_surface *surface, bool enabled);

void xwayland_surface_apply_shape_region(struct wlr_xwayland_surface *surface);

bool xwayland_unmanaged_set_shape_region(struct xwayland_server *xwayland, xcb_window_t window_id,
                                         xcb_shape_sk_t kind, const pixman_region32_t *region);

bool xwayland_unmanaged_set_opacity(struct xwayland_server *xwayland, xcb_window_t window_id,
                                    float opacity);

bool xwayland_view_set_shape_region(struct xwayland_server *xwayland, xcb_window_t window_id,
                                    xcb_shape_sk_t kind, const pixman_region32_t *region);

void xwayland_surface_debug_type(struct wlr_xwayland_surface *wlr_xwayland_surface);

void xwayland_view_set_above_or_below(struct wlr_xwayland_surface *surface, bool above_or_below,
                                      bool state, bool toggle);

void xwayland_view_set_skip_taskbar(struct wlr_xwayland_surface *surface, bool skip_taskbar);

void xwayland_view_set_skip_switcher(struct wlr_xwayland_surface *surface, bool skip_switcher);

struct wlr_xwayland_surface *xwayland_view_look_surface(struct xwayland_server *xwayland,
                                                        xcb_window_t window_id);

void xwayland_view_add_new_wm_icon(struct wlr_xwayland_surface *surface, uint32_t width,
                                   uint32_t height, uint32_t size, uint32_t *data);

bool xwayland_view_set_opacity(struct xwayland_server *xwayland, xcb_window_t window_id,
                               float opacity);

void xwayland_update_seat(struct seat *seat);

void xwayland_update_hovered_surface(struct wlr_surface *surface);

int xwayland_read_wm_state(xcb_window_t window_id);

int xwayland_read_wm_icon(xcb_window_t window_id);

int xwayland_read_wm_window_opacity(xcb_window_t window_id);

#endif /* _XWAYLAND_P_H_ */
