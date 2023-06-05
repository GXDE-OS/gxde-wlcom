#define _POSIX_C_SOURCE 200809L
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <time.h>

#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <kywc/log.h>

#include "input/cursor.h"

/* cursor images used in compositor */
static char *cursor_image[] = {
    "",
    "left_ptr",
    "grabbing",
    "top_left_corner",
    "top_side",
    "top_right_corner",
    "right_side",
    "bottom_right_corner",
    "bottom_side",
    "bottom_left_corner",
    "left_side",
};

static bool cursor_set_hover(struct cursor *cursor, struct ky_scene_node *hover)
{
    if (hover == cursor->hover) {
        return false;
    }

    if (cursor->hover) {
        wl_list_remove(&cursor->hover_destroy.link);
    }
    if (hover) {
        ky_scene_node_add_destroy_listener(hover, &cursor->hover_destroy);
    }
    cursor->hover = hover;
    return true;
}

static bool cursor_set_focus(struct cursor *cursor, struct ky_scene_node *hover)
{
    if (hover == cursor->focus) {
        return false;
    }

    if (cursor->focus) {
        wl_list_remove(&cursor->focus_destroy.link);
    }
    if (hover) {
        ky_scene_node_add_destroy_listener(hover, &cursor->focus_destroy);
    }
    cursor->focus = hover;
    return true;
}

static bool cursor_update_node(struct cursor *cursor, bool click)
{
    struct seat *seat = cursor->seat;

    /* find node below the cursor */
    struct ky_scene_node *hover = ky_scene_node_at(
        ky_scene_node_from_scene(seat->scene), cursor->lx, cursor->ly, &cursor->sx, &cursor->sy);

    /* update cursor hover node */
    if (!click) {
        return cursor_set_hover(cursor, hover);
    }
    /* update cursor focus node */
    return cursor_set_focus(cursor, hover);
}

static void _cursor_feed_motion(struct cursor *cursor, uint32_t time)
{
    struct seat *seat = cursor->seat;
    struct ky_scene_node *old_hover = cursor->hover;
    bool changed = cursor_update_node(cursor, false);

    bool left_button_pressed =
        LEFT_BUTTON_PRESSED(cursor->last_click_button, cursor->last_click_pressed);
    /* if hold press moving but not draging */
    if (left_button_pressed && cursor->focus && cursor->focus != cursor->hover) {
        // && !seat->selection->draging) {
        struct input_event_node *inode = input_event_node_from_node(cursor->focus);
        if (inode && inode->impl->hover) {
            cursor->hold_mode = inode->impl->hover(seat, cursor->focus, cursor->lx, cursor->ly,
                                                   time, false, true, inode->data);
        }
        if (cursor->hold_mode) {
            return;
        }
    }

    /* mark grab_mode to false, hover to node again */
    cursor->hold_mode = false;

    /* cursor has moved to another node */
    struct input_event_node *inode = input_event_node_from_node(cursor->hover);
    if (changed && old_hover) {
        struct input_event_node *old_inode = input_event_node_from_node(old_hover);
        if (old_inode && old_inode->impl->leave) {
            bool leave = input_event_node_root(old_inode) != input_event_node_root(inode);
            old_inode->impl->leave(seat, old_hover, leave, old_inode->data);
        }
    }

    /* hover current node */
    if (inode && inode->impl->hover) {
        inode->impl->hover(seat, cursor->hover, cursor->sx, cursor->sy, time, changed, false,
                           inode->data);
    }

#if 0
    /* update dnd icon if support */
    if (seat->selection->draging && seat->selection->tree_icon) {
        ky_scene_node_set_position(&seat->selection->tree_icon->node, cursor->wlr_cursor->x,
                                    cursor->wlr_cursor->y);
    }
#endif

    if (!cursor->hover) {
        /* once no node found under the cursor, restore cursor to default */
        cursor_set_image(cursor, CURSOR_DEFAULT);
        /* clear pointer focus if hover changed to null */
        if (changed) {
            seat_notify_leave(seat, NULL);
        }
    }
}

static void cursor_feed_motion(struct cursor *cursor, double lx, double ly, uint32_t time)
{
    cursor->lx = lx;
    cursor->ly = ly;
    // kywc_log(KYWC_DEBUG, "cursor move to (%f, %f)", cursor->lx, cursor->ly);

    struct seat *seat = cursor->seat;
    if (seat->pointer_grab && seat->pointer_grab->interface->motion) {
        seat->pointer_grab->interface->motion(seat->pointer_grab, time, lx, ly);
        return;
    }

    _cursor_feed_motion(cursor, time);
}

static void cursor_feed_fake_motion(struct cursor *cursor, bool leave)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t time = now.tv_sec * 1000 + now.tv_nsec / 1000000;

    /* force leave current hover node, then re-hover it */
    if (leave && cursor->hover) {
        struct input_event_node *inode = input_event_node_from_node(cursor->hover);
        if (inode && inode->impl->leave) {
            inode->impl->leave(cursor->seat, cursor->hover, false, inode->data);
        }
        /* clear hover */
        wl_list_remove(&cursor->hover_destroy.link);
        cursor->hover = NULL;
    }
    _cursor_feed_motion(cursor, time);
}

static void cursor_feed_button(struct cursor *cursor, uint32_t button, bool pressed, uint32_t time)
{
    struct seat *seat = cursor->seat;
    if (seat->pointer_grab && seat->pointer_grab->interface->button) {
        seat->pointer_grab->interface->button(seat->pointer_grab, time, button, pressed);
        return;
    }

    bool last_is_pressed = cursor->last_click_pressed;
    uint32_t last_button = cursor->last_click_button;
    cursor->last_click_pressed = pressed;

    /* current focus node */
    struct ky_scene_node *old_focus = cursor->focus;
    bool changed = cursor_update_node(cursor, true);

    /* old focus node and view */
    struct input_event_node *old_inode = input_event_node_from_node(old_focus);
    struct input_event_node *inode = input_event_node_from_node(cursor->focus);

    /* exit hold mode if any botton clicked */
    if (cursor->hold_mode) {
        /* send button release to last focus node */
        if (old_inode && old_inode->impl->click) {
            old_inode->impl->click(seat, old_focus, BTN_LEFT, false, time, false, old_inode->data);
        }
        /* leave focus node, otherwise wrong curser image */
        if (old_inode && old_inode->impl->leave) {
            bool leave = input_event_node_root(old_inode) != input_event_node_root(inode);
            old_inode->impl->leave(seat, old_focus, leave, old_inode->data);
        }
        if (inode && inode->impl->hover) {
            inode->impl->hover(seat, cursor->focus, cursor->sx, cursor->sy, time, true, false,
                               inode->data);
        } else {
            cursor_set_image(cursor, CURSOR_DEFAULT);
        }
        cursor->hold_mode = false;
        return;
    }

    /* update surface coord if surface size changed when click, like maximize */
    // if (cursor->hover == cursor->focus && !seat->selection->draging) {
    if (cursor->hover == cursor->focus) {
        cursor_feed_fake_motion(cursor, false);
    }

    /* send a button released event to old focus node */
    if (old_focus && changed && !pressed && last_is_pressed) {
        kywc_log(KYWC_INFO, "release button %d in %p", last_button, old_focus);
        if (old_inode && old_inode->impl->click) {
            old_inode->impl->click(seat, old_focus, last_button, false, time, false,
                                   old_inode->data);
        }
#if 0
        /* fix cursor image sometimes */
        if (!seat->selection->draging) {
            cursor_feed_fake_motion(cursor, false);
        }
#endif
        return;
    }

    // TODO: double click time in seat config
    bool double_click = false;
    if (pressed) {
        if (!changed && button == cursor->last_click_button &&
            time - cursor->last_click_time < 500) {
            double_click = true;
        }
        /* reset after a double click */
        cursor->last_click_time = double_click ? 0 : time;
        cursor->last_click_button = button;
    }

    if (inode && inode->impl->click) {
        inode->impl->click(seat, cursor->focus, button, pressed, time, double_click, inode->data);
    }

    if (!cursor->focus) {
        /* clear keyboard focus */
        seat_focus_surface(seat, NULL);
    }
}

static void cursor_handle_motion(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, motion);
    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;
    struct wlr_pointer_motion_event *event = data;

    wlr_cursor_move(wlr_cursor, &event->pointer->base, event->delta_x, event->delta_y);
    cursor_feed_motion(cursor, wlr_cursor->x, wlr_cursor->y, event->time_msec);
}

static void cursor_handle_motion_absolute(struct wl_listener *listener, void *data)
{

    struct cursor *cursor = wl_container_of(listener, cursor, motion_absolute);
    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;
    struct wlr_pointer_motion_absolute_event *event = data;

    wlr_cursor_warp_absolute(wlr_cursor, &event->pointer->base, event->x, event->y);
    cursor_feed_motion(cursor, wlr_cursor->x, wlr_cursor->y, event->time_msec);
}

static void cursor_handle_button(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, button);
    struct wlr_pointer_button_event *event = data;

    cursor_feed_button(cursor, event->button, event->state == WLR_BUTTON_PRESSED, event->time_msec);
}

static void cursor_handle_axis(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, axis);
    struct wlr_pointer_axis_event *event = data;

    struct seat *seat = cursor->seat;
    if (seat->pointer_grab && seat->pointer_grab->interface->axis) {
        seat->pointer_grab->interface->axis(seat->pointer_grab, event->time_msec,
                                            event->orientation == WLR_AXIS_ORIENTATION_VERTICAL,
                                            event->delta);
        return;
    }

    /* Notify the client with pointer focus of the axis event. */
    struct wlr_seat *wlr_seat = cursor->seat->wlr_seat;
    wlr_seat_pointer_notify_axis(wlr_seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source);
}

static void cursor_handle_frame(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, frame);

    /* Notify the client with pointer focus of the frame event. */
    struct wlr_seat *wlr_seat = cursor->seat->wlr_seat;
    wlr_seat_pointer_notify_frame(wlr_seat);
}

static void cursor_handle_request_set_cursor(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, request_set_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client = cursor->seat->wlr_seat->pointer_state.focused_client;

    if (focused_client != event->seat_client) {
        return;
    }

    /* use this to filter cursor image */
    cursor->client_requested = true;
    wlr_cursor_set_surface(cursor->wlr_cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

static void cursor_handle_hover_destroy(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, hover_destroy);
    wl_list_remove(&cursor->hover_destroy.link);
    cursor->hover = NULL;
}

static void cursor_handle_focus_destroy(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, focus_destroy);
    wl_list_remove(&cursor->focus_destroy.link);
    cursor->focus = NULL;
}

#define CURSOR_ADD_SIGNAL(signal)                                                                  \
    cursor->signal.notify = cursor_handle_##signal;                                                \
    wl_signal_add(&wlr_cursor->events.signal, &cursor->signal);

struct cursor *cursor_create(struct seat *seat)
{
    struct cursor *cursor = calloc(1, sizeof(struct cursor));
    if (!cursor) {
        return false;
    }

    struct wlr_cursor *wlr_cursor = wlr_cursor_create();
    if (!wlr_cursor) {
        free(cursor);
        return false;
    }

    cursor->seat = seat;
    seat->cursor = cursor;
    cursor->wlr_cursor = wlr_cursor;

    // TODO: multi-layout for multi-seat
    wlr_cursor_attach_output_layout(wlr_cursor, seat->layout);

    const char *xcursor_theme = getenv("XCURSOR_THEME");
    const char *xcursor_size = getenv("XCURSOR_SIZE");
    uint32_t size = xcursor_size ? atoi(xcursor_size) : 24;

    /* xcursor manager per seat for cursor theme */
    cursor->xcursor_manager = wlr_xcursor_manager_create(xcursor_theme, size);
    wlr_xcursor_manager_load(cursor->xcursor_manager, 1.0);

    CURSOR_ADD_SIGNAL(motion);
    CURSOR_ADD_SIGNAL(motion_absolute);
    CURSOR_ADD_SIGNAL(button);
    CURSOR_ADD_SIGNAL(axis);
    CURSOR_ADD_SIGNAL(frame);

    cursor->request_set_cursor.notify = cursor_handle_request_set_cursor;
    wl_signal_add(&seat->wlr_seat->events.request_set_cursor, &cursor->request_set_cursor);
    cursor->hover_destroy.notify = cursor_handle_hover_destroy;
    cursor->focus_destroy.notify = cursor_handle_focus_destroy;

    return cursor;
}

#undef CURSOR_ADD_SIGNAL

void cursor_destroy(struct cursor *cursor)
{
    wl_list_remove(&cursor->motion.link);
    wl_list_remove(&cursor->motion_absolute.link);
    wl_list_remove(&cursor->button.link);
    wl_list_remove(&cursor->axis.link);
    wl_list_remove(&cursor->frame.link);
    wl_list_remove(&cursor->request_set_cursor.link);

    if (cursor->hover) {
        wl_list_remove(&cursor->hover_destroy.link);
    }
    if (cursor->focus) {
        wl_list_remove(&cursor->focus_destroy.link);
    }

    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;
    struct wlr_xcursor_manager *xcursor_manager = cursor->xcursor_manager;
    wlr_xcursor_manager_destroy(xcursor_manager);

    wlr_cursor_destroy(wlr_cursor);

    cursor->seat->cursor = NULL;
    free(cursor);
}

void curosr_add_input(struct seat *seat, struct input *input)
{
    struct wlr_cursor *wlr_cursor = seat->cursor->wlr_cursor;
    wlr_cursor_attach_input_device(wlr_cursor, input->wlr_input);
}

void cursor_remove_input(struct input *input)
{
    struct wlr_cursor *wlr_cursor = input->seat->cursor->wlr_cursor;
    wlr_cursor_detach_input_device(wlr_cursor, input->wlr_input);
}

static void _cursor_set_image(struct cursor *cursor, enum cursor_name name, bool force)
{
    /* early return if cursor not changed when client not requested */
    if (!force && name == cursor->name && !cursor->client_requested) {
        return;
    }

    if (name == CURSOR_NONE) {
        wlr_cursor_set_surface(cursor->wlr_cursor, NULL, 0, 0);
        return;
    }

    cursor->client_requested = false;
    cursor->name = name;
    wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager, cursor_image[name],
                                         cursor->wlr_cursor);
    kywc_log(KYWC_DEBUG, "set cursor to %s", cursor_image[name]);
}

void cursor_set_image(struct cursor *cursor, enum cursor_name name)
{
    _cursor_set_image(cursor, name, false);
}

void cursor_reload_image(struct cursor *cursor, float scale)
{
    wlr_xcursor_manager_load(cursor->xcursor_manager, scale);
    _cursor_set_image(cursor, CURSOR_DEFAULT, true);
}

void cursor_move(struct cursor *cursor, double x, double y, bool delta)
{
    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;

    if (delta) {
        wlr_cursor_move(wlr_cursor, NULL, x, y);
    } else {
        wlr_cursor_warp(wlr_cursor, NULL, x, y);
    }
}
