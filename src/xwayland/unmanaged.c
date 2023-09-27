// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>

#include "input/event.h"
#include "input/seat.h"
#include "scene/surface.h"
#include "view/view.h"
#include "xwayland_p.h"

struct xwayland_unmanaged {
    struct wl_list link;
    struct xwayland_server *xwayland;
    struct wlr_xwayland_surface *wlr_xwayland_surface;
    struct ky_scene_node *surface_node;
    struct wl_listener node_destroy;

    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;

    struct wl_listener request_activate;
    struct wl_listener request_configure;
    // struct wl_listener request_fullscreen;

    struct wl_listener set_geometry;
    struct wl_listener set_override_redirect;
};

static bool xwayland_unmanaged_hover(struct seat *seat, struct ky_scene_node *node, double x,
                                     double y, uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    struct xwayland_unmanaged *unmanaged = data;
    struct wlr_xwayland *wlr_xwayland = unmanaged->xwayland->wlr_xwayland;

    if (wlr_xwayland->seat != seat->wlr_seat) {
        wlr_xwayland_set_seat(wlr_xwayland, seat->wlr_seat);
    }

    if (!hold) {
        seat_notify_motion(seat, surface, time, x, y, first);
        return false;
    }

    int lx, ly;
    ky_scene_node_coords(unmanaged->surface_node, &lx, &ly);
    seat_notify_motion(seat, surface, time, x - lx, y - ly, first);
    return true;
}

static bool xwayland_unmanaged_is_focusable(struct xwayland_unmanaged *unmanaged)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    if (!wlr_xwayland_or_surface_wants_focus(wlr_xwayland_surface)) {
        return false;
    }

    /* No Input and Globally Active clients set the input field to False,
     * which requests that the window manager not set the input focus to their top-level window.
     */
    if (wlr_xwayland_surface->hints && !wlr_xwayland_surface->hints->input) {
        return false;
    }

    return true;
}

static void xwayland_unmanaged_focus(struct xwayland_unmanaged *unmanaged)
{
    if (!xwayland_unmanaged_is_focusable(unmanaged)) {
        return;
    }

    struct seat *seat = seat_from_wlr_seat(unmanaged->xwayland->wlr_xwayland->seat);
    seat_focus_surface(seat, unmanaged->wlr_xwayland_surface->surface);
}

static void xwayland_unmanaged_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                                     bool pressed, uint32_t time, bool dual, void *data)
{
    struct xwayland_unmanaged *unmanaged = data;
    struct wlr_xwayland *wlr_xwayland = unmanaged->xwayland->wlr_xwayland;
    if (wlr_xwayland->seat != seat->wlr_seat) {
        wlr_xwayland_set_seat(wlr_xwayland, seat->wlr_seat);
    }

    seat_notify_button(seat, time, button, pressed);

    /* only do activated when button pressed */
    if (!pressed) {
        return;
    }

    /* only activate and focus top surface */
    xwayland_unmanaged_focus(unmanaged);
}

static void xwayland_unmanaged_leave(struct seat *seat, struct ky_scene_node *node, bool last,
                                     void *data)
{
    /* so surface will call set_cursor when enter again */
    struct wlr_surface *surface = wlr_surface_try_from_node(node);
    seat_notify_leave(seat, surface);
}

static struct ky_scene_node *xwayland_unmanaged_get_root(void *data)
{
    struct xwayland_unmanaged *unmanaged = data;
    return unmanaged->surface_node;
}

static struct wlr_surface *xwayland_unmanaged_get_toplevel(void *data)
{
    struct xwayland_unmanaged *unmanaged = data;
    /* only return surface if focusable */
    if (!xwayland_unmanaged_is_focusable(unmanaged)) {
        return NULL;
    }
    return unmanaged->wlr_xwayland_surface->surface;
}

static const struct input_event_node_impl xwayland_unmanaged_event_node_impl = {
    .hover = xwayland_unmanaged_hover,
    .click = xwayland_unmanaged_click,
    .leave = xwayland_unmanaged_leave,
};

static void unmanaged_handle_request_activate(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, request_activate);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;
    struct seat *seat = seat_from_wlr_seat(unmanaged->xwayland->wlr_xwayland->seat);
    seat_focus_surface(seat, wlr_xwayland_surface->surface);
}

static void unmanaged_handle_request_configure(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, request_configure);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;
    struct wlr_xwayland_surface_configure_event *event = data;
    wlr_xwayland_surface_configure(wlr_xwayland_surface, event->x, event->y, event->width,
                                   event->height);
    if (unmanaged->surface_node) {
        ky_scene_node_set_position(unmanaged->surface_node, event->x, event->y);
    }
}

static void unmanaged_handle_set_geometry(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, set_geometry);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;
    if (unmanaged->surface_node) {
        ky_scene_node_set_position(unmanaged->surface_node, wlr_xwayland_surface->x,
                                   wlr_xwayland_surface->y);
    }
}

static void unmanaged_handle_map(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, map);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    wl_list_insert(&unmanaged->xwayland->unmanaged_surfaces, &unmanaged->link);
    /* Stack new surface on top */
    wlr_xwayland_surface_restack(wlr_xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
    xwayland_unmanaged_focus(unmanaged);

    unmanaged->set_geometry.notify = unmanaged_handle_set_geometry;
    wl_signal_add(&wlr_xwayland_surface->events.set_geometry, &unmanaged->set_geometry);

    ky_scene_node_set_enabled(unmanaged->surface_node, true);
    ky_scene_node_set_position(unmanaged->surface_node, wlr_xwayland_surface->x,
                               wlr_xwayland_surface->y);
}

static void unmanaged_handle_unmap(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, unmap);

    wl_list_remove(&unmanaged->link);
    wl_list_remove(&unmanaged->set_geometry.link);
    if (unmanaged->surface_node) {
        ky_scene_node_set_enabled(unmanaged->surface_node, false);
    }
}

static void unmanaged_handle_node_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, node_destroy);
    wl_list_remove(&unmanaged->node_destroy.link);
    unmanaged->surface_node = NULL;
}

static void unmanaged_handle_associate(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, associate);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    struct view_layer *layer = view_manager_get_layer(LAYER_UNMANAGED, false);
    struct ky_scene_surface *scene_surface =
        ky_scene_surface_create(layer->tree, wlr_xwayland_surface->surface);
    unmanaged->surface_node = ky_scene_node_from_buffer(scene_surface->buffer);
    ky_scene_node_set_enabled(unmanaged->surface_node, false);

    input_event_node_create(unmanaged->surface_node, &xwayland_unmanaged_event_node_impl,
                            xwayland_unmanaged_get_root, xwayland_unmanaged_get_toplevel,
                            unmanaged);

    unmanaged->map.notify = unmanaged_handle_map;
    wl_signal_add(&wlr_xwayland_surface->surface->events.map, &unmanaged->map);
    unmanaged->unmap.notify = unmanaged_handle_unmap;
    wl_signal_add(&wlr_xwayland_surface->surface->events.unmap, &unmanaged->unmap);
    unmanaged->node_destroy.notify = unmanaged_handle_node_destroy;
    ky_scene_node_add_destroy_listener(unmanaged->surface_node, &unmanaged->node_destroy);
}

static void unmanaged_handle_dissociate(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, dissociate);

    ky_scene_node_destroy(unmanaged->surface_node);

    wl_list_remove(&unmanaged->map.link);
    wl_list_remove(&unmanaged->unmap.link);
}

static void unmanaged_handle_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, destroy);

    wl_list_remove(&unmanaged->request_configure.link);
    wl_list_remove(&unmanaged->set_override_redirect.link);
    wl_list_remove(&unmanaged->request_activate.link);
    wl_list_remove(&unmanaged->associate.link);
    wl_list_remove(&unmanaged->dissociate.link);
    wl_list_remove(&unmanaged->destroy.link);

    free(unmanaged);
}

static void unmanaged_handle_set_override_redirect(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged =
        wl_container_of(listener, unmanaged, set_override_redirect);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    if (wlr_xwayland_surface->surface && wlr_xwayland_surface->surface->mapped) {
        unmanaged_handle_unmap(&unmanaged->unmap, NULL);
        unmanaged_handle_dissociate(&unmanaged->dissociate, NULL);
    }
    unmanaged_handle_destroy(&unmanaged->destroy, NULL);

    xwayland_view_create(unmanaged->xwayland, wlr_xwayland_surface);
}

void xwayland_unmanaged_create(struct xwayland_server *xwayland,
                               struct wlr_xwayland_surface *wlr_xwayland_surface)
{
    struct xwayland_unmanaged *unmanaged = calloc(1, sizeof(struct xwayland_unmanaged));
    if (!unmanaged) {
        return;
    }

    unmanaged->xwayland = xwayland;
    unmanaged->wlr_xwayland_surface = wlr_xwayland_surface;

    unmanaged->associate.notify = unmanaged_handle_associate;
    wl_signal_add(&wlr_xwayland_surface->events.associate, &unmanaged->associate);
    unmanaged->dissociate.notify = unmanaged_handle_dissociate;
    wl_signal_add(&wlr_xwayland_surface->events.dissociate, &unmanaged->dissociate);
    unmanaged->destroy.notify = unmanaged_handle_destroy;
    wl_signal_add(&wlr_xwayland_surface->events.destroy, &unmanaged->destroy);

    unmanaged->request_activate.notify = unmanaged_handle_request_activate;
    wl_signal_add(&wlr_xwayland_surface->events.request_activate, &unmanaged->request_activate);
    unmanaged->request_configure.notify = unmanaged_handle_request_configure;
    wl_signal_add(&wlr_xwayland_surface->events.request_configure, &unmanaged->request_configure);

    unmanaged->set_override_redirect.notify = unmanaged_handle_set_override_redirect;
    wl_signal_add(&wlr_xwayland_surface->events.set_override_redirect,
                  &unmanaged->set_override_redirect);

    if (wlr_xwayland_surface->surface && wlr_xwayland_surface->surface->mapped) {
        unmanaged_handle_associate(&unmanaged->associate, NULL);
        unmanaged_handle_map(&unmanaged->map, NULL);
    }
}

void xwayland_restack_unmanaged(struct xwayland_server *xwayland)
{
    /* Restack unmanaged surfaces on top */
    struct xwayland_unmanaged *unmanaged;
    wl_list_for_each(unmanaged, &xwayland->unmanaged_surfaces, link) {
        wlr_xwayland_surface_restack(unmanaged->wlr_xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
    }
}
