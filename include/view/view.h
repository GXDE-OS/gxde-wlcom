#ifndef _VIEW_H_
#define _VIEW_H_

#include <kywc/view.h>

#include "scene/scene.h"

struct server;

/* taken from kwin layer */
enum layer {
    LAYER_UNKNOWN = -1,
    LAYER_FIRST = 0,
    /* layer-shell background */
    LAYER_DESKTOP = LAYER_FIRST,
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
    /* tooltips, sub- and context menus, popups */
    LAYER_POPUP,
    /* layer for notifications that should be shown even on top of fullscreen */
    LAYER_CRITICAL_NOTIFICATION,
    /* layer for On Screen Display windows such as volume feedback, maybe dragicon */
    LAYER_ON_SCREEN_DISPLAY,
    /* layer for override redirect windows, layer-shell overlay */
    LAYER_UNMANAGED,
    LAYER_NUMBER,
};

struct view_layer {
    enum layer layer;
    struct ky_scene_tree *tree;
};

/* actions need send configure to client */
enum view_action {
    VIEW_ACTION_NOP = 0,
    VIEW_ACTION_ACTIVATE = 1 << 0,
    VIEW_ACTION_FULLSCREEN = 1 << 1,
    VIEW_ACTION_MAXIMIZE = 1 << 2,
    VIEW_ACTION_RESIZE = 1 << 3,
    VIEW_ACTION_TILE = 1 << 4,
    VIEW_ACTION_MINIMIZE = 1 << 5,
};

struct view_configure_state {
    enum view_action action;
    struct kywc_box geometry;

    /* check serial to ensure configure is finished (0) */
    uint32_t configure_serial;
    struct wl_event_source *configure_timeout;
};

struct view {
    struct kywc_view base;
    struct wlr_surface *surface;

    /* insert to workspace */
    struct wl_list link;

    /* parent and children */
    struct view *parent;
    struct wl_list parent_link;
    struct wl_list children;

    struct ky_scene_tree *tree;

    struct workspace *workspace;
    struct kywc_output *output;

    struct {
        // struct wl_signal output;
        /* emit if view's parent changed */
        struct wl_signal parent;
        /* emit if view's workspace changed */
        struct wl_signal workspace;
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
};

struct view_impl {
    void (*configure)(struct view *view);
    void (*close)(struct view *view);
    void (*destroy)(struct view *view);
};

struct view_manager *view_manager_create(struct server *server);

struct view_layer *view_manager_get_layer(enum layer layer, bool in_workspace);

struct view *view_from_kywc_view(struct kywc_view *kywc_view);

void view_init(struct view *view, const struct view_impl *impl, void *data);

void view_map(struct view *view);

void view_unmap(struct view *view);

void view_destroy(struct view *view);

void view_set_title(struct view *view, const char *title);

void view_set_app_id(struct view *view, const char *app_id);

void view_set_decoration(struct view *view, bool need_ssd);

void view_set_shadow(struct view *view, bool need_shadow);

void view_set_output(struct view *view, struct kywc_output *output);

void view_set_workspace(struct view *view, struct workspace *workspace);

void view_set_parent(struct view *view, struct view *parent);

void view_configure(struct view *view, uint32_t serial);

void view_configured(struct view *view);

void view_helper_move(struct view *view, int x, int y);

void view_update_size(struct view *view, int width, int height, int min_width, int min_height,
                      int max_width, int max_height);

bool view_is_moveable(struct view *view);

bool view_is_resizable(struct view *view);

#endif /* __VIEW_H_ */
