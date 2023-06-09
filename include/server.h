#ifndef _SERVER_H_
#define _SERVER_H_

#include <wayland-server-core.h>

#include "scene/scene.h"

struct server {
    struct wl_display *display;
    struct wl_event_loop *event_loop;

    /* signal handler */
    struct wl_event_source *sigint;
    struct wl_event_source *sigterm;

    struct {
        bool enable_xwayland;
        bool log_to_file;
    } options;

    struct wl_signal destroy_list;

    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_backend *backend;
    struct wlr_session *session;
    struct wlr_compositor *compositor;

    struct ky_scene *scene;
    struct wlr_output_layout *layout;
};

bool server_init(struct server *server);

bool server_start(struct server *server);

void server_run(struct server *server);

void server_finish(struct server *server);

/**
 * first-in last-out signal for server finish
 */
void server_add_destroy_listener(struct server *server, struct wl_listener *listener);

#endif /* _SERVER_H_ */
