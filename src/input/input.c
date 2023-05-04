#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <kywc/log.h>
#include <kywc/output.h>

#include "input.h"
#include "server.h"

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

static struct seat *input_manager_get_seat(const char *name)
{
    struct seat *seat = NULL;
    wl_list_for_each(seat, &input_manager->seats, link) {
        if (!strcmp(seat->name, name)) {
            return seat;
        }
    }

    /* create a new seat */
    return seat_create(input_manager, name);
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

    input_manager_config_init(input_manager);
    input_monitor_create(input_manager);
    input_manager->bindings = bindings_create(input_manager);

    idle_manager_create(server);
    input_manager_get_seat("seat0");

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

    input->seat = input_manager_get_seat(seat);
    seat_add_input(input->seat, input);
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

struct input *input_create(const char *name, const struct input_impl *impl, void *data)
{
    struct input *input = calloc(1, sizeof(struct input));
    if (!input) {
        return NULL;
    }

    input->impl = impl;
    input->data = data;
    input->name = name;

    input->manager = input_manager;
    wl_signal_init(&input->events.destroy);
    wl_list_insert(&input_manager->inputs, &input->link);

    input->mapped_output_off.notify = handle_mapped_output_off;
    input->mapped_output_destroy.notify = handle_mapped_output_destroy;

    input->impl->get_prop(input, &input->prop);

    input->impl->get_state(input, &input->state);
    struct input_state state = input->state;
    bool found = input_read_config(input, &state);
    if (!found) {
        // keep default
    }

    input_set_state(input, &state);

    wl_signal_emit_mutable(&input_manager->events.new_input, input);

    kywc_log(KYWC_DEBUG, "input device %s create", input->name);
    input_prop_and_state_debug(input);

    return input;
}

bool input_set_state(struct input *input, struct input_state *state)
{
    struct kywc_output *old_mapped_output = input->mapped_output;

    bool sucess = input->impl->set_state(input, state);
    /* update state anyway */
    input->impl->get_state(input, &input->state);

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

            cursor_move_to_output_center(input->seat, input->mapped_output);
        }
    }

    if (!input->prop.is_virtual) {
        input_write_config(input);
    }
    return sucess;
}

void input_destroy(struct input *input)
{
    wl_signal_emit_mutable(&input->events.destroy, input);

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

struct seat *seat_from_resource(struct wl_resource *resource)
{
    struct seat *seat;
    wl_list_for_each(seat, &input_manager->seats, link) {
        if (seat->impl->has_resource(seat, resource)) {
            return seat;
        }
    }

    return NULL;
}
