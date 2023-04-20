#ifndef _SERVER_H_
#define _SERVER_H_

#include <wayland-server-core.h>

struct server {
    struct wl_display *display;
    struct wl_event_loop *event_loop;

    /* signal handler */
    struct wl_event_source *sigint;
    struct wl_event_source *sigterm;

    struct {
        bool enable_xwayland;
    } options;

    struct wl_signal destroy_list;

    /* adapter server */
    const struct server_impl *impl;
    void *data;
};

struct seat;

struct server_impl {
    bool (*init)(struct server *server);
    bool (*start)(struct server *server);
    void (*finish)(struct server *server);

    bool (*init_seat)(struct server *server, struct seat *seat);
};

bool server_init(struct server *server);

bool server_start(struct server *server);

void server_finish(struct server *server);

/**
 * first-in last-out signal for server finish
 */
void server_add_destroy_listener(struct server *server, struct wl_listener *listener);

#endif /* _SERVER_H_ */
