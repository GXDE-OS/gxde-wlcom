#include <stdlib.h>

#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>

#include "input.h"
#include "output.h"
#include "wlroots_p.h"

#define VIRTUAL_INPUT_DEVICE ((void *)0xdead)

static enum kywc_input_device_type type_from_type(enum wlr_input_device_type type)
{
    return (enum kywc_input_device_type)type;
}

static void wlroots_input_get_prop(struct input *input, struct input_prop *prop)
{
    struct wlr_input_device *wlr_input = input->data;

    input->prop.type = type_from_type(wlr_input->type);
    input->prop.vendor = wlr_input->vendor;
    input->prop.product = wlr_input->product;
    input->prop.is_virtual = wlr_input->data == VIRTUAL_INPUT_DEVICE;
    input->prop.support_mapped_to_output = wlr_input->type == WLR_INPUT_DEVICE_POINTER ||
                                           wlr_input->type == WLR_INPUT_DEVICE_TOUCH ||
                                           wlr_input->type == WLR_INPUT_DEVICE_TABLET_TOOL;

    if (!wlr_input_device_is_libinput(wlr_input)) {
        return;
    }

    // XXX: tricks to fix input->device
    if (!input->device) {
        input->device = wlr_libinput_get_device_handle(wlr_input);
    }

    libinput_get_prop(input, prop);
}

static void wlroots_input_get_state(struct input *input, struct input_state *state)
{
    struct wlr_input_device *wlr_input = input->data;

    state->seat = input->seat ? input->seat->name : NULL;
    state->mapped_to_output = input->mapped_output ? input->mapped_output->base.name : NULL;

    if (input->prop.type == KYWC_INPUT_DEVICE_KEYBOARD) {
        struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);
        state->repeat_rate = wlr_keyboard->repeat_info.rate;
        state->repeat_delay = wlr_keyboard->repeat_info.delay;
    }

    if (wlr_input_device_is_libinput(wlr_input)) {
        libinput_get_state(input, state);
    }
}

static bool wlroots_input_set_state(struct input *input, struct input_state *state)
{
    struct wlr_input_device *wlr_input = input->data;

    // TODO: add keyboard config support, add xkb helper
    if (input->prop.type == KYWC_INPUT_DEVICE_KEYBOARD) {
        struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);
        /* config keyboard with input config */
        struct xkb_rule_names rules = { 0 };
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap *keymap =
            xkb_map_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
        wlr_keyboard_set_keymap(wlr_keyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        wlr_keyboard_set_repeat_info(wlr_keyboard, state->repeat_rate, state->repeat_delay);
    }

    /* choose a suitable seat, add the input device to the seat */
    input_set_seat(input, state->seat ? state->seat : "seat0");

    if (input->prop.support_mapped_to_output) {
        struct output *mapped_output = NULL;
        if (state->mapped_to_output) {
            mapped_output = output_by_name(state->mapped_to_output);
            /* keep orig mapped output if invalid */
            if (!mapped_output) {
                mapped_output = input->mapped_output;
            }
        }

        struct wlr_cursor *wlr_cursor = input->seat->cursor->data;
        struct wlr_output *wlr_output = mapped_output ? mapped_output->data : NULL;
        wlr_cursor_map_input_to_output(wlr_cursor, wlr_input, wlr_output);
        input->mapped_output = mapped_output;
    }

    if (wlr_input_device_is_libinput(wlr_input)) {
        return libinput_set_state(input, state);
    }

    return true;
}

static const struct input_impl wlroots_input_impl = {
    .get_prop = wlroots_input_get_prop,
    .get_state = wlroots_input_get_state,
    .set_state = wlroots_input_set_state,
};

static void handle_input_destroy(struct wl_listener *listener, void *data)
{
    struct input *input = wl_container_of(listener, input, destroy);

    wl_list_remove(&input->destroy.link);

    input_destroy(input);
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
    struct wlroots_server *wlroots = wl_container_of(listener, wlroots, new_input);
    struct wlr_input_device *wlr_input = data;

    struct input *input = input_create(wlr_input->name, &wlroots_input_impl, wlr_input);
    if (!input) {
        return;
    }

    input->destroy.notify = handle_input_destroy;
    wl_signal_add(&wlr_input->events.destroy, &input->destroy);
}

static void handle_new_virtual_pointer(struct wl_listener *listener, void *data)
{
    struct wlroots_server *wlroots = wl_container_of(listener, wlroots, new_virtual_pointer);

    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    struct wlr_virtual_pointer_v1 *pointer = event->new_pointer;
    struct wlr_input_device *wlr_input = &pointer->pointer.base;

    wlr_input->data = VIRTUAL_INPUT_DEVICE;

    struct input *input = input_create(wlr_input->name, &wlroots_input_impl, wlr_input);
    if (!input) {
        return;
    }

    /* apply suggested seat and output */
    if (event->suggested_seat || event->suggested_output) {
        struct input_state state = input->state;
        if (event->suggested_seat) {
            state.seat = event->suggested_seat->name;
        }
        if (event->suggested_output) {
            state.mapped_to_output = event->suggested_output->name;
        }
        input_set_state(input, &state);
    }

    input->destroy.notify = handle_input_destroy;
    wl_signal_add(&wlr_input->events.destroy, &input->destroy);
}

static void handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
    struct wlroots_server *wlroots = wl_container_of(listener, wlroots, new_input);
    struct wlr_virtual_keyboard_v1 *keyboard = data;
    struct wlr_input_device *wlr_input = &keyboard->keyboard.base;

    wlr_input->data = VIRTUAL_INPUT_DEVICE;

    struct input *input = input_create(wlr_input->name, &wlroots_input_impl, wlr_input);
    if (!input) {
        return;
    }

    /* apply keyboard seat */
    if (strcmp(input->seat->name, keyboard->seat->name)) {
        struct input_state state = input->state;
        state.seat = keyboard->seat->name;
        input_set_state(input, &state);
    }

    input->destroy.notify = handle_input_destroy;
    wl_signal_add(&wlr_input->events.destroy, &input->destroy);
}

bool wlroots_input_init(struct wlroots_server *wlroots)
{
    wlr_data_device_manager_create(wlroots->server->display);
    wlr_primary_selection_v1_device_manager_create(wlroots->server->display);

    wlroots->new_input.notify = handle_new_input;
    wl_signal_add(&wlroots->backend->events.new_input, &wlroots->new_input);

    struct wlr_virtual_pointer_manager_v1 *virtual_pointer_manager =
        wlr_virtual_pointer_manager_v1_create(wlroots->server->display);
    wlroots->new_virtual_pointer.notify = handle_new_virtual_pointer;
    wl_signal_add(&virtual_pointer_manager->events.new_virtual_pointer,
                  &wlroots->new_virtual_pointer);

    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager =
        wlr_virtual_keyboard_manager_v1_create(wlroots->server->display);
    wlroots->new_virtual_keyboard.notify = handle_new_virtual_keyboard;
    wl_signal_add(&virtual_keyboard_manager->events.new_virtual_keyboard,
                  &wlroots->new_virtual_keyboard);

    return true;
}
