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

bool xdg_shell_init(struct view_manager *view_manager);

void decoration_init(struct view_manager *view_manager);

#endif /* _VIEW_P_H_ */
