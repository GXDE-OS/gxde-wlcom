// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>

#include <linux/input-event-codes.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/region.h>

#include <kywc/log.h>
#include <kywc/view.h>

#include "input/event.h"
#include "input_p.h"
#include "scene/surface.h"
#include "server.h"
#include "util/time.h"
#include "xwayland.h"

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
        wl_signal_add(&hover->events.destroy, &cursor_node->destroy);
    }
    cursor_node->node = hover;

    return true;
}

static bool cursor_update_node(struct cursor *cursor, bool click)
{
    struct seat *seat = cursor->seat;

    /* find node below the cursor */
    struct ky_scene_node *hover =
        ky_scene_node_at(&seat->scene->tree.node, cursor->lx, cursor->ly, &cursor->sx, &cursor->sy);

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
    /* if hold press moving but not dragging */
    if (left_button_pressed && cursor->focus.node && cursor->focus.node != cursor->hover.node &&
        !selection_is_dragging(seat)) {
        struct input_event_node *inode = input_event_node_from_node(cursor->focus.node);
        if (inode && inode->impl->hover) {
            cursor->hold_mode = inode->impl->hover(seat, cursor->focus.node, cursor->lx, cursor->ly,
                                                   time, false, true, inode->data);
        }
        if (cursor->hold_mode) {
            return;
        }
    }

    /* mark hold_mode to false, hover to node again */
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

/* return true if pointer is locked */
static bool cursor_apply_constraint(struct cursor *cursor, struct wlr_input_device *device,
                                    double *dx, double *dy);

void cursor_feed_motion(struct cursor *cursor, uint32_t time, struct wlr_input_device *device,
                        double dx, double dy, double dx_unaccel, double dy_unaccel)
{
    wlr_relative_pointer_manager_v1_send_relative_motion(
        cursor->seat->manager->relative_pointer, cursor->seat->wlr_seat, (uint64_t)time * 1000, dx,
        dy, dx_unaccel, dy_unaccel);

    if (cursor_apply_constraint(cursor, device, &dx, &dy)) {
        return;
    }

    cursor_move(cursor, device, dx, dy, true, false);

    struct seat_cursor_motion_event event = {
        .device = device ? input_from_wlr_input(device) : NULL,
        .time_msec = time,
        .lx = cursor->lx,
        .ly = cursor->ly,
    };
    wl_signal_emit_mutable(&cursor->seat->events.cursor_motion, &event);

    struct seat_pointer_grab *pointer_grab = cursor->seat->pointer_grab;
    if (pointer_grab && pointer_grab->interface->motion &&
        pointer_grab->interface->motion(pointer_grab, time, cursor->lx, cursor->ly)) {
        return;
    }

    _cursor_feed_motion(cursor, time);
}

static void cursor_motion_absolute(struct cursor *cursor, uint32_t time,
                                   struct wlr_input_device *dev, double x, double y)
{
    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(cursor->wlr_cursor, dev, x, y, &lx, &ly);

    double dx = lx - cursor->lx;
    double dy = ly - cursor->ly;
    cursor_feed_motion(cursor, time, dev, dx, dy, dx, dy);
}

static void cursor_feed_fake_motion(struct cursor *cursor, bool need_frame)
{
    /* skip motion when has grab */
    if (cursor->seat->pointer_grab && cursor->seat->pointer_grab->interface->motion) {
        return;
    }

    _cursor_feed_motion(cursor, current_time_msec());
    if (need_frame) {
        wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
    }
}

void cursor_feed_button(struct cursor *cursor, uint32_t button, bool pressed, uint32_t time,
                        uint32_t double_click_time)
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

    if (wlr_seat_pointer_has_grab(seat->wlr_seat)) {
        seat_notify_button(seat, time, button, pressed);
        return;
    }

    /* old focus node and view */
    struct input_event_node *old_inode = input_event_node_from_node(old_focus);
    struct input_event_node *inode = input_event_node_from_node(cursor->focus.node);

    /* exit hold mode if any button clicked */
    if (cursor->hold_mode) {
        /* send button release to last focus node */
        if (old_inode && old_inode->impl->click) {
            old_inode->impl->click(seat, old_focus, BTN_LEFT, false, time, CLICK_STATE_NONE,
                                   old_inode->data);
        }
        /* leave focus node, otherwise wrong cursor image */
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

    /* no pointer motion or hover node was destroyed */
    if (!cursor->hover.node && cursor->focus.node && inode) {
        inode->impl->hover(seat, cursor->focus.node, cursor->sx, cursor->sy, time, true, false,
                           inode->data);
    }

    /* send a button released event to old focus node */
    if (old_focus && changed && !pressed && last_is_pressed) {
        kywc_log(KYWC_DEBUG, "release button %d in %p", last_button, old_focus);
        if (old_inode && old_inode->impl->click) {
            old_inode->impl->click(seat, old_focus, last_button, false, time,
                                   CLICK_STATE_FOCUS_LOST, old_inode->data);
        }
        /* fix cursor image sometimes */
        if (!selection_is_dragging(seat)) {
            cursor_feed_fake_motion(cursor, false);
        }
        return;
    }

    bool double_click = false;
    if (pressed) {
        if (!changed && button == cursor->last_click_button &&
            time - cursor->last_click_time < double_click_time) {
            double_click = true;
        }
        /* reset after a double click */
        cursor->last_click_time = double_click ? 0 : time;
        cursor->last_click_button = button;
    }

    if (inode && inode->impl->click) {
        inode->impl->click(seat, cursor->focus.node, button, pressed, time,
                           double_click ? CLICK_STATE_DOUBLE : CLICK_STATE_NONE, inode->data);
    }

    /* update surface coord if surface size changed when click, like maximize */
    if (cursor->hover.node == cursor->focus.node && !selection_is_dragging(seat)) {
        cursor_feed_fake_motion(cursor, false);
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

    cursor_set_hidden(cursor, false);
    cursor_feed_motion(cursor, event->time_msec, &event->pointer->base, event->delta_x,
                       event->delta_y, event->unaccel_dx, event->unaccel_dy);
}

static void cursor_handle_motion_absolute(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    cursor_set_hidden(cursor, false);
    cursor_motion_absolute(cursor, event->time_msec, &event->pointer->base, event->x, event->y);
}

static void cursor_handle_button(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, button);
    struct wlr_pointer_button_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    cursor_set_hidden(cursor, false);
    struct input *input = input_from_wlr_input(&event->pointer->base);
    cursor_feed_button(cursor, event->button, event->state == WLR_BUTTON_PRESSED, event->time_msec,
                       input->state.double_click_time);
}

void cursor_feed_axis(struct cursor *cursor, uint32_t orientation, uint32_t source, double delta,
                      int32_t delta_discrete, uint32_t time)
{
    struct seat *seat = cursor->seat;
    if (seat->pointer_grab && seat->pointer_grab->interface->axis &&
        seat->pointer_grab->interface->axis(seat->pointer_grab, time,
                                            orientation == WLR_AXIS_ORIENTATION_VERTICAL, delta)) {
        return;
    }

    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(seat->wlr_seat, time, orientation, delta, delta_discrete, source);
}

static void cursor_handle_axis(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, axis);
    struct wlr_pointer_axis_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    /* workaround to fix zwcada2023 crash */
    static uint32_t time_tmp = 0;
    if (time_tmp == event->time_msec) {
        wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
    }
    time_tmp = event->time_msec;

    cursor_set_hidden(cursor, false);
    struct input *input = input_from_wlr_input(&event->pointer->base);
    cursor_feed_axis(cursor, event->orientation, event->source,
                     input->state.scroll_factor * event->delta,
                     roundf(input->state.scroll_factor * event->delta_discrete), event->time_msec);
}

static void cursor_handle_frame(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, frame);
    if (cursor->seat->pointer_grab) {
        return;
    }
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
}

static void cursor_handle_tablet_tool_axis(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, tablet_tool_axis);
    struct wlr_tablet_tool_axis_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    cursor_set_hidden(cursor, false);
    /* force to pointer when move and resize */
    if (cursor->seat->pointer_grab) {
        cursor->tablet_tool_tip_simulation_pointer = true;
    }

    bool change_x = event->updated_axes & WLR_TABLET_TOOL_AXIS_X;
    bool change_y = event->updated_axes & WLR_TABLET_TOOL_AXIS_Y;
    if (cursor->tablet_tool_tip_simulation_pointer && (change_x || change_y)) {
        if (event->tool->type == WLR_TABLET_TOOL_TYPE_LENS ||
            event->tool->type == WLR_TABLET_TOOL_TYPE_MOUSE) {
            cursor_feed_motion(cursor, event->time_msec, &event->tablet->base, event->dx, event->dy,
                               event->dx, event->dy);
        } else {
            cursor_motion_absolute(cursor, event->time_msec, &event->tablet->base,
                                   change_x ? event->x : NAN, change_y ? event->y : NAN);
        }
        wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
        return;
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
    cursor_set_hidden(cursor, false);

    if (!cursor->tablet_tool_tip_simulation_pointer && tablet_handle_tool_proximity(event)) {
        return;
    }

    if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT) {
        return;
    }

    cursor_motion_absolute(cursor, event->time_msec, &event->tablet->base, event->x, event->y);
    wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
}

static void cursor_handle_tablet_tool_tip(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, tablet_tool_tip);
    struct wlr_tablet_tool_tip_event *event = data;
    struct input *input = input_from_wlr_input(&event->tablet->base);
    idle_manager_notify_activity(cursor->seat);

    cursor_set_hidden(cursor, false);

    /* workaround: tablet simulate pointer drag to fix peony drag */
    cursor_motion_absolute(cursor, event->time_msec, &event->tablet->base, event->x, event->y);
    wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);

    if (cursor->tablet_tool_tip_simulation_pointer && event->state == WLR_TABLET_TOOL_TIP_UP) {
        cursor->tablet_tool_tip_simulation_pointer = false;
        cursor_feed_button(cursor, BTN_LEFT, false, event->time_msec,
                           input->state.double_click_time);
        wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
        /* workaround to send a tool-tip up */
        tablet_handle_tool_tip(event);
        return;
    }

    if (selection_is_dragging(cursor->seat)) {
        return;
    }

    if (tablet_handle_tool_tip(event)) {
        return;
    }

    cursor->tablet_tool_tip_simulation_pointer = true;
    cursor_feed_button(cursor, BTN_LEFT, true, event->time_msec, input->state.double_click_time);
    wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
}

static void cursor_handle_tablet_tool_button(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, tablet_tool_button);
    struct wlr_tablet_tool_button_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    if (selection_is_dragging(cursor->seat)) {
        return;
    }

    cursor_set_hidden(cursor, false);
    if (cursor->tablet_tool_buttons > 0 && cursor->tablet_tool_button_simulation_pointer) {
        struct input *input = input_from_wlr_input(&event->tablet->base);
        cursor_feed_button(cursor, BTN_RIGHT, event->state == WLR_BUTTON_PRESSED, event->time_msec,
                           input->state.double_click_time);
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

    touch_handle_up(event, !cursor->touch_simulation_pointer);

    if (cursor->touch_simulation_pointer && cursor->pointer_touch_id == event->touch_id) {
        cursor->pointer_touch_up = true;
        struct input *input = input_from_wlr_input(&event->touch->base);
        cursor_feed_button(cursor, BTN_LEFT, false, event->time_msec,
                           input->state.double_click_time);
    }
}

static void cursor_handle_touch_down(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, touch_down);
    struct wlr_touch_down_event *event = data;
    idle_manager_notify_activity(cursor->seat);
    cursor_set_hidden(cursor, true);

    /* workaround to fix peony drag icon position */
    cursor_motion_absolute(cursor, event->time_msec, &event->touch->base, event->x, event->y);
    wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);

    if (touch_handle_down(event)) {
        return;
    }

    cursor->touch_simulation_pointer = true;
    cursor->pointer_touch_id = event->touch_id;

    struct input *input = input_from_wlr_input(&event->touch->base);
    cursor_feed_button(cursor, BTN_LEFT, true, event->time_msec, input->state.double_click_time);
}

static void cursor_handle_touch_motion(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, touch_motion);
    struct wlr_touch_motion_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    touch_handle_motion(event, !cursor->touch_simulation_pointer);

    if ((cursor->seat->pointer_grab || cursor->touch_simulation_pointer) &&
        cursor->pointer_touch_id == event->touch_id) {
        cursor_motion_absolute(cursor, event->time_msec, &event->touch->base, event->x, event->y);
    }
}

static void cursor_handle_touch_cancel(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, touch_cancel);
    struct wlr_touch_cancel_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    touch_handle_cancel(event, !cursor->touch_simulation_pointer);

    if (cursor->touch_simulation_pointer && cursor->pointer_touch_id == event->touch_id) {
        cursor->pointer_touch_up = true;
        struct input *input = input_from_wlr_input(&event->touch->base);
        cursor_feed_button(cursor, BTN_LEFT, false, event->time_msec,
                           input->state.double_click_time);
    }
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

    gesture_state_begin(&cursor->gestures, GESTURE_TYPE_SWIPE, GESTURE_DEVICE_TOUCHPAD,
                        GESTURE_EDGE_NONE, event->fingers);
    wlr_pointer_gestures_v1_send_swipe_begin(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                             event->time_msec, event->fingers);
}

static void cursor_handle_swipe_update(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, swipe_update);
    struct wlr_pointer_swipe_update_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    gesture_state_update(&cursor->gestures, GESTURE_TYPE_SWIPE, GESTURE_DEVICE_TOUCHPAD, event->dx,
                         event->dy, NAN, NAN);
    wlr_pointer_gestures_v1_send_swipe_update(cursor->seat->pointer_gestures,
                                              cursor->seat->wlr_seat, event->time_msec, event->dx,
                                              event->dy);
}

static void cursor_handle_swipe_end(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, swipe_end);
    struct wlr_pointer_swipe_end_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    bool handled = gesture_state_end(&cursor->gestures, GESTURE_TYPE_SWIPE, GESTURE_DEVICE_TOUCHPAD,
                                     event->cancelled);
    /* tell client the gesture is cancelled if gesture handled by compositor */
    wlr_pointer_gestures_v1_send_swipe_end(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                           event->time_msec, event->cancelled || handled);
}

static void cursor_handle_pinch_begin(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, pinch_begin);
    struct wlr_pointer_pinch_begin_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    gesture_state_begin(&cursor->gestures, GESTURE_TYPE_PINCH, GESTURE_DEVICE_TOUCHPAD,
                        GESTURE_EDGE_NONE, event->fingers);
    wlr_pointer_gestures_v1_send_pinch_begin(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                             event->time_msec, event->fingers);
}

static void cursor_handle_pinch_update(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, pinch_update);
    struct wlr_pointer_pinch_update_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    gesture_state_update(&cursor->gestures, GESTURE_TYPE_PINCH, GESTURE_DEVICE_TOUCHPAD, event->dx,
                         event->dy, event->scale, event->rotation);
    wlr_pointer_gestures_v1_send_pinch_update(cursor->seat->pointer_gestures,
                                              cursor->seat->wlr_seat, event->time_msec, event->dx,
                                              event->dy, event->scale, event->rotation);
}

static void cursor_handle_pinch_end(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, pinch_end);
    struct wlr_pointer_pinch_end_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    bool handled = gesture_state_end(&cursor->gestures, GESTURE_TYPE_PINCH, GESTURE_DEVICE_TOUCHPAD,
                                     event->cancelled);
    wlr_pointer_gestures_v1_send_pinch_end(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                           event->time_msec, event->cancelled || handled);
}

static void cursor_handle_hold_begin(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, hold_begin);
    struct wlr_pointer_hold_begin_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    gesture_state_begin(&cursor->gestures, GESTURE_TYPE_HOLD, GESTURE_DEVICE_TOUCHPAD,
                        GESTURE_EDGE_NONE, event->fingers);
    wlr_pointer_gestures_v1_send_hold_begin(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                            event->time_msec, event->fingers);
}

static void cursor_handle_hold_end(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, hold_end);
    struct wlr_pointer_hold_end_event *event = data;
    idle_manager_notify_activity(cursor->seat);

    bool handled = gesture_state_end(&cursor->gestures, GESTURE_TYPE_HOLD, GESTURE_DEVICE_TOUCHPAD,
                                     event->cancelled);
    wlr_pointer_gestures_v1_send_hold_end(cursor->seat->pointer_gestures, cursor->seat->wlr_seat,
                                          event->time_msec, event->cancelled || handled);
}

static void cursor_handle_surface_precommit(struct wl_listener *listener, void *data)
{
    float scale = xwayland_get_scale();
    if (scale == 1.0) {
        return;
    }

    struct cursor *cursor = wl_container_of(listener, cursor, surface_precommit);
    struct wlr_surface_state *pending = data;

    pending->width = xwayland_unscale(pending->width);
    pending->height = xwayland_unscale(pending->height);

    if (pending->committed & WLR_SURFACE_STATE_SURFACE_DAMAGE) {
        wlr_region_scale(&pending->surface_damage, &pending->surface_damage, 1.0 / scale);
    }
    if (pending->committed & WLR_SURFACE_STATE_OFFSET) {
        pending->dx = xwayland_unscale(pending->dx);
        pending->dy = xwayland_unscale(pending->dy);
    }
}

static void cursor_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, surface_destroy);
    wl_list_remove(&cursor->surface_precommit.link);
    wl_list_remove(&cursor->surface_destroy.link);
    wl_list_init(&cursor->surface_precommit.link);
    wl_list_init(&cursor->surface_destroy.link);
    cursor->surface = NULL;
}

void cursor_set_surface(struct cursor *cursor, struct wlr_surface *surface, int32_t hotspot_x,
                        int32_t hotspot_y, struct wl_client *client)
{
    /* unscale cursor surface from xwayland */
    if (xwayland_check_client(client)) {
        if (surface) {
            hotspot_x = xwayland_unscale(hotspot_x);
            hotspot_y = xwayland_unscale(hotspot_y);
        }

        if (cursor->surface != surface) {
            if (cursor->surface) {
                cursor_handle_surface_destroy(&cursor->surface_destroy, NULL);
            }

            if (surface) {
                cursor->surface = surface;
                wl_signal_add(&surface->events.precommit, &cursor->surface_precommit);
                wl_signal_add(&surface->events.destroy, &cursor->surface_destroy);
            }
        }
    }

    /* use this to filter cursor image */
    cursor->client_requested = true;
    wlr_cursor_set_surface(cursor->wlr_cursor, surface, hotspot_x, hotspot_y);
}

static void cursor_handle_request_set_cursor(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, request_set_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client = cursor->seat->wlr_seat->pointer_state.focused_client;
    struct seat_pointer_grab *grab = cursor->seat->pointer_grab;

    if (cursor->image_locks > 0 || cursor->hidden) {
        return;
    }
    if (grab || focused_client != event->seat_client) {
        return;
    }

    cursor_set_surface(cursor, event->surface, event->hotspot_x, event->hotspot_y,
                       event->seat_client->client);
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

void cursor_set_xcursor_manager(struct cursor *cursor, const char *theme, uint32_t size, bool saved)
{
    bool need_set = !cursor->xcursor_manager;
    if (!need_set) {
        bool same_theme = (!cursor->xcursor_manager->name && !theme) ||
                          (theme && cursor->xcursor_manager->name &&
                           strcmp(theme, cursor->xcursor_manager->name) == 0);
        need_set = !same_theme || cursor->xcursor_manager->size != size;
    }
    if (!need_set) {
        return;
    }

    /* clear wlr_cursor state */
    wlr_cursor_unset_image(cursor->wlr_cursor);
    /* destroy the prev one, NULL is ok */
    wlr_xcursor_manager_destroy(cursor->xcursor_manager);
    cursor->xcursor_manager = wlr_xcursor_manager_create(theme, size);
    /* apply the new configuration */
    cursor_rebase(cursor);

    if (!saved) {
        return;
    }

    free((void *)cursor->seat->state.cursor_theme);
    cursor->seat->state.cursor_theme = strdup(cursor->xcursor_manager->name);
    cursor->seat->state.cursor_size = cursor->xcursor_manager->size;

    wl_signal_emit_mutable(&cursor->seat->events.cursor_configure, NULL);
}

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

    /* xcursor manager per seat for cursor theme */
    cursor_set_xcursor_manager(
        cursor, xcursor_theme ? xcursor_theme : seat->state.cursor_theme,
        xcursor_size ? (uint32_t)atoi(xcursor_size) : seat->state.cursor_size, false);

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

    cursor->surface_precommit.notify = cursor_handle_surface_precommit;
    wl_list_init(&cursor->surface_precommit.link);
    cursor->surface_destroy.notify = cursor_handle_surface_destroy;
    wl_list_init(&cursor->surface_destroy.link);

    cursor->hover.destroy.notify = cursor_node_handle_destroy;
    cursor->focus.destroy.notify = cursor_node_handle_destroy;

    gesture_state_init(&cursor->gestures, seat->wlr_seat->display);

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

    wl_list_remove(&cursor->surface_precommit.link);
    wl_list_remove(&cursor->surface_destroy.link);

    if (cursor->hover.node) {
        wl_list_remove(&cursor->hover.destroy.link);
    }
    if (cursor->focus.node) {
        wl_list_remove(&cursor->focus.destroy.link);
    }

    wlr_xcursor_manager_destroy(cursor->xcursor_manager);
    wlr_cursor_destroy(cursor->wlr_cursor);
    gesture_state_finish(&cursor->gestures);

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
    struct server *server = cursor->seat->manager->server;
    if (server->terminate) {
        return;
    }

    if (cursor->image_locks > 0 || cursor->hidden) {
        return;
    }

    /* early return if cursor not changed when client not requested */
    if (!force && name == cursor->name && !cursor->client_requested) {
        return;
    }

    if (name == CURSOR_NONE) {
        wlr_cursor_unset_image(cursor->wlr_cursor);
    } else {
        wlr_cursor_set_xcursor(cursor->wlr_cursor, cursor->xcursor_manager, cursor_image[name]);
    }

    cursor->client_requested = false;
    cursor->name = name;
    kywc_log(KYWC_DEBUG, "set cursor to %s", cursor_image[name]);
}

void cursor_set_image(struct cursor *cursor, enum cursor_name name)
{
    _cursor_set_image(cursor, name, false);
}

void cursor_set_resize_image(struct cursor *cursor, uint32_t edges)
{
    enum cursor_name name = CURSOR_DEFAULT;

    if (edges == (KYWC_EDGE_TOP | KYWC_EDGE_LEFT)) {
        name = CURSOR_RESIZE_TOP_LEFT;
    } else if (edges == KYWC_EDGE_TOP) {
        name = CURSOR_RESIZE_TOP;
    } else if (edges == (KYWC_EDGE_TOP | KYWC_EDGE_RIGHT)) {
        name = CURSOR_RESIZE_TOP_RIGHT;
    } else if (edges == KYWC_EDGE_RIGHT) {
        name = CURSOR_RESIZE_RIGHT;
    } else if (edges == (KYWC_EDGE_BOTTOM | KYWC_EDGE_RIGHT)) {
        name = CURSOR_RESIZE_BOTTOM_RIGHT;
    } else if (edges == KYWC_EDGE_BOTTOM) {
        name = CURSOR_RESIZE_BOTTOM;
    } else if (edges == (KYWC_EDGE_BOTTOM | KYWC_EDGE_LEFT)) {
        name = CURSOR_RESIZE_BOTTOM_LEFT;
    } else if (edges == KYWC_EDGE_LEFT) {
        name = CURSOR_RESIZE_LEFT;
    }

    _cursor_set_image(cursor, name, false);
}

void cursor_rebase(struct cursor *cursor)
{
    /* some app may think that moving 0 is not moving, so we add a bit of offset */
    cursor_move(cursor, NULL, 0.01, 0.01, true, false);
    _cursor_set_image(cursor, CURSOR_DEFAULT, true);
    cursor_feed_fake_motion(cursor, true);
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

void cursor_set_hidden(struct cursor *cursor, bool hidden)
{
    if (cursor->hidden == hidden) {
        return;
    }

    if (hidden) {
        cursor_set_image(cursor, CURSOR_NONE);
        cursor->hidden = true;
    } else {
        cursor->hidden = false;
        cursor_rebase(cursor);
    }
}

void cursor_lock_image(struct cursor *cursor, bool lock)
{
    if (lock) {
        ++cursor->image_locks;
    } else {
        assert(cursor->image_locks > 0);
        --cursor->image_locks;
    }
}

static void cursor_constraint_warp_to_hint(struct cursor_constraint *constraint)
{
    struct wlr_pointer_constraint_v1_state *current = &constraint->constraint->current;

    if (!(current->committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT)) {
        return;
    }

    struct wlr_surface *surface = constraint->constraint->surface;
    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(surface);
    if (!buffer) {
        return;
    }

    int lx, ly;
    ky_scene_node_coords(&buffer->node, &lx, &ly);

    double sx = current->cursor_hint.x;
    double sy = current->cursor_hint.y;
    cursor_move(constraint->cursor, NULL, lx + sx, ly + sy, false, false);
    wlr_seat_pointer_warp(constraint->cursor->seat->wlr_seat, sx, sy);
}

static void cursor_constraint_deactivate(void *data)
{
    struct cursor_constraint *constraint = data;
    wlr_pointer_constraint_v1_send_deactivated(constraint->constraint);
}

static void cursor_active_constraint(struct cursor *cursor, struct cursor_constraint *constraint,
                                     bool deactivate_later)
{
    struct cursor_constraint *old_constraint = cursor->active_constraint;
    if (old_constraint == constraint) {
        return;
    }
    cursor->active_constraint = constraint;

    if (old_constraint) {
        wl_list_remove(&old_constraint->surface_unmap.link);
        wl_list_init(&old_constraint->surface_unmap.link);
        wl_list_remove(&old_constraint->set_region.link);
        wl_list_init(&old_constraint->set_region.link);

        if (!constraint) {
            cursor_constraint_warp_to_hint(old_constraint);
        }

        if (deactivate_later) {
            struct wl_event_loop *loop = wl_display_get_event_loop(cursor->seat->wlr_seat->display);
            wl_event_loop_add_idle(loop, cursor_constraint_deactivate, old_constraint);
        } else {
            wlr_pointer_constraint_v1_send_deactivated(old_constraint->constraint);
        }
    }

    if (!constraint) {
        return;
    }

    cursor->pending_constraint = NULL;
    wlr_pointer_constraint_v1_send_activated(constraint->constraint);

    struct wlr_surface *surface = constraint->constraint->surface;
    wl_signal_add(&surface->events.unmap, &constraint->surface_unmap);
}

static bool cursor_constraint_check_region(struct cursor_constraint *constraint)
{
    struct wlr_surface *surface = constraint->constraint->surface;
    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(surface);
    if (!buffer) {
        return false;
    }

    int lx, ly;
    ky_scene_node_coords(&buffer->node, &lx, &ly);

    int sx = constraint->cursor->lx - lx;
    int sy = constraint->cursor->ly - ly;

    return pixman_region32_contains_point(&constraint->constraint->region, sx, sy, NULL);
}

static void cursor_set_constraint(struct cursor *cursor, struct cursor_constraint *constraint);

static void cursor_constraint_handle_set_region(struct wl_listener *listener, void *data)
{
    struct cursor_constraint *constraint = wl_container_of(listener, constraint, set_region);
    struct wlr_pointer_constraint_v1 *wlr_constraint = constraint->constraint;
    struct cursor *cursor = constraint->cursor;
    /* check if the cursor is in the region */
    bool in_region = cursor_constraint_check_region(constraint);

    /* type always be WLR_POINTER_CONSTRAINT_V1_CONFINED */
    if (cursor->pending_constraint == constraint && in_region) {
        cursor_active_constraint(cursor, cursor->pending_constraint, false);
    } else if (cursor->active_constraint == constraint && !in_region) {
        /* deactivate the constraint and set pending if not oneshot */
        bool oneshot = wlr_constraint->lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
        cursor_active_constraint(cursor, NULL, oneshot);
        if (!oneshot) {
            cursor_set_constraint(cursor, constraint);
        }
    }
}

static void cursor_set_constraint(struct cursor *cursor, struct cursor_constraint *constraint)
{
    if (cursor->active_constraint) {
        if (cursor->active_constraint == constraint) {
            return;
        }
        /* clear activated constraint always */
        cursor_active_constraint(cursor, NULL, false);
    }

    if (cursor->pending_constraint == constraint) {
        return;
    }

    if (cursor->pending_constraint) {
        wl_list_remove(&cursor->pending_constraint->set_region.link);
        wl_list_init(&cursor->pending_constraint->set_region.link);
    }

    cursor->pending_constraint = constraint;
    if (!constraint) {
        return;
    }

    if (constraint->constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
        wl_signal_add(&constraint->constraint->events.set_region, &constraint->set_region);
    }
    /* activate this constraint if cursor is in the region */
    if (cursor_constraint_check_region(constraint)) {
        cursor_active_constraint(cursor, constraint, false);
    }
}

static bool cursor_apply_constraint(struct cursor *cursor, struct wlr_input_device *device,
                                    double *dx, double *dy)
{
    struct cursor_constraint *constraint = cursor->active_constraint;
    if (!constraint) {
        constraint = cursor->pending_constraint;
    }
    bool has_constraint = constraint && (!device || device->type == WLR_INPUT_DEVICE_POINTER);
    if (!has_constraint) {
        return false;
    }

    /* no need to check the region once locked */
    if (cursor->active_constraint &&
        cursor->active_constraint->constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        return true;
    }

    struct wlr_surface *surface = constraint->constraint->surface;
    struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(surface);
    if (!buffer) {
        return false;
    }

    int lx, ly;
    ky_scene_node_coords(&buffer->node, &lx, &ly);

    double sx = constraint->cursor->lx - lx, sy = constraint->cursor->ly - ly;
    double sx_confined, sy_confined;
    if (!wlr_region_confine(&constraint->constraint->region, sx, sy, sx + *dx, sy + *dy,
                            &sx_confined, &sy_confined)) {
        return false;
    }

    /* activate the pending constraint */
    if (constraint == cursor->pending_constraint) {
        cursor_active_constraint(cursor, constraint, false);
    }

    if (!cursor->active_constraint) {
        return false;
    }
    if (cursor->active_constraint->constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        return true;
    }

    *dx = sx_confined - sx;
    *dy = sy_confined - sy;

    return false;
}

static void cursor_constraint_handle_surface_unmap(struct wl_listener *listener, void *data)
{
    struct cursor_constraint *constraint = wl_container_of(listener, constraint, surface_unmap);
    cursor_active_constraint(constraint->cursor, NULL, false);
}

static void cursor_constraint_handle_destroy(struct wl_listener *listener, void *data)
{
    struct cursor_constraint *constraint = wl_container_of(listener, constraint, destroy);
    struct cursor *cursor = constraint->cursor;

    wl_list_remove(&constraint->destroy.link);
    wl_list_remove(&constraint->set_region.link);
    wl_list_remove(&constraint->surface_unmap.link);

    if (cursor->active_constraint == constraint) {
        cursor_constraint_warp_to_hint(constraint);
        cursor->active_constraint = NULL;
    } else if (cursor->pending_constraint == constraint) {
        cursor->pending_constraint = NULL;
    }

    free(constraint);
}

struct cursor_constraint *cursor_constraint_create(struct cursor *cursor,
                                                   struct wlr_pointer_constraint_v1 *constraint)
{
    struct cursor_constraint *cursor_constraint = calloc(1, sizeof(*cursor_constraint));
    if (!cursor_constraint) {
        return NULL;
    }

    cursor_constraint->cursor = cursor;
    cursor_constraint->constraint = constraint;
    constraint->data = cursor_constraint;

    cursor_constraint->set_region.notify = cursor_constraint_handle_set_region;
    wl_list_init(&cursor_constraint->set_region.link);
    cursor_constraint->destroy.notify = cursor_constraint_handle_destroy;
    wl_signal_add(&constraint->events.destroy, &cursor_constraint->destroy);
    cursor_constraint->surface_unmap.notify = cursor_constraint_handle_surface_unmap;
    wl_list_init(&cursor_constraint->surface_unmap.link);

    struct wlr_surface *surface = cursor->seat->wlr_seat->keyboard_state.focused_surface;
    if (surface && surface == constraint->surface) {
        cursor_set_constraint(cursor, cursor_constraint);
    }

    return cursor_constraint;
}

void cursor_constraint_set_focus(struct seat *seat, struct wlr_surface *surface)
{
    struct wlr_pointer_constraint_v1 *constraint =
        wlr_pointer_constraints_v1_constraint_for_surface(seat->manager->pointer_constraints,
                                                          surface, seat->wlr_seat);
    struct cursor_constraint *cursor_constraint = constraint ? constraint->data : NULL;
    cursor_set_constraint(seat->cursor, cursor_constraint);
}
