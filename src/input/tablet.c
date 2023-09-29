// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>

#include <kywc/log.h>

#include "input/seat.h"
#include "input_p.h"
#include "scene/surface.h"
#include "server.h"
#include "view/view.h"
#include "xwayland.h"

struct tablet_manager {
    struct wlr_tablet_manager_v2 *manager;

    struct wl_list tablets;
    struct wl_list tablet_pads;

    struct wl_listener new_input;
    struct wl_listener server_destroy;
};

struct tablet {
    struct wlr_tablet *wlr_tablet;
    struct wlr_tablet_v2_tablet *tablet;
    struct wl_list link;

    struct input *input;
    struct wl_listener input_destroy;
};

struct tablet_tool {
    struct wlr_tablet_v2_tablet_tool *tablet_tool;
    struct tablet *tablet;

    double tilt_x, tilt_y;

    struct wl_listener set_cursor;
    struct wl_listener tool_destroy;
};

struct tablet_pad {
    struct wlr_tablet_pad *wlr_tablet_pad;
    struct wlr_tablet_v2_tablet_pad *tablet_pad;
    struct wl_list link;

    struct tablet *tablet;
    struct wl_listener tablet_destroy;

    struct input *input;
    struct wl_listener input_destroy;

    struct wl_listener attach;
    struct wl_listener button;
    struct wl_listener ring;
    struct wl_listener strip;

    struct wlr_surface *current_surface;
    struct wl_listener surface_destroy;
};

static struct tablet_manager *manager = NULL;

static struct tablet *tablet_from_wlr_tablet(struct wlr_tablet *wlr_tablet)
{
    return wlr_tablet->data;
}

static struct tablet_tool *tablet_tool_from_wlr_tablet_tool(struct wlr_tablet_tool *wlr_tablet_tool)
{
    return wlr_tablet_tool->data;
}

static void tablet_tool_handle_set_cursor(struct wl_listener *listener, void *data)
{
    struct tablet_tool *tablet_tool = wl_container_of(listener, tablet_tool, set_cursor);
    struct wlr_tablet_v2_event_cursor *event = data;

    struct wl_client *focused_client = NULL;
    struct wlr_surface *focused_surface = tablet_tool->tablet_tool->focused_surface;
    if (focused_surface != NULL) {
        focused_client = wl_resource_get_client(focused_surface->resource);
    }

    if (focused_client == NULL || event->seat_client->client != focused_client) {
        kywc_log(KYWC_DEBUG, "denying request to set cursor from unfocused client");
        return;
    }

    struct cursor *cursor = tablet_tool->tablet->input->seat->cursor;
    cursor->client_requested = true;
    wlr_cursor_set_surface(cursor->wlr_cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

static void tablet_tool_handle_tool_destroy(struct wl_listener *listener, void *data)
{
    struct tablet_tool *tablet_tool = wl_container_of(listener, tablet_tool, tool_destroy);
    wl_list_remove(&tablet_tool->tool_destroy.link);
    wl_list_remove(&tablet_tool->set_cursor.link);
    free(tablet_tool);
}

static struct tablet_tool *tablet_tool_create(struct tablet *tablet,
                                              struct wlr_tablet_tool *wlr_tablet_tool)
{
    struct tablet_tool *tablet_tool = calloc(1, sizeof(struct tablet_tool));
    if (!tablet_tool) {
        return NULL;
    }

    tablet_tool->tablet = tablet;
    wlr_tablet_tool->data = tablet_tool;

    tablet_tool->tablet_tool =
        wlr_tablet_tool_create(manager->manager, tablet->input->seat->wlr_seat, wlr_tablet_tool);
    tablet_tool->set_cursor.notify = tablet_tool_handle_set_cursor;
    wl_signal_add(&tablet_tool->tablet_tool->events.set_cursor, &tablet_tool->set_cursor);
    tablet_tool->tool_destroy.notify = tablet_tool_handle_tool_destroy;
    wl_signal_add(&wlr_tablet_tool->events.destroy, &tablet_tool->tool_destroy);

    return tablet_tool;
}

static void tablet_pad_handle_tablet_destroy(struct wl_listener *listener, void *data)
{
    struct tablet_pad *tablet_pad = wl_container_of(listener, tablet_pad, tablet_destroy);
    tablet_pad->tablet = NULL;
    wl_list_remove(&tablet_pad->tablet_destroy.link);
    wl_list_init(&tablet_pad->tablet_destroy.link);
}

static void attach_tablet_pad(struct tablet_pad *tablet_pad, struct tablet *tablet)
{
    kywc_log(KYWC_DEBUG, "Attaching tablet pad \"%s\" to tablet tool \"%s\"",
             tablet_pad->input->wlr_input->name, tablet->input->wlr_input->name);

    tablet_pad->tablet = tablet;
    wl_list_remove(&tablet_pad->tablet_destroy.link);
    tablet_pad->tablet_destroy.notify = tablet_pad_handle_tablet_destroy;
    wl_signal_add(&tablet->input->wlr_input->events.destroy, &tablet_pad->tablet_destroy);
}

static void tablet_handle_input_destroy(struct wl_listener *listener, void *data)
{
    struct tablet *tablet = wl_container_of(listener, tablet, input_destroy);
    wl_list_remove(&tablet->input_destroy.link);
    wl_list_remove(&tablet->link);
    free(tablet);
}

static void tablet_create(struct tablet_manager *manager, struct input *input)
{
    struct tablet *tablet = calloc(1, sizeof(struct tablet));
    if (!tablet) {
        return;
    }

    tablet->input = input;
    tablet->input_destroy.notify = tablet_handle_input_destroy;
    wl_signal_add(&input->events.destroy, &tablet->input_destroy);

    wl_list_insert(&manager->tablets, &tablet->link);

    tablet->wlr_tablet = wlr_tablet_from_input_device(input->wlr_input);
    tablet->wlr_tablet->data = tablet;

    tablet->tablet = wlr_tablet_create(manager->manager, input->seat->wlr_seat, input->wlr_input);

    if (!input->device) {
        return;
    }

    struct libinput_device_group *group =
        libinput_device_get_device_group(wlr_libinput_get_device_handle(input->wlr_input));

    struct tablet_pad *tablet_pad;
    wl_list_for_each(tablet_pad, &manager->tablet_pads, link) {
        if (!tablet_pad->input->device) {
            continue;
        }

        struct libinput_device_group *tablet_pad_group = libinput_device_get_device_group(
            wlr_libinput_get_device_handle(tablet_pad->input->wlr_input));

        if (tablet_pad_group == group) {
            attach_tablet_pad(tablet_pad, tablet);
            break;
        }
    }
}

static void tablet_pad_handle_input_destroy(struct wl_listener *listener, void *data)
{
    struct tablet_pad *tablet_pad = wl_container_of(listener, tablet_pad, input_destroy);
    wl_list_remove(&tablet_pad->input_destroy.link);
    wl_list_remove(&tablet_pad->link);
    wl_list_remove(&tablet_pad->attach.link);
    wl_list_remove(&tablet_pad->button.link);
    wl_list_remove(&tablet_pad->strip.link);
    wl_list_remove(&tablet_pad->ring.link);
    wl_list_remove(&tablet_pad->tablet_destroy.link);
    wl_list_remove(&tablet_pad->surface_destroy.link);
    free(tablet_pad);
}

static void tablet_pad_handle_attach(struct wl_listener *listener, void *data)
{
    struct tablet_pad *tablet_pad = wl_container_of(listener, tablet_pad, attach);
    struct wlr_tablet_tool *wlr_tablet_tool = data;

    struct tablet_tool *tablet_tool = tablet_tool_from_wlr_tablet_tool(wlr_tablet_tool);
    if (!tablet_tool) {
        return;
    }

    attach_tablet_pad(tablet_pad, tablet_tool->tablet);
}

static void tablet_pad_handle_button(struct wl_listener *listener, void *data)
{
    struct tablet_pad *tablet_pad = wl_container_of(listener, tablet_pad, button);
    struct wlr_tablet_pad_button_event *event = data;

    if (!tablet_pad->current_surface) {
        return;
    }

    wlr_tablet_v2_tablet_pad_notify_mode(tablet_pad->tablet_pad, event->group, event->mode,
                                         event->time_msec);
    wlr_tablet_v2_tablet_pad_notify_button(tablet_pad->tablet_pad, event->button, event->time_msec,
                                           (enum zwp_tablet_pad_v2_button_state)event->state);
}

static void tablet_pad_handle_strip(struct wl_listener *listener, void *data)
{
    struct tablet_pad *tablet_pad = wl_container_of(listener, tablet_pad, strip);
    struct wlr_tablet_pad_strip_event *event = data;

    if (!tablet_pad->current_surface) {
        return;
    }

    wlr_tablet_v2_tablet_pad_notify_strip(tablet_pad->tablet_pad, event->strip, event->position,
                                          event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
                                          event->time_msec);
}

static void tablet_pad_handle_ring(struct wl_listener *listener, void *data)
{
    struct tablet_pad *tablet_pad = wl_container_of(listener, tablet_pad, ring);
    struct wlr_tablet_pad_ring_event *event = data;

    if (!tablet_pad->current_surface) {
        return;
    }

    wlr_tablet_v2_tablet_pad_notify_ring(tablet_pad->tablet_pad, event->ring, event->position,
                                         event->source == WLR_TABLET_PAD_RING_SOURCE_FINGER,
                                         event->time_msec);
}

static void tablet_pad_create(struct tablet_manager *manager, struct input *input)
{
    struct tablet_pad *tablet_pad = calloc(1, sizeof(struct tablet_pad));
    if (!tablet_pad) {
        return;
    }

    tablet_pad->input = input;
    tablet_pad->input_destroy.notify = tablet_pad_handle_input_destroy;
    wl_signal_add(&input->events.destroy, &tablet_pad->input_destroy);

    wl_list_insert(&manager->tablet_pads, &tablet_pad->link);

    tablet_pad->wlr_tablet_pad = wlr_tablet_pad_from_input_device(input->wlr_input);
    tablet_pad->wlr_tablet_pad->data = tablet_pad;

    tablet_pad->tablet_pad =
        wlr_tablet_pad_create(manager->manager, input->seat->wlr_seat, input->wlr_input);

    tablet_pad->attach.notify = tablet_pad_handle_attach;
    wl_signal_add(&tablet_pad->wlr_tablet_pad->events.attach_tablet, &tablet_pad->attach);
    tablet_pad->button.notify = tablet_pad_handle_button;
    wl_signal_add(&tablet_pad->wlr_tablet_pad->events.button, &tablet_pad->button);
    tablet_pad->strip.notify = tablet_pad_handle_strip;
    wl_signal_add(&tablet_pad->wlr_tablet_pad->events.strip, &tablet_pad->strip);
    tablet_pad->ring.notify = tablet_pad_handle_ring;
    wl_signal_add(&tablet_pad->wlr_tablet_pad->events.ring, &tablet_pad->ring);

    wl_list_init(&tablet_pad->tablet_destroy.link);
    wl_list_init(&tablet_pad->surface_destroy.link);

    if (!input->device) {
        return;
    }

    struct libinput_device_group *group =
        libinput_device_get_device_group(wlr_libinput_get_device_handle(input->wlr_input));

    struct tablet *tablet;
    wl_list_for_each(tablet, &manager->tablets, link) {
        if (!tablet->input->device) {
            continue;
        }

        struct libinput_device_group *tablet_group = libinput_device_get_device_group(
            wlr_libinput_get_device_handle(tablet->input->wlr_input));

        if (tablet_group == group) {
            attach_tablet_pad(tablet_pad, tablet);
            break;
        }
    }
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
    struct input *input = data;
    /* input has been configured, only care about tablet_tool and tablet_pad */
    if (input->prop.type != WLR_INPUT_DEVICE_TABLET_TOOL &&
        input->prop.type != WLR_INPUT_DEVICE_TABLET_PAD) {
        return;
    }

    if (input->prop.type == WLR_INPUT_DEVICE_TABLET_TOOL) {
        tablet_create(manager, input);
    } else {
        tablet_pad_create(manager, input);
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
    manager = NULL;
}

bool tablet_manager_create(struct input_manager *input_manager)
{
    manager = calloc(1, sizeof(struct tablet_manager));
    if (!manager) {
        return false;
    }

    manager->manager = wlr_tablet_v2_create(input_manager->server->display);
    if (!manager->manager) {
        kywc_log(KYWC_WARN, "table manager create failed");
        free(manager);
        manager = NULL;
        return false;
    }

    wl_list_init(&manager->tablets);
    wl_list_init(&manager->tablet_pads);

    manager->new_input.notify = handle_new_input;
    input_add_new_listener(&manager->new_input);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &manager->server_destroy);

    return true;
}

static struct wlr_surface *tablet_get_surface(struct tablet *tablet, double *sx, double *sy,
                                              struct wlr_surface **toplevel)
{
    struct seat *seat = tablet->input->seat;
    struct cursor *cursor = seat->cursor;

    struct ky_scene_node *node =
        ky_scene_node_at(ky_scene_node_from_scene(seat->scene), cursor->lx, cursor->ly, sx, sy);
    if (!node) {
        return NULL;
    }

    if (toplevel) {
        *toplevel = input_event_node_toplevel(input_event_node_from_node(node));
    }
    return wlr_surface_try_from_node(node);
}

static bool tablet_handle_tool_position(struct tablet_tool *tablet_tool)
{
    struct cursor *cursor = tablet_tool->tablet->input->seat->cursor;
    double sx = 0, sy = 0;

    struct wlr_surface *surface = tablet_get_surface(tablet_tool->tablet, &sx, &sy, NULL);
    if ((surface && wlr_surface_accepts_tablet_v2(tablet_tool->tablet->tablet, surface)) ||
        wlr_tablet_tool_v2_has_implicit_grab(tablet_tool->tablet_tool)) {
        if (surface) {
            wlr_tablet_v2_tablet_tool_notify_proximity_in(tablet_tool->tablet_tool,
                                                          tablet_tool->tablet->tablet, surface);
            if (xwayland_check_client(wl_resource_get_client(surface->resource))) {
                sx = xwayland_scale(sx);
                sy = xwayland_scale(sy);
            }
            wlr_tablet_v2_tablet_tool_notify_motion(tablet_tool->tablet_tool, sx, sy);
        } else {
            wlr_tablet_v2_tablet_tool_notify_proximity_out(tablet_tool->tablet_tool);
            cursor_set_image(cursor, CURSOR_DEFAULT);
        }
        selection_handle_cursor_move(cursor->seat, cursor->lx, cursor->ly);
        return true;
    }

    wlr_tablet_v2_tablet_tool_notify_proximity_out(tablet_tool->tablet_tool);
    cursor_set_image(cursor, CURSOR_DEFAULT);
    return false;
}

bool tablet_handle_tool_proximity(struct wlr_tablet_tool_proximity_event *event)
{
    struct tablet *tablet = tablet_from_wlr_tablet(event->tablet);
    if (!tablet) {
        return false;
    }

    /* create tablet_tool when proximity */
    struct tablet_tool *tablet_tool = tablet_tool_from_wlr_tablet_tool(event->tool);
    if (!tablet_tool) {
        tablet_tool = tablet_tool_create(tablet, event->tool);
    }
    if (!tablet_tool) {
        return false;
    }

    if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT) {
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tablet_tool->tablet_tool);
        return true;
    }

    return tablet_handle_tool_position(tablet_tool);
}

void tablet_handle_tool_axis(struct wlr_tablet_tool_axis_event *event)
{
    struct tablet_tool *tablet_tool = tablet_tool_from_wlr_tablet_tool(event->tool);
    if (!tablet_tool) {
        return;
    }

    if (event->updated_axes & WLR_TABLET_TOOL_AXIS_X ||
        event->updated_axes & WLR_TABLET_TOOL_AXIS_Y) {
        tablet_handle_tool_position(tablet_tool);
    }

    if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE) {
        wlr_tablet_v2_tablet_tool_notify_pressure(tablet_tool->tablet_tool, event->pressure);
    }
    if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE) {
        wlr_tablet_v2_tablet_tool_notify_distance(tablet_tool->tablet_tool, event->distance);
    }
    if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X) {
        tablet_tool->tilt_x = event->tilt_x;
    }
    if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y) {
        tablet_tool->tilt_y = event->tilt_y;
    }
    if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y)) {
        wlr_tablet_v2_tablet_tool_notify_tilt(tablet_tool->tablet_tool, tablet_tool->tilt_x,
                                              tablet_tool->tilt_y);
    }
    if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION) {
        wlr_tablet_v2_tablet_tool_notify_rotation(tablet_tool->tablet_tool, event->rotation);
    }
    if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER) {
        wlr_tablet_v2_tablet_tool_notify_slider(tablet_tool->tablet_tool, event->slider);
    }
    if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL) {
        wlr_tablet_v2_tablet_tool_notify_wheel(tablet_tool->tablet_tool, event->wheel_delta, 0);
    }
}

bool tablet_handle_tool_tip(struct wlr_tablet_tool_tip_event *event)
{
    struct tablet_tool *tablet_tool = tablet_tool_from_wlr_tablet_tool(event->tool);
    if (!tablet_tool) {
        return false;
    }

    struct wlr_surface *toplevel = NULL;
    struct wlr_surface *surface = tablet_get_surface(tablet_tool->tablet, NULL, NULL, &toplevel);
    if (!surface || !wlr_surface_accepts_tablet_v2(tablet_tool->tablet->tablet, surface)) {
        if (event->state == WLR_TABLET_TOOL_TIP_UP) {
            wlr_tablet_v2_tablet_tool_notify_up(tablet_tool->tablet_tool);
        }
        return false;
    }

    if (event->state == WLR_TABLET_TOOL_TIP_UP) {
        wlr_tablet_v2_tablet_tool_notify_up(tablet_tool->tablet_tool);
        return true;
    }

    wlr_tablet_v2_tablet_tool_notify_down(tablet_tool->tablet_tool);
    wlr_tablet_tool_v2_start_implicit_grab(tablet_tool->tablet_tool);

    /* activate and focus the toplevel surface */
    if (toplevel) {
        seat_focus_surface(tablet_tool->tablet->input->seat, toplevel);
        struct view *view = view_try_from_wlr_surface(toplevel);
        if (view) {
            kywc_view_activate(&view->base);
        }
    }

    return true;
}

bool tablet_handle_tool_button(struct wlr_tablet_tool_button_event *event)
{
    struct tablet_tool *tablet_tool = tablet_tool_from_wlr_tablet_tool(event->tool);
    if (!tablet_tool) {
        return false;
    }

    struct wlr_surface *surface = tablet_get_surface(tablet_tool->tablet, NULL, NULL, NULL);
    if (!surface || !wlr_surface_accepts_tablet_v2(tablet_tool->tablet->tablet, surface)) {
        return false;
    }

    wlr_tablet_v2_tablet_tool_notify_button(tablet_tool->tablet_tool, event->button,
                                            (enum zwp_tablet_pad_v2_button_state)event->state);
    return true;
}

static void tablet_pad_set_focus(struct tablet_pad *tablet_pad, struct wlr_surface *surface);
static void tablet_pad_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct tablet_pad *tablet_pad = wl_container_of(listener, tablet_pad, surface_destroy);
    tablet_pad_set_focus(tablet_pad, NULL);
}

static void tablet_pad_set_focus(struct tablet_pad *tablet_pad, struct wlr_surface *surface)
{
    if (!tablet_pad || !tablet_pad->tablet) {
        return;
    }
    if (surface == tablet_pad->current_surface) {
        return;
    }

    /* Leave current surface */
    if (tablet_pad->current_surface) {
        wlr_tablet_v2_tablet_pad_notify_leave(tablet_pad->tablet_pad, tablet_pad->current_surface);
        wl_list_remove(&tablet_pad->surface_destroy.link);
        wl_list_init(&tablet_pad->surface_destroy.link);
        tablet_pad->current_surface = NULL;
    }

    if (surface == NULL || !wlr_surface_accepts_tablet_v2(tablet_pad->tablet->tablet, surface)) {
        return;
    }

    wlr_tablet_v2_tablet_pad_notify_enter(tablet_pad->tablet_pad, tablet_pad->tablet->tablet,
                                          surface);

    tablet_pad->current_surface = surface;
    tablet_pad->surface_destroy.notify = tablet_pad_handle_surface_destroy;
    wl_signal_add(&surface->events.destroy, &tablet_pad->surface_destroy);
}

void tablet_set_focus(struct seat *seat, struct wlr_surface *surface)
{
    if (!manager) {
        return;
    }

    struct tablet_pad *tablet_pad;
    wl_list_for_each(tablet_pad, &manager->tablet_pads, link) {
        tablet_pad_set_focus(tablet_pad, surface);
    }
}
