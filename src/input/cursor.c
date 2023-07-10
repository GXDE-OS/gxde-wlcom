#define _POSIX_C_SOURCE 200809L
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <time.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <kywc/log.h>

#include "input/cursor.h"
#include "input_p.h"

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

static bool cursor_set_node(struct cursor_node *cursor_node, struct ky_scene_node *hover)
{
    if (hover == cursor_node->node) {
        return false;
    }

    if (cursor_node->node) {
        wl_list_remove(&cursor_node->destroy.link);
    }
    if (hover) {
        ky_scene_node_add_destroy_listener(hover, &cursor_node->destroy);
    }
    cursor_node->node = hover;

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
        return cursor_set_node(&cursor->hover, hover);
    }
    /* update cursor focus node */
    return cursor_set_node(&cursor->focus, hover);
}

static void _cursor_feed_motion(struct cursor *cursor, uint32_t time)
{
    struct seat *seat = cursor->seat;
    struct ky_scene_node *old_hover = cursor->hover.node;
    bool changed = cursor_update_node(cursor, false);

    bool left_button_pressed =
        LEFT_BUTTON_PRESSED(cursor->last_click_button, cursor->last_click_pressed);
    /* if hold press moving but not draging */
    if (left_button_pressed && cursor->focus.node && cursor->focus.node != cursor->hover.node) {
        // && !seat->selection->draging) {
        struct input_event_node *inode = input_event_node_from_node(cursor->focus.node);
        if (inode && inode->impl->hover) {
            cursor->hold_mode = inode->impl->hover(seat, cursor->focus.node, cursor->lx, cursor->ly,
                                                   time, false, true, inode->data);
        }
        if (cursor->hold_mode) {
            return;
        }
    }

    /* mark grab_mode to false, hover to node again */
    cursor->hold_mode = false;

    /* cursor has moved to another node */
    struct input_event_node *inode = input_event_node_from_node(cursor->hover.node);
    if (changed && old_hover) {
        struct input_event_node *old_inode = input_event_node_from_node(old_hover);
        if (old_inode && old_inode->impl->leave) {
            bool leave = input_event_node_root(old_inode) != input_event_node_root(inode);
            old_inode->impl->leave(seat, old_hover, leave, old_inode->data);
        }
    }

    /* hover current node */
    if (inode && inode->impl->hover) {
        inode->impl->hover(seat, cursor->hover.node, cursor->sx, cursor->sy, time, changed, false,
                           inode->data);
    }

    selection_handle_cursor_move(seat, cursor->lx, cursor->ly);

    if (!cursor->hover.node) {
        /* once no node found under the cursor, restore cursor to default */
        cursor_set_image(cursor, CURSOR_DEFAULT);
        /* clear pointer focus if hover changed to null */
        if (changed) {
            seat_notify_leave(seat, NULL);
        }
    }
}

static void cursor_feed_motion(struct cursor *cursor, uint32_t time)
{
    struct seat *seat = cursor->seat;
    if (seat->pointer_grab && seat->pointer_grab->interface->motion &&
        seat->pointer_grab->interface->motion(seat->pointer_grab, time, cursor->lx, cursor->ly)) {
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
    if (leave && cursor->hover.node) {
        struct input_event_node *inode = input_event_node_from_node(cursor->hover.node);
        if (inode && inode->impl->leave) {
            inode->impl->leave(cursor->seat, cursor->hover.node, false, inode->data);
        }
        /* clear hover */
        wl_list_remove(&cursor->hover.destroy.link);
        cursor->hover.node = NULL;
    }
    _cursor_feed_motion(cursor, time);
}

static void cursor_feed_button(struct cursor *cursor, uint32_t button, bool pressed, uint32_t time)
{
    struct seat *seat = cursor->seat;
    if (seat->pointer_grab && seat->pointer_grab->interface->button &&
        seat->pointer_grab->interface->button(seat->pointer_grab, time, button, pressed)) {
        return;
    }

    bool last_is_pressed = cursor->last_click_pressed;
    uint32_t last_button = cursor->last_click_button;
    cursor->last_click_pressed = pressed;

    /* current focus node */
    struct ky_scene_node *old_focus = cursor->focus.node;
    bool changed = cursor_update_node(cursor, true);

    /* old focus node and view */
    struct input_event_node *old_inode = input_event_node_from_node(old_focus);
    struct input_event_node *inode = input_event_node_from_node(cursor->focus.node);

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
            inode->impl->hover(seat, cursor->focus.node, cursor->sx, cursor->sy, time, true, false,
                               inode->data);
        } else {
            cursor_set_image(cursor, CURSOR_DEFAULT);
        }
        cursor->hold_mode = false;
        return;
    }

    /* update surface coord if surface size changed when click, like maximize */
    // if (cursor->hover == cursor->focus && !seat->selection->draging) {
    if (cursor->hover.node == cursor->focus.node) {
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
        inode->impl->click(seat, cursor->focus.node, button, pressed, time, double_click,
                           inode->data);
    }

    if (!cursor->focus.node) {
        /* clear keyboard focus */
        seat_focus_surface(seat, NULL);
    }
}

static void cursor_handle_motion(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, motion);
    struct wlr_pointer_motion_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    cursor_move(cursor, &event->pointer->base, event->delta_x, event->delta_y, true, false);
    cursor_feed_motion(cursor, event->time_msec);
}

static void cursor_handle_motion_absolute(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    cursor_move(cursor, &event->pointer->base, event->x, event->y, false, true);
    cursor_feed_motion(cursor, event->time_msec);
}

static void cursor_handle_button(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, button);
    struct wlr_pointer_button_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    cursor_feed_button(cursor, event->button, event->state == WLR_BUTTON_PRESSED, event->time_msec);
}

static void cursor_handle_axis(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, axis);
    struct wlr_pointer_axis_event *event = data;
    struct seat *seat = cursor->seat;
    idle_manager_notify_activity(seat);

    if (seat->pointer_grab && seat->pointer_grab->interface->axis &&
        seat->pointer_grab->interface->axis(seat->pointer_grab, event->time_msec,
                                            event->orientation == WLR_AXIS_ORIENTATION_VERTICAL,
                                            event->delta)) {
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

static void cursor_handle_tablet_tool_axis(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, tablet_tool_axis);
    struct wlr_tablet_tool_axis_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    /* force to pointer when move and resize */
    if (cursor->seat->pointer_grab) {
        cursor->tablet_tool_tip_simulation_pointer = true;
    }

    bool change_x = event->updated_axes & WLR_TABLET_TOOL_AXIS_X;
    bool change_y = event->updated_axes & WLR_TABLET_TOOL_AXIS_Y;
    if (change_x || change_y) {
        switch (event->tool->type) {
        case WLR_TABLET_TOOL_TYPE_LENS:
        case WLR_TABLET_TOOL_TYPE_MOUSE:
            cursor_move(cursor, &event->tablet->base, event->dx, event->dy, true, false);
            break;
        default:
            cursor_move(cursor, &event->tablet->base, change_x ? event->x : NAN,
                        change_y ? event->y : NAN, false, true);
            break;
        }

        if (cursor->tablet_tool_tip_simulation_pointer) {
            cursor_feed_motion(cursor, event->time_msec);
            wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
            return;
        }
    }

    if (!cursor->tablet_tool_tip_simulation_pointer) {
        tablet_handle_tool_axis(event);
    }
}

static void cursor_handle_tablet_tool_proximity(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, tablet_tool_proximity);
    struct wlr_tablet_tool_proximity_event *event = data;
    idle_manager_notify_activity(cursor->seat);
    cursor_move(cursor, &event->tablet->base, event->x, event->y, false, true);

    if (!cursor->tablet_tool_tip_simulation_pointer && tablet_handle_tool_proximity(event)) {
        return;
    }

    if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT) {
        return;
    }

    cursor_feed_motion(cursor, event->time_msec);
    wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
}

static void cursor_handle_tablet_tool_tip(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, tablet_tool_tip);
    struct wlr_tablet_tool_tip_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    if (cursor->tablet_tool_tip_simulation_pointer && event->state == WLR_TABLET_TOOL_TIP_UP) {
        cursor->tablet_tool_tip_simulation_pointer = false;
        cursor_feed_button(cursor, BTN_LEFT, false, event->time_msec);
        wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
        /* workaround to send a tool-tip up */
    }

    if (tablet_handle_tool_tip(event)) {
        return;
    }

    cursor->tablet_tool_tip_simulation_pointer = true;
    cursor_feed_button(cursor, BTN_LEFT, true, event->time_msec);
    wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
}

static void cursor_handle_tablet_tool_button(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, tablet_tool_button);
    struct wlr_tablet_tool_button_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    if (cursor->tablet_tool_buttons > 0 && cursor->tablet_tool_button_simulation_pointer) {
        cursor_feed_button(cursor, BTN_RIGHT, event->state == WLR_BUTTON_PRESSED, event->time_msec);
        wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
    } else if (tablet_handle_tool_button(event)) {
        cursor->tablet_tool_button_simulation_pointer = false;
    } else {
        cursor->tablet_tool_button_simulation_pointer = true;
    }

    switch (event->state) {
    case WLR_BUTTON_PRESSED:
        cursor->tablet_tool_buttons++;
        break;
    case WLR_BUTTON_RELEASED:
        if (cursor->tablet_tool_buttons == 0) {
            kywc_log(KYWC_ERROR, "inconsistent tablet tool button events");
        } else {
            cursor->tablet_tool_buttons--;
        }
        break;
    }
}

static void cursor_handle_touch_up(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, touch_up);
    struct wlr_touch_up_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    if (cursor->touch_simulation_pointer) {
        if (cursor->pointer_touch_id == event->touch_id) {
            cursor->pointer_touch_up = true;
            cursor_feed_button(cursor, BTN_LEFT, false, event->time_msec);
        }
        return;
    }

    touch_handle_up(event);
}

static void cursor_handle_touch_down(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, touch_down);
    struct wlr_touch_down_event *event = data;
    idle_manager_notify_activity(cursor->seat);
    // TODO: hide cursor and show it when pointer motion
    // cursor_set_image(cursor, CURSOR_NONE);
    cursor_move(cursor, &event->touch->base, event->x, event->y, false, true);

    if (touch_handle_down(event)) {
        return;
    }

    cursor->touch_simulation_pointer = true;
    cursor->pointer_touch_id = event->touch_id;
    cursor_feed_motion(cursor, event->time_msec);
    cursor_feed_button(cursor, BTN_LEFT, true, event->time_msec);
}

static void cursor_handle_touch_motion(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, touch_motion);
    struct wlr_touch_motion_event *event = data;
    idle_manager_notify_activity(cursor->seat);
    cursor_move(cursor, &event->touch->base, event->x, event->y, false, true);

    if (cursor->touch_simulation_pointer) {
        if (cursor->pointer_touch_id == event->touch_id) {
            cursor_feed_motion(cursor, event->time_msec);
        }
        return;
    }

    touch_handle_motion(event);
}

static void cursor_handle_touch_cancel(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, touch_cancel);
    struct wlr_touch_cancel_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    if (cursor->touch_simulation_pointer) {
        if (cursor->pointer_touch_id == event->touch_id) {
            cursor->pointer_touch_up = true;
            cursor_feed_button(cursor, BTN_LEFT, false, event->time_msec);
        }
        return;
    }

    touch_handle_cancel(event);
}

static void cursor_handle_touch_frame(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, touch_frame);

    if (cursor->touch_simulation_pointer) {
        wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
        if (cursor->pointer_touch_up) {
            cursor->pointer_touch_up = false;
            cursor->touch_simulation_pointer = false;
        }
        return;
    }

    wlr_seat_touch_notify_frame(cursor->seat->wlr_seat);
}

static void cursor_handle_swipe_begin(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, swipe_begin);
    struct wlr_pointer_swipe_begin_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    wlr_pointer_gestures_v1_send_swipe_begin(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                             event->time_msec, event->fingers);
}

static void cursor_handle_swipe_update(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, swipe_update);
    struct wlr_pointer_swipe_update_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    wlr_pointer_gestures_v1_send_swipe_update(cursor->seat->pointer_gestures,
                                              cursor->seat->wlr_seat, event->time_msec, event->dx,
                                              event->dy);
}

static void cursor_handle_swipe_end(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, swipe_end);
    struct wlr_pointer_swipe_end_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    wlr_pointer_gestures_v1_send_swipe_end(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                           event->time_msec, event->cancelled);
}

static void cursor_handle_pinch_begin(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, pinch_begin);
    struct wlr_pointer_pinch_begin_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    wlr_pointer_gestures_v1_send_pinch_begin(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                             event->time_msec, event->fingers);
}

static void cursor_handle_pinch_update(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, pinch_update);
    struct wlr_pointer_pinch_update_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    wlr_pointer_gestures_v1_send_pinch_update(cursor->seat->pointer_gestures,
                                              cursor->seat->wlr_seat, event->time_msec, event->dx,
                                              event->dy, event->scale, event->rotation);
}

static void cursor_handle_pinch_end(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, pinch_end);
    struct wlr_pointer_pinch_end_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    wlr_pointer_gestures_v1_send_pinch_end(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                           event->time_msec, event->cancelled);
}

static void cursor_handle_hold_begin(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, hold_begin);
    struct wlr_pointer_hold_begin_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    wlr_pointer_gestures_v1_send_hold_begin(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                            event->time_msec, event->fingers);
}

static void cursor_handle_hold_end(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, hold_end);
    struct wlr_pointer_hold_end_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    wlr_pointer_gestures_v1_send_hold_end(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                          event->time_msec, event->cancelled);
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

static void cursor_node_handle_destroy(struct wl_listener *listener, void *data)
{
    struct cursor_node *cursor_node = wl_container_of(listener, cursor_node, destroy);
    wl_list_remove(&cursor_node->destroy.link);
    cursor_node->node = NULL;
}

#define CURSOR_ADD_SIGNAL(signal)                                                                  \
    cursor->signal.notify = cursor_handle_##signal;                                                \
    wl_signal_add(&wlr_cursor->events.signal, &cursor->signal);

struct cursor *cursor_create(struct seat *seat)
{
    struct cursor *cursor = calloc(1, sizeof(struct cursor));
    if (!cursor) {
        return NULL;
    }

    struct wlr_cursor *wlr_cursor = wlr_cursor_create();
    if (!wlr_cursor) {
        free(cursor);
        return NULL;
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

    CURSOR_ADD_SIGNAL(motion);
    CURSOR_ADD_SIGNAL(motion_absolute);
    CURSOR_ADD_SIGNAL(button);
    CURSOR_ADD_SIGNAL(axis);
    CURSOR_ADD_SIGNAL(frame);

    CURSOR_ADD_SIGNAL(swipe_begin);
    CURSOR_ADD_SIGNAL(swipe_update);
    CURSOR_ADD_SIGNAL(swipe_end);
    CURSOR_ADD_SIGNAL(pinch_begin);
    CURSOR_ADD_SIGNAL(pinch_update);
    CURSOR_ADD_SIGNAL(pinch_end);
    CURSOR_ADD_SIGNAL(hold_begin);
    CURSOR_ADD_SIGNAL(hold_end);

    CURSOR_ADD_SIGNAL(touch_up);
    CURSOR_ADD_SIGNAL(touch_down);
    CURSOR_ADD_SIGNAL(touch_motion);
    CURSOR_ADD_SIGNAL(touch_cancel);
    CURSOR_ADD_SIGNAL(touch_frame);

    CURSOR_ADD_SIGNAL(tablet_tool_axis);
    CURSOR_ADD_SIGNAL(tablet_tool_proximity);
    CURSOR_ADD_SIGNAL(tablet_tool_tip);
    CURSOR_ADD_SIGNAL(tablet_tool_button);

    cursor->request_set_cursor.notify = cursor_handle_request_set_cursor;
    wl_signal_add(&seat->wlr_seat->events.request_set_cursor, &cursor->request_set_cursor);

    cursor->hover.destroy.notify = cursor_node_handle_destroy;
    cursor->focus.destroy.notify = cursor_node_handle_destroy;

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
    wl_list_remove(&cursor->swipe_begin.link);
    wl_list_remove(&cursor->swipe_update.link);
    wl_list_remove(&cursor->swipe_end.link);
    wl_list_remove(&cursor->pinch_begin.link);
    wl_list_remove(&cursor->pinch_update.link);
    wl_list_remove(&cursor->pinch_end.link);
    wl_list_remove(&cursor->hold_begin.link);
    wl_list_remove(&cursor->hold_end.link);
    wl_list_remove(&cursor->touch_up.link);
    wl_list_remove(&cursor->touch_down.link);
    wl_list_remove(&cursor->touch_motion.link);
    wl_list_remove(&cursor->touch_cancel.link);
    wl_list_remove(&cursor->touch_frame.link);
    wl_list_remove(&cursor->tablet_tool_axis.link);
    wl_list_remove(&cursor->tablet_tool_proximity.link);
    wl_list_remove(&cursor->tablet_tool_tip.link);
    wl_list_remove(&cursor->tablet_tool_button.link);

    if (cursor->hover.node) {
        wl_list_remove(&cursor->hover.destroy.link);
    }
    if (cursor->focus.node) {
        wl_list_remove(&cursor->focus.destroy.link);
    }

    wlr_xcursor_manager_destroy(cursor->xcursor_manager);
    wlr_cursor_destroy(cursor->wlr_cursor);

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
        wlr_cursor_unset_image(cursor->wlr_cursor);
        return;
    }

    cursor->client_requested = false;
    cursor->name = name;
    wlr_cursor_set_xcursor(cursor->wlr_cursor, cursor->xcursor_manager, cursor_image[name]);
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

void cursor_move(struct cursor *cursor, struct wlr_input_device *dev, double x, double y,
                 bool delta, bool absolute)
{
    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;

    if (delta) {
        wlr_cursor_move(wlr_cursor, dev, x, y);
    } else if (absolute) {
        wlr_cursor_warp_absolute(wlr_cursor, dev, x, y);
    } else {
        wlr_cursor_warp(wlr_cursor, dev, x, y);
    }

    cursor->lx = wlr_cursor->x;
    cursor->ly = wlr_cursor->y;
}
