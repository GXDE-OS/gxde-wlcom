#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <kywc/log.h>
#include <wlr/types/wlr_seat.h>

#include "input/keyboard.h"
#include "input/seat.h"
#include "input_p.h"
#include "server.h"

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

    seat->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->wlr_seat->events.destroy, &seat->destroy);

    seat->name = strdup(name);
    wl_list_init(&seat->inputs);
    wl_list_init(&seat->keyboards);
    wl_signal_init(&seat->events.destroy);

    seat->cursor = cursor_create(seat);

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

    struct input *input;
    wl_list_for_each(input, &seat->inputs, seat_link) {
        seat_remove_input(input);
    }

    _seat_destroy(seat);

    free(seat->name);
    free(seat);
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
            seat->caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            seat->caps |= WL_SEAT_CAPABILITY_TOUCH;
            break;
        case WLR_INPUT_DEVICE_TABLET_TOOL:
            seat->caps |= WL_SEAT_CAPABILITY_POINTER;
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

    if (input->prop.type == WLR_INPUT_DEVICE_POINTER) {
        curosr_add_input(seat, input);
    } else if (input->prop.type == WLR_INPUT_DEVICE_KEYBOARD) {
        keyboard_add_input(seat, input);
    }

    seat_update_capabilities(seat);
}

void seat_remove_input(struct input *input)
{
    struct seat *seat = input->seat;

    if (input->prop.type == WLR_INPUT_DEVICE_POINTER) {
        cursor_remove_input(input);
    } else if (input->prop.type == WLR_INPUT_DEVICE_KEYBOARD) {
        keyboard_remove_input(input);
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

bool seat_set_pointer_grab(struct seat *seat, struct seat_pointer_grab *pointer_grab)
{
    if (seat->pointer_grab == pointer_grab) {
        return true;
    }

    if (seat->pointer_grab && pointer_grab) {
        kywc_log(KYWC_WARN, "%s is alreay has a pointer grab", seat->name);
        return false;
        // TODO: or replace current grab ?
        // seat->pointer_grab->interface->cancel(seat->pointer_grab);
    }

    seat->pointer_grab = pointer_grab;

    /* when we move quickly with left buttion pressed at view edges,
     * hold_mode is entered by a cursor motion event then client requests move
     */
    seat->cursor->hold_mode = false;
    /* clear last click state */
    seat->cursor->last_click_pressed = false;

    return true;
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

void seat_focus_surface(struct seat *seat, struct wlr_surface *surface)
{
    struct wlr_seat *wlr_seat = seat->wlr_seat;
    struct wlr_surface *prev = wlr_seat->keyboard_state.focused_surface;

    if (!surface) {
        wlr_seat_keyboard_notify_clear_focus(wlr_seat);
        return;
    }

    // if (prev == surface || seat->keyboard_grab) {
    if (prev == surface) {
        return;
    }

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(wlr_seat);
    if (kb) {
        wlr_seat_keyboard_notify_enter(wlr_seat, surface, kb->keycodes, kb->num_keycodes,
                                       &kb->modifiers);
    }
}
