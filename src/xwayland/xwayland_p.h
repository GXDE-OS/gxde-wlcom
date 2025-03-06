// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _XWAYLAND_P_H_
#define _XWAYLAND_P_H_

#include <pixman.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/xwayland.h>
#include <xcb/shape.h>

#include <kywc/log.h>

#include "xwayland.h"

struct kywc_box;

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
    NET_WM_STATE_DEMANDS_ATTENTION,
    // KDE-specific atom
    KDE_NET_WM_STATE_SKIP_SWITCHER,

    NET_WM_ICON,
    NET_WM_WINDOW_OPACITY,

    UTF8_STRING,
    NET_WM_NAME,
    NET_SUPPORTING_WM_CHECK,

    INCR,
    TEXT,
    WL_SELECTION,

    DND_SELECTION,
    DND_AWARE,
    DND_STATUS,
    DND_POSITION,
    DND_ENTER,
    DND_LEAVE,
    DND_DROP,
    DND_FINISHED,
    DND_PROXY,
    DND_TYPE_LIST,
    DND_ACTION_MOVE,
    DND_ACTION_COPY,
    DND_ACTION_ASK,
    DND_ACTION_PRIVATE,

    ATOM_LAST,
};

struct xwayland_data_transfer {
    struct wl_list link;

    int wl_client_fd; // target fd
    char *mime_type;
    int property_start;
    xcb_get_property_reply_t *property_reply;
    struct wl_event_source *event_source; // for read loop
};

struct xwayland_data_source {
    struct wlr_data_source base;
    struct wl_array mime_types_atoms;
    struct xwayland_drag_x11 *drag_x11;
};

struct xwayland_drag_x11 {
    struct xwayland_server *xwayland;

    xcb_window_t source_window;
    struct xwayland_data_source *data_source;

    struct wlr_surface *hovered_surface;
    struct wl_listener surface_destroy;
    struct wlr_seat_client *hovered_client;
    struct wl_listener seat_client_destroy;

    struct wl_list transfers; // struct xwayland_data_transfer

    // TODO: listen touch, tablet
    struct wl_listener cursor_motion;
    struct wl_listener cursor_button;
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
    struct wl_listener seat_destroy;

    xcb_atom_t atoms[ATOM_LAST];

    xcb_connection_t *xcb_conn;
    xcb_screen_t *screen;
    const xcb_query_extension_reply_t *shape;
    const xcb_query_extension_reply_t *xfixes;

    struct wlr_surface *hoverd_surface;
    struct wl_listener surface_destroy;

    struct wl_listener activate_view;
    struct wlr_xwayland_surface *activated_surface;

    // for drag x11 to wayland target
    xcb_window_t window_catcher;
    // TODO: multiple drags
    struct xwayland_drag_x11 *drag_x11;

    float scale;
};

void xwayland_view_create(struct xwayland_server *xwayland,
                          struct wlr_xwayland_surface *wlr_xwayland_surface);

void xwayland_unmanaged_create(struct xwayland_server *xwayland,
                               struct wlr_xwayland_surface *wlr_xwayland_surface);

void xwayland_restack_unmanaged(struct xwayland_server *xwayland);

bool xwayland_surface_has_type(struct wlr_xwayland_surface *wlr_xwayland_surface, int type);

enum {
    INPUT_MASK_POINTER = 1 << 0,
    INPUT_MASK_KEYBOARD = 1 << 1,
};

bool xwayland_surface_has_input(struct wlr_xwayland_surface *wlr_xwayland_surface, uint32_t input);

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

void xwayland_view_set_demands_attention(struct wlr_xwayland_surface *surface, bool state,
                                         bool toggle);

struct wlr_xwayland_surface *xwayland_view_look_surface(struct xwayland_server *xwayland,
                                                        xcb_window_t window_id);

void xwayland_view_add_new_wm_icon(struct wlr_xwayland_surface *surface, uint32_t width,
                                   uint32_t height, uint32_t size, uint32_t *data);

bool xwayland_view_set_opacity(struct xwayland_server *xwayland, xcb_window_t window_id,
                               float opacity);

void xwayland_update_seat(struct seat *seat);

void xwayland_update_hovered_surface(struct wlr_surface *surface);

void xwayland_fixup_pointer_position(struct wlr_surface *surface);

int xwayland_read_wm_state(xcb_window_t window_id);

int xwayland_read_wm_icon(xcb_window_t window_id);

int xwayland_read_wm_window_opacity(xcb_window_t window_id);

char *xwayland_mime_type_from_atom(xcb_atom_t atom);

enum wl_data_device_manager_dnd_action
data_device_manager_dnd_action_from_atom(enum atom_name atom);

xcb_atom_t data_device_manager_dnd_action_to_atom(enum wl_data_device_manager_dnd_action action);

// selection
int xwayland_handle_selection_event(struct xwayland_server *xwayland, xcb_generic_event_t *event);

void xwayland_create_seletion_window(struct xwayland_server *xwayland, xcb_window_t *window,
                                     int16_t x, int16_t y, uint16_t width, uint16_t height);

void xwayland_map_selection_window(struct xwayland_server *xwayland, xcb_window_t window,
                                   struct kywc_box *box, bool map);

struct xwayland_data_transfer *xwayland_data_transfer_create(struct xwayland_drag_x11 *drag_x11,
                                                             const char *mime_type, int fd);

void xwayland_data_transfer_destroy(struct xwayland_data_transfer *transfer);

struct xwayland_data_transfer *
xwayland_data_transfer_find_by_type(struct xwayland_drag_x11 *drag_x11, const char *mime_type);

// drag_x11
bool xwayland_is_dragging_x11(struct xwayland_server *xwayland);

bool drag_x11_has_data_source(struct xwayland_drag_x11 *drag_x11);

bool xwayland_start_drag_x11(struct xwayland_server *xwayland, xcb_window_t source_window);

void xwayland_end_drag_x11(struct xwayland_server *xwayland);

void drag_set_focus(struct xwayland_drag_x11 *drag, struct wlr_surface *surface, double sx,
                    double sy);

// dnd protocol
int xwayland_handle_dnd_message(struct xwayland_server *xwayland,
                                xcb_client_message_event_t *client_message);

void xwayland_send_dnd_status(struct xwayland_server *xwayland, xcb_window_t requestor,
                              xcb_window_t window, uint32_t action);

void xwayland_send_dnd_finish(struct xwayland_server *xwayland, xcb_window_t requestor,
                              xcb_window_t window, bool accept, uint32_t action);

#endif /* _XWAYLAND_P_H_ */
