// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <kywc/log.h>

#include "input/seat.h"
#include "input_p.h"
#include "output.h"
#include "server.h"

struct cursor_output {
    struct wl_list link;
    struct input_monitor *monitor;

    struct kywc_output *ouput;
    struct wl_listener on;
    struct wl_listener destroy;
};

struct input_monitor {
    struct input_manager *input_manager;

    struct wl_list outputs;
    struct wl_listener new_output;
    struct wl_listener configured;

    struct wl_listener new_seat;
    struct wl_listener server_destroy;
};

void cursor_move_to_output_center(struct cursor *cursor, struct kywc_output *kywc_output)
{
    struct kywc_box geo;
    kywc_output_effective_geometry(kywc_output, &geo);
    geo.x += geo.width / 2;
    geo.y += geo.height / 2;

    cursor_move(cursor, NULL, geo.x, geo.y, false, false);
    // kywc_log(KYWC_INFO, "move %s cursor to %s conter", cursor->seat->name, kywc_output->name);
}

static void input_restore_mapped_output(struct input_monitor *input_monitor,
                                        struct kywc_output *kywc_output)
{
    struct input *input;
    wl_list_for_each(input, &input_monitor->input_manager->inputs, link) {
        if (!input->desired_mapped_output ||
            strcmp(input->desired_mapped_output, kywc_output->name)) {
            continue;
        }

        struct input_state state = input->state;
        state.mapped_to_output = kywc_output->name;
        input_set_state(input, &state);
    }
}

static struct kywc_output *seat_pick_mapped_output(struct seat *seat)
{
    struct input *input;
    wl_list_for_each(input, &seat->inputs, seat_link) {
        if (input->mapped_output && !input->mapped_output->destroying) {
            return input->mapped_output;
        }
    }

    return NULL;
}

static void seat_rebase_cursor(struct seat *seat)
{
    /* prefer to move cursor to mapped output */
    struct kywc_output *output = seat_pick_mapped_output(seat);
    output = output ? output : kywc_output_get_primary();
    if (output && !output->destroying) {
        cursor_move_to_output_center(seat->cursor, output);
    }

    cursor_rebase(seat->cursor);
}

static void output_rebase_cursor(struct input_monitor *input_monitor)
{
    struct seat *seat;
    wl_list_for_each(seat, &input_monitor->input_manager->seats, link) {
        seat_rebase_cursor(seat);
    }
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, destroy);
    wl_list_remove(&cursor_output->link);
    wl_list_remove(&cursor_output->on.link);
    wl_list_remove(&cursor_output->destroy.link);
    free(cursor_output);
}

static void handle_output_on(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, on);
    struct kywc_output *kywc_output = cursor_output->ouput;

    input_restore_mapped_output(cursor_output->monitor, kywc_output);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = calloc(1, sizeof(struct cursor_output));
    if (!cursor_output) {
        return;
    }

    struct input_monitor *input_monitor = wl_container_of(listener, input_monitor, new_output);
    struct kywc_output *kywc_output = data;

    cursor_output->monitor = input_monitor;
    cursor_output->ouput = kywc_output;
    wl_list_insert(&input_monitor->outputs, &cursor_output->link);

    cursor_output->on.notify = handle_output_on;
    wl_signal_add(&kywc_output->events.on, &cursor_output->on);
    cursor_output->destroy.notify = handle_output_destroy;
    wl_signal_add(&kywc_output->events.destroy, &cursor_output->destroy);

    if (kywc_output->state.enabled) {
        handle_output_on(&cursor_output->on, NULL);
    }
}

static void handle_configured(struct wl_listener *listener, void *data)
{
    struct input_monitor *input_monitor = wl_container_of(listener, input_monitor, configured);
    output_rebase_cursor(input_monitor);
}

static void handle_seat_idle(struct idle *idle, void *data){};

static void handle_seat_resume(struct idle *idle, void *data)
{
    output_manager_power_outputs(true);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct input_monitor *input_monitor = wl_container_of(listener, input_monitor, new_seat);
    struct seat *seat = data;

    seat_rebase_cursor(seat);
    idle_manager_add_idle(seat, false, 0, handle_seat_idle, handle_seat_resume, NULL, NULL);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct input_monitor *input_monitor = wl_container_of(listener, input_monitor, server_destroy);

    wl_list_remove(&input_monitor->server_destroy.link);
    wl_list_remove(&input_monitor->new_seat.link);
    wl_list_remove(&input_monitor->new_output.link);
    wl_list_remove(&input_monitor->configured.link);

    struct cursor_output *cursor_output, *tmp;
    wl_list_for_each_safe(cursor_output, tmp, &input_monitor->outputs, link) {
        handle_output_destroy(&cursor_output->destroy, NULL);
    }

    free(input_monitor);
}

struct input_monitor *input_monitor_create(struct input_manager *input_manager)
{
    struct input_monitor *input_monitor = calloc(1, sizeof(struct input_monitor));
    if (!input_monitor) {
        return NULL;
    }

    wl_list_init(&input_monitor->outputs);
    input_monitor->input_manager = input_manager;
    input_monitor->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &input_monitor->server_destroy);

    input_monitor->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&input_monitor->new_output);
    input_monitor->configured.notify = handle_configured;
    output_manager_add_configured_listener(&input_monitor->configured);

    input_monitor->new_seat.notify = handle_new_seat;
    wl_signal_add(&input_manager->events.new_seat, &input_monitor->new_seat);

    return input_monitor;
}
