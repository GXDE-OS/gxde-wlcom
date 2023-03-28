#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <kywc/log.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/util/log.h>

#include "wlroots_p.h"

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
    struct wlroots_server *wlroots = wl_container_of(listener, wlroots, xwayland_ready);
}
#endif

static bool wlroots_server_init(struct server *server)
{
    kywc_log(KYWC_INFO, "adapter: start wlroots server");
    struct wlroots_server *wlroots = wlroots_server_from_server(server);

    wlroots->backend = wlr_backend_autocreate(server->display, &wlroots->session);
    if (!wlroots->backend) {
        kywc_log(KYWC_ERROR, "unable to create backend");
        return false;
    }

    wlroots->renderer = wlr_renderer_autocreate(wlroots->backend);
    if (!wlroots->renderer) {
        kywc_log(KYWC_ERROR, "unable to create renderer");
        return false;
    }

    wlroots->allocator = wlr_allocator_autocreate(wlroots->backend, wlroots->renderer);
    if (!wlroots->allocator) {
        kywc_log(KYWC_ERROR, "unable to create allocator");
        return false;
    }

    wlroots->compositor = wlr_compositor_create(server->display, wlroots->renderer);
    wlr_subcompositor_create(server->display);
    wlr_renderer_init_wl_display(wlroots->renderer, server->display);

    wlroots->scene = wlr_scene_create();
    wlroots->layout = wlr_output_layout_create();

    struct wlr_presentation *presentation =
        wlr_presentation_create(server->display, wlroots->backend);
    if (presentation) {
        wlr_scene_set_presentation(wlroots->scene, presentation);
    }

    wlr_screencopy_manager_v1_create(server->display);
    wlr_export_dmabuf_manager_v1_create(server->display);

#if HAVE_XWAYLAND
    wlroots->xwayland = wlr_xwayland_create(server->display, wlroots->compositor, true);
    if (!wlroots->xwayland) {
        kywc_log(KYWC_ERROR, "cannot create xwayland server");
        unsetenv("DISPLAY");
    } else {
        wlroots->xwayland_ready.notify = handle_xwayland_ready;
        wl_signal_add(&wlroots->xwayland->events.ready, &wlroots->xwayland_ready);

        setenv("DISPLAY", wlroots->xwayland->display_name, true);
        kywc_log(KYWC_INFO, "xwayland is running on display %s", wlroots->xwayland->display_name);
    }
#endif

    return true;
}

static bool wlroots_server_start(struct server *server)
{
    kywc_log(KYWC_INFO, "adapter: start wlroots server");
    struct wlroots_server *wlroots = wlroots_server_from_server(server);

    if (!wlr_backend_start(wlroots->backend)) {
        kywc_log(KYWC_ERROR, "unable to start the wlroots backend");
        return false;
    }

    return true;
}

static void wlroots_server_finish(struct server *server)
{
    kywc_log(KYWC_INFO, "adapter: finish wlroots server");
    struct wlroots_server *wlroots = wlroots_server_from_server(server);

#if HAVE_XWAYLAND
    wlr_xwayland_destroy(wlroots->xwayland);
#endif
    if (wlroots->scene) {
        wlr_scene_node_destroy(&wlroots->scene->tree.node);
    }
    wlr_output_layout_destroy(wlroots->layout);
    wlr_allocator_destroy(wlroots->allocator);
    wlr_renderer_destroy(wlroots->renderer);

    server->impl = server->data = NULL;
    free(wlroots);
}

static const struct server_impl wlroots_server_impl = {
    .init = wlroots_server_init,
    .start = wlroots_server_start,
    .finish = wlroots_server_finish,
};

struct wlroots_server *wlroots_server_from_server(struct server *server)
{
    assert(server->impl == &wlroots_server_impl);
    return server->data;
}

bool wlroots_adapter_init(struct server *server)
{
    kywc_log(KYWC_INFO, "adapter: init wlroots server");

    struct wlroots_server *wlroots = NULL;
    wlroots = calloc(1, sizeof(struct wlroots_server));
    if (!wlroots) {
        return false;
    }

    wlroots->server = server;
    server->data = wlroots;
    server->impl = &wlroots_server_impl;

    /* verbosity is not used when we replaced log_callback */
    wlr_log_init(WLR_DEBUG, kywc_log_callback);

    return true;
}
