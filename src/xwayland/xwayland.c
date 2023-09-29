// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

#include <wlr/types/wlr_xcursor_manager.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "server.h"
#include "xwayland_p.h"

static const char *const atom_map[ATOM_LAST] = {
    [NET_WM_WINDOW_TYPE_DESKTOP] = "_NET_WM_WINDOW_TYPE_DESKTOP",
    [NET_WM_WINDOW_TYPE_DOCK] = "_NET_WM_WINDOW_TYPE_DOCK",
    [NET_WM_WINDOW_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
    [NET_WM_WINDOW_TYPE_TOOLBAR] = "_NET_WM_WINDOW_TYPE_TOOLBAR",
};

static struct xwayland_server *xwayland = NULL;

static void handle_new_xwayland_surface(struct wl_listener *listener, void *data)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = data;
    wlr_xwayland_surface_ping(wlr_xwayland_surface);

    if (wlr_xwayland_surface->override_redirect) {
        xwayland_unmanaged_create(xwayland, wlr_xwayland_surface);
        return;
    }

    xwayland_view_create(xwayland, wlr_xwayland_surface);
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data)
{
    kywc_log(KYWC_INFO, "xwayland is ready");
    struct seat *seat = input_manager_get_default_seat();
    wlr_xwayland_set_seat(xwayland->wlr_xwayland, seat->wlr_seat);

    xcb_connection_t *xcb_conn = xcb_connect(NULL, NULL);
    int err = xcb_connection_has_error(xcb_conn);
    if (err) {
        kywc_log(KYWC_ERROR, "XCB connect failed: %d", err);
        return;
    }

    xcb_intern_atom_cookie_t cookies[ATOM_LAST];
    for (size_t i = 0; i < ATOM_LAST; i++) {
        cookies[i] = xcb_intern_atom(xcb_conn, 0, strlen(atom_map[i]), atom_map[i]);
    }
    for (size_t i = 0; i < ATOM_LAST; i++) {
        xcb_generic_error_t *error = NULL;
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(xcb_conn, cookies[i], &error);
        if (reply != NULL && error == NULL) {
            xwayland->atoms[i] = reply->atom;
        }
        free(reply);

        if (error != NULL) {
            kywc_log(KYWC_ERROR, "could not resolve atom %s, X11 error code %d", atom_map[i],
                     error->error_code);
            free(error);
            break;
        }
    }

    xcb_disconnect(xcb_conn);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&xwayland->server_destroy.link);
    free(xwayland);
    xwayland = NULL;
}

bool xwayland_server_create(struct server *server)
{
    if (!server->options.enable_xwayland) {
        kywc_log(KYWC_INFO, "xwayland is disabled by cmdline");
        return false;
    }

    xwayland = calloc(1, sizeof(struct xwayland_server));
    if (!xwayland) {
        kywc_log(KYWC_ERROR, "cannot create xwayland server");
        return false;
    }

    xwayland->wlr_xwayland = wlr_xwayland_create(server->display, server->compositor, true);
    if (!xwayland->wlr_xwayland) {
        kywc_log(KYWC_ERROR, "cannot create wlroots xwayland server");
        free(xwayland);
        xwayland = NULL;
        return false;
    }

    xwayland->scale = 1.0;
    wl_list_init(&xwayland->unmanaged_surfaces);

    xwayland->new_xwayland_surface.notify = handle_new_xwayland_surface;
    wl_signal_add(&xwayland->wlr_xwayland->events.new_surface, &xwayland->new_xwayland_surface);
    xwayland->xwayland_ready.notify = handle_xwayland_ready;
    wl_signal_add(&xwayland->wlr_xwayland->events.ready, &xwayland->xwayland_ready);
    xwayland->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &xwayland->server_destroy);

    setenv("DISPLAY", xwayland->wlr_xwayland->display_name, true);
    kywc_log(KYWC_INFO, "xwayland is running on display %s", xwayland->wlr_xwayland->display_name);

    /* set xwayland cursor, use the default seat0 */
    struct seat *seat = input_manager_get_default_seat();
    wlr_xcursor_manager_load(seat->cursor->xcursor_manager, 1.0);
    struct wlr_xcursor *xcursor =
        wlr_xcursor_manager_get_xcursor(seat->cursor->xcursor_manager, "left_ptr", 1);
    if (xcursor) {
        struct wlr_xcursor_image *image = xcursor->images[0];
        wlr_xwayland_set_cursor(xwayland->wlr_xwayland, image->buffer, image->width * 4,
                                image->width, image->height, image->hotspot_x, image->hotspot_y);
    }

    return true;
}

void xwayland_server_destroy(void)
{
    if (xwayland) {
        wlr_xwayland_destroy(xwayland->wlr_xwayland);
    }
}

bool xwayland_check_client(struct wl_client *client)
{
    return xwayland && xwayland->wlr_xwayland->server &&
           xwayland->wlr_xwayland->server->client == client;
}

float xwayland_get_scale(void)
{
    return xwayland ? xwayland->scale : 1.0;
}

void xwayland_set_scale(float scale)
{
    if (!xwayland || xwayland->scale == scale) {
        return;
    }

    xwayland->scale = scale;
}

float xwayland_unscale(int value)
{
    return xwayland ? roundf(value / xwayland->scale) : value;
}

float xwayland_scale(int value)
{
    return xwayland ? value * xwayland->scale : value;
}
