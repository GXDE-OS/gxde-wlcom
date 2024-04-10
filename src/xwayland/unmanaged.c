// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <float.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/region.h>

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

    struct wl_listener precommit;
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

    struct wlr_seat_pointer_grab pointer_grab;
};

static bool xwayland_unmanaged_hover(struct seat *seat, struct ky_scene_node *node, double x,
                                     double y, uint32_t time, bool first, bool hold, void *data)
{
    struct wlr_surface *surface = wlr_surface_try_from_node(node);

    xwayland_update_seat(seat);

    if (!hold) {
        seat_notify_motion(seat, surface, time, xwayland_scale(x), xwayland_scale(y), first);
        return false;
    }

    struct xwayland_unmanaged *unmanaged = data;
    int lx, ly;
    ky_scene_node_coords(unmanaged->surface_node, &lx, &ly);
    seat_notify_motion(seat, surface, time, xwayland_scale(x - lx), xwayland_scale(y - ly), first);
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
                                     bool pressed, uint32_t time, enum click_state state,
                                     void *data)
{
    xwayland_update_seat(seat);

    seat_notify_button(seat, time, button, pressed);

    /* only do activated when button pressed */
    if (!pressed) {
        return;
    }

    /* only activate and focus top surface */
    struct xwayland_unmanaged *unmanaged = data;
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
        ky_scene_node_set_position(unmanaged->surface_node, xwayland_unscale(event->x),
                                   xwayland_unscale(event->y));
    }
}

static void unmanaged_handle_set_geometry(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, set_geometry);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;
    if (unmanaged->surface_node) {
        ky_scene_node_set_position(unmanaged->surface_node,
                                   xwayland_unscale(wlr_xwayland_surface->x),
                                   xwayland_unscale(wlr_xwayland_surface->y));
    }
}

static void unmanaged_pointer_grab_enter(struct wlr_seat_pointer_grab *grab,
                                         struct wlr_surface *surface, double sx, double sy)
{
    if (wlr_xwayland_surface_try_from_wlr_surface(surface)) {
        wlr_seat_pointer_enter(grab->seat, surface, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(grab->seat);
    }
}

static void unmanaged_pointer_grab_clear_focus(struct wlr_seat_pointer_grab *grab)
{
    wlr_seat_pointer_clear_focus(grab->seat);
}

static void unmanaged_pointer_grab_motion(struct wlr_seat_pointer_grab *grab, uint32_t time,
                                          double sx, double sy)
{
    wlr_seat_pointer_send_motion(grab->seat, time, sx, sy);
}

static uint32_t unmanaged_pointer_grab_button(struct wlr_seat_pointer_grab *grab, uint32_t time,
                                              uint32_t button, uint32_t state)
{
    uint32_t serial = wlr_seat_pointer_send_button(grab->seat, time, button, state);
    if (serial) {
        return serial;
    }

    struct xwayland_unmanaged *unmanaged = grab->data;
    struct wlr_surface *surface = unmanaged->wlr_xwayland_surface->surface;
    wlr_seat_pointer_enter(grab->seat, surface, FLT_MAX, FLT_MAX);
    wlr_seat_pointer_send_button(grab->seat, time, button, state);
    /* clear focus to eat the release button event */
    wlr_seat_pointer_clear_focus(grab->seat);

    return 0;
}

static void unmanaged_pointer_grab_axis(struct wlr_seat_pointer_grab *grab, uint32_t time,
                                        enum wlr_axis_orientation orientation, double value,
                                        int32_t value_discrete, enum wlr_axis_source source)
{
    wlr_seat_pointer_send_axis(grab->seat, time, orientation, value, value_discrete, source);
}

static void unmanaged_pointer_grab_frame(struct wlr_seat_pointer_grab *grab)
{
    wlr_seat_pointer_send_frame(grab->seat);
}

static void unmanaged_pointer_grab_cancel(struct wlr_seat_pointer_grab *grab)
{
    kywc_log(KYWC_DEBUG, "unmanaged popup menu pointer grab cancel");
    grab->seat = NULL;
}

static const struct wlr_pointer_grab_interface unmanaged_pointer_grab_impl = {
    .enter = unmanaged_pointer_grab_enter,
    .clear_focus = unmanaged_pointer_grab_clear_focus,
    .motion = unmanaged_pointer_grab_motion,
    .button = unmanaged_pointer_grab_button,
    .cancel = unmanaged_pointer_grab_cancel,
    .axis = unmanaged_pointer_grab_axis,
    .frame = unmanaged_pointer_grab_frame,
};

static void xwayland_unmanaged_grab_pointer(struct xwayland_unmanaged *unmanaged)
{
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    if (wlr_xwayland_surface->parent &&
        xwayland_surface_has_type(wlr_xwayland_surface, NET_WM_WINDOW_TYPE_POPUP_MENU)) {
        unmanaged->pointer_grab.interface = &unmanaged_pointer_grab_impl;
        unmanaged->pointer_grab.data = unmanaged;
        wlr_seat_pointer_start_grab(unmanaged->xwayland->wlr_xwayland->seat,
                                    &unmanaged->pointer_grab);
        kywc_log(KYWC_DEBUG, "unmanaged popup menu start grab pointer");
    }
}

static void unmanaged_handle_map(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, map);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    /* Stack new surface on top */
    wlr_xwayland_surface_restack(wlr_xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);

    xwayland_unmanaged_focus(unmanaged);
    xwayland_unmanaged_grab_pointer(unmanaged);

    unmanaged->set_geometry.notify = unmanaged_handle_set_geometry;
    wl_signal_add(&wlr_xwayland_surface->events.set_geometry, &unmanaged->set_geometry);

    ky_scene_node_set_enabled(unmanaged->surface_node, true);
    ky_scene_node_set_position(unmanaged->surface_node, xwayland_unscale(wlr_xwayland_surface->x),
                               xwayland_unscale(wlr_xwayland_surface->y));
}

static void unmanaged_handle_unmap(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, unmap);

    struct wlr_seat *wlr_seat = unmanaged->pointer_grab.seat;
    if (wlr_seat && wlr_seat->pointer_state.grab == &unmanaged->pointer_grab) {
        wlr_seat_pointer_end_grab(unmanaged->pointer_grab.seat);
    }

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

static void unmanaged_handle_precommit(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, precommit);
    if (unmanaged->xwayland->scale == 1.0) {
        return;
    }

    struct wlr_surface_state *pending = data;
    pending->width = xwayland_unscale(pending->width);
    pending->height = xwayland_unscale(pending->height);

    float scale = 1.0 / unmanaged->xwayland->scale;
    if (pending->committed & WLR_SURFACE_STATE_SURFACE_DAMAGE) {
        wlr_region_scale(&pending->surface_damage, &pending->surface_damage, scale);
    }
    if (pending->committed & WLR_SURFACE_STATE_OPAQUE_REGION) {
        wlr_region_scale(&pending->opaque, &pending->opaque, scale);
    }
    if (pending->committed & WLR_SURFACE_STATE_INPUT_REGION) {
        wlr_region_scale(&pending->input, &pending->input, scale);
    }
    if (pending->committed & WLR_SURFACE_STATE_OFFSET) {
        pending->dx = xwayland_unscale(pending->dx);
        pending->dy = xwayland_unscale(pending->dy);
    }
}

static void unmanaged_handle_associate(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, associate);
    struct wlr_xwayland_surface *wlr_xwayland_surface = unmanaged->wlr_xwayland_surface;

    struct view_layer *layer = view_manager_get_layer(LAYER_UNMANAGED, false);
    struct ky_scene_surface *scene_surface =
        ky_scene_surface_create(layer->tree, wlr_xwayland_surface->surface);
    unmanaged->surface_node = &scene_surface->buffer->node;
    ky_scene_node_set_enabled(unmanaged->surface_node, false);
    input_event_node_create(unmanaged->surface_node, &xwayland_unmanaged_event_node_impl,
                            xwayland_unmanaged_get_root, xwayland_unmanaged_get_toplevel,
                            unmanaged);

    xwayland_surface_shape_select_input(wlr_xwayland_surface, true);
    xwayland_surface_apply_shape_region(wlr_xwayland_surface);

    unmanaged->precommit.notify = unmanaged_handle_precommit;
    wl_signal_add(&wlr_xwayland_surface->surface->events.precommit, &unmanaged->precommit);
    unmanaged->map.notify = unmanaged_handle_map;
    wl_signal_add(&wlr_xwayland_surface->surface->events.map, &unmanaged->map);
    unmanaged->unmap.notify = unmanaged_handle_unmap;
    wl_signal_add(&wlr_xwayland_surface->surface->events.unmap, &unmanaged->unmap);
    unmanaged->node_destroy.notify = unmanaged_handle_node_destroy;
    wl_signal_add(&unmanaged->surface_node->events.destroy, &unmanaged->node_destroy);
}

static void unmanaged_handle_dissociate(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, dissociate);

    ky_scene_node_destroy(unmanaged->surface_node);

    wl_list_remove(&unmanaged->precommit.link);
    wl_list_remove(&unmanaged->map.link);
    wl_list_remove(&unmanaged->unmap.link);
}

static void unmanaged_handle_destroy(struct wl_listener *listener, void *data)
{
    struct xwayland_unmanaged *unmanaged = wl_container_of(listener, unmanaged, destroy);

    wl_list_remove(&unmanaged->link);
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
    struct xwayland_server *xwayland = unmanaged->xwayland;

    if (wlr_xwayland_surface->surface && wlr_xwayland_surface->surface->mapped) {
        unmanaged_handle_unmap(&unmanaged->unmap, NULL);
        unmanaged_handle_dissociate(&unmanaged->dissociate, NULL);
    }
    unmanaged_handle_destroy(&unmanaged->destroy, NULL);

    xwayland_view_create(xwayland, wlr_xwayland_surface);
}

void xwayland_unmanaged_create(struct xwayland_server *xwayland,
                               struct wlr_xwayland_surface *wlr_xwayland_surface)
{
    struct xwayland_unmanaged *unmanaged = calloc(1, sizeof(struct xwayland_unmanaged));
    if (!unmanaged) {
        return;
    }

    unmanaged->xwayland = xwayland;
    wl_list_insert(&xwayland->unmanaged_surfaces, &unmanaged->link);
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

    wl_list_init(&unmanaged->precommit.link);
    wl_list_init(&unmanaged->map.link);
    wl_list_init(&unmanaged->unmap.link);

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

static struct xwayland_unmanaged *xwayland_unmanaged_look_surface(struct xwayland_server *xwayland,
                                                                  xcb_window_t window_id)
{
    struct xwayland_unmanaged *unmanaged;
    wl_list_for_each(unmanaged, &xwayland->unmanaged_surfaces, link) {
        if (unmanaged->wlr_xwayland_surface->window_id == window_id) {
            return unmanaged;
        }
    }
    return NULL;
}

bool xwayland_unmanaged_set_shape_region(struct xwayland_server *xwayland, xcb_window_t window_id,
                                         xcb_shape_sk_t kind, const pixman_region32_t *region)
{
    struct xwayland_unmanaged *unmanaged = xwayland_unmanaged_look_surface(xwayland, window_id);
    if (!unmanaged || !unmanaged->surface_node) {
        return false;
    }

    if (kind == XCB_SHAPE_SK_BOUNDING || kind == XCB_SHAPE_SK_CLIP) {
        ky_scene_node_set_clip_region(unmanaged->surface_node, region);
    }
    if (kind == XCB_SHAPE_SK_BOUNDING || kind == XCB_SHAPE_SK_INPUT) {
        ky_scene_node_set_input_region(unmanaged->surface_node, region);
        /* empty input region means no input support */
        bool need_bypassed = kind == XCB_SHAPE_SK_INPUT && !pixman_region32_not_empty(region);
        ky_scene_node_set_bypassed(unmanaged->surface_node, need_bypassed);
    }

    return true;
}
