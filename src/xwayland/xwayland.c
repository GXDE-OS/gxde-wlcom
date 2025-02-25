// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include <xcb/shape.h>
#include <xcb/xfixes.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/xwayland/shell.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "scene/surface.h"
#include "security.h"
#include "server.h"
#include "util/string.h"
#include "view/view.h"
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

    [KDE_NET_WM_WINDOW_TYPE_OVERRIDE] = "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE",

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
    [NET_WM_STATE_SKIP_TASKBAR] = "_NET_WM_STATE_SKIP_TASKBAR",
    [NET_WM_STATE_DEMANDS_ATTENTION] = "_NET_WM_STATE_DEMANDS_ATTENTION",
    [KDE_NET_WM_STATE_SKIP_SWITCHER] = "_KDE_NET_WM_STATE_SKIP_SWITCHER",

    [NET_WM_ICON] = "_NET_WM_ICON",
    [NET_WM_WINDOW_OPACITY] = "_NET_WM_WINDOW_OPACITY",

    [UTF8_STRING] = "UTF8_STRING",
    [NET_WM_NAME] = "_NET_WM_NAME",
    [NET_SUPPORTING_WM_CHECK] = "_NET_SUPPORTING_WM_CHECK",

    [INCR] = "INCR",
    [TEXT] = "TEXT",
    [WL_SELECTION] = "_WL_SELECTION",

    [DND_SELECTION] = "XdndSelection",
    [DND_AWARE] = "XdndAware",
    [DND_STATUS] = "XdndStatus",
    [DND_POSITION] = "XdndPosition",
    [DND_ENTER] = "XdndEnter",
    [DND_LEAVE] = "XdndLeave",
    [DND_DROP] = "XdndDrop",
    [DND_FINISHED] = "XdndFinished",
    [DND_PROXY] = "XdndProxy",
    [DND_TYPE_LIST] = "XdndTypeList",
    [DND_ACTION_MOVE] = "XdndActionMove",
    [DND_ACTION_COPY] = "XdndActionCopy",
    [DND_ACTION_ASK] = "XdndActionAsk",
    [DND_ACTION_PRIVATE] = "XdndActionPrivate",
};

static struct xwayland_server *xwayland = NULL;

static void handle_new_xwayland_surface(struct wl_listener *listener, void *data)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = data;
    /* dnd window catcher no need map */
    if (wlr_xwayland_surface->window_id == xwayland->window_catcher) {
        return;
    }

    // wlr_xwayland_surface_ping(wlr_xwayland_surface);

    if (wlr_xwayland_surface->override_redirect) {
        xwayland_unmanaged_create(xwayland, wlr_xwayland_surface);
        return;
    }

    xwayland_view_create(xwayland, wlr_xwayland_surface);
}

static void xwayland_update_xresources(xcb_connection_t *xcb_conn)
{
    struct seat *seat = seat_from_wlr_seat(xwayland->wlr_xwayland->seat);

    const char *prop_str =
        string_create("Xft.dpi:\t%d\nXcursor.size:\t%d\nXcursor.theme:\t%s\n",
                      (int)(xwayland->scale * 96), (int)(seat->state.cursor_size * xwayland->scale),
                      seat->state.cursor_theme ? seat->state.cursor_theme : "default");
    if (!prop_str) {
        return;
    }

    xcb_change_property(xcb_conn, XCB_PROP_MODE_REPLACE, xwayland->screen->root,
                        XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 8, strlen(prop_str), prop_str);
    xcb_flush(xcb_conn);

    free((void *)prop_str);
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

static void xwayland_get_shape_extension(xcb_connection_t *xcb_conn)
{
    xwayland->shape = xcb_get_extension_data(xcb_conn, &xcb_shape_id);
    if (!xwayland->shape || !xwayland->shape->present) {
        kywc_log(KYWC_WARN, "shape not available");
        xwayland->shape = NULL;
        return;
    }

    xcb_shape_query_version_cookie_t shape_cookie;
    xcb_shape_query_version_reply_t *shape_reply;
    shape_cookie = xcb_shape_query_version(xcb_conn);
    shape_reply = xcb_shape_query_version_reply(xcb_conn, shape_cookie, NULL);

    kywc_log(KYWC_INFO, "shape version: %" PRIu32 ".%" PRIu32, shape_reply->major_version,
             shape_reply->minor_version);
    free(shape_reply);
}

static void xwayland_get_xfixes_extension(xcb_connection_t *xcb_conn)
{
    xwayland->xfixes = xcb_get_extension_data(xcb_conn, &xcb_xfixes_id);
    if (!xwayland->xfixes || !xwayland->xfixes->present) {
        kywc_log(KYWC_WARN, "xfixes not available");
        xwayland->xfixes = NULL;
        return;
    }

    xcb_xfixes_query_version_cookie_t xfixes_cookie;
    xcb_xfixes_query_version_reply_t *xfixes_reply;
    xfixes_cookie =
        xcb_xfixes_query_version(xcb_conn, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);
    xfixes_reply = xcb_xfixes_query_version_reply(xcb_conn, xfixes_cookie, NULL);

    kywc_log(KYWC_INFO, "xfixes version: %" PRIu32 ".%" PRIu32, xfixes_reply->major_version,
             xfixes_reply->minor_version);
    free(xfixes_reply);
}

static void xwayland_get_resources(xcb_connection_t *xcb_conn)
{
    xcb_prefetch_extension_data(xcb_conn, &xcb_shape_id);

    xwayland_get_atoms(xcb_conn);

    /**
     * workaround to fix java apps show blank window
     * wlcom is not a reparenting window manager
     * or set _JAVA_AWT_WM_NONREPARENTING=1
     */
    const char name[] = "LG3D";
    xcb_change_property(xcb_conn, XCB_PROP_MODE_REPLACE, xwayland->screen->root,
                        xwayland->atoms[NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1,
                        &xwayland->screen->root);
    xcb_change_property(xcb_conn, XCB_PROP_MODE_REPLACE, xwayland->screen->root,
                        xwayland->atoms[NET_WM_NAME], xwayland->atoms[UTF8_STRING], 8, strlen(name),
                        name);

    xwayland_get_shape_extension(xcb_conn);
    xwayland_get_xfixes_extension(xcb_conn);
}

void xwayland_surface_shape_select_input(struct wlr_xwayland_surface *surface, bool enabled)
{
    if (xwayland->shape) {
        xcb_shape_select_input(xwayland->xcb_conn, surface->window_id, enabled);
        xcb_flush(xwayland->xcb_conn);
    }
}

static bool xwayland_get_shape_region(xcb_window_t window, xcb_shape_kind_t kind,
                                      pixman_region32_t *region, int *count)
{
    xcb_shape_get_rectangles_reply_t *reply = xcb_shape_get_rectangles_reply(
        xwayland->xcb_conn, xcb_shape_get_rectangles_unchecked(xwayland->xcb_conn, window, kind),
        NULL);
    if (!reply) {
        return false;
    }

    xcb_rectangle_t *rects = xcb_shape_get_rectangles_rectangles(reply);
    int len = xcb_shape_get_rectangles_rectangles_length(reply);
    if (region) {
        for (int i = 0; i < len; i++) {
            pixman_region32_union_rect(
                region, region, xwayland_unscale(rects[i].x), xwayland_unscale(rects[i].y),
                xwayland_unscale(rects[i].width), xwayland_unscale(rects[i].height));
        }
    }
    if (count) {
        *count = len;
    }

    free(reply);
    return true;
}

void xwayland_surface_apply_shape_region(struct wlr_xwayland_surface *surface)
{
    if (!xwayland->shape || !surface->surface) {
        return;
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(surface->surface);
    if (!buffer) {
        return;
    }

    xcb_shape_query_extents_reply_t *reply = xcb_shape_query_extents_reply(
        xwayland->xcb_conn,
        xcb_shape_query_extents_unchecked(xwayland->xcb_conn, surface->window_id), NULL);
    if (!reply) {
        return;
    }

    pixman_region32_t region;
    pixman_region32_init(&region);

    if (reply->clip_shaped) {
        if (xwayland_get_shape_region(surface->window_id, XCB_SHAPE_SK_CLIP, &region, NULL)) {
            ky_scene_node_set_clip_region(&buffer->node, &region);
        }
    } else if (reply->bounding_shaped) {
        if (xwayland_get_shape_region(surface->window_id, XCB_SHAPE_SK_BOUNDING, &region, NULL)) {
            ky_scene_node_set_clip_region(&buffer->node, &region);
            ky_scene_node_set_input_region(&buffer->node, &region);
        }
    }

    int count = 0;
    xwayland_get_shape_region(surface->window_id, XCB_SHAPE_SK_INPUT, NULL, &count);
    ky_scene_node_set_input_bypassed(&buffer->node, count == 0);

    pixman_region32_fini(&region);
    free(reply);
}

static int xwayland_handle_shape_notify(xcb_shape_notify_event_t *notify)
{
    pixman_region32_t region;
    pixman_region32_init(&region);

    if (!xwayland_get_shape_region(notify->affected_window, notify->shape_kind, &region, NULL)) {
        pixman_region32_fini(&region);
        return 1;
    }

    if (!xwayland_unmanaged_set_shape_region(xwayland, notify->affected_window, notify->shape_kind,
                                             &region)) {
        xwayland_view_set_shape_region(xwayland, notify->affected_window, notify->shape_kind,
                                       &region);
    }

    pixman_region32_fini(&region);
    return 1;
}

void xwayland_set_cursor(struct seat *seat)
{
    if (!xwayland || !xwayland->wlr_xwayland || xwayland->wlr_xwayland->seat != seat->wlr_seat) {
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

    xwayland_update_xresources(xwayland->xcb_conn);
}

void xwayland_update_seat(struct seat *seat)
{
    if (!xwayland->wlr_xwayland || xwayland->wlr_xwayland->seat == seat->wlr_seat) {
        return;
    }

    wlr_xwayland_set_seat(xwayland->wlr_xwayland, seat->wlr_seat);
    wl_list_remove(&xwayland->seat_destroy.link);
    wl_signal_add(&seat->events.destroy, &xwayland->seat_destroy);

    /* update xwayland cursor */
    xwayland_set_cursor(seat);
}

void xwayland_update_hovered_surface(struct wlr_surface *surface)
{
    if (!xwayland || xwayland->hoverd_surface == surface) {
        return;
    }

    if (xwayland->hoverd_surface) {
        wl_list_remove(&xwayland->surface_destroy.link);
    }

    xwayland->hoverd_surface = surface;
    wl_signal_add(&surface->events.destroy, &xwayland->surface_destroy);
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data)
{
    kywc_log(KYWC_INFO, "xwayland is ready");

    xwayland->xcb_conn = wlr_xwayland_get_xwm_connection(xwayland->wlr_xwayland);
    xcb_screen_iterator_t screen_iterator =
        xcb_setup_roots_iterator(xcb_get_setup(xwayland->xcb_conn));
    xwayland->screen = screen_iterator.data;

    /* use the default seat0 */
    xwayland_update_seat(input_manager_get_default_seat());

    xwayland_get_resources(xwayland->xcb_conn);

    // for selection
    int width = 0, height = 0;
    output_layout_get_size(&width, &height);
    xwayland_create_seletion_window(xwayland, &xwayland->window_catcher, 0, 0, width, height);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&xwayland->server_destroy.link);
    wl_list_remove(&xwayland->output_configured.link);
    wl_list_remove(&xwayland->surface_destroy.link);
    wl_list_remove(&xwayland->activate_view.link);
    free(xwayland);
    xwayland = NULL;
}

static bool xwayland_filter_global(const struct security_client *client, void *data)
{
    /* only expose this global to Xwayland clients */
    return xwayland_check_client(client->client);
}

int xwayland_read_wm_state(xcb_window_t window_id)
{
    xcb_get_property_cookie_t cookie = xcb_get_property(
        xwayland->xcb_conn, 0, window_id, xwayland->atoms[NET_WM_STATE], XCB_ATOM_ANY, 0, 2048);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(xwayland->xcb_conn, cookie, NULL);
    if (reply == NULL) {
        kywc_log(KYWC_ERROR, "Failed to get window state property");
        return 0;
    }

    /* nothing to be handled */
    if (reply->value_len == 0) {
        free(reply);
        return 1;
    }

    struct wlr_xwayland_surface *surface = xwayland_view_look_surface(xwayland, window_id);
    if (!surface) {
        free(reply);
        return 0;
    }

    xcb_atom_t *atoms = xcb_get_property_value(reply);
    int ret = 1;
    for (uint32_t i = 0; i < reply->value_len; i++) {
        if (atoms[i] == xwayland->atoms[NET_WM_STATE_ABOVE]) {
            xwayland_view_set_above_or_below(surface, true, true, false);
        } else if (atoms[i] == xwayland->atoms[NET_WM_STATE_BELOW]) {
            xwayland_view_set_above_or_below(surface, false, true, false);
        } else if (atoms[i] == xwayland->atoms[NET_WM_STATE_SKIP_TASKBAR]) {
            xwayland_view_set_skip_taskbar(surface, true);
        } else if (atoms[i] == xwayland->atoms[NET_WM_STATE_DEMANDS_ATTENTION]) {
            xwayland_view_set_demands_attention(surface, true, false);
        } else if (atoms[i] == xwayland->atoms[KDE_NET_WM_STATE_SKIP_SWITCHER]) {
            xwayland_view_set_skip_switcher(surface, true);
        } else {
            ret = 0;
        }
    }

    free(reply);
    return ret;
}

int xwayland_read_wm_icon(xcb_window_t window_id)
{
    xcb_get_property_cookie_t cookie =
        xcb_get_property(xwayland->xcb_conn, 0, window_id, xwayland->atoms[NET_WM_ICON],
                         XCB_ATOM_CARDINAL, 0, 0xffffffff);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(xwayland->xcb_conn, cookie, NULL);
    if (!reply || reply->value_len < 3 || reply->format != 32) {
        free(reply);
        return 0;
    }

    struct wlr_xwayland_surface *surface = xwayland_view_look_surface(xwayland, window_id);
    if (!surface || !surface->surface) {
        free(reply);
        return 0;
    }

    uint32_t *data = (uint32_t *)xcb_get_property_value(reply);
    for (unsigned int j = 0; j < reply->value_len - 2;) {
        uint32_t width = data[j++], height = data[j++];
        uint32_t size = width * height * sizeof(uint32_t);
        if (j + width * height > reply->value_len) {
            kywc_log(KYWC_WARN, "proposed size leads to out of bounds (%d x %d) ", width, height);
            break;
        }
        if (width > 1024 || height > 1024) {
            kywc_log(KYWC_WARN, "found huge icon may be ill-encoded (%d x %d)", width, height);
        }

        xwayland_view_add_new_wm_icon(surface, width, height, size, &data[j]);
        j += width * height;
    }

    free(reply);
    return 1;
}

int xwayland_read_wm_window_opacity(xcb_window_t window_id)
{
    xcb_get_property_cookie_t cookie =
        xcb_get_property(xwayland->xcb_conn, 0, window_id, xwayland->atoms[NET_WM_WINDOW_OPACITY],
                         XCB_ATOM_CARDINAL, 0, 1);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(xwayland->xcb_conn, cookie, NULL);
    if (!reply || reply->value_len != 1 || reply->format != 32) {
        free(reply);
        return 0;
    }

    uint32_t *value = (uint32_t *)xcb_get_property_value(reply);
    float opacity = value[0] == 0xffffffff ? 1.0 : value[0] * 1.0 / 0xffffffff;
    free(reply);

    if (!xwayland_unmanaged_set_opacity(xwayland, window_id, opacity)) {
        return xwayland_view_set_opacity(xwayland, window_id, opacity);
    }

    return 1;
}

static int xwayland_handle_wm_state_message(xcb_client_message_event_t *client_message)
{
    struct wlr_xwayland_surface *surface =
        xwayland_view_look_surface(xwayland, client_message->window);
    if (!surface || client_message->format != 32) {
        return 0;
    }

    int ret = 0;
    uint32_t action = client_message->data.data32[0];
    bool state = action;
    bool toggle = action == 2 ? true : false;

    for (size_t i = 0; i < 2; i++) {
        xcb_atom_t property = client_message->data.data32[1 + i];
        if (property == xwayland->atoms[NET_WM_STATE_ABOVE]) {
            xwayland_view_set_above_or_below(surface, true, state, toggle);
            ret = 1;
        } else if (property == xwayland->atoms[NET_WM_STATE_BELOW]) {
            xwayland_view_set_above_or_below(surface, false, state, toggle);
            ret = 1;
        } else if (property == xwayland->atoms[NET_WM_STATE_DEMANDS_ATTENTION]) {
            xwayland_view_set_demands_attention(surface, state, toggle);
            ret = 1;
        }
    }

    return ret;
}

/* return 0 as we only handle few things */
static int xwayland_handle_event(struct wlr_xwm *xwm, xcb_generic_event_t *event)
{
    const uint8_t response_type = event->response_type & 0x7f;

    if (response_type == XCB_PROPERTY_NOTIFY) {
        xcb_property_notify_event_t *ev = (xcb_property_notify_event_t *)event;
        if (ev->atom == xwayland->atoms[NET_WM_STATE]) {
            return xwayland_read_wm_state(ev->window);
        } else if (ev->atom == xwayland->atoms[NET_WM_ICON]) {
            return xwayland_read_wm_icon(ev->window);
        } else if (ev->atom == xwayland->atoms[NET_WM_WINDOW_OPACITY]) {
            return xwayland_read_wm_window_opacity(ev->window);
        }
    } else if (response_type == XCB_CLIENT_MESSAGE) {
        xcb_client_message_event_t *ev = (xcb_client_message_event_t *)event;
        if (ev->type == xwayland->atoms[NET_WM_STATE]) {
            return xwayland_handle_wm_state_message((xcb_client_message_event_t *)event);
        } else if (xwayland_handle_dnd_message(xwayland, (xcb_client_message_event_t *)event)) {
            return 1;
        }
    } else if (xwayland->shape &&
               response_type == xwayland->shape->first_event + XCB_SHAPE_NOTIFY) {
        return xwayland_handle_shape_notify((xcb_shape_notify_event_t *)event);
    } else if (xwayland_handle_selection_event(xwayland, event)) {
        return 1;
    }

    return 0;
}

static void handle_surface_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&xwayland->surface_destroy.link);
    wl_list_init(&xwayland->surface_destroy.link);

    xwayland->hoverd_surface = NULL;
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&xwayland->seat_destroy.link);
    wl_list_init(&xwayland->seat_destroy.link);

    xwayland_update_seat(input_manager_get_default_seat());
}

static void handle_activate_view(struct wl_listener *listener, void *data)
{
    /* xwayland server is destroyed or not ready */
    if (!xwayland->wlr_xwayland || !xwayland->wlr_xwayland->xwm) {
        return;
    }
    if (!xwayland->activated_surface) {
        return;
    }

    struct kywc_view *view = data;
    if (view && xwayland_check_view(view_from_kywc_view(view))) {
        return;
    }

    wlr_xwayland_surface_activate(xwayland->activated_surface, false);
    xwayland->activated_surface = NULL;
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
    xwayland->seat_destroy.notify = handle_seat_destroy;
    wl_list_init(&xwayland->seat_destroy.link);
    xwayland->activate_view.notify = handle_activate_view;
    view_manager_add_activate_view_listener(&xwayland->activate_view);

    xwayland->surface_destroy.notify = handle_surface_destroy;
    wl_list_init(&xwayland->surface_destroy.link);

    security_add_global_filter(xwayland->wlr_xwayland->shell_v1->global, xwayland_filter_global,
                               xwayland->wlr_xwayland);

    setenv("DISPLAY", xwayland->wlr_xwayland->display_name, true);
    kywc_log(KYWC_INFO, "xwayland is running on display %s", xwayland->wlr_xwayland->display_name);

    return true;
}

void xwayland_server_destroy(void)
{
    if (!xwayland) {
        return;
    }

    wl_list_remove(&xwayland->xwayland_ready.link);
    wl_list_remove(&xwayland->new_xwayland_surface.link);
    wl_list_remove(&xwayland->seat_destroy.link);

    struct wlr_xwayland *wlr_xwayland = xwayland->wlr_xwayland;
    /* prevent xwayland_update_seat in hover */
    xwayland->wlr_xwayland = NULL;
    wlr_xwayland_destroy(wlr_xwayland);
}

bool xwayland_check_client(const struct wl_client *client)
{
    return xwayland && xwayland->wlr_xwayland && xwayland->wlr_xwayland->server &&
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

float xwayland_get_scale(void)
{
    return xwayland ? xwayland->scale : 1.0;
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

bool xwayland_surface_has_input(struct wlr_xwayland_surface *wlr_xwayland_surface, uint32_t input)
{
    xcb_get_window_attributes_reply_t *reply = xcb_get_window_attributes_reply(
        xwayland->xcb_conn,
        xcb_get_window_attributes_unchecked(xwayland->xcb_conn, wlr_xwayland_surface->window_id),
        NULL);
    if (!reply) {
        return true;
    }

    uint32_t input_mask = 0;
    if (input & INPUT_MASK_POINTER) {
        input_mask |= XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                      XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_ENTER_WINDOW |
                      XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_POINTER_MOTION;
    }
    if (input & INPUT_MASK_KEYBOARD) {
        input_mask |= XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;
    }

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

char *xwayland_mime_type_from_atom(xcb_atom_t atom)
{
    if (atom == xwayland->atoms[UTF8_STRING]) {
        return strdup("text/plain;charset=utf-8");
    } else if (atom == xwayland->atoms[TEXT]) {
        return strdup("text/plain");
    } else {
        return xwayland_get_atom_name(atom);
    }
}

void xwayland_fixup_pointer_position(struct wlr_surface *surface)
{
    struct wlr_seat *seat = xwayland->wlr_xwayland->seat;
    if (xwayland->hoverd_surface || !xwayland->wlr_xwayland->seat) {
        return;
    }

    struct wl_client *wl_client = wl_resource_get_client(surface->resource);
    struct wlr_seat_client *client = wlr_seat_client_for_wl_client(seat, wl_client);
    if (!client) {
        return;
    }

    uint32_t serial = wlr_seat_client_next_serial(client);
    struct wl_resource *resource;
    wl_resource_for_each(resource, &client->pointers) {
        if (!wlr_seat_client_from_pointer_resource(resource)) {
            continue;
        }

        wl_pointer_send_enter(resource, serial, surface->resource, wl_fixed_from_double(FLT_MAX),
                              wl_fixed_from_double(FLT_MAX));
        wl_pointer_send_leave(resource, serial, surface->resource);
        if (wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION) {
            wl_pointer_send_frame(resource);
        }
    }
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

void xwayland_update_workarea(void)
{
    if (!xwayland || !xwayland->wlr_xwayland || !xwayland->wlr_xwayland->xwm) {
        return;
    }

    struct wlr_box box;
    output_layout_get_workarea(&box);
    box.x = xwayland_scale(box.x);
    box.y = xwayland_scale(box.y);
    box.width = xwayland_scale(box.width);
    box.height = xwayland_scale(box.height);
    wlr_xwayland_set_workareas(xwayland->wlr_xwayland, &box, 1);
}
