// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "scene/surface.h"
#include "ukui-shell-protocol.h"
#include "view_p.h"

#define UKUI_SHELL_VERSION 4

struct ukui_shell {
    struct wl_global *global;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct ukui_surface {
    struct wl_resource *resource;

    struct wlr_surface *wlr_surface;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;

    /* for popup position */
    struct ky_scene_buffer *buffer;

    /* get in map listener */
    struct view *view;
    struct wl_listener view_map;
    struct wl_listener view_unmap;
    struct wl_listener view_minimize;
    struct wl_listener view_size;
    struct wl_listener view_position;
    struct wl_listener view_output;
    struct wl_listener view_destroy;

    struct output *output;
    struct wl_listener output_update_usable_area;
    struct wl_listener output_disable;

    int x, y;
    enum ukui_surface_role role;
    enum ukui_surface_property property;
    bool role_changed;
    bool skip_taskbar, skip_switcher;
    bool open_under_cursor;
    bool panel_auto_hide;

    uint32_t property_state;
    bool no_titlebar;

    struct seat_keyboard_grab keyboard_grab;

    struct wl_list ukui_keyboard_grabs;
    bool grab_all_keyboard;

    char *icon_name;
};

struct ukui_keyboard_grab {
    struct seat_keyboard_grab keyboard_grab;
    struct wl_list link;
};

enum property_flag {
    STATE_NO_TITLEBAR = 0x1,
    STATE_THEME = 0x2,
    STATE_WINDOW_RADIUS = 0x4,
    STATE_BORDER_WIDTH = 0x8,
    STATE_BORDER_COLOR = 0x10,
    STATE_SHADOW_RADIUS = 0x20,
    STATE_SHADOW_OFFSET = 0x40,
    STATE_SHADOW_COLOR = 0x80,
};

static void handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void surface_handle_output_disable(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, output_disable);
    surface->output = NULL;
    wl_list_remove(&surface->output_disable.link);
    wl_list_init(&surface->output_disable.link);
}

static void handle_set_output(struct wl_client *client, struct wl_resource *resource,
                              struct wl_resource *output)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    struct output *surface_output = output_from_resource(output);
    if (!surface_output || surface->output == surface_output) {
        return;
    }

    surface->output = surface_output;
    wl_list_remove(&surface->output_disable.link);
    wl_signal_add(&surface->output->events.disable, &surface->output_disable);
}

static void ukui_surface_apply_position(struct ukui_surface *surface)
{
    if (surface->view) {
#if 0
        if (surface->output) {
            surface->x += surface->output->geometry.x;
            surface->y += surface->output->geometry.y;
        } else {
            struct seat *seat = surface->view->base.focused_seat;
            struct kywc_output *kywc_output =
                kywc_output_at_point(seat->cursor->lx, seat->cursor->ly);
            surface->x += kywc_output->state.lx;
            surface->y += kywc_output->state.ly;
        }
#endif
        view_do_move(surface->view, surface->x, surface->y);
    } else if (surface->buffer) {
        struct ky_scene_node *node = &surface->buffer->node;
        int lx, ly;
        ky_scene_node_coords(node, &lx, &ly);
        ky_scene_node_set_position(&surface->buffer->node, node->x + surface->x - lx,
                                   node->y + surface->y - ly);
    }
}

static void handle_set_position(struct wl_client *client, struct wl_resource *resource, int32_t x,
                                int32_t y)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    surface->x = x;
    surface->y = y;

    ukui_surface_apply_position(surface);
}

static void ukui_surface_apply_role(struct ukui_surface *surface)
{
    struct kywc_view *kywc_view = &surface->view->base;

    switch (surface->role) {
    case UKUI_SURFACE_ROLE_NORMAL:
        kywc_view->role = KYWC_VIEW_ROLE_NORMAL;
        break;
    case UKUI_SURFACE_ROLE_DESKTOP:
        kywc_view->role = KYWC_VIEW_ROLE_DESKTOP;
        break;
    case UKUI_SURFACE_ROLE_PANEL:
        kywc_view->role = KYWC_VIEW_ROLE_PANEL;
        break;
    case UKUI_SURFACE_ROLE_ONSCREENDISPLAY:
        kywc_view->role = KYWC_VIEW_ROLE_ONSCREENDISPLAY;
        break;
    case UKUI_SURFACE_ROLE_NOTIFICATION:
        kywc_view->role = KYWC_VIEW_ROLE_NOTIFICATION;
        break;
    case UKUI_SURFACE_ROLE_TOOLTIP:
        kywc_view->role = KYWC_VIEW_ROLE_TOOLTIP;
        break;
    case UKUI_SURFACE_ROLE_CRITICALNOTIFICATION:
        kywc_view->role = KYWC_VIEW_ROLE_CRITICALNOTIFICATION;
        break;
    case UKUI_SURFACE_ROLE_APPLETPOPUP:
        kywc_view->role = KYWC_VIEW_ROLE_APPLETPOPUP;
        break;
    case UKUI_SURFACE_ROLE_SCREENLOCK:
        kywc_view->role = KYWC_VIEW_ROLE_SCREENLOCK;
        break;
    case UKUI_SURFACE_ROLE_WATERMARK:
        kywc_view->role = KYWC_VIEW_ROLE_WATERMARK;
        break;
    case UKUI_SURFACE_ROLE_SYSTEMWINDOW:
        kywc_view->role = KYWC_VIEW_ROLE_SYSTEMWINDOW;
        break;
    case UKUI_SURFACE_ROLE_INPUTPANEL:
        kywc_view->role = KYWC_VIEW_ROLE_INPUTPANEL;
        break;
    case UKUI_SURFACE_ROLE_LOGOUT:
        kywc_view->role = KYWC_VIEW_ROLE_LOGOUT;
        break;
    case UKUI_SURFACE_ROLE_SCREENLOCKNOTIFICATION:
        kywc_view->role = KYWC_VIEW_ROLE_SCREENLOCKNOTIFICATION;
        break;
    case UKUI_SURFACE_ROLE_SWITCHER:
        kywc_view->role = KYWC_VIEW_ROLE_SWITCHER;
        break;
    }

    view_apply_role(view_from_kywc_view(kywc_view));
    surface->role_changed = false;
}

static void ukui_surface_apply_property(struct ukui_surface *surface)
{
    struct kywc_view *kywc_view = &surface->view->base;
    enum kywc_ssd ssd = kywc_view->ssd;

    if ((surface->property_state >> UKUI_SURFACE_PROPERTY_NO_TITLEBAR) & 0x1) {
        if (surface->no_titlebar) {
            ssd &= ~KYWC_SSD_TITLE;
        } else {
            ssd |= KYWC_SSD_TITLE;
        }
        view_set_decoration(surface->view, ssd);
    }

    surface->property_state = 0;
}

static void ukui_surface_set_usable_area(struct ukui_surface *surface, bool enabled);

static void handle_set_role(struct wl_client *client, struct wl_resource *resource, uint32_t role)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    if (surface->role != role) {
        surface->role = role;
        surface->role_changed = true;
    }

    if (surface->view && surface->role_changed) {
        ukui_surface_apply_role(surface);
        /* if plasma shell change role after map */
        ukui_surface_set_usable_area(surface, true);
    }
}

static void handle_set_skip_taskbar(struct wl_client *client, struct wl_resource *resource,
                                    uint32_t skip)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    surface->skip_taskbar = skip;

    if (surface->view && surface->skip_taskbar != surface->view->base.skip_taskbar) {
        surface->view->base.skip_taskbar = surface->skip_taskbar;
        wl_signal_emit_mutable(&surface->view->base.events.capabilities, NULL);
    }
}

static void handle_set_panel_takes_focus(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t takes_focus)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface || surface->role != UKUI_SURFACE_ROLE_PANEL) {
        return;
    }

    if (surface->view && surface->view->base.focusable != takes_focus) {
        surface->view->base.focusable = surface->view->base.activatable = takes_focus;
    }
}

static void handle_set_skip_switcher(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t skip)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    surface->skip_switcher = skip;

    if (surface->view && surface->skip_switcher != surface->view->base.skip_switcher) {
        surface->view->base.skip_switcher = surface->skip_switcher;
        wl_signal_emit_mutable(&surface->view->base.events.capabilities, NULL);
    }
}

static void handle_set_property(struct wl_client *client, struct wl_resource *resource,
                                uint32_t property, uint32_t value)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    switch (property) {
    case UKUI_SURFACE_PROPERTY_NO_TITLEBAR:
        surface->property_state |= STATE_NO_TITLEBAR;
        surface->no_titlebar = value;
        break;
    case UKUI_SURFACE_PROPERTY_THEME:
    case UKUI_SURFACE_PROPERTY_WINDOW_RADIUS:
    case UKUI_SURFACE_PROPERTY_BORDER_WIDTH:
    case UKUI_SURFACE_PROPERTY_BORDER_COLOR:
    case UKUI_SURFACE_PROPERTY_SHADOW_RADIUS:
    case UKUI_SURFACE_PROPERTY_SHADOW_OFFSET:
    case UKUI_SURFACE_PROPERTY_SHADOW_COLOR:
        break;
    }

    if (surface->view) {
        ukui_surface_apply_property(surface);
    }
}

static void panel_change_layer(struct view *view, bool hide)
{
    // For ukui panel
    // when ukui panel hide, will show a transparent window, it need above other window

    enum layer layer = hide ? LAYER_ABOVE : LAYER_DOCK;
    struct view_layer *view_layer = view_manager_get_layer(layer, false);
    ky_scene_node_reparent(&view->tree->node, view_layer->tree);
}

static void handle_set_panel_auto_hide(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t hide)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface || surface->role != UKUI_SURFACE_ROLE_PANEL) {
        return;
    }

    surface->panel_auto_hide = hide;

    if (surface->view) {
        ukui_surface_set_usable_area(surface, !surface->panel_auto_hide);
        panel_change_layer(surface->view, surface->panel_auto_hide);
    }
}

static void handle_open_under_cursor(struct wl_client *client, struct wl_resource *resource,
                                     int32_t x, int32_t y)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    surface->open_under_cursor = true;

    if (surface->view) {
        struct seat *seat = surface->view->base.focused_seat;
        view_do_move(surface->view, seat->cursor->lx + x, seat->cursor->ly + y);
    }
}

static struct ukui_keyboard_grab *get_ukui_keyboard_grab_from_seat(struct ukui_surface *surface,
                                                                   struct seat *seat)
{
    struct ukui_keyboard_grab *grab, *tmp;
    wl_list_for_each_safe(grab, tmp, &surface->ukui_keyboard_grabs, link) {
        if (grab->keyboard_grab.seat == seat) {
            return grab;
        }
    }
    return NULL;
}

static bool keyboard_grab_key(struct seat_keyboard_grab *keyboard_grab, uint32_t time, uint32_t key,
                              bool pressed, uint32_t modifiers)
{
    struct ukui_surface *surface = keyboard_grab->data;
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(keyboard_grab->seat->wlr_seat);
    wlr_seat_keyboard_enter(keyboard_grab->seat->wlr_seat, surface->wlr_surface, keyboard->keycodes,
                            keyboard->num_keycodes, &keyboard->modifiers);
    wlr_seat_keyboard_send_key(keyboard_grab->seat->wlr_seat, time, key, pressed);
    return true;
}

static void keyboard_grab_cancel(struct seat_keyboard_grab *keyboard_grab)
{
    // For seat destroy
    struct ukui_surface *surface = keyboard_grab->data;
    struct ukui_keyboard_grab *grab =
        get_ukui_keyboard_grab_from_seat(surface, keyboard_grab->seat);
    seat_end_keyboard_grab(keyboard_grab->seat, keyboard_grab);
    wl_list_remove(&grab->link);
    free(grab);
}

static const struct seat_keyboard_grab_interface keyboard_grab_impl = {
    .key = keyboard_grab_key,
    .cancel = keyboard_grab_cancel,
};

static bool end_remain_grab(struct seat *seat, int index, void *data)
{
    wlr_seat_keyboard_end_grab(seat->wlr_seat);
    wlr_seat_keyboard_clear_focus(seat->wlr_seat);
    return false;
}

static struct ukui_keyboard_grab *get_or_create_keyboard_grab(struct ukui_surface *surface,
                                                              struct seat *seat)
{
    struct ukui_keyboard_grab *grab = get_ukui_keyboard_grab_from_seat(surface, seat);
    if (grab) {
        return grab;
    }

    grab = calloc(1, sizeof(struct ukui_keyboard_grab));
    if (!grab) {
        return NULL;
    }

    grab->keyboard_grab.data = surface;
    grab->keyboard_grab.interface = &keyboard_grab_impl;
    seat_start_keyboard_grab(seat, &grab->keyboard_grab);
    wl_list_insert(&surface->ukui_keyboard_grabs, &grab->link);

    return grab;
}

static bool start_grab_keyboard_foreach(struct seat *seat, int index, void *data)
{
    struct ukui_surface *surface = data;
    get_or_create_keyboard_grab(surface, seat);
    return false;
}

static void end_ukui_grab_keyboard(struct ukui_keyboard_grab *ukui_keyboard_grab)
{
    struct ukui_surface *surface = ukui_keyboard_grab->keyboard_grab.data;
    if (surface->view && surface->view->base.role == KYWC_VIEW_ROLE_SWITCHER) {
        view_manager_set_switcher_shown(false);
    }

    seat_end_keyboard_grab(ukui_keyboard_grab->keyboard_grab.seat,
                           &ukui_keyboard_grab->keyboard_grab);
    wl_list_remove(&ukui_keyboard_grab->link);
    free(ukui_keyboard_grab);
}

static void handle_grab_keyboard(struct wl_client *client, struct wl_resource *resource,
                                 struct wl_resource *wl_seat)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface || surface->grab_all_keyboard) {
        return;
    }

    if (surface->view && surface->view->base.role == KYWC_VIEW_ROLE_SWITCHER) {
        view_manager_set_switcher_shown(true);
    }

    if (wl_seat) {
        struct seat *seat = seat_from_resource(wl_seat);
        if (seat) {
            end_remain_grab(seat, 0, NULL);
            get_or_create_keyboard_grab(surface, seat);
        }
    } else {
        surface->grab_all_keyboard = true;
        input_manager_for_each_seat(end_remain_grab, NULL);
        input_manager_for_each_seat(start_grab_keyboard_foreach, surface);
    }
}

static void handle_set_icon(struct wl_client *client, struct wl_resource *resource,
                            const char *icon_name)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);
    if (!surface->wlr_surface) {
        return;
    }

    if (surface->view) {
        view_set_icon_name(surface->view, icon_name);
        return;
    }

    free(surface->icon_name);
    surface->icon_name = icon_name ? strdup(icon_name) : NULL;
}

static void handle_activate(struct wl_client *client, struct wl_resource *resource)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);

    if (surface->wlr_surface && surface->view && surface->wlr_surface->mapped) {
        /* activation request */
        kywc_view_activate(&surface->view->base);
        seat_focus_surface(input_manager_get_default_seat(), surface->view->surface);
        return;
    }
}

static const struct ukui_surface_interface ukui_surface_impl = {
    .destroy = handle_destroy,
    .set_output = handle_set_output,
    .set_position = handle_set_position,
    .set_skip_taskbar = handle_set_skip_taskbar,
    .set_skip_switcher = handle_set_skip_switcher,
    .set_property = handle_set_property,
    .set_role = handle_set_role,
    .set_panel_auto_hide = handle_set_panel_auto_hide,
    .open_under_cursor = handle_open_under_cursor,
    .set_panel_takes_focus = handle_set_panel_takes_focus,
    .grab_keyboard = handle_grab_keyboard,
    .set_icon = handle_set_icon,
    .activate = handle_activate,
};

static void surface_handle_output_update_usable_area(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, output_update_usable_area);
    struct kywc_box *usable_area = data;

    struct kywc_box geo;
    kywc_output_effective_geometry(surface->view->output, &geo);

    struct kywc_box *view_geo = &surface->view->base.geometry;
    int mid_x = view_geo->x + view_geo->width / 2;
    int mid_y = view_geo->y + view_geo->height / 2;

    if (view_geo->width > view_geo->height) {
        if (mid_y - geo.y < geo.y + geo.height - mid_y) {
            // position is top
            int y = view_geo->y + view_geo->height;
            geo.height -= y - geo.y;
            geo.y = y;
        } else {
            // position is bottom
            geo.height = view_geo->y - geo.y;
        }
    } else {
        if (mid_x - geo.x < geo.x + geo.width - mid_x) {
            // position is left
            int x = view_geo->width + view_geo->x;
            geo.width -= x - geo.x;
            geo.x = x;
        } else {
            // position is right
            geo.width = view_geo->x - geo.x;
        }
    }

    /* intersect usable_area and geo */
    usable_area->x = geo.x > usable_area->x ? geo.x : usable_area->x;
    usable_area->y = geo.y > usable_area->y ? geo.y : usable_area->y;
    usable_area->width = geo.width < usable_area->width ? geo.width : usable_area->width;
    usable_area->height = geo.height < usable_area->height ? geo.height : usable_area->height;
}

static void ukui_surface_set_usable_area(struct ukui_surface *surface, bool enabled)
{
    bool had_area = !wl_list_empty(&surface->output_update_usable_area.link);
    bool has_area = enabled && surface->role == UKUI_SURFACE_ROLE_PANEL;

    if (!has_area) {
        if (had_area) {
            wl_list_remove(&surface->output_update_usable_area.link);
            wl_list_init(&surface->output_update_usable_area.link);
            output_update_usable_area(surface->view->output);
            surface->view->base.unconstrained = false;
        }
        return;
    }

    if (!had_area) {
        surface->output_update_usable_area.notify = surface_handle_output_update_usable_area;
        output_add_update_usable_area_listener(surface->view->output,
                                               &surface->output_update_usable_area, false);
        surface->view->base.unconstrained = true;
    }

    output_update_usable_area(surface->view->output);
}

static void surface_handle_view_minimize(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, view_minimize);
    ukui_surface_set_usable_area(surface, !surface->view->base.minimized);
}

static void surface_handle_view_size(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, view_size);
    ukui_surface_set_usable_area(surface, true);
}

static void surface_handle_view_position(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, view_position);
    ukui_surface_set_usable_area(surface, true);

    if (wl_resource_get_version(surface->resource) >= UKUI_SURFACE_POSITION_SINCE_VERSION) {
        ukui_surface_send_position(surface->resource, surface->view->base.geometry.x,
                                   surface->view->base.geometry.y);
    }
}

static void surface_handle_view_output(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, view_output);
    struct kywc_output *old_output = data;

    if (!wl_list_empty(&surface->output_update_usable_area.link)) {
        wl_list_remove(&surface->output_update_usable_area.link);
        wl_list_init(&surface->output_update_usable_area.link);
        output_update_usable_area(old_output);
    }

    ukui_surface_set_usable_area(surface, true);
}

static void surface_handle_view_map(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, view_map);
    ukui_surface_set_usable_area(surface, !surface->panel_auto_hide);

    surface->view_minimize.notify = surface_handle_view_minimize;
    wl_signal_add(&surface->view->base.events.minimize, &surface->view_minimize);
    surface->view_size.notify = surface_handle_view_size;
    wl_signal_add(&surface->view->base.events.size, &surface->view_size);
    surface->view_position.notify = surface_handle_view_position;
    wl_signal_add(&surface->view->base.events.position, &surface->view_position);
    surface->view_output.notify = surface_handle_view_output;
    wl_signal_add(&surface->view->events.output, &surface->view_output);

    if (wl_resource_get_version(surface->resource) >= UKUI_SURFACE_POSITION_SINCE_VERSION) {
        ukui_surface_send_position(surface->resource, surface->view->base.geometry.x,
                                   surface->view->base.geometry.y);
    }
}

static void surface_handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, view_unmap);

    wl_list_remove(&surface->view_minimize.link);
    wl_list_remove(&surface->view_size.link);
    wl_list_remove(&surface->view_position.link);
    wl_list_remove(&surface->view_output.link);

    ukui_surface_set_usable_area(surface, false);

    struct ukui_keyboard_grab *grab, *tmp;
    wl_list_for_each_safe(grab, tmp, &surface->ukui_keyboard_grabs, link) {
        end_ukui_grab_keyboard(grab);
    }
}

static void surface_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, view_destroy);

    wl_list_remove(&surface->view_destroy.link);
    wl_list_remove(&surface->view_map.link);
    wl_list_remove(&surface->view_unmap.link);

    surface->view = NULL;
}

static void surface_handle_map(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, surface_map);

    /* useless once we connect surface and view */
    wl_list_remove(&surface->surface_map.link);
    wl_list_init(&surface->surface_map.link);

    /* get view from surface */
    if (!surface->view) {
        surface->view = view_try_from_wlr_surface(surface->wlr_surface);
        if (!surface->view) {
            kywc_log(KYWC_DEBUG, "surface is not a toplevel");
            surface->buffer = ky_scene_buffer_try_from_surface(surface->wlr_surface);
            return;
        }
        wl_signal_add(&surface->view->base.events.destroy, &surface->view_destroy);
    }

    surface->view->base.skip_taskbar = surface->skip_taskbar;
    surface->view->base.skip_switcher = surface->skip_switcher;

    if (surface->role_changed) {
        ukui_surface_apply_role(surface);
    }

    if (surface->icon_name) {
        view_set_icon_name(surface->view, surface->icon_name);
    }

    if (surface->property_state != 0) {
        ukui_surface_apply_property(surface);
    }

    if (surface->open_under_cursor) {
        struct seat *seat = surface->view->base.focused_seat;
        surface->view->base.has_initial_position = true;
        view_do_move(surface->view, seat->cursor->lx, seat->cursor->ly);
    }

    /* apply set_position called before map */
    if (surface->x != INT32_MAX || surface->y != INT32_MAX) {
        surface->view->base.has_initial_position = true;
        ukui_surface_apply_position(surface);
    }

    surface->view_map.notify = surface_handle_view_map;
    wl_signal_add(&surface->view->base.events.map, &surface->view_map);
    surface->view_unmap.notify = surface_handle_view_unmap;
    wl_signal_add(&surface->view->base.events.unmap, &surface->view_unmap);

    /* workaround to fix this listener is behind view map */
    if (surface->view->base.mapped) {
        surface_handle_view_map(&surface->view_map, NULL);
    }
}

static void surface_handle_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_surface *surface = wl_container_of(listener, surface, surface_destroy);

    struct ukui_keyboard_grab *grab, *tmp;
    wl_list_for_each_safe(grab, tmp, &surface->ukui_keyboard_grabs, link) {
        end_ukui_grab_keyboard(grab);
    }

    wl_list_remove(&surface->surface_map.link);
    wl_list_remove(&surface->surface_destroy.link);
    wl_list_remove(&surface->output_disable.link);

    surface->wlr_surface = NULL;
    surface->output = NULL;
}

static void ukui_surface_handle_resource_destroy(struct wl_resource *resource)
{
    struct ukui_surface *surface = wl_resource_get_user_data(resource);

    if (surface->wlr_surface) {
        surface_handle_destroy(&surface->surface_destroy, NULL);
    }

    if (surface->view) {
        if (surface->view->base.mapped) {
            surface_handle_view_unmap(&surface->view_unmap, NULL);
        }
        surface_handle_view_destroy(&surface->view_destroy, NULL);
    }

    free(surface->icon_name);
    free(surface);
}

static void handle_create_surface(struct wl_client *client, struct wl_resource *shell_resource,
                                  uint32_t id, struct wl_resource *surface_resource)
{
    struct wlr_surface *wlr_surface = wlr_surface_from_resource(surface_resource);
    if (!wlr_surface) {
        return;
    }

    /* create a plasma surface */
    struct ukui_surface *surface = calloc(1, sizeof(struct ukui_surface));
    if (!surface) {
        return;
    }

    int version = wl_resource_get_version(shell_resource);
    struct wl_resource *resource = wl_resource_create(client, &ukui_surface_interface, version, id);
    if (!resource) {
        free(surface);
        wl_client_post_no_memory(client);
        return;
    }
    surface->resource = resource;

    wl_resource_set_implementation(resource, &ukui_surface_impl, surface,
                                   ukui_surface_handle_resource_destroy);

    surface->x = surface->y = INT32_MAX;
    surface->role = UKUI_SURFACE_ROLE_NORMAL;
    wl_list_init(&surface->output_update_usable_area.link);
    wl_list_init(&surface->ukui_keyboard_grabs);

    surface->wlr_surface = wlr_surface;
    surface->surface_map.notify = surface_handle_map;
    wl_signal_add(&wlr_surface->events.map, &surface->surface_map);
    surface->surface_destroy.notify = surface_handle_destroy;
    wl_signal_add(&wlr_surface->events.destroy, &surface->surface_destroy);
    surface->output_disable.notify = surface_handle_output_disable;
    wl_list_init(&surface->output_disable.link);

    surface->view = view_try_from_wlr_surface(surface->wlr_surface);
    surface->view_destroy.notify = surface_handle_view_destroy;

    if (surface->view) {
        wl_list_init(&surface->view_map.link);
        wl_list_init(&surface->view_unmap.link);
        wl_signal_add(&surface->view->base.events.destroy, &surface->view_destroy);
    }

    /* workaround to fix this request is behind surface map */
    if (surface->wlr_surface->mapped) {
        surface_handle_map(&surface->surface_map, NULL);
    }
}

static bool send_seat_output(struct seat *seat, int index, void *data)
{
    struct wl_resource *resource = data;
    struct kywc_output *kywc_output = kywc_output_at_point(seat->cursor->lx, seat->cursor->ly);
    ukui_shell_send_current_output(resource, kywc_output->name, seat->name);
    return false;
}

static void handle_get_current_output(struct wl_client *client, struct wl_resource *shell_resource)
{
    uint32_t version = wl_resource_get_version(shell_resource);
    if (version >= UKUI_SHELL_CURRENT_OUTPUT_SINCE_VERSION) {
        input_manager_for_each_seat(send_seat_output, shell_resource);
    }
    if (version >= UKUI_SHELL_DONE_SINCE_VERSION) {
        ukui_shell_send_done(shell_resource);
    }
}

static const struct ukui_shell_interface ukui_shell_impl = {
    .create_surface = handle_create_surface,
    .get_current_output = handle_get_current_output,
};

static void ukui_shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(client, &ukui_shell_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct ukui_shell *shell = data;
    wl_resource_set_implementation(resource, &ukui_shell_impl, shell, NULL);

    if (version >= UKUI_SHELL_CURRENT_OUTPUT_SINCE_VERSION) {
        input_manager_for_each_seat(send_seat_output, resource);
    }
    if (version >= UKUI_SHELL_DONE_SINCE_VERSION) {
        ukui_shell_send_done(resource);
    }
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_shell *shell = wl_container_of(listener, shell, display_destroy);
    wl_list_remove(&shell->display_destroy.link);
    wl_global_destroy(shell->global);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct ukui_shell *shell = wl_container_of(listener, shell, server_destroy);
    wl_list_remove(&shell->server_destroy.link);
    free(shell);
}

bool ukui_shell_create(struct server *server)
{
    struct ukui_shell *shell = calloc(1, sizeof(struct ukui_shell));
    if (!shell) {
        return false;
    }

    shell->global = wl_global_create(server->display, &ukui_shell_interface, UKUI_SHELL_VERSION,
                                     shell, ukui_shell_bind);
    if (!shell->global) {
        kywc_log(KYWC_WARN, "ukui shell create failed");
        free(shell);
        return false;
    }

    shell->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &shell->server_destroy);
    shell->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &shell->display_destroy);

    return true;
}
