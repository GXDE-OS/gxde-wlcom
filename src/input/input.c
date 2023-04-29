#include <stdlib.h>

#include <kywc/log.h>
#include <kywc/output.h>

#include "input.h"
#include "output.h"
#include "server.h"

struct cursor_output {
    struct wl_list link;
    struct kywc_output *ouput;

    struct wl_listener scale;
    struct wl_listener destroy;
};

static struct input_manager *input_manager = NULL;

void input_add_new_listener(struct wl_listener *listener)
{
    wl_signal_add(&input_manager->events.new_input, listener);
}

static void move_cursor_to_output_center(struct seat *seat, struct kywc_output *kywc_output)
{
    int x, y, width, height;

    kywc_output_effective_resolution(kywc_output, &width, &height);
    x = kywc_output->state.lx + width / 2;
    y = kywc_output->state.ly + height / 2;

    kywc_log(KYWC_DEBUG, "move %s's cursor to %s (%d, %d)", seat->name, kywc_output->name, x, y);
    seat_move_cursor(seat, x, y, false);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&input_manager->new_output.link);
    wl_list_remove(&input_manager->server_destroy.link);

    struct cursor_output *cursor_output, *cursor_output_tmp;
    wl_list_for_each_safe(cursor_output, cursor_output_tmp, &input_manager->outputs, link) {
        wl_list_remove(&cursor_output->link);
        wl_list_remove(&cursor_output->scale.link);
        wl_list_remove(&cursor_output->destroy.link);
        free(cursor_output);
    }

    struct seat *seat, *seat_tmp;
    wl_list_for_each_safe(seat, seat_tmp, &input_manager->seats, link) {
        seat_destroy(seat);
    }

    bindings_destroy(input_manager->bindings);

    free(input_manager);
    input_manager = NULL;
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, destroy);

    wl_list_remove(&cursor_output->link);
    wl_list_remove(&cursor_output->scale.link);
    wl_list_remove(&cursor_output->destroy.link);

    free(cursor_output);
}

static void handle_output_scale(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, scale);
    struct kywc_output *kywc_output = cursor_output->ouput;

    struct seat *seat;
    wl_list_for_each(seat, &input_manager->seats, link) {
        seat_set_cursor_image(seat, CURSOR_DEFAULT, kywc_output->state.scale, true);
    }
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = calloc(1, sizeof(struct cursor_output));
    if (!cursor_output) {
        return;
    }

    struct kywc_output *kywc_output = data;
    cursor_output->ouput = kywc_output;
    wl_list_insert(&input_manager->outputs, &cursor_output->link);

    cursor_output->scale.notify = handle_output_scale;
    wl_signal_add(&kywc_output->events.scale, &cursor_output->scale);
    cursor_output->destroy.notify = handle_output_destroy;
    wl_signal_add(&kywc_output->events.destroy, &cursor_output->destroy);

    struct seat *seat;
    wl_list_for_each(seat, &input_manager->seats, link) {
        seat_set_cursor_image(seat, CURSOR_DEFAULT, kywc_output->state.scale, true);
        seat_move_cursor(seat, 0, 0, true);
    }
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

    wl_list_init(&input_manager->outputs);
    input_manager->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&input_manager->new_output);
    wl_signal_init(&input_manager->events.new_seat);

    input_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &input_manager->server_destroy);

    input_manager_get_seat("seat0");
    input_manager_config_init(input_manager);
    input_manager->bindings = bindings_create(input_manager);

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

static void handle_mapped_output_destroy(struct wl_listener *listener, void *data)
{
    struct input *input = wl_container_of(listener, input, mapped_output_destroy);

    struct input_state state = input->state;
    state.mapped_to_output = NULL;
    input_set_state(input, &state);
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
    struct output *old_mapped_output = input->mapped_output;

    bool sucess = input->impl->set_state(input, state);
    /* update state anyway */
    input->impl->get_state(input, &input->state);

    if (old_mapped_output != input->mapped_output) {
        if (old_mapped_output) {
            wl_list_remove(&input->mapped_output_destroy.link);
        }
        if (input->mapped_output) {
            wl_signal_add(&input->mapped_output->base.events.destroy,
                          &input->mapped_output_destroy);

            move_cursor_to_output_center(input->seat, &input->mapped_output->base);
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
        wl_list_remove(&input->mapped_output_destroy.link);
    }

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
