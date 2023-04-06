#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <kywc/log.h>

#include "adapter.h"
#include "config.h"
#include "output.h"
#include "plugin.h"
#include "server.h"

static int handle_sigterm(int signal, void *data)
{
    struct wl_display *display = data;
    wl_display_terminate(display);
    return 0;
}

void server_add_destroy_listener(struct server *server, struct wl_listener *listener)
{
    struct wl_signal *signal = &server->destroy_list;
    wl_list_insert(&signal->listener_list, &listener->link);
}

bool server_init(struct server *server)
{
    server->display = wl_display_create();
    server->event_loop = wl_display_get_event_loop(server->display);

    server->sigint =
        wl_event_loop_add_signal(server->event_loop, SIGINT, handle_sigterm, server->display);
    server->sigterm =
        wl_event_loop_add_signal(server->event_loop, SIGTERM, handle_sigterm, server->display);

    wl_signal_init(&server->destroy_list);

    config_manager_create(server);
    plugin_manager_create(server);
    output_manager_create(server);

    adapter_init(server);

    return true;
}

bool server_start(struct server *server)
{
    /* Add a Unix socket to the Wayland display. */
    const char *socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        kywc_log_errno(KYWC_FATAL, "unable to open wayland socket");
        return false;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    if (setenv("WAYLAND_DISPLAY", socket, true) < 0) {
        kywc_log_errno(KYWC_ERROR, "unable to set WAYLAND_DISPLAY");
    } else {
        kywc_log(KYWC_DEBUG, "WAYLAND_DISPLAY=%s", socket);
    }

    adapter_start(server);

    wl_display_run(server->display);
    return true;
}

void server_finish(struct server *server)
{
    wl_event_source_remove(server->sigint);
    wl_event_source_remove(server->sigterm);
    wl_display_destroy_clients(server->display);

    adapter_finish(server);

    wl_display_destroy(server->display);
    wl_signal_emit(&server->destroy_list, server);

    kywc_log(KYWC_SILENT, "kylin-wlcom finished...\n");
}
