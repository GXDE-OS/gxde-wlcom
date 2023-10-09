// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "view_p.h"

struct decoration_manager {
    struct wl_list decos;

    struct wl_listener xdg_toplevel_decoration;
    struct wl_listener server_decoration;
    struct wl_listener server_destroy;
};

struct decoration {
    struct wl_list link;

    struct wlr_surface *surface;
    struct wl_listener surface_map;

    struct wlr_server_decoration *server_deco;
    struct wl_listener server_deco_apply_mode;
    struct wl_listener server_deco_destroy;

    struct wlr_xdg_toplevel_decoration_v1 *xdg_deco;
    struct wl_listener xdg_deco_request_mode;
    struct wl_listener xdg_deco_destroy;

    struct view *view;
    bool should_use_ssd;
};

static struct decoration_manager *manager = NULL;

static void decoration_handle_surface_map(struct wl_listener *listener, void *data)
{
    struct decoration *deco = wl_container_of(listener, deco, surface_map);

    wl_list_remove(&deco->surface_map.link);
    wl_list_init(&deco->surface_map.link);

    deco->view = view_try_from_wlr_surface(deco->surface);
    if (deco->view) {
        view_set_decoration(deco->view, deco->should_use_ssd);
    }
}

static struct decoration *decoration_from_surface(struct wlr_surface *surface)
{
    struct decoration *deco;
    wl_list_for_each(deco, &manager->decos, link) {
        if (deco->surface == surface) {
            return deco;
        }
    }

    deco = calloc(1, sizeof(struct decoration));
    if (!deco) {
        return NULL;
    }

    deco->surface = surface;
    deco->view = view_try_from_wlr_surface(surface);

    /* if xdg_shell new_surface is not emit */
    if (!deco->view) {
        deco->surface_map.notify = decoration_handle_surface_map;
        wl_signal_add(&surface->events.map, &deco->surface_map);
    } else {
        wl_list_init(&deco->surface_map.link);
    }

    wl_list_insert(&manager->decos, &deco->link);

    return deco;
}

static void decoration_consider_destroy(struct decoration *deco)
{
    if (deco->xdg_deco || deco->server_deco) {
        return;
    }

    wl_list_remove(&deco->surface_map.link);
    wl_list_remove(&deco->link);
    free(deco);
}

static void handle_server_deco_destroy(struct wl_listener *listener, void *data)
{
    struct decoration *deco = wl_container_of(listener, deco, server_deco_destroy);

    wl_list_remove(&deco->server_deco_destroy.link);
    wl_list_remove(&deco->server_deco_apply_mode.link);
    deco->server_deco = NULL;

    decoration_consider_destroy(deco);
}

static void handle_xdg_deco_destroy(struct wl_listener *listener, void *data)
{
    struct decoration *deco = wl_container_of(listener, deco, xdg_deco_destroy);

    wl_list_remove(&deco->xdg_deco_destroy.link);
    wl_list_remove(&deco->xdg_deco_request_mode.link);
    deco->xdg_deco = NULL;

    decoration_consider_destroy(deco);
}

static void handle_xdg_deco_request_mode(struct wl_listener *listener, void *data)
{
    struct decoration *deco = wl_container_of(listener, deco, xdg_deco_request_mode);
    struct wlr_xdg_toplevel_decoration_v1 *wlr_xdg_decoration = data;
    enum wlr_xdg_toplevel_decoration_v1_mode mode = wlr_xdg_decoration->requested_mode;

    if (mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE) {
        mode = ((deco->server_deco || deco->xdg_deco) && !deco->should_use_ssd)
                   ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                   : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    }

    kywc_log(KYWC_DEBUG, "surface %p xdg-decoration mode is %d", deco->surface, mode);
    wlr_xdg_toplevel_decoration_v1_set_mode(wlr_xdg_decoration, mode);
    deco->should_use_ssd = mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

    if (deco->server_deco) {
        uint32_t server_mode = deco->should_use_ssd ? WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
                                                    : WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
        if (deco->server_deco->mode != server_mode) {
            deco->server_deco->mode = server_mode;
            /* org_kde_kwin_server_decoration_send_mode */
            wl_resource_post_event(deco->server_deco->resource, 0, server_mode);
        }
    }

    if (deco->view) {
        view_set_decoration(deco->view, deco->should_use_ssd);
    }
}

static void xdg_toplevel_decoration(struct wl_listener *listener, void *data)
{
    struct wlr_xdg_toplevel_decoration_v1 *wlr_xdg_decoration = data;
    struct wlr_surface *surface = wlr_xdg_decoration->toplevel->base->surface;

    struct decoration *deco = decoration_from_surface(surface);
    if (!deco) {
        return;
    }

    deco->xdg_deco_destroy.notify = handle_xdg_deco_destroy;
    wl_signal_add(&wlr_xdg_decoration->events.destroy, &deco->xdg_deco_destroy);
    deco->xdg_deco_request_mode.notify = handle_xdg_deco_request_mode;
    wl_signal_add(&wlr_xdg_decoration->events.request_mode, &deco->xdg_deco_request_mode);

    handle_xdg_deco_request_mode(&deco->xdg_deco_request_mode, wlr_xdg_decoration);

    deco->xdg_deco = wlr_xdg_decoration;
}

static void handle_server_deco_apply_mode(struct wl_listener *listener, void *data)
{
    struct decoration *deco = wl_container_of(listener, deco, server_deco_apply_mode);
    struct wlr_server_decoration *wlr_server_decoration = data;
    uint32_t mode = wlr_server_decoration->mode;

    kywc_log(KYWC_DEBUG, "surface %p server decoration mode is %d", deco->surface, mode);
    deco->should_use_ssd = mode == WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;

    if (deco->xdg_deco) {
        enum wlr_xdg_toplevel_decoration_v1_mode xdg_mode =
            deco->should_use_ssd ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                 : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        wlr_xdg_toplevel_decoration_v1_set_mode(deco->xdg_deco, xdg_mode);
    }

    if (deco->view) {
        view_set_decoration(deco->view, deco->should_use_ssd);
    }
}

static void server_decoration(struct wl_listener *listener, void *data)
{
    struct wlr_server_decoration *wlr_server_decoration = data;
    struct wlr_surface *surface = wlr_server_decoration->surface;

    struct decoration *deco = decoration_from_surface(surface);
    if (!deco) {
        return;
    }

    /* multi decoration for one surface, use the last one */
    if (deco->server_deco) {
        wl_list_remove(&deco->server_deco_destroy.link);
        wl_list_remove(&deco->server_deco_apply_mode.link);
    }

    deco->server_deco_destroy.notify = handle_server_deco_destroy;
    wl_signal_add(&wlr_server_decoration->events.destroy, &deco->server_deco_destroy);
    deco->server_deco_apply_mode.notify = handle_server_deco_apply_mode;
    wl_signal_add(&wlr_server_decoration->events.mode, &deco->server_deco_apply_mode);

    /* apply current deco type only when first time, may no mode signal */
    if (!deco->xdg_deco && !deco->server_deco) {
        handle_server_deco_apply_mode(&deco->server_deco_apply_mode, wlr_server_decoration);
    }

    deco->server_deco = wlr_server_decoration;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
    manager = NULL;
}

bool decoration_manager_create(struct view_manager *view_manager)
{
    manager = calloc(1, sizeof(struct decoration_manager));
    if (!manager) {
        return false;
    }

    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(view_manager->server->display);
    if (!xdg_decoration_manager) {
        kywc_log(KYWC_ERROR, "unable to create the XDG deco manager");
    } else {
        manager->xdg_toplevel_decoration.notify = xdg_toplevel_decoration;
        wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration,
                      &manager->xdg_toplevel_decoration);
    }

    struct wlr_server_decoration_manager *server_decoration_manager =
        wlr_server_decoration_manager_create(view_manager->server->display);
    if (!server_decoration_manager) {
        kywc_log(KYWC_ERROR, "unable to create the server deco manager");
    } else {
        uint32_t default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
        wlr_server_decoration_manager_set_default_mode(server_decoration_manager, default_mode);
        manager->server_decoration.notify = server_decoration;
        wl_signal_add(&server_decoration_manager->events.new_decoration,
                      &manager->server_decoration);
    }

    /* oops, all decoration manager create failed */
    if (!xdg_decoration_manager && !server_decoration_manager) {
        free(manager);
        return false;
    }

    wl_list_init(&manager->decos);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &manager->server_destroy);

    return true;
}
