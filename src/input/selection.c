#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>

#include <kywc/log.h>

#include "input_p.h"
#include "scene/surface.h"
#include "server.h"
#include "view/view.h"

struct selection_manager {
    struct wl_listener new_seat;
    struct wl_listener server_destroy;
};

/* selection per seat */
struct selection {
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

    bool draging;
};

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

    int x = 0, y = 0;
    ky_scene_node_get_position(selection->surface_node, &x, &y);
    ky_scene_node_set_position(selection->surface_node, x + surface->current.dx,
                               y + surface->current.dy);
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
    /* drag icon may be NULL */
    if (!drag_icon) {
        kywc_log(KYWC_INFO, "Started drag but not set a drag icon");
        return;
    }

    selection->draging = true;
    selection->drag_icon = drag_icon;

    struct view_layer *layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
    struct ky_scene_tree *tree = ky_scene_tree_create(layer->tree);
    struct ky_scene_tree *surface_tree = ky_scene_subsurface_tree_create(tree, drag_icon->surface);
    selection->icon_node = ky_scene_node_from_tree(tree);
    selection->surface_node = ky_scene_node_from_tree(surface_tree);

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
    wl_signal_add(&wlr_drag->events.destroy, &selection->destroy_drag);
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
    wl_list_remove(&selection->request_set_selection.link);
    wl_list_remove(&selection->request_set_primary_selection.link);

    free(selection);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct selection *selection = calloc(1, sizeof(struct selection));
    if (!selection) {
        return;
    }

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

    wlr_data_device_manager_create(input_manager->server->display);
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
