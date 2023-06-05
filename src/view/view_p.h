#ifndef _VIEW_P_H_
#define _VIEW_P_H_

#include <kywc/log.h>

#include "server.h"
#include "view/view.h"

struct view_manager {
    struct server *server;
    struct wl_list views;

    struct {
        struct wl_signal new_view;
    } events;

    struct view_layer layers[LAYER_NUMBER];

    // TODO: views keyboard focused when multi-seat
    struct {
        /* only one activated view in all workspaces at once */
        struct view *view;
        struct wl_listener minimize;
        struct wl_listener destroy;
    } activated;


    struct wl_listener new_xdg_surface;
    struct wl_listener server_destroy;
};

/* for interactive move and resize */
struct seat;
void interactive_begin_move(struct view *view, struct seat *seat);
void interactive_begin_resize(struct view *view, uint32_t edges, struct seat *seat);

bool xdg_shell_init(struct view_manager *view_manager);

bool decoration_manager_create(struct view_manager *view_manager);
bool decoration_shoud_use_ssd(struct wlr_surface *surface);

struct wlr_xdg_popup;
void xdg_popup_create(struct wlr_xdg_popup *wlr_xdg_popup, struct ky_scene_tree *shell);

#endif /* _VIEW_P_H_ */
