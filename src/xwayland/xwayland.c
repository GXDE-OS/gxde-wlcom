// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <xcb/shape.h>

#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/xwayland/shell.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "security.h"
#include "server.h"
#include "xwayland_p.h"

static const char *const atom_map[ATOM_LAST] = {
    [NET_WM_WINDOW_TYPE_DESKTOP] = "_NET_WM_WINDOW_TYPE_DESKTOP",
    [NET_WM_WINDOW_TYPE_DOCK] = "_NET_WM_WINDOW_TYPE_DOCK",
    [NET_WM_WINDOW_TYPE_TOOLBAR] = "_NET_WM_WINDOW_TYPE_TOOLBAR",
    [NET_WM_WINDOW_TYPE_MENU] = "_NET_WM_WINDOW_TYPE_MENU",
    [NET_WM_WINDOW_TYPE_UTILITY] = "_NET_WM_WINDOW_TYPE_UTILITY",
    [NET_WM_WINDOW_TYPE_SPLASH] = "_NET_WM_WINDOW_TYPE_SPLASH",
    [NET_WM_WINDOW_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
    [NET_WM_WINDOW_TYPE_DROPDOWN_MENU] = "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
    [NET_WM_WINDOW_TYPE_POPUP_MENU] = "_NET_WM_WINDOW_TYPE_POPUP_MENU",
    [NET_WM_WINDOW_TYPE_TOOLTIP] = "_NET_WM_WINDOW_TYPE_TOOLTIP",
    [NET_WM_WINDOW_TYPE_NOTIFICATION] = "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    [NET_WM_WINDOW_TYPE_NORMAL] = "_NET_WM_WINDOW_TYPE_NORMAL",

    [UKUI_NET_WM_WINDOW_TYPE_SYSTEMWINDOW] = "_UKUI_NET_WM_WINDOW_TYPE_SYSTEMWINDOW",
    [UKUI_NET_WM_WINDOW_TYPE_INPUTPANEL] = "_UKUI_NET_WM_WINDOW_TYPE_INPUTPANEL",
    [UKUI_NET_WM_WINDOW_TYPE_LOGOUT] = "_UKUI_NET_WM_WINDOW_TYPE_LOGOUT",
    [UKUI_NET_WM_WINDOW_TYPE_SCREENLOCK] = "_UKUI_NET_WM_WINDOW_TYPE_SCREENLOCK",
    [UKUI_NET_WM_WINDOW_TYPE_SCREENLOCKNOTIFICATION] =
        "_UKUI_NET_WM_WINDOW_TYPE_SCREENLOCKNOTIFICATION",
    [UKUI_NET_WM_WINDOW_TYPE_WATERMARK] = "_UKUI_NET_WM_WINDOW_TYPE_WATERMARK",

    [NET_WM_STATE] = "_NET_WM_STATE",
    [NET_WM_STATE_ABOVE] = "_NET_WM_STATE_ABOVE",
    [NET_WM_STATE_BELOW] = "_NET_WM_STATE_BELOW",
};

static struct xwayland_server *xwayland = NULL;

static void handle_new_xwayland_surface(struct wl_listener *listener, void *data)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = data;
    // wlr_xwayland_surface_ping(wlr_xwayland_surface);

    if (xwayland->shape) {
        xcb_shape_select_input(xwayland->xcb_conn, wlr_xwayland_surface->window_id, true);
        xcb_flush(xwayland->xcb_conn);
    }

    if (wlr_xwayland_surface->override_redirect) {
        xwayland_unmanaged_create(xwayland, wlr_xwayland_surface);
        return;
    }

    xwayland_view_create(xwayland, wlr_xwayland_surface);
}

static void xwayland_update_dpi(xcb_connection_t *xcb_conn)
{
    /* get current props */
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(xcb_conn)).data;
    xcb_get_property_cookie_t cookie = xcb_get_property(
        xcb_conn, 0, screen->root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 8192);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(xcb_conn, cookie, NULL);

    char *props = xcb_get_property_value(reply);
    size_t len = xcb_get_property_value_length(reply);
    len = strnlen(props, len);

    char dpi_str[16];
    snprintf(dpi_str, 16, "Xft.dpi:\t%d\n", (int)(xwayland->scale * 96));
    size_t dpi_str_len = strlen(dpi_str);

    char *prop_str = NULL, *p = NULL;
    /* if no dpi prop found, we can just append it */
    bool has_dpi = len > 0 && (p = strstr(props, "Xft.dpi:"));

    /* replace the dpi prop with new value */
    if (has_dpi && (prop_str = malloc(len + 8))) {
        size_t left = p - props;
        size_t offset = 0;
        memcpy(prop_str, props, left);
        offset += left;
        memcpy(prop_str + offset, dpi_str, dpi_str_len);
        offset += dpi_str_len;
        while (*p != '\n') {
            p++;
        }
        left = len - (++p - props);
        memcpy(prop_str + offset, p, left);
        offset += left;
        prop_str[offset] = '\0';
    } else {
        prop_str = dpi_str;
        has_dpi = false;
    }

    free(reply);
    xcb_change_property(xcb_conn, has_dpi ? XCB_PROP_MODE_REPLACE : XCB_PROP_MODE_APPEND,
                        screen->root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 8,
                        strlen(prop_str), prop_str);
    xcb_flush(xcb_conn);

    if (has_dpi) {
        free(prop_str);
    }
}

static void handle_output_configured(struct wl_listener *listener, void *data)
{
    float scale = output_manager_get_scale();
    if (xwayland->scale == scale) {
        return;
    }

    xwayland->scale = scale;
    kywc_log(KYWC_INFO, "xwayland set scale to %f", xwayland->scale);

    /* xwayland server is destroyed or not ready */
    if (!xwayland->wlr_xwayland || !xwayland->wlr_xwayland->xwm) {
        return;
    }

    output_manager_update_scale(xwayland->scale);
    xwayland_update_dpi(xwayland->xcb_conn);

    /* update default cursor with current scale */
    xwayland_set_cursor(seat_from_wlr_seat(xwayland->wlr_xwayland->seat));
}

static void xwayland_get_atoms(xcb_connection_t *xcb_conn)
{
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
}

static void xwayland_get_resources(xcb_connection_t *xcb_conn)
{
    xcb_prefetch_extension_data(xcb_conn, &xcb_shape_id);

    xwayland_get_atoms(xcb_conn);

    xwayland->shape = xcb_get_extension_data(xcb_conn, &xcb_shape_id);
    if (!xwayland->shape || !xwayland->shape->present) {
        kywc_log(KYWC_WARN, "shape not available");
        return;
    }

    xcb_shape_query_version_cookie_t shape_cookie;
    xcb_shape_query_version_reply_t *shape_reply;
    shape_cookie = xcb_shape_query_version(xcb_conn);
    shape_reply = xcb_shape_query_version_reply(xcb_conn, shape_cookie, NULL);

    kywc_log(KYWC_DEBUG, "shape version: %" PRIu32 ".%" PRIu32, shape_reply->major_version,
             shape_reply->minor_version);
    free(shape_reply);
}

static void xwayland_handle_shape_notify(xcb_shape_notify_event_t *notify)
{
    if (notify->shape_kind != XCB_SHAPE_SK_BOUNDING) {
        return;
    }

    xcb_shape_get_rectangles_reply_t *reply = xcb_shape_get_rectangles_reply(
        xwayland->xcb_conn,
        xcb_shape_get_rectangles_unchecked(xwayland->xcb_conn, notify->affected_window,
                                           notify->shape_kind),
        NULL);
    if (!reply) {
        return;
    }

    const xcb_rectangle_t *rects = xcb_shape_get_rectangles_rectangles(reply);
    const int count = xcb_shape_get_rectangles_rectangles_length(reply);
    xwayland_unmanaged_set_shape_region(xwayland, notify->affected_window, rects, count);
    free(reply);
}

static int xwayland_event_handler(int fd, uint32_t mask, void *data)
{
    int count = 0;
    xcb_generic_event_t *event;

    if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
        kywc_log(KYWC_ERROR, "xwayland is crashed");
        wl_event_source_remove(xwayland->event_source);
        return 0;
    }

    while ((event = xcb_poll_for_event(xwayland->xcb_conn))) {
        count++;

        const uint8_t response_type = event->response_type & 0x7f;
        if (response_type == xwayland->shape->first_event + XCB_SHAPE_NOTIFY) {
            xwayland_handle_shape_notify((xcb_shape_notify_event_t *)event);
        }

        free(event);
    }

    if (count) {
        xcb_flush(xwayland->xcb_conn);
    }

    return count;
}

void xwayland_set_cursor(struct seat *seat)
{
    if (xwayland->wlr_xwayland->seat != seat->wlr_seat) {
        return;
    }

    wlr_xcursor_manager_load(seat->cursor->xcursor_manager, xwayland->scale);
    struct wlr_xcursor *xcursor =
        wlr_xcursor_manager_get_xcursor(seat->cursor->xcursor_manager, "left_ptr", xwayland->scale);
    if (xcursor) {
        struct wlr_xcursor_image *image = xcursor->images[0];
        wlr_xwayland_set_cursor(xwayland->wlr_xwayland, image->buffer, image->width * 4,
                                image->width, image->height, image->hotspot_x, image->hotspot_y);
    }
}

void xwayland_update_seat(struct seat *seat)
{
    if (xwayland->wlr_xwayland->seat == seat->wlr_seat) {
        return;
    }

    wlr_xwayland_set_seat(xwayland->wlr_xwayland, seat->wlr_seat);
    /* update xwayland cursor */
    xwayland_set_cursor(seat);
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data)
{
    kywc_log(KYWC_INFO, "xwayland is ready");

    xwayland->xcb_conn = xcb_connect(NULL, NULL);
    int err = xcb_connection_has_error(xwayland->xcb_conn);
    if (err) {
        kywc_log(KYWC_ERROR, "XCB connect failed: %d", err);
        return;
    }

    xwayland->event_source =
        wl_event_loop_add_fd(wl_display_get_event_loop(xwayland->server->display),
                             xcb_get_file_descriptor(xwayland->xcb_conn), WL_EVENT_READABLE,
                             xwayland_event_handler, NULL);
    wl_event_source_check(xwayland->event_source);

    /* use the default seat0 */
    xwayland_update_seat(input_manager_get_default_seat());

    /* set xft.dpi */
    xwayland_update_dpi(xwayland->xcb_conn);

    xwayland_get_resources(xwayland->xcb_conn);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&xwayland->server_destroy.link);
    wl_list_remove(&xwayland->output_configured.link);
    free(xwayland);
    xwayland = NULL;
}

static bool xwayland_filter_global(const struct security_client *client, void *data)
{
    /* only expose this global to Xwayland clients */
    return xwayland_check_client(client->client);
}

/* return 0 as we only handle few things */
static int xwayland_handle_event(struct wlr_xwm *xwm, xcb_generic_event_t *event)
{
    if ((event->response_type & 0x7f) != XCB_PROPERTY_NOTIFY) {
        return 0;
    }

    xcb_property_notify_event_t *ev = (xcb_property_notify_event_t *)event;
    if (ev->atom != xwayland->atoms[NET_WM_STATE]) {
        return 0;
    }

    xcb_get_property_cookie_t cookie =
        xcb_get_property(xwayland->xcb_conn, 0, ev->window, ev->atom, XCB_ATOM_ANY, 0, 2048);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(xwayland->xcb_conn, cookie, NULL);
    if (reply == NULL) {
        kywc_log(KYWC_ERROR, "Failed to get window property");
        return 0;
    }

    /* nothing to be handled */
    if (reply->value_len == 0) {
        free(reply);
        return 1;
    }

    xcb_atom_t *atom = xcb_get_property_value(reply);
    bool keep_above, keep_below;

    for (uint32_t i = 0; i < reply->value_len; i++) {
        keep_above = atom[i] == xwayland->atoms[NET_WM_STATE_ABOVE];
        keep_below = atom[i] == xwayland->atoms[NET_WM_STATE_BELOW];
        if (!keep_above && !keep_below) {
            continue;
        }

        struct wlr_xwayland_surface *surface = xwayland_view_look_surface(xwayland, ev->window);
        if (surface) {
            xwayland_view_set_above_or_below(surface, keep_above, keep_below);
        }

        /* return 1 if we handle all things in the event */
        if (reply->value_len == 1) {
            free(reply);
            return 1;
        }

        break;
    }

    free(reply);
    return 0;
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
    xwayland->server = server;
    wl_list_init(&xwayland->surfaces);
    wl_list_init(&xwayland->unmanaged_surfaces);
    xwayland->wlr_xwayland->user_event_handler = xwayland_handle_event;

    xwayland->new_xwayland_surface.notify = handle_new_xwayland_surface;
    wl_signal_add(&xwayland->wlr_xwayland->events.new_surface, &xwayland->new_xwayland_surface);
    xwayland->xwayland_ready.notify = handle_xwayland_ready;
    wl_signal_add(&xwayland->wlr_xwayland->events.ready, &xwayland->xwayland_ready);
    xwayland->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &xwayland->server_destroy);
    xwayland->output_configured.notify = handle_output_configured;
    output_manager_add_configured_listener(&xwayland->output_configured);

    security_add_global_filter(xwayland->wlr_xwayland->shell_v1->global, xwayland_filter_global,
                               xwayland->wlr_xwayland);

    setenv("DISPLAY", xwayland->wlr_xwayland->display_name, true);
    kywc_log(KYWC_INFO, "xwayland is running on display %s", xwayland->wlr_xwayland->display_name);

    return true;
}

void xwayland_server_destroy(void)
{
    if (xwayland) {
        wlr_xwayland_destroy(xwayland->wlr_xwayland);
        xwayland->wlr_xwayland = NULL;
    }
}

bool xwayland_check_client(const struct wl_client *client)
{
    return xwayland && xwayland->wlr_xwayland->server &&
           xwayland->wlr_xwayland->server->client == client;
}

int xwayland_unscale(int value)
{
    float val = xwayland ? roundf(value / xwayland->scale) : value;
    /* check overflow for int32_t */
    return !(val > INT32_MIN && val < INT32_MAX) ? 0 : val;
}

float xwayland_scale(int value)
{
    return xwayland ? value * xwayland->scale : value;
}

bool xwayland_surface_has_type(struct wlr_xwayland_surface *wlr_xwayland_surface, int type)
{
    for (size_t i = 0; i < wlr_xwayland_surface->window_type_len; ++i) {
        xcb_atom_t atom = wlr_xwayland_surface->window_type[i];
        if (atom == xwayland->atoms[type]) {
            return true;
        }
    }
    return false;
}

bool xwayland_surface_has_input(struct wlr_xwayland_surface *wlr_xwayland_surface)
{
    xcb_get_window_attributes_reply_t *reply = xcb_get_window_attributes_reply(
        xwayland->xcb_conn,
        xcb_get_window_attributes_unchecked(xwayland->xcb_conn, wlr_xwayland_surface->window_id),
        NULL);
    if (!reply) {
        return true;
    }

    uint32_t input_mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                          XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                          XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_ENTER_WINDOW |
                          XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_POINTER_MOTION;
    bool has_input = reply->all_event_masks & input_mask;
    free(reply);

    return has_input;
}

static char *xwayland_get_atom_name(xcb_atom_t atom)
{
    xcb_get_atom_name_cookie_t name_cookie = xcb_get_atom_name(xwayland->xcb_conn, atom);
    xcb_get_atom_name_reply_t *name_reply =
        xcb_get_atom_name_reply(xwayland->xcb_conn, name_cookie, NULL);
    if (name_reply == NULL) {
        return NULL;
    }
    size_t len = xcb_get_atom_name_name_length(name_reply);
    char *buf = xcb_get_atom_name_name(name_reply); // not a C string
    char *name = strndup(buf, len);
    free(name_reply);
    return name;
}

void xwayland_surface_debug_type(struct wlr_xwayland_surface *wlr_xwayland_surface)
{
    for (size_t i = 0; i < wlr_xwayland_surface->window_type_len; ++i) {
        xcb_atom_t atom = wlr_xwayland_surface->window_type[i];
        char *atom_name = xwayland_get_atom_name(atom);
        kywc_log(KYWC_INFO, "%s: type atom %s %ld(%ld)", wlr_xwayland_surface->class, atom_name, i,
                 wlr_xwayland_surface->window_type_len);
        free(atom_name);
    }
    kywc_log(KYWC_INFO, "%s: OR %d size %d x %d %d", wlr_xwayland_surface->class,
             wlr_xwayland_surface->override_redirect, wlr_xwayland_surface->width,
             wlr_xwayland_surface->height, wlr_xwayland_surface->fullscreen);
}
