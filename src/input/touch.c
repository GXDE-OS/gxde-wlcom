#include <stdlib.h>

#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>

#include <kywc/log.h>

#include "input/seat.h"
#include "input_p.h"
#include "scene/surface.h"
#include "server.h"
#include "view/view.h"

struct touch_manager {
    struct wl_listener new_input;
    struct wl_listener server_destroy;
};

struct touch {
    struct wlr_touch *wlr_touch;
    struct input *input;
    struct wl_listener input_destroy;

    struct wl_list pointers;
};

struct touch_pointer {
    struct touch *touch;
    struct wl_list link;
    struct wlr_surface *surface;
    double ref_lx, ref_ly, ref_sx, ref_sy;
    int32_t touch_id;
};

static struct touch *touch_from_wlr_touch(struct wlr_touch *wlr_touch)
{
    return wlr_touch->data;
}

static void touch_handle_input_destroy(struct wl_listener *listener, void *data)
{
    struct touch *touch = wl_container_of(listener, touch, input_destroy);
    wl_list_remove(&touch->input_destroy.link);

    struct touch_pointer *pointer, *tmp;
    wl_list_for_each_safe(pointer, tmp, &touch->pointers, link) {
        wl_list_remove(&pointer->link);
        free(pointer);
    }

    free(touch);
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
    struct touch_manager *manager = wl_container_of(listener, manager, new_input);
    struct input *input = data;

    /* input has been configured, only care about tablet_tool and tablet_pad */
    if (input->prop.type != WLR_INPUT_DEVICE_TOUCH) {
        return;
    }

    struct touch *touch = calloc(1, sizeof(struct touch));
    if (!touch) {
        return;
    }

    touch->input = input;
    touch->input_destroy.notify = touch_handle_input_destroy;
    wl_signal_add(&input->events.destroy, &touch->input_destroy);

    touch->wlr_touch = wlr_touch_from_input_device(input->wlr_input);
    touch->wlr_touch->data = touch;
    wl_list_init(&touch->pointers);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct touch_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

bool touch_manager_create(struct input_manager *input_manager)
{
    struct touch_manager *manager = calloc(1, sizeof(struct touch_manager));
    if (!manager) {
        return false;
    }

    manager->new_input.notify = handle_new_input;
    input_add_new_listener(&manager->new_input);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &manager->server_destroy);

    return true;
}

static struct wlr_surface *touch_get_surface(struct touch *touch, double *sx, double *sy,
                                             struct wlr_surface **toplevel)
{
    struct seat *seat = touch->input->seat;
    struct cursor *cursor = seat->cursor;

    struct ky_scene_node *node =
        ky_scene_node_at(ky_scene_node_from_scene(seat->scene), cursor->lx, cursor->ly, sx, sy);
    if (!node) {
        return NULL;
    }

    *toplevel = input_event_node_toplevel(input_event_node_from_node(node));
    return wlr_surface_try_from_node(node);
}

static struct touch_pointer *touch_pointer_create(struct touch *touch, int32_t touch_id)
{
    struct touch_pointer *pointer, *free_pointer = NULL;
    wl_list_for_each(pointer, &touch->pointers, link) {
        if (pointer->touch_id == touch_id) {
            return pointer;
        }
        if (!free_pointer && pointer->touch_id < 0) {
            free_pointer = pointer;
        }
    }

    /* not found, reuse the first free one */
    if (free_pointer) {
        free_pointer->touch_id = touch_id;
        return free_pointer;
    }

    /* alloc one if all in used */
    pointer = calloc(1, sizeof(struct touch_pointer));
    if (!pointer) {
        return NULL;
    }

    pointer->touch_id = touch_id;
    pointer->touch = touch;
    wl_list_insert(touch->pointers.prev, &pointer->link);
    return pointer;
}

static struct touch_pointer *touch_pointer_from_id(struct touch *touch, int32_t touch_id)
{
    struct touch_pointer *pointer;
    wl_list_for_each(pointer, &touch->pointers, link) {
        if (pointer->touch_id == touch_id) {
            return pointer;
        }
    }
    return NULL;
}

static void touch_pointer_reset(struct touch_pointer *pointer)
{
    pointer->touch_id = -1;
    /* reinsert to tail */
    wl_list_remove(&pointer->link);
    wl_list_insert(pointer->touch->pointers.prev, &pointer->link);
}

bool touch_handle_down(struct wlr_touch_down_event *event)
{
    struct touch *touch = touch_from_wlr_touch(event->touch);
    if (!touch) {
        return false;
    }

    struct seat *seat = touch->input->seat;
    if (seat->touch_grab && seat->touch_grab->interface->touch &&
        seat->touch_grab->interface->touch(seat->touch_grab, event->time_msec, true)) {
        return true;
    }

    struct wlr_surface *toplevel;
    double sx, sy;
    struct wlr_surface *surface = touch_get_surface(touch, &sx, &sy, &toplevel);
    if (!surface || !wlr_surface_accepts_touch(seat->wlr_seat, surface)) {
        return false;
    }

    struct touch_pointer *pointer = touch_pointer_create(touch, event->touch_id);
    pointer->surface = surface;
    pointer->ref_lx = seat->cursor->lx;
    pointer->ref_ly = seat->cursor->ly;
    pointer->ref_sx = sx;
    pointer->ref_sy = sy;

    wlr_seat_touch_notify_down(seat->wlr_seat, surface, event->time_msec, event->touch_id, sx, sy);

    /* activate and focus the toplevel surface */
    if (toplevel) {
        seat_focus_surface(seat, toplevel);
        struct view *view = view_try_from_wlr_surface(toplevel);
        if (view) {
            kywc_view_activate(&view->base);
        }
    }

    return true;
}

void touch_handle_motion(struct wlr_touch_motion_event *event)
{
    struct touch *touch = touch_from_wlr_touch(event->touch);
    if (!touch) {
        return;
    }

    struct seat *seat = touch->input->seat;
    if (seat->touch_grab && seat->touch_grab->interface->motion &&
        seat->touch_grab->interface->motion(seat->touch_grab, event->time_msec, seat->cursor->lx,
                                            seat->cursor->ly)) {
        return;
    }

    struct touch_pointer *pointer = touch_pointer_from_id(touch, event->touch_id);
    if (!pointer) {
        kywc_log(KYWC_DEBUG, "touch pointer %d may not has down", event->touch_id);
        return;
    }

    double moved_x = seat->cursor->lx - pointer->ref_lx;
    double moved_y = seat->cursor->ly - pointer->ref_ly;
    double sx = moved_x + pointer->ref_sx;
    double sy = moved_y + pointer->ref_sy;
    selection_handle_cursor_move(seat, seat->cursor->lx, seat->cursor->ly);
    wlr_seat_touch_notify_motion(seat->wlr_seat, event->time_msec, event->touch_id, sx, sy);
}

void touch_handle_up(struct wlr_touch_up_event *event)
{
    struct touch *touch = touch_from_wlr_touch(event->touch);
    if (!touch) {
        return;
    }

    struct seat *seat = touch->input->seat;
    if (seat->touch_grab && seat->touch_grab->interface->touch &&
        seat->touch_grab->interface->touch(seat->touch_grab, event->time_msec, false)) {
        return;
    }

    struct touch_pointer *pointer = touch_pointer_from_id(touch, event->touch_id);
    if (!pointer) {
        kywc_log(KYWC_DEBUG, "touch pointer %d may not has down", event->touch_id);
        return;
    }

    wlr_seat_touch_notify_up(seat->wlr_seat, event->time_msec, event->touch_id);
    touch_pointer_reset(pointer);
}

void touch_handle_cancel(struct wlr_touch_cancel_event *event)
{
    struct touch *touch = touch_from_wlr_touch(event->touch);
    if (!touch) {
        return;
    }

    struct touch_pointer *pointer = touch_pointer_from_id(touch, event->touch_id);
    if (!pointer) {
        kywc_log(KYWC_DEBUG, "touch pointer %d may not has down", event->touch_id);
        return;
    }

    struct seat *seat = touch->input->seat;
    wlr_seat_touch_notify_cancel(seat->wlr_seat, pointer->surface);
    touch_pointer_reset(pointer);
}
