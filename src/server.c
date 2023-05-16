#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/util/log.h>
#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif

#include <kywc/log.h>

#include "config.h"
#include "input.h"
#include "output.h"
#include "plugin.h"
#include "server.h"
#include "view.h"

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

static void kywc_log_callback(enum wlr_log_importance verbosity, const char *fmt, va_list args)
{
    /* switch wlr_log_importance to kywc_log_level */
    enum kywc_log_level level = KYWC_WARN;
    switch (verbosity) {
    case WLR_SILENT:
        level = KYWC_SILENT;
        break;
    case WLR_ERROR:
        level = KYWC_ERROR;
        break;
    case WLR_INFO:
        level = KYWC_INFO;
        break;
    case WLR_DEBUG:
        level = KYWC_DEBUG;
        break;
    default:
        break;
    }
    kywc_vlog(level, fmt, args);
}

#if HAVE_XWAYLAND
static void handle_xwayland_ready(struct wl_listener *listener, void *data)
{
    struct server *server = wl_container_of(listener, server, xwayland_ready);
}
#endif

static bool wlroots_server_init(struct server *server)
{
    /* verbosity is not used when we replaced log_callback */
    wlr_log_init(WLR_DEBUG, kywc_log_callback);

    server->backend = wlr_backend_autocreate(server->display, &server->session);
    if (!server->backend) {
        kywc_log(KYWC_ERROR, "unable to create backend");
        return false;
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        kywc_log(KYWC_ERROR, "unable to create renderer");
        return false;
    }

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        kywc_log(KYWC_ERROR, "unable to create allocator");
        return false;
    }

    server->compositor = wlr_compositor_create(server->display, 5, NULL);
    wlr_subcompositor_create(server->display);
    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->layout = wlr_output_layout_create();

    wlr_presentation_create(server->display, server->backend);
    wlr_screencopy_manager_v1_create(server->display);
    wlr_export_dmabuf_manager_v1_create(server->display);
    wlr_viewporter_create(server->display);
    wlr_fractional_scale_manager_v1_create(server->display, 1);

#if HAVE_XWAYLAND
    if (server->options.enable_xwayland) {
        server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
        if (!server->xwayland) {
            kywc_log(KYWC_ERROR, "cannot create xwayland server");
            unsetenv("DISPLAY");
        } else {
            server->xwayland_ready.notify = handle_xwayland_ready;
            wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);

            setenv("DISPLAY", server->xwayland->display_name, true);
            kywc_log(KYWC_INFO, "xwayland is running on display %s",
                     server->xwayland->display_name);
        }
    }
#endif

    return true;
}

bool server_init(struct server *server)
{
    server->display = wl_display_create();
    server->event_loop = wl_display_get_event_loop(server->display);

    /* ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    server->sigint =
        wl_event_loop_add_signal(server->event_loop, SIGINT, handle_sigterm, server->display);
    server->sigterm =
        wl_event_loop_add_signal(server->event_loop, SIGTERM, handle_sigterm, server->display);

    wl_signal_init(&server->destroy_list);

    config_manager_create(server);

    if (!wlroots_server_init(server)) {
        return false;
    }

    output_manager_create(server);
    input_manager_create(server);
    view_manager_create(server);

    plugin_manager_create(server);

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

    if (!wlr_backend_start(server->backend)) {
        kywc_log(KYWC_ERROR, "unable to start the wlroots backend");
        return false;
    }

    wl_display_run(server->display);
    return true;
}

void server_finish(struct server *server)
{
    wl_event_source_remove(server->sigint);
    wl_event_source_remove(server->sigterm);
    wl_display_destroy_clients(server->display);

    wl_display_destroy(server->display);
    wlr_output_layout_destroy(server->layout);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);

    wl_signal_emit(&server->destroy_list, server);

    kywc_log(KYWC_SILENT, "kylin-wlcom finished...\n");
}
