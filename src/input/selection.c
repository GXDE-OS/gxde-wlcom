// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif

#include <kywc/log.h>

#include "config.h"
#include "input_p.h"
#include "scene/surface.h"
#include "server.h"
#include "view/view.h"

struct selection_manager {
    struct config *config;
    struct wl_listener new_seat;
    struct wl_listener server_destroy;
};

/* selection per seat */
struct selection {
    struct selection_manager *manager;
    struct seat *seat;
    struct wl_listener seat_destroy;

    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener destroy_drag;

    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;

    /* only one drag_icon in seat at the same time */
    struct wlr_drag_icon *drag_icon;
    struct ky_scene_node *icon_node;
    struct ky_scene_node *surface_node;

    struct wl_listener drag_icon_map;
    struct wl_listener drag_icon_unmap;
    struct wl_listener drag_icon_commit;
    struct wl_listener drag_icon_destroy;

#if HAVE_KDE_CLIPBOARD
    struct wl_listener set_selection;
    struct wl_listener set_primary_selection;
    int clipboard_selection_pid;
    int primary_selection_pid;
#endif
    bool draging;
};

#if HAVE_KDE_CLIPBOARD
static const char *service_bus = "org.kde.KWin";
static const char *service_path = "/Clipboard";
static const char *service_interface = "org.kde.kwin.Clipboard";

// SD_BUS_METHOD("getClipboardSelectionPid", "", "i", get_clipboard_selection_pid, 0),
static int get_clipboard_selection_pid(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct seat *seat = input_manager_get_default_seat();
    struct selection *selection = seat->selection;

    return sd_bus_reply_method_return(msg, "i", selection->clipboard_selection_pid);
}

// SD_BUS_METHOD("getPrimarySelectionPid", "", "i", get_primary_selection_pid, 0),
static int get_primary_selection_pid(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{
    struct seat *seat = input_manager_get_default_seat();
    struct selection *selection = seat->selection;

    return sd_bus_reply_method_return(msg, "i", selection->primary_selection_pid);
}

// SD_BUS_PROPERTY("GetClipboardSelectionPid", "i", get_selection_pid, 0,
// SD_BUS_VTABLE_PROPERTY_CONST),
static int get_selection_pid(sd_bus *bus, const char *path, const char *interface,
                             const char *property, sd_bus_message *reply, void *userdata,
                             sd_bus_error *ret_error)
{
    struct seat *seat = input_manager_get_default_seat();
    struct selection *selection = seat->selection;

    return sd_bus_message_append_basic(reply, 'i', &selection->clipboard_selection_pid);
}

// SD_BUS_PROPERTY("GetPrimarySelectionPid", "i", get_primary_pid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
static int get_primary_pid(sd_bus *bus, const char *path, const char *interface,
                           const char *property, sd_bus_message *reply, void *userdata,
                           sd_bus_error *ret_error)
{
    struct seat *seat = input_manager_get_default_seat();
    struct selection *selection = seat->selection;

    return sd_bus_message_append_basic(reply, 'i', &selection->primary_selection_pid);
}

static const sd_bus_vtable clipboard_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("getClipboardSelectionPid", "", "i", get_clipboard_selection_pid, 0),
    SD_BUS_METHOD("getPrimarySelectionPid", "", "i", get_primary_selection_pid, 0),
    SD_BUS_PROPERTY("GetClipboardSelectionPid", "i", get_selection_pid, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("GetPrimarySelectionPid", "i", get_primary_pid, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_SIGNAL("clipboardSelectionPidChanged", "i", 0),
    SD_BUS_SIGNAL("primarySelectionPidChanged", "i", 0),
    SD_BUS_VTABLE_END,
};

static pid_t get_pid_by_wlr_surface(struct wlr_surface *surface)
{
    pid_t pid = 0;
    if (!surface) {
        return pid;
    }
#if HAVE_XWAYLAND
    struct wlr_xwayland_surface *xwayland = wlr_xwayland_surface_try_from_wlr_surface(surface);
    if (xwayland) {
        return xwayland->pid;
    }
#endif
    struct wl_client *client = wl_resource_get_client(surface->resource);
    wl_client_get_credentials(client, &pid, NULL, NULL);
    return pid;
}

static void send_selection_pid_changed(struct selection *selection, struct wlr_seat *seat,
                                       bool is_primary)
{
    pid_t pid = 0, current_pid = 0;
    char *signal_name = NULL;
    struct wlr_surface *focused_surface = seat->keyboard_state.focused_surface;

    pid = get_pid_by_wlr_surface(focused_surface);
    if (!is_primary && pid != selection->clipboard_selection_pid) {
        current_pid = pid;
        selection->clipboard_selection_pid = pid;
        signal_name = "clipboardSelectionPidChanged";
    } else if (is_primary && pid != selection->primary_selection_pid) {
        current_pid = pid;
        selection->primary_selection_pid = pid;
        signal_name = "primarySelectionPidChanged";
    }

    if (signal_name) {
        sd_bus *bus = sd_bus_slot_get_bus(selection->manager->config->slot);
        sd_bus_emit_signal(bus, service_path, service_interface, signal_name, "i", current_pid);
    }
}

static void handle_set_selection(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, set_selection);
    struct wlr_seat *seat = data;

    send_selection_pid_changed(selection, seat, false);
}

static void handle_set_primary_selection(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, set_primary_selection);
    struct wlr_seat *seat = data;

    send_selection_pid_changed(selection, seat, true);
}
#endif

static void handle_drag_icon_map(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, drag_icon_map);
    ky_scene_node_set_enabled(selection->icon_node, true);
}

static void handle_drag_icon_unmap(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, drag_icon_unmap);
    ky_scene_node_set_enabled(selection->icon_node, false);
}

static void handle_drag_icon_commit(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, drag_icon_commit);
    struct wlr_surface *surface = selection->drag_icon->surface;

    ky_scene_node_set_position(selection->surface_node,
                               selection->surface_node->x + surface->current.dx,
                               selection->surface_node->y + surface->current.dy);
}

static void handle_drag_icon_destroy(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, drag_icon_destroy);

    wl_list_remove(&selection->drag_icon_map.link);
    wl_list_remove(&selection->drag_icon_unmap.link);
    wl_list_remove(&selection->drag_icon_commit.link);
    wl_list_remove(&selection->drag_icon_destroy.link);

    ky_scene_node_destroy(selection->icon_node);
    selection->icon_node = NULL;
}

static void handle_request_set_selection(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, request_set_selection);
    struct wlr_seat *seat = selection->seat->wlr_seat;
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(seat, event->source, event->serial);
}

static void handle_request_start_drag(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, request_start_drag);
    struct wlr_seat *wlr_seat = selection->seat->wlr_seat;
    struct wlr_seat_request_start_drag_event *event = data;

    if (wlr_seat_validate_pointer_grab_serial(wlr_seat, event->origin, event->serial)) {
        wlr_seat_start_pointer_drag(wlr_seat, event->drag, event->serial);
        return;
    }

    struct wlr_touch_point *point;
    if (wlr_seat_validate_touch_grab_serial(wlr_seat, event->origin, event->serial, &point)) {
        wlr_seat_start_touch_drag(wlr_seat, event->drag, event->serial, point);
        return;
    }

    wlr_data_source_destroy(event->drag->source);
}

static void handle_start_drag(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, start_drag);
    struct wlr_drag *wlr_drag = data;
    struct wlr_drag_icon *drag_icon = wlr_drag->icon;

    selection->draging = true;
    selection->drag_icon = drag_icon;
    wl_signal_add(&wlr_drag->events.destroy, &selection->destroy_drag);

    /* drag icon may be NULL */
    if (!drag_icon) {
        kywc_log(KYWC_INFO, "Started drag but not set a drag icon");
        return;
    }

    struct view_layer *layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
    struct ky_scene_tree *tree = ky_scene_tree_create(layer->tree);
    struct ky_scene_tree *surface_tree = ky_scene_subsurface_tree_create(tree, drag_icon->surface);
    selection->icon_node = &tree->node;
    selection->surface_node = &surface_tree->node;

    ky_scene_node_set_position(selection->icon_node, selection->seat->cursor->lx,
                               selection->seat->cursor->ly);
    ky_scene_node_set_enabled(selection->icon_node, drag_icon->surface->mapped);

    selection->drag_icon_map.notify = handle_drag_icon_map;
    wl_signal_add(&drag_icon->surface->events.map, &selection->drag_icon_map);
    selection->drag_icon_unmap.notify = handle_drag_icon_unmap;
    wl_signal_add(&drag_icon->surface->events.unmap, &selection->drag_icon_unmap);
    selection->drag_icon_commit.notify = handle_drag_icon_commit;
    wl_signal_add(&drag_icon->surface->events.commit, &selection->drag_icon_commit);
    selection->drag_icon_destroy.notify = handle_drag_icon_destroy;
    wl_signal_add(&drag_icon->events.destroy, &selection->drag_icon_destroy);
}

static void handle_destroy_drag(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, destroy_drag);
    wl_list_remove(&selection->destroy_drag.link);
    selection->draging = false;
}

static void handle_request_set_primary_selection(struct wl_listener *listener, void *data)
{
    struct selection *selection =
        wl_container_of(listener, selection, request_set_primary_selection);
    struct wlr_seat *seat = selection->seat->wlr_seat;
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

static void handle_seat_destory(struct wl_listener *listener, void *data)
{
    struct selection *selection = wl_container_of(listener, selection, seat_destroy);

    wl_list_remove(&selection->request_start_drag.link);
    wl_list_remove(&selection->start_drag.link);
#if HAVE_KDE_CLIPBOARD
    wl_list_remove(&selection->set_selection.link);
    wl_list_remove(&selection->set_primary_selection.link);
#endif
    wl_list_remove(&selection->request_set_selection.link);
    wl_list_remove(&selection->request_set_primary_selection.link);
    wl_list_remove(&selection->seat_destroy.link);

    free(selection);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct selection *selection = calloc(1, sizeof(struct selection));
    if (!selection) {
        return;
    }

    struct selection_manager *manager = wl_container_of(listener, manager, new_seat);
    selection->manager = manager;
    struct seat *seat = data;
    selection->seat = seat;
    seat->selection = selection;

    selection->seat_destroy.notify = handle_seat_destory;
    wl_signal_add(&seat->events.destroy, &selection->seat_destroy);

    selection->request_start_drag.notify = handle_request_start_drag;
    wl_signal_add(&seat->wlr_seat->events.request_start_drag, &selection->request_start_drag);
    selection->start_drag.notify = handle_start_drag;
    wl_signal_add(&seat->wlr_seat->events.start_drag, &selection->start_drag);
    selection->destroy_drag.notify = handle_destroy_drag;
    wl_list_init(&selection->destroy_drag.link);

#if HAVE_KDE_CLIPBOARD
    selection->set_selection.notify = handle_set_selection;
    wl_signal_add(&seat->wlr_seat->events.set_selection, &selection->set_selection);
    selection->set_primary_selection.notify = handle_set_primary_selection;
    wl_signal_add(&seat->wlr_seat->events.set_primary_selection, &selection->set_primary_selection);
#endif
    selection->request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&seat->wlr_seat->events.request_set_selection, &selection->request_set_selection);
    selection->request_set_primary_selection.notify = handle_request_set_primary_selection;
    wl_signal_add(&seat->wlr_seat->events.request_set_primary_selection,
                  &selection->request_set_primary_selection);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct selection_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->new_seat.link);
    free(manager);
}

bool selection_manager_create(struct input_manager *input_manager)
{
    struct selection_manager *manager = calloc(1, sizeof(struct selection_manager));
    if (!manager) {
        return false;
    }

#if HAVE_KDE_CLIPBOARD
    manager->config = config_manager_add_config(NULL, service_bus, service_path, service_interface,
                                                clipboard_vtable, manager);
    if (!manager->config) {
        free(manager);
        return false;
    }
#endif
    wlr_data_device_manager_create(input_manager->server->display);
    wlr_data_control_manager_v1_create(input_manager->server->display);
    wlr_primary_selection_v1_device_manager_create(input_manager->server->display);

    manager->new_seat.notify = handle_new_seat;
    wl_signal_add(&input_manager->events.new_seat, &manager->new_seat);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &manager->server_destroy);

    return true;
}

void selection_handle_cursor_move(struct seat *seat, int lx, int ly)
{
    if (!seat->selection || !seat->selection->draging || !seat->selection->icon_node) {
        return;
    }

    /* update dnd icon if support */
    ky_scene_node_set_position(seat->selection->icon_node, lx, ly);
}

bool selection_is_draging(struct seat *seat)
{
    return seat->selection && seat->selection->draging;
}
