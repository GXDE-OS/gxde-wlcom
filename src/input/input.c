#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>

#include <kywc/identifier.h>
#include <kywc/log.h>

#include "input/input.h"
#include "input/keyboard.h"
#include "input/seat.h"
#include "input_p.h"
#include "output.h"
#include "server.h"

#define VIRTUAL_INPUT_DEVICE ((void *)0xdead)

static struct input_manager *input_manager = NULL;

void input_add_new_listener(struct wl_listener *listener)
{
    wl_signal_add(&input_manager->events.new_input, listener);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&input_manager->server_destroy.link);

    struct seat *seat, *seat_tmp;
    wl_list_for_each_safe(seat, seat_tmp, &input_manager->seats, link) {
        seat_destroy(seat);
    }

    bindings_destroy(input_manager->bindings);

    free(input_manager);
    input_manager = NULL;
}

static struct seat *input_manager_get_seat(const char *name, bool create)
{
    struct seat *seat = NULL;
    wl_list_for_each(seat, &input_manager->seats, link) {
        if (!strcmp(seat->name, name)) {
            return seat;
        }
    }

    if (!create) {
        return NULL;
    }

    /* create a new seat */
    return seat_create(input_manager, name);
}

static void input_clear_mapped_output(struct input *input)
{
    /* current mapped output is being off or destroyed, restore it later */
    free(input->desired_mapped_output);
    input->desired_mapped_output = strdup(input->mapped_output->name);

    struct input_state state = input->state;
    state.mapped_to_output = NULL;
    input_set_state(input, &state);
}

static void handle_mapped_output_off(struct wl_listener *listener, void *data)
{
    struct input *input = wl_container_of(listener, input, mapped_output_off);

    input_clear_mapped_output(input);
}

static void handle_mapped_output_destroy(struct wl_listener *listener, void *data)
{
    struct input *input = wl_container_of(listener, input, mapped_output_destroy);

    input_clear_mapped_output(input);
}

static void input_destroy(struct input *input)
{
    wl_signal_emit_mutable(&input->events.destroy, NULL);

    wl_list_remove(&input->link);

    if (input->mapped_output) {
        wl_list_remove(&input->mapped_output_off.link);
        wl_list_remove(&input->mapped_output_destroy.link);
    }
    free(input->desired_mapped_output);

    kywc_log(KYWC_DEBUG, "input device %s destroy", input->name);

    struct seat *seat = input->seat;
    if (seat) {
        seat_remove_input(input);
        if (wl_list_empty(&seat->inputs)) {
            seat_destroy(seat);
        }
    }

    free((void *)input->name);
    free(input);
}

static void handle_input_destroy(struct wl_listener *listener, void *data)
{
    struct input *input = wl_container_of(listener, input, destroy);

    wl_list_remove(&input->destroy.link);

    input_destroy(input);
}

static void input_get_prop(struct input *input, struct input_prop *prop)
{
    struct wlr_input_device *wlr_input = input->wlr_input;

    input->prop.type = wlr_input->type;
    input->prop.vendor = wlr_input->vendor;
    input->prop.product = wlr_input->product;
    input->prop.is_virtual = wlr_input->data == VIRTUAL_INPUT_DEVICE;
    input->prop.support_mapped_to_output = wlr_input->type == WLR_INPUT_DEVICE_POINTER ||
                                           wlr_input->type == WLR_INPUT_DEVICE_TOUCH ||
                                           wlr_input->type == WLR_INPUT_DEVICE_TABLET_TOOL;

    if (input->device) {
        libinput_get_prop(input, prop);
    }
}

static void input_get_state(struct input *input, struct input_state *state)
{
    struct wlr_input_device *wlr_input = input->wlr_input;

    state->seat = input->seat ? input->seat->name : NULL;
    state->mapped_to_output = input->mapped_output ? input->mapped_output->name : NULL;

    if (input->prop.type == WLR_INPUT_DEVICE_KEYBOARD) {
        struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);
        state->repeat_rate = wlr_keyboard->repeat_info.rate;
        state->repeat_delay = wlr_keyboard->repeat_info.delay;
    }

    if (input->device) {
        libinput_get_state(input, state);
    }
}

static struct input *input_create(const char *name, struct wlr_input_device *wlr_input)
{
    struct input *input = calloc(1, sizeof(struct input));
    if (!input) {
        return NULL;
    }

    input->wlr_input = wlr_input;
    input->name = name;

    input->manager = input_manager;
    wl_signal_init(&input->events.destroy);
    wl_list_insert(&input_manager->inputs, &input->link);

    input->mapped_output_off.notify = handle_mapped_output_off;
    input->mapped_output_destroy.notify = handle_mapped_output_destroy;

    if (wlr_input_device_is_libinput(wlr_input)) {
        input->device = wlr_libinput_get_device_handle(wlr_input);
    }

    input_get_prop(input, &input->prop);
    input_get_state(input, &input->state);

    struct input_state state = input->state;
    bool found = input_read_config(input, &state);
    if (!found) {
        // keep default
    }

    input_set_state(input, &state);

    wl_signal_emit_mutable(&input_manager->events.new_input, input);

    if (kywc_log_get_level() == KYWC_DEBUG) {
        kywc_log(KYWC_DEBUG, "input device %s create", input->name);
        input_prop_and_state_debug(input);
    }

    return input;
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
    struct wlr_input_device *wlr_input = data;

    const char *name = kywc_identifier_generate("%d:%d:%s", wlr_input->vendor, wlr_input->product,
                                                wlr_input->name);

    struct input *input = input_create(name, wlr_input);
    if (!input) {
        return;
    }

    input->destroy.notify = handle_input_destroy;
    wl_signal_add(&wlr_input->events.destroy, &input->destroy);
}

static void handle_new_virtual_pointer(struct wl_listener *listener, void *data)
{
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    struct wlr_virtual_pointer_v1 *pointer = event->new_pointer;
    struct wlr_input_device *wlr_input = &pointer->pointer.base;

    wlr_input->data = VIRTUAL_INPUT_DEVICE;

    const char *name = kywc_identifier_generate("V_%s", wlr_input->name);

    struct input *input = input_create(name, wlr_input);
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
    struct wlr_virtual_keyboard_v1 *keyboard = data;
    struct wlr_input_device *wlr_input = &keyboard->keyboard.base;

    wlr_input->data = VIRTUAL_INPUT_DEVICE;

    const char *name = kywc_identifier_generate("V_%s", wlr_input->name);

    struct input *input = input_create(name, wlr_input);
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

struct input_manager *input_manager_create(struct server *server)
{
    input_manager = calloc(1, sizeof(struct input_manager));
    if (!input_manager) {
        return NULL;
    }

    input_manager->server = server;
    wl_list_init(&input_manager->seats);
    wl_list_init(&input_manager->inputs);
    wl_signal_init(&input_manager->events.new_input);
    wl_signal_init(&input_manager->events.new_seat);

    input_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &input_manager->server_destroy);

    input_manager->new_input.notify = handle_new_input;
    wl_signal_add(&server->backend->events.new_input, &input_manager->new_input);

    input_manager->virtual_pointer =
        wlr_virtual_pointer_manager_v1_create(input_manager->server->display);
    input_manager->new_virtual_pointer.notify = handle_new_virtual_pointer;
    wl_signal_add(&input_manager->virtual_pointer->events.new_virtual_pointer,
                  &input_manager->new_virtual_pointer);

    input_manager->virtual_keyboard =
        wlr_virtual_keyboard_manager_v1_create(input_manager->server->display);
    input_manager->new_virtual_keyboard.notify = handle_new_virtual_keyboard;
    wl_signal_add(&input_manager->virtual_keyboard->events.new_virtual_keyboard,
                  &input_manager->new_virtual_keyboard);

    input_manager->pointer_gestures =
        wlr_pointer_gestures_v1_create(input_manager->server->display);

    input_manager_config_init(input_manager);
    selection_manager_create(input_manager);
    input_monitor_create(input_manager);
    input_manager->bindings = bindings_create(input_manager);
    input_method_manager_create(input_manager);

    touch_manager_create(input_manager);

    idle_manager_create(server);
    input_manager_get_seat("seat0", true);

    return input_manager;
}

void input_set_seat(struct input *input, const char *seat)
{
    /* alreay have attached to seat */
    if (input->seat) {
        if (!strcmp(seat, input->seat->name)) {
            return;
        } else {
            /* remove from prev seat */
            struct seat *prev = input->seat;
            seat_remove_input(input);
            if (wl_list_empty(&prev->inputs)) {
                seat_destroy(prev);
            }
        }
    }

    input->seat = input_manager_get_seat(seat, true);
    seat_add_input(input->seat, input);
}

static struct xkb_keymap *keyboard_compile_keymap(struct input_state *state)
{
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_SECURE_GETENV);
    struct xkb_rule_names rules = {
        .layout = state->xkb_layout,
        .model = state->xkb_model,
        .options = state->xkb_options,
        .rules = state->xkb_rules,
        .variant = state->xkb_variant,
    };
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_context_unref(context);
    return keymap;
}

static bool _input_set_state(struct input *input, struct input_state *state)
{
    struct wlr_input_device *wlr_input = input->wlr_input;

    /* config keyboard with input state */
    if (input->prop.type == WLR_INPUT_DEVICE_KEYBOARD) {
        struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(wlr_input);
        struct xkb_keymap *keymap = keyboard_compile_keymap(state);

        bool keymap_changed =
            wlr_keyboard->keymap ? !wlr_keyboard_keymaps_match(wlr_keyboard->keymap, keymap) : true;
        bool repeat_info_changed = wlr_keyboard->repeat_info.rate != state->repeat_rate ||
                                   wlr_keyboard->repeat_info.delay != state->repeat_delay;

        /* we need remove this input and add later */
        if (!input->prop.is_virtual && wlr_keyboard->group &&
            (keymap_changed || repeat_info_changed)) {
            kywc_log(KYWC_DEBUG, "input %s is removed and be added later", input->name);
            seat_remove_input(input);
        }

        if (keymap_changed) {
            wlr_keyboard_set_keymap(wlr_keyboard, keymap);
        }
        if (repeat_info_changed) {
            wlr_keyboard_set_repeat_info(wlr_keyboard, state->repeat_rate, state->repeat_delay);
        }
        xkb_keymap_unref(keymap);
    }

    /* choose a suitable seat, add the input device to the seat */
    input_set_seat(input, state->seat ? state->seat : "seat0");

    if (input->prop.support_mapped_to_output) {
        struct kywc_output *mapped_output = NULL;
        if (state->mapped_to_output) {
            mapped_output = kywc_output_by_name(state->mapped_to_output);
            /* keep orig mapped output if invalid */
            if (!mapped_output || !mapped_output->state.enabled) {
                free(input->desired_mapped_output);
                input->desired_mapped_output = strdup(state->mapped_to_output);
                mapped_output = input->mapped_output;
            }
        }

        struct wlr_cursor *wlr_cursor = input->seat->cursor->wlr_cursor;
        struct wlr_output *wlr_output =
            mapped_output ? output_from_kywc_output(mapped_output)->wlr_output : NULL;
        wlr_cursor_map_input_to_output(wlr_cursor, wlr_input, wlr_output);
        input->mapped_output = mapped_output;
    }

    if (input->device) {
        return libinput_set_state(input, state);
    }

    return true;
}

bool input_set_state(struct input *input, struct input_state *state)
{
    struct kywc_output *old_mapped_output = input->mapped_output;

    bool sucess = _input_set_state(input, state);
    /* update state anyway */
    input_get_state(input, &input->state);

    if (old_mapped_output != input->mapped_output) {
        if (old_mapped_output) {
            wl_list_remove(&input->mapped_output_off.link);
            wl_list_remove(&input->mapped_output_destroy.link);
        }
        if (input->mapped_output) {
            wl_signal_add(&input->mapped_output->events.off, &input->mapped_output_off);
            wl_signal_add(&input->mapped_output->events.destroy, &input->mapped_output_destroy);

            free(input->desired_mapped_output);
            input->desired_mapped_output = NULL;

            cursor_move_to_output_center(input->seat->cursor, input->mapped_output);
        }
    }

    if (!input->prop.is_virtual) {
        input_write_config(input);
    }
    return sucess;
}

struct input *input_by_name(const char *name)
{
    struct input *input;
    wl_list_for_each(input, &input_manager->inputs, link) {
        if (!strcmp(input->name, name)) {
            return input;
        }
    }

    return NULL;
}

struct seat *input_manager_get_default_seat(void)
{
    // TODO: return last activated seat ?
    struct seat *seat = input_manager_get_seat("seat0", false);
    if (!seat) {
        seat = wl_container_of(input_manager->seats.prev, seat, link);
    }
    return seat;
}

struct output *input_current_output(struct seat *seat)
{
    struct wlr_output *wlr_output =
        wlr_output_layout_output_at(seat->layout, seat->cursor->lx, seat->cursor->ly);
    return wlr_output ? output_from_wlr_output(wlr_output) : NULL;
}
