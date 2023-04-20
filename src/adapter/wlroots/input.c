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

struct virtual_input_config {
    struct wlr_seat *wlr_seat;
    struct wlr_output *wlr_output;
};

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
    input->prop.is_virtual = !!wlr_input->data;

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

    if (input->prop.is_virtual) {
        struct virtual_input_config *config = wlr_input->data;
        if (config->wlr_seat) {
            state->seat = config->wlr_seat->name;
        }
        if (config->wlr_output) {
            state->mapped_to_output = config->wlr_output->name;
        }
    }

    if (input->prop.type == KYWC_INPUT_DEVICE_KEYBOARD) {
        state->repeat_rate = 25;
        state->repeat_delay = 600;
    }

    if (wlr_input_device_is_libinput(wlr_input)) {
        libinput_get_state(input, state);
    }
}

static bool wlroots_input_set_state(struct input *input, struct input_state *state)
{
    struct wlr_input_device *wlr_input = input->data;

    if (state->mapped_to_output) {
        struct output *output = output_by_name(state->mapped_to_output);
        if (output) {
            struct wlr_cursor *wlr_cursor = input->seat->cursor->data;
            struct wlr_output *wlr_output = output->data;
            wlr_cursor_map_input_to_output(wlr_cursor, wlr_input, wlr_output);
        }
    }

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

    struct wlr_input_device *wlr_input = input->data;
    free(wlr_input->data);

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

    struct virtual_input_config *config = calloc(1, sizeof(struct virtual_input_config));
    config->wlr_seat = event->suggested_seat;
    config->wlr_output = event->suggested_output;
    wlr_input->data = config;

    struct input *input = input_create(wlr_input->name, &wlroots_input_impl, wlr_input);
    if (!input) {
        return;
    }

    input->destroy.notify = handle_input_destroy;
    wl_signal_add(&wlr_input->events.destroy, &input->destroy);
}

static void handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
    struct wlroots_server *wlroots = wl_container_of(listener, wlroots, new_input);
    struct wlr_virtual_keyboard_v1 *keyboard = data;
    struct wlr_input_device *wlr_input = &keyboard->keyboard.base;

    struct virtual_input_config *config = calloc(1, sizeof(struct virtual_input_config));
    config->wlr_seat = keyboard->seat;
    wlr_input->data = config;

    struct input *input = input_create(wlr_input->name, &wlroots_input_impl, wlr_input);
    if (!input) {
        return;
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
