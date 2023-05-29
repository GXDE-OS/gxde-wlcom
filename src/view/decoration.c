#include <stdlib.h>

#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "view_p.h"

struct view_deco {
    struct view *view;
    struct wl_listener destroy;
    struct wl_listener request_mode;
};

static void view_deco_destroy(struct wl_listener *listener, void *data)
{
    struct view_deco *view_deco = wl_container_of(listener, view_deco, destroy);
    wl_list_remove(&view_deco->destroy.link);
    wl_list_remove(&view_deco->request_mode.link);
    free(view_deco);
}

static void xdg_deco_request_mode(struct wl_listener *listener, void *data)
{
    struct view_deco *view_deco = wl_container_of(listener, view_deco, request_mode);
    struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;
    enum wlr_xdg_toplevel_decoration_v1_mode mode = wlr_decoration->requested_mode;

    if (mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE) {
        mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    }

    kywc_log(KYWC_DEBUG, "xdg-decoration mode is %d", mode);
    wlr_xdg_toplevel_decoration_v1_set_mode(wlr_decoration, mode);
    view_set_decoration(view_deco->view, mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void xdg_toplevel_decoration(struct wl_listener *listener, void *data)
{
    struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;
    struct view *view = wlr_decoration->surface->surface->data;
    if (!view) {
        return;
    }

    struct view_deco *view_deco = calloc(1, sizeof(struct view_deco));
    if (!view_deco) {
        return;
    }

    view_deco->view = view;
    view_deco->destroy.notify = view_deco_destroy;
    wl_signal_add(&wlr_decoration->events.destroy, &view_deco->destroy);
    view_deco->request_mode.notify = xdg_deco_request_mode;
    wl_signal_add(&wlr_decoration->events.request_mode, &view_deco->request_mode);

    xdg_deco_request_mode(&view_deco->request_mode, wlr_decoration);
}

static void server_deco_apply_mode(struct wl_listener *listener, void *data)
{
    struct view_deco *view_deco = wl_container_of(listener, view_deco, request_mode);
    struct wlr_server_decoration *wlr_decoration = data;
    uint32_t mode = wlr_decoration->mode;

    kywc_log(KYWC_DEBUG, "server decoration mode is %d", mode);
    view_set_decoration(view_deco->view, mode == WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
}

static void server_decoration(struct wl_listener *listener, void *data)
{
    struct wlr_server_decoration *wlr_decoration = data;
    struct view *view = wlr_decoration->surface->data;
    if (!view) {
        return;
    }

    struct view_deco *view_deco = calloc(1, sizeof(struct view_deco));
    if (!view_deco) {
        return;
    }

    view_deco->view = view;
    view_deco->destroy.notify = view_deco_destroy;
    wl_signal_add(&wlr_decoration->events.destroy, &view_deco->destroy);
    view_deco->request_mode.notify = server_deco_apply_mode;
    wl_signal_add(&wlr_decoration->events.mode, &view_deco->request_mode);
}

void decoration_init(struct view_manager *view_manager)
{
    struct server *server = view_manager->server;
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(server->display);
    if (!xdg_decoration_manager) {
        kywc_log(KYWC_ERROR, "unable to create the XDG deco manager");
    } else {
        view_manager->xdg_toplevel_decoration.notify = xdg_toplevel_decoration;
        wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration,
                      &view_manager->xdg_toplevel_decoration);
    }

    struct wlr_server_decoration_manager *server_decoration_manager =
        wlr_server_decoration_manager_create(server->display);
    if (!server_decoration_manager) {
        kywc_log(KYWC_ERROR, "unable to create the server deco manager");
    } else {
        uint32_t default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_NONE;
        wlr_server_decoration_manager_set_default_mode(server_decoration_manager, default_mode);
        view_manager->server_decoration.notify = server_decoration;
        wl_signal_add(&server_decoration_manager->events.new_decoration,
                      &view_manager->server_decoration);
    }
}
