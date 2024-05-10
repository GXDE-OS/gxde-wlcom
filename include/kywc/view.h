// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _KYWC_VIEW_H_
#define _KYWC_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

#include <wayland-server-core.h>

#include "boxes.h"

struct kywc_output;

enum kywc_edges {
    KYWC_EDGE_NONE = 0,
    KYWC_EDGE_TOP = 1 << 0,
    KYWC_EDGE_BOTTOM = 1 << 1,
    KYWC_EDGE_LEFT = 1 << 2,
    KYWC_EDGE_RIGHT = 1 << 3,
};

enum kywc_tile {
    KYWC_TILE_NONE = 0,
    KYWC_TILE_TOP,
    KYWC_TILE_BOTTOM,
    KYWC_TILE_LEFT,
    KYWC_TILE_RIGHT,
    KYWC_TILE_TOP_LEFT,
    KYWC_TILE_BOTTOM_LEFT,
    KYWC_TILE_TOP_RIGHT,
    KYWC_TILE_BOTTOM_RIGHT,
    KYWC_TILE_CENTER,
    KYWC_TILE_ALL,
};

enum kywc_ssd {
    KYWC_SSD_NONE = 0,
    KYWC_SSD_TITLE = 1 << 0,
    KYWC_SSD_BORDER = 1 << 1,
    KYWC_SSD_RESIZE = 1 << 2,
    KYWC_SSD_ALL = (1 << 3) - 1,
};

enum kywc_view_role {
    KYWC_VIEW_ROLE_NORMAL = 0,
    KYWC_VIEW_ROLE_DESKTOP,
    KYWC_VIEW_ROLE_PANEL,
    KYWC_VIEW_ROLE_ONSCREENDISPLAY,
    KYWC_VIEW_ROLE_NOTIFICATION,
    KYWC_VIEW_ROLE_TOOLTIP,
    KYWC_VIEW_ROLE_CRITICALNOTIFICATION,
    KYWC_VIEW_ROLE_SYSTEMWINDOW,
    KYWC_VIEW_ROLE_INPUTPANEL,
    KYWC_VIEW_ROLE_LOGOUT,
    KYWC_VIEW_ROLE_SCREENLOCK,
    KYWC_VIEW_ROLE_SCREENLOCKNOTIFICATION,
    KYWC_VIEW_ROLE_WATERMARK,
};

struct kywc_view {
    enum kywc_view_role role;

    /* current geometry in global layout */
    struct kywc_box geometry;

    /* margin if used ssd */
    struct {
        int off_x, off_y, off_width, off_height;
    } margin;

    /* padding in buffer and surface */
    struct {
        int top, bottom, left, right;
    } padding;

    /* minimize size client set or default */
    int32_t min_width, min_height;
    int32_t max_width, max_height;

    enum kywc_ssd ssd;
    bool shaded;
    bool has_initial_position;
    bool has_round_corner;

    /* have a buffer attached and can shown in screen */
    bool mapped;
    bool minimized, kept_above, kept_below;

    /* current configured states */
    bool maximized, fullscreen, activated;
    enum kywc_tile tiled;
    bool modal;

    /* wm capabilities of the view */
    bool minimizable, maximizable, fullscreenable;
    bool closeable, movable, resizable;
    bool activatable, focusable, shadeable;
    bool skip_taskbar, skip_switcher;

    const char *uuid;
    /* app_id: class when xwayland shell */
    const char *title, *app_id;

    /* need focused by this seat when map */
    struct seat *focused_seat;

    struct {
        /* emit before map for position, ssd ... */
        struct wl_signal premap;
        /* emit when view is mapped */
        struct wl_signal map;
        /* emit when view is going to be unmapped */
        struct wl_signal unmap;
        /* emit when view is going to be destroyed */
        struct wl_signal destroy;

        /* emit when view's activated state has changed */
        struct wl_signal activate;
        /* emit when view's maximized state has changed */
        struct wl_signal maximize;
        /* emit when view's minimized state has changed */
        struct wl_signal minimize;
        /* emit when view's fullscreen state has changed */
        struct wl_signal fullscreen;
        /* emit when view's tiled state has changed */
        struct wl_signal tile;
        /* emit when view's capabilities state has changed */
        struct wl_signal capabilities;

        /* emit when view's title has changed */
        struct wl_signal title;
        /* emit when view's app_id(class) has changed */
        struct wl_signal app_id;

        /* emit when view's position has changed */
        struct wl_signal position;
        /* emit when view's size has changed */
        struct wl_signal size;
        /* emit when view's decoration mode has changed */
        struct wl_signal decoration;
        /* emit when view's drop-shadow mode has changed */
        struct wl_signal shadow;
        /* emit when view unset modal */
        struct wl_signal unset_modal;
    } events;
};

/**
 * listen new_view signal for xdg-shell and xwayland-shell
 */
void kywc_view_add_show_desktop_listener(struct wl_listener *listener);

void kywc_view_add_new_listener(struct wl_listener *listener);

void kywc_view_add_new_mapped_listener(struct wl_listener *listener);

void kywc_view_close(struct kywc_view *kywc_view);
void kywc_view_move(struct kywc_view *kywc_view, int x, int y);
void kywc_view_resize(struct kywc_view *kywc_view, struct kywc_box *geometry);
void kywc_view_activate(struct kywc_view *kywc_view);

void kywc_view_set_tiled(struct kywc_view *kywc_view, enum kywc_tile tile,
                         struct kywc_output *kywc_output);

void kywc_view_set_minimized(struct kywc_view *kywc_view, bool minimized);
void kywc_view_toggle_minimized(struct kywc_view *kywc_view);

void kywc_view_set_maximized(struct kywc_view *kywc_view, bool maximized,
                             struct kywc_output *kywc_output);
void kywc_view_toggle_maximized(struct kywc_view *kywc_view);

void kywc_view_set_fullscreen(struct kywc_view *kywc_view, bool fullscreen,
                              struct kywc_output *kywc_output);
void kywc_view_toggle_fullscreen(struct kywc_view *kywc_view);

void kywc_view_set_kept_above(struct kywc_view *kywc_view, bool kept_above);
void kywc_view_toggle_kept_above(struct kywc_view *kywc_view);

void kywc_view_set_kept_below(struct kywc_view *kywc_view, bool kept_below);
void kywc_view_toggle_kept_below(struct kywc_view *kywc_view);

struct kywc_view *kywc_view_by_uuid(const char *uuid);

#endif /* _KYWC_VIEW_H_ */
