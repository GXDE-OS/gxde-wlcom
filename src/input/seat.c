// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <kywc/log.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>

#include "input/keyboard.h"
#include "input/seat.h"
#include "input_p.h"
#include "server.h"
#include "util/time.h"
#include "view/view.h"

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat *seat = wl_container_of(listener, seat, destroy);

    wl_list_remove(&seat->destroy.link);

    seat->wlr_seat = NULL;
    seat_destroy(seat);
}

struct seat *seat_create(struct input_manager *input_manager, const char *name)
{
    struct seat *seat = calloc(1, sizeof(struct seat));
    if (!seat) {
        return NULL;
    }

    seat->wlr_seat = wlr_seat_create(input_manager->server->display, name);
    if (!seat->wlr_seat) {
        free(seat);
        return false;
    }

    seat->wlr_seat->data = seat;
    seat->scene = input_manager->server->scene;
    seat->layout = input_manager->server->layout;
    seat->pointer_gestures = input_manager->pointer_gestures;
    seat->manager = input_manager;

    seat->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->wlr_seat->events.destroy, &seat->destroy);

    seat->name = strdup(name);
    wl_list_init(&seat->inputs);
    wl_list_init(&seat->keyboards);
    wl_signal_init(&seat->events.destroy);

    seat->state.cursor_theme = NULL;
    seat->state.cursor_size = 24;
    if (!seat_read_config(seat)) {
        kywc_log(KYWC_ERROR, "seat(%s) read config error!", seat->name);
    }

    seat->cursor = cursor_create(seat);
    seat->keyboard = keyboard_create(seat, NULL);

    wl_list_insert(&input_manager->seats, &seat->link);
    kywc_log(KYWC_DEBUG, "seat(%s) is created", seat->name);
    wl_signal_emit_mutable(&input_manager->events.new_seat, seat);

    return seat;
}

static void _seat_destroy(struct seat *seat)
{
    struct wlr_seat *wlr_seat = seat->wlr_seat;
    /* wlr_seat is null when display_destroy */
    if (!wlr_seat) {
        return;
    }

    wl_list_remove(&seat->destroy.link);

    struct keyboard *keyboard, *keyboard_tmp;
    wl_list_for_each_safe(keyboard, keyboard_tmp, &seat->keyboards, link) {
        keyboard_destroy(keyboard);
    }

    cursor_destroy(seat->cursor);

    wlr_seat_destroy(wlr_seat);
}

void seat_destroy(struct seat *seat)
{
    kywc_log(KYWC_DEBUG, "seat(%s) destroy", seat->name);
    wl_signal_emit_mutable(&seat->events.destroy, NULL);

    wl_list_remove(&seat->link);

    /* cancel grab when seat destroy */
    if (seat->pointer_grab && seat->pointer_grab->interface->cancel) {
        seat->pointer_grab->interface->cancel(seat->pointer_grab);
    }
    if (seat->keyboard_grab && seat->keyboard_grab->interface->cancel) {
        seat->keyboard_grab->interface->cancel(seat->keyboard_grab);
    }
    if (seat->touch_grab && seat->touch_grab->interface->cancel) {
        seat->touch_grab->interface->cancel(seat->touch_grab);
    }

    struct input *input, *tmp;
    wl_list_for_each_safe(input, tmp, &seat->inputs, seat_link) {
        seat_remove_input(input);
    }

    _seat_destroy(seat);

    free(seat->name);
    free(seat);
}

void seat_consider_destroy(struct seat *seat)
{
    struct input *input;
    wl_list_for_each(input, &seat->inputs, seat_link) {
        if (!input->prop.is_virtual) {
            return;
        }
    }

    seat_destroy(seat);
}

static void seat_update_capabilities(struct seat *seat)
{
    struct input *input;
    seat->caps = 0;
    wl_list_for_each(input, &seat->inputs, seat_link) {
        switch (input->prop.type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            seat->caps |= WL_SEAT_CAPABILITY_KEYBOARD;
            break;
        case WLR_INPUT_DEVICE_POINTER:
        case WLR_INPUT_DEVICE_TABLET_TOOL:
            seat->caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            seat->caps |= WL_SEAT_CAPABILITY_TOUCH;
            break;
        case WLR_INPUT_DEVICE_SWITCH:
        case WLR_INPUT_DEVICE_TABLET_PAD:
            break;
        }
    }

    wlr_seat_set_capabilities(seat->wlr_seat, seat->caps);
}

void seat_add_input(struct seat *seat, struct input *input)
{
    input->seat = seat;
    wl_list_insert(&seat->inputs, &input->seat_link);

    switch (input->prop.type) {
    case WLR_INPUT_DEVICE_POINTER:
    case WLR_INPUT_DEVICE_TOUCH:
    case WLR_INPUT_DEVICE_TABLET_TOOL:
        curosr_add_input(seat, input);
        break;
    case WLR_INPUT_DEVICE_KEYBOARD:
        keyboard_add_input(seat, input);
        break;
    case WLR_INPUT_DEVICE_TABLET_PAD:
        break;
    case WLR_INPUT_DEVICE_SWITCH:
        break;
    }

    seat_update_capabilities(seat);
}

void seat_remove_input(struct input *input)
{
    struct seat *seat = input->seat;

    switch (input->prop.type) {
    case WLR_INPUT_DEVICE_POINTER:
    case WLR_INPUT_DEVICE_TOUCH:
    case WLR_INPUT_DEVICE_TABLET_TOOL:
        cursor_remove_input(input);
        break;
    case WLR_INPUT_DEVICE_KEYBOARD:
        keyboard_remove_input(input);
        break;
    case WLR_INPUT_DEVICE_TABLET_PAD:
        break;
    case WLR_INPUT_DEVICE_SWITCH:
        break;
    }

    wl_list_remove(&input->seat_link);
    input->seat = NULL;

    seat_update_capabilities(seat);
}

struct seat *seat_from_resource(struct wl_resource *resource)
{
    struct wlr_seat_client *seat_client = wl_resource_get_user_data(resource);
    struct wlr_seat *wlr_seat = seat_client->seat;

    return wlr_seat ? wlr_seat->data : NULL;
}

struct seat *seat_from_wlr_seat(struct wlr_seat *wlr_seat)
{
    return wlr_seat->data;
}

void seat_start_pointer_grab(struct seat *seat, struct seat_pointer_grab *pointer_grab)
{
    struct seat_pointer_grab *grab = seat->pointer_grab;
    if (grab == pointer_grab) {
        return;
    }

    pointer_grab->seat = seat;
    seat->pointer_grab = pointer_grab;

    if (grab && grab->interface->cancel) {
        grab->interface->cancel(grab);
    }

    /* when we move quickly with left buttion pressed at view edges,
     * hold_mode is entered by a cursor motion event then client requests move
     */
    seat->cursor->hold_mode = false;
    /* clear last click state */
    seat->cursor->last_click_pressed = false;
}

void seat_end_pointer_grab(struct seat *seat, struct seat_pointer_grab *pointer_grab)
{
    if (seat->pointer_grab == pointer_grab) {
        seat->pointer_grab = NULL;
    }
}

void seat_start_keyboard_grab(struct seat *seat, struct seat_keyboard_grab *keyboard_grab)
{
    struct seat_keyboard_grab *grab = seat->keyboard_grab;
    if (grab == keyboard_grab) {
        return;
    }

    keyboard_grab->seat = seat;
    seat->keyboard_grab = keyboard_grab;

    if (grab && grab->interface->cancel) {
        grab->interface->cancel(grab);
    }
}

void seat_end_keyboard_grab(struct seat *seat, struct seat_keyboard_grab *keyboard_grab)
{
    if (seat->keyboard_grab == keyboard_grab) {
        seat->keyboard_grab = NULL;
    }
}

void seat_start_touch_grab(struct seat *seat, struct seat_touch_grab *touch_grab)
{
    struct seat_touch_grab *grab = seat->touch_grab;
    if (grab == touch_grab) {
        return;
    }

    touch_grab->seat = seat;
    seat->touch_grab = touch_grab;

    if (grab && grab->interface->cancel) {
        grab->interface->cancel(grab);
    }
}

void seat_end_touch_grab(struct seat *seat, struct seat_touch_grab *touch_grab)
{
    if (seat->touch_grab == touch_grab) {
        seat->touch_grab = NULL;
    }
}

void seat_notify_motion(struct seat *seat, struct wlr_surface *surface, uint32_t time, double sx,
                        double sy, bool first_enter)
{
    struct wlr_seat *wlr_seat = seat->wlr_seat;
    struct wlr_surface *prev = wlr_seat->pointer_state.focused_surface;

    if (first_enter || surface != prev) {
        wlr_seat_pointer_notify_enter(wlr_seat, surface, sx, sy);
        kywc_log(KYWC_DEBUG, "pointer enter surface %p", surface);
    }

    wlr_seat_pointer_notify_motion(wlr_seat, time, sx, sy);
}

void seat_notify_button(struct seat *seat, uint32_t time, uint32_t button, bool pressed)
{
    struct wlr_seat *wlr_seat = seat->wlr_seat;

    enum wlr_button_state state = pressed ? WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED;
    wlr_seat_pointer_notify_button(wlr_seat, time, button, state);
}

void seat_notify_leave(struct seat *seat, struct wlr_surface *surface)
{
    struct wlr_seat *wlr_seat = seat->wlr_seat;
    struct wlr_surface *prev = wlr_seat->pointer_state.focused_surface;

    if (!surface || surface == prev) {
        wlr_seat_pointer_notify_clear_focus(wlr_seat);
    }
}

// TODO: set xwayland seat if surface is xwayland surface
void seat_focus_surface(struct seat *seat, struct wlr_surface *surface)
{
    struct view *view = surface ? view_try_from_wlr_surface(surface) : NULL;
    if (view && !view->base.focusable) {
        return;
    }

    struct wlr_seat *wlr_seat = seat->wlr_seat;
    if (surface) {
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(wlr_seat);
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(wlr_seat, surface, keyboard->keycodes,
                                           keyboard->num_keycodes, &keyboard->modifiers);
        } else {
            wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface, NULL, 0, NULL);
        }
    } else {
        wlr_seat_keyboard_notify_clear_focus(wlr_seat);
    }

    input_method_set_focus(seat, surface);
    tablet_set_focus(seat, surface);
}

void seat_feed_pointer_motion(struct seat *seat, double x, double y, bool absolute)
{
    cursor_move(seat->cursor, NULL, x, y, !absolute, false);
    cursor_feed_motion(seat->cursor, current_time_msec());
    wlr_seat_pointer_notify_frame(seat->wlr_seat);
}

void seat_feed_pointer_button(struct seat *seat, uint32_t button, bool pressed)
{
    cursor_feed_button(seat->cursor, button, pressed, current_time_msec(),
                       DEFAULT_DOUBLE_CLICK_TIME);
    wlr_seat_pointer_notify_frame(seat->wlr_seat);
}

void seat_feed_pointer_axis(struct seat *seat, uint32_t axis, double step)
{
    cursor_feed_axis(seat->cursor, axis, WL_POINTER_AXIS_SOURCE_WHEEL, step, 0,
                     current_time_msec());
    wlr_seat_pointer_notify_frame(seat->wlr_seat);
}

void seat_feed_keyboard_key(struct seat *seat, uint32_t key, bool pressed)
{
    struct wlr_keyboard_key_event wlr_event = {
        .time_msec = current_time_msec(),
        .keycode = key,
        .update_state = true,
        .state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
    };

    wlr_keyboard_notify_key(seat->keyboard->wlr_keyboard, &wlr_event);
}
