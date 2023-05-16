#ifndef _VIEW_H_
#define _VIEW_H_

#include <kywc/view.h>

struct server;

// enum view_layer {
//};

struct view {
    struct kywc_view base;

    /* parent and children */
    struct view *parent;
    struct wl_list link;
    struct wl_list children;

    /* geometry saved for tile, maximize and fullscreen */
    struct {
        int x, y, width, height;
    } saved;

    // XXX: view layer level

    const struct view_impl *impl;
    void *data;
};

struct view_impl {
    void (*configure)(struct view *view);
    void (*close)(struct view *view);
    void (*destroy)(struct view *view);
};

struct workspace {
    struct wl_list link; // view_manager.workspaces

    /* save views when no layout output */
    struct wl_list orphan_views;
};

struct view_manager {
    struct server *server;
    struct wl_list views;

    struct {
        struct wl_signal new_view;
    } events;

    // TODO: layers

    /* workspaces */
    struct wl_list workspaces;
    struct workspace *activated_workspace;

    /* only one activated view in all workspaces at once */
    struct view *activated_view;

    struct wl_listener new_xdg_surface;
    struct wl_listener new_xwayland_surface;
    struct wl_listener new_layer_surface;
    struct wl_listener xdg_toplevel_decoration;
    struct wl_listener server_decoration;

    struct wl_listener server_destroy;
};

struct view_manager *view_manager_create(struct server *server);

void view_init(struct view *view, const struct view_impl *impl, void *data);

void view_destroy(struct view *view);

void view_set_title(struct view *view, const char *title);

void view_set_app_id(struct view *view, const char *app_id);

void view_set_decoration(struct view *view, bool need_ssd);

void view_set_parent(struct view *view, struct view *parent);

void view_map(struct view *view);

void view_unmap(struct view *view);

void view_commit(struct view *view);

bool xdg_shell_init(struct view_manager *view_manager);

#endif /* __VIEW_H_ */
