#include <stdlib.h>

#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "input.h"
#include "wlroots_p.h"

static void cursor_handle_motion(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, motion);
    struct wlr_cursor *wlr_cursor = cursor->data;
    struct wlr_pointer_motion_event *event = data;

    wlr_cursor_move(wlr_cursor, &event->pointer->base, event->delta_x, event->delta_y);
    cursor_feed_motion(cursor, wlr_cursor->x, wlr_cursor->y, event->time_msec);
}

static void cursor_handle_motion_absolute(struct wl_listener *listener, void *data)
{

    struct cursor *cursor = wl_container_of(listener, cursor, motion_absolute);
    struct wlr_cursor *wlr_cursor = cursor->data;
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

    /* Notify the client with pointer focus of the axis event. */
    struct wlr_seat *wlr_seat = cursor->seat->data;
    wlr_seat_pointer_notify_axis(wlr_seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source);
}

static void cursor_handle_frame(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, frame);

    /* Notify the client with pointer focus of the frame event. */
    struct wlr_seat *wlr_seat = cursor->seat->data;
    wlr_seat_pointer_notify_frame(wlr_seat);
}

static void wlroots_cursor_destroy(struct cursor *cursor)
{
    wl_list_remove(&cursor->motion.link);
    wl_list_remove(&cursor->motion_absolute.link);
    wl_list_remove(&cursor->button.link);
    wl_list_remove(&cursor->axis.link);
    wl_list_remove(&cursor->frame.link);

    struct wlr_cursor *wlr_cursor = cursor->data;
    struct wlr_xcursor_manager *xcursor_manager = wlr_cursor->data;
    wlr_xcursor_manager_destroy(xcursor_manager);

    wlr_cursor_destroy(wlr_cursor);

    cursor->seat->cursor = NULL;
    free(cursor);
}

static void wlroots_seat_set_cursor_image(struct seat *seat, enum cursor_name name, float scale)
{
    struct wlr_cursor *wlr_cursor = seat->cursor->data;
    struct wlr_xcursor_manager *xcursor_manager = wlr_cursor->data;

    if (name == CURSOR_NONE) {
        wlr_cursor_set_surface(wlr_cursor, NULL, 0, 0);
        return;
    }

    wlr_xcursor_manager_load(xcursor_manager, scale);

    const char *image = cursor_image_by_name(name);
    wlr_xcursor_manager_set_cursor_image(xcursor_manager, image, wlr_cursor);
}

static void wlroots_seat_move_cursor(struct seat *seat, double x, double y, bool delta)
{
    struct wlr_cursor *wlr_cursor = seat->cursor->data;

    if (delta) {
        wlr_cursor_move(wlr_cursor, NULL, x, y);
    } else {
        wlr_cursor_warp(wlr_cursor, NULL, x, y);
    }
}

#define CURSOR_ADD_SIGNAL(signal)                                                                  \
    cursor->signal.notify = cursor_handle_##signal;                                                \
    wl_signal_add(&wlr_cursor->events.signal, &cursor->signal);

static bool wlroots_seat_create_cursor(struct seat *seat)
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
    cursor->data = wlr_cursor;

    struct wlr_seat *wlr_seat = seat->data;
    struct wlroots_server *wlroots = wlr_seat->data;
    wlr_cursor_attach_output_layout(wlr_cursor, wlroots->layout);

    const char *xcursor_theme = getenv("XCURSOR_THEME");
    const char *xcursor_size = getenv("XCURSOR_SIZE");
    uint32_t size = xcursor_size ? atoi(xcursor_size) : 24;

    struct wlr_xcursor_manager *xcursor_manager;
    xcursor_manager = wlr_xcursor_manager_create(xcursor_theme, size);
    wlr_xcursor_manager_load(xcursor_manager, 1.0);
    wlr_cursor->data = xcursor_manager;

    CURSOR_ADD_SIGNAL(motion);
    CURSOR_ADD_SIGNAL(motion_absolute);
    CURSOR_ADD_SIGNAL(button);
    CURSOR_ADD_SIGNAL(axis);
    CURSOR_ADD_SIGNAL(frame);

    return true;
}

#undef CURSOR_ADD_SIGNAL

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
    struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard *wlr_keyboard = keyboard->data;
    struct wlr_keyboard_key_event *event = data;

    uint32_t modifiers = wlr_keyboard_get_modifiers(wlr_keyboard);
    keyboard_feed_key(keyboard, event->keycode, event->state == WL_KEYBOARD_KEY_STATE_PRESSED,
                      event->time_msec, modifiers);
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
    struct keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    struct wlr_keyboard *wlr_keyboard = keyboard->data;
    struct wlr_keyboard_modifiers *modifiers = &wlr_keyboard->modifiers;

    keyboard_feed_modifiers(keyboard, modifiers->depressed, modifiers->latched, modifiers->locked,
                            modifiers->group);
}

static void wlroots_seat_add_keyboard(struct seat *seat, struct input *input)
{
    struct wlr_input_device *wlr_input = input->data;
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);
    struct wlr_keyboard *dst_keyboard;
    struct keyboard *keyboard;

    /* virtual keyboard is not managed by group */
    if (input->prop.is_virtual) {
        dst_keyboard = wlr_keyboard;
        goto create;
    }

    /* find a suitable group */
    wl_list_for_each(keyboard, &seat->keyboards, link) {
        if (keyboard->is_virtual) {
            continue;
        }

        dst_keyboard = keyboard->data;
        struct wlr_keyboard_group *wlr_group = wlr_keyboard_group_from_wlr_keyboard(dst_keyboard);

        if (wlr_keyboard_keymaps_match(wlr_keyboard->keymap, dst_keyboard->keymap) &&
            wlr_keyboard->repeat_info.rate == dst_keyboard->repeat_info.rate &&
            wlr_keyboard->repeat_info.delay == dst_keyboard->repeat_info.delay) {
            kywc_log(KYWC_DEBUG, "Adding keyboard %s to group %p", input->name, wlr_group);
            wlr_keyboard_group_add_keyboard(wlr_group, wlr_keyboard);
            wlr_keyboard->data = wlr_group;
            return;
        }
    }

create:
    /* create a new keyboard with keyboard configuration */
    keyboard = calloc(1, sizeof(struct keyboard));
    if (!keyboard) {
        return;
    }

    /* create a new keyboard group */
    if (!input->prop.is_virtual) {
        struct wlr_keyboard_group *wlr_group = wlr_keyboard_group_create();
        dst_keyboard = &wlr_group->keyboard;
        wlr_keyboard->data = wlr_group;
        wlr_keyboard_set_keymap(dst_keyboard, wlr_keyboard->keymap);
        wlr_keyboard_set_repeat_info(dst_keyboard, wlr_keyboard->repeat_info.rate,
                                     wlr_keyboard->repeat_info.delay);
        wlr_keyboard_group_add_keyboard(wlr_group, wlr_keyboard);
    }

    keyboard->data = dst_keyboard;
    dst_keyboard->data = keyboard;
    keyboard->is_virtual = input->prop.is_virtual;
    keyboard->state.xkb_state = dst_keyboard->xkb_state;

    /* insert new keyboard to seat keyboard list */
    keyboard->seat = seat;
    wl_list_insert(&seat->keyboards, &keyboard->link);

    struct wlr_seat *wlr_seat = seat->data;
    wlr_seat_set_keyboard(wlr_seat, dst_keyboard);

    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&dst_keyboard->events.key, &keyboard->key);
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&dst_keyboard->events.modifiers, &keyboard->modifiers);
}

static void wlroots_keyboard_destroy(struct keyboard *keyboard)
{
    struct wlr_seat *wlr_seat = keyboard->seat->data;
    struct wlr_keyboard *wlr_keyboard = keyboard->data;

    if (wlr_seat_get_keyboard(wlr_seat) == wlr_keyboard) {
        wlr_seat_set_keyboard(wlr_seat, NULL);
    }

    wl_list_remove(&keyboard->link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->modifiers.link);

    if (!keyboard->is_virtual) {
        struct wlr_keyboard_group *wlr_group = wlr_keyboard_group_from_wlr_keyboard(wlr_keyboard);
        wlr_keyboard_group_destroy(wlr_group);
    }

    free(keyboard);
}

static void wlroots_seat_remove_keyboard(struct seat *seat, struct input *input)
{
    struct wlr_input_device *wlr_input = input->data;
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);
    struct keyboard *keyboard;

    if (input->prop.is_virtual) {
        keyboard = wlr_keyboard->data;
        wlroots_keyboard_destroy(keyboard);
        return;
    }

    struct wlr_keyboard_group *wlr_group = wlr_keyboard->group;
    /* already remove when input destroy at keyboard group */
    if (!wlr_group) {
        wlr_group = wlr_keyboard->data;
    } else {
        wlr_keyboard_group_remove_keyboard(wlr_group, wlr_keyboard);
    }

    /* destroy keyboard group if empty */
    if (wl_list_empty(&wlr_group->devices)) {
        keyboard = wlr_group->keyboard.data;
        wlroots_keyboard_destroy(keyboard);
    }
}

static void wlroots_seat_add_input(struct seat *seat, struct input *input)
{
    struct wlr_input_device *wlr_input = input->data;

    if (input->prop.type == KYWC_INPUT_DEVICE_POINTER) {
        struct wlr_cursor *wlr_cursor = seat->cursor->data;
        wlr_cursor_attach_input_device(wlr_cursor, wlr_input);
    } else if (input->prop.type == KYWC_INPUT_DEVICE_KEYBOARD) {
        wlroots_seat_add_keyboard(seat, input);
    }
}

static void wlroots_seat_remove_input(struct seat *seat, struct input *input)
{
    struct wlr_input_device *wlr_input = input->data;

    if (input->prop.type == KYWC_INPUT_DEVICE_POINTER) {
        struct wlr_cursor *wlr_cursor = seat->cursor->data;
        wlr_cursor_detach_input_device(wlr_cursor, wlr_input);
    } else if (input->prop.type == KYWC_INPUT_DEVICE_KEYBOARD) {
        wlroots_seat_remove_keyboard(seat, input);
    }
}

static void wlroots_seat_destroy(struct seat *seat)
{
    struct wlr_seat *wlr_seat = seat->data;
    /* wlr_seat is null when display_destroy */
    if (!wlr_seat) {
        return;
    }

    wl_list_remove(&seat->destroy.link);

    struct keyboard *keyboard, *keyboard_tmp;
    wl_list_for_each_safe(keyboard, keyboard_tmp, &seat->keyboards, link) {
        wlroots_keyboard_destroy(keyboard);
    }

    wlroots_cursor_destroy(seat->cursor);

    wlr_seat_destroy(wlr_seat);
}

static void wlroots_seat_set_caps(struct seat *seat, uint32_t caps)
{
    struct wlr_seat *wlr_seat = seat->data;

    wlr_seat_set_capabilities(wlr_seat, caps);
}

static const struct seat_impl wlroots_seat_impl = {
    .move_cursor = wlroots_seat_move_cursor,
    .set_cursor_image = wlroots_seat_set_cursor_image,

    .add_input = wlroots_seat_add_input,
    .remove_input = wlroots_seat_remove_input,

    .set_caps = wlroots_seat_set_caps,
    .destroy = wlroots_seat_destroy,
};

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat *seat = wl_container_of(listener, seat, destroy);

    wl_list_remove(&seat->destroy.link);

    seat->data = NULL;
    seat_destroy(seat);
}

bool wlroots_server_init_seat(struct server *server, struct seat *seat)
{
    struct wlr_seat *wlr_seat = wlr_seat_create(server->display, seat->name);
    if (!wlr_seat) {
        return false;
    }

    seat->data = wlr_seat;
    seat->impl = &wlroots_seat_impl;
    wlr_seat->data = server->data; // wlroots server

    seat->destroy.notify = handle_seat_destroy;
    wl_signal_add(&wlr_seat->events.destroy, &seat->destroy);

    wlroots_seat_create_cursor(seat);

    return true;
}
