// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _VIEW_H_
#define _VIEW_H_

#include <kywc/view.h>

#include "scene/scene.h"

struct server;
struct workspace;

/* taken from kwin layer */
enum layer {
    LAYER_UNKNOWN = -1,
    LAYER_FIRST = 0,
    /* layer-shell background */
    LAYER_DESKTOP = LAYER_FIRST,
    /* layer for a watermark window */
    LAYER_WATERMARK,
    /* layer-shell bottom */
    LAYER_BELOW,
    LAYER_NORMAL,
    /* dock, panel */
    LAYER_DOCK,
    /* layer-shell top */
    LAYER_ABOVE,
    /* layer for windows of type notification */
    LAYER_NOTIFICATION,
    /* active fullscreen, or active dialog */
    LAYER_ACTIVE,
    /* layer for system window, ukui-sidebar, ukui-menu */
    LAYER_SYSTEM_WINDOW,
    /* tooltips, sub- and context menus, popups */
    LAYER_POPUP,
    /* layer for input method */
    LAYER_INPUT_PANEL,
    /* layer for notifications that should be shown even on top of fullscreen */
    LAYER_CRITICAL_NOTIFICATION,
    /* layer for logout window */
    LAYER_LOGOUT,
    /* layer for override redirect windows, layer-shell overlay */
    LAYER_UNMANAGED,
    /* layer for lockscreen window */
    LAYER_SCREEN_LOCK,
    /* layer for notification windows on top of lockscreen */
    LAYER_SCREEN_LOCK_NOTIFICATION,
    /* layer for On Screen Display windows such as volume feedback, maybe dragicon */
    LAYER_ON_SCREEN_DISPLAY,
    LAYER_NUMBER,
};

struct view_layer {
    enum layer layer;
    struct ky_scene_tree *tree;
};

enum view_action {
    VIEW_ACTION_NOP = 0,
    VIEW_ACTION_ACTIVATE = 1 << 0,
    VIEW_ACTION_FULLSCREEN = 1 << 1,
    VIEW_ACTION_MAXIMIZE = 1 << 2,
    VIEW_ACTION_RESIZE = 1 << 3,
    VIEW_ACTION_TILE = 1 << 4,
    VIEW_ACTION_MINIMIZE = 1 << 5,
    VIEW_ACTION_MOVE = 1 << 6,
};

#define view_action_change_size(action)                                                            \
    (action & ~(VIEW_ACTION_ACTIVATE | VIEW_ACTION_MINIMIZE | VIEW_ACTION_MOVE))

struct view_configure_state {
    enum view_action action;
    struct kywc_box geometry;

    /* check serial to ensure configure is finished (0) */
    uint32_t configure_serial;
    enum view_action configure_action;
    struct kywc_box configure_geometry;
    struct wl_event_source *configure_timeout;
};

struct view {
    struct kywc_view base;
    struct wlr_surface *surface;
    struct wl_list link;

    /* parent and children */
    struct view *parent;
    struct wl_list parent_link;
    struct wl_list children;

    /* view in workspace */
    struct view_proxy *current_proxy;
    struct wl_list view_proxies;

    struct ky_scene_tree *tree;
    struct ky_scene_tree *content;

    // TODO: we may need update output when current output geometry/off/destroy changed
    // but something is done by positioner
    struct kywc_output *output;
    struct wl_listener output_destroy;

    struct {
        struct wl_signal output;
        /* emit if view's parent changed */
        struct wl_signal parent;
        /* emit if view's workspace changed */
        struct wl_signal workspace;
        /* emit if view enter a workspace */
        struct wl_signal workspace_enter;
        /* emit if view leave a workspace */
        struct wl_signal workspace_leave;
    } events;

    struct {
        /* geometry saved for tile, maximize and fullscreen */
        struct kywc_box geometry;
        /* restore view to layer when ... */
        enum layer layer;
    } saved;

    struct view_configure_state pending;

    const struct view_impl *impl;
    void *data;

    pid_t pid;
    bool minimized_when_show_desktop;
    bool show_in_all_workspaces;
    uint32_t current_resize_edges;
};

struct view_impl {
    void (*configure)(struct view *view);
    void (*close_popups)(struct view *view);
    void (*close)(struct view *view);
    void (*destroy)(struct view *view);

    struct wlr_buffer *(*get_icon_buffer)(struct view *view, float scale);
};

struct view_manager *view_manager_create(struct server *server);

struct view_layer *view_manager_get_layer(enum layer layer, bool in_workspace);

struct view *view_manager_get_activated(void);

void view_manager_show_desktop(bool enabled, bool apply);

bool view_manager_get_show_desktop(void);

struct view *view_from_kywc_view(struct kywc_view *kywc_view);

struct view *view_try_from_wlr_surface(struct wlr_surface *wlr_surface);

void view_init(struct view *view, const struct view_impl *impl, void *data);

void view_map(struct view *view);

void view_unmap(struct view *view);

void view_destroy(struct view *view);

void view_set_title(struct view *view, const char *title);

void view_set_app_id(struct view *view, const char *app_id);

void view_set_decoration(struct view *view, enum kywc_ssd ssd);

void view_set_shaded(struct view *view, bool shaded);

void view_move_to_output(struct view *view, struct kywc_box *src_box,
                         struct kywc_output *kywc_output);

void view_set_workspace(struct view *view, struct workspace *workspace);

void view_unset_workspace(struct view *view, struct view_layer *layer);

void view_add_all_workspace(struct view *view);

struct view_proxy *view_add_workspace(struct view *view, struct workspace *workspace);

void view_remove_workspace(struct view *view, struct workspace *workspace);

void view_set_parent(struct view *view, struct view *parent);

void view_configure(struct view *view, uint32_t serial);

void view_configured(struct view *view);

void view_helper_move(struct view *view, int x, int y);

void view_update_size(struct view *view, int width, int height, int min_width, int min_height,
                      int max_width, int max_height);

bool view_is_moveable(struct view *view);

bool view_is_resizable(struct view *view);

void view_get_tiled_geometry(struct view *view, struct kywc_box *geometry,
                             struct kywc_output *kywc_output, enum kywc_tile tile);

struct wlr_buffer *view_get_icon_buffer(struct view *view, float scale);

#endif /* __VIEW_H_ */
