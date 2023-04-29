#include <stdlib.h>

#include <kywc/log.h>

#include "input.h"
#include "output.h"
#include "server.h"

struct cursor_output {
    struct wl_list link;

    struct output_monitor *monitor;
    struct kywc_output *ouput;

    struct wl_listener on;
    struct wl_listener off;
    struct wl_listener scale;
    struct wl_listener destroy;
};

struct output_monitor {
    struct input_manager *input_manager;
    struct kywc_output *primary;

    struct wl_list outputs;
    struct wl_listener new_output;
    struct wl_listener primary_output;

    struct wl_listener new_seat;
    struct wl_listener server_destroy;
};

void output_move_cursor_to_center(struct seat *seat, struct kywc_output *kywc_output)
{
    int x, y, width, height;

    kywc_output_effective_resolution(kywc_output, &width, &height);
    x = kywc_output->state.lx + width / 2;
    y = kywc_output->state.ly + height / 2;

    kywc_log(KYWC_DEBUG, "move %s's cursor to %s (%d, %d)", seat->name, kywc_output->name, x, y);
    seat_move_cursor(seat, x, y, false);
}

static void input_restore_mapped_output(struct output_monitor *output_monitor,
                                        struct kywc_output *kywc_output)
{
    struct input *input;
    wl_list_for_each(input, &output_monitor->input_manager->inputs, link) {
        if (!input->desired_mapped_output ||
            strcmp(input->desired_mapped_output, kywc_output->name)) {
            continue;
        }

        struct input_state state = input->state;
        state.mapped_to_output = kywc_output->name;
        input_set_state(input, &state);
    }
}

static void output_rebase_cursor(struct output_monitor *output_monitor, float scale)
{
    struct seat *seat;
    wl_list_for_each(seat, &output_monitor->input_manager->seats, link) {
        seat_set_cursor_image(seat, CURSOR_DEFAULT, scale, true);
        /* move seat cursor to primary whether inputs mapped output */
        if (output_monitor->primary) {
            output_move_cursor_to_center(seat, output_monitor->primary);
        }
    }
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, destroy);

    wl_list_remove(&cursor_output->link);
    wl_list_remove(&cursor_output->on.link);
    wl_list_remove(&cursor_output->off.link);
    wl_list_remove(&cursor_output->scale.link);
    wl_list_remove(&cursor_output->destroy.link);

    /* if primary not update when primary output is being destroy */
    if (cursor_output->monitor->primary == cursor_output->ouput) {
        cursor_output->monitor->primary = NULL;
    }

    output_rebase_cursor(cursor_output->monitor, 1.0);

    free(cursor_output);
}

static void handle_output_on(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, on);
    struct kywc_output *kywc_output = cursor_output->ouput;

    output_rebase_cursor(cursor_output->monitor, kywc_output->state.scale);
    input_restore_mapped_output(cursor_output->monitor, kywc_output);
}

static void handle_output_off(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, off);

    /* if primary not update when primary output is being off */
    if (cursor_output->monitor->primary == cursor_output->ouput) {
        cursor_output->monitor->primary = NULL;
    }

    output_rebase_cursor(cursor_output->monitor, 1.0);
}

static void handle_output_scale(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, scale);
    struct kywc_output *kywc_output = cursor_output->ouput;

    output_rebase_cursor(cursor_output->monitor, kywc_output->state.scale);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = calloc(1, sizeof(struct cursor_output));
    if (!cursor_output) {
        return;
    }

    struct output_monitor *output_monitor = wl_container_of(listener, output_monitor, new_output);
    struct kywc_output *kywc_output = data;

    cursor_output->monitor = output_monitor;
    cursor_output->ouput = kywc_output;
    wl_list_insert(&output_monitor->outputs, &cursor_output->link);

    cursor_output->on.notify = handle_output_on;
    wl_signal_add(&kywc_output->events.on, &cursor_output->on);
    cursor_output->off.notify = handle_output_off;
    wl_signal_add(&kywc_output->events.off, &cursor_output->off);
    cursor_output->scale.notify = handle_output_scale;
    wl_signal_add(&kywc_output->events.scale, &cursor_output->scale);
    cursor_output->destroy.notify = handle_output_destroy;
    wl_signal_add(&kywc_output->events.destroy, &cursor_output->destroy);

    if (!kywc_output->state.enabled) {
        return;
    }

    output_rebase_cursor(cursor_output->monitor, kywc_output->state.scale);
    input_restore_mapped_output(cursor_output->monitor, kywc_output);
}

static void handle_primary_output(struct wl_listener *listener, void *data)
{
    struct output_monitor *output_monitor =
        wl_container_of(listener, output_monitor, primary_output);

    struct kywc_output *kywc_output = data;
    output_monitor->primary = kywc_output;

    output_rebase_cursor(output_monitor, kywc_output ? kywc_output->state.scale : 1.0);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct output_monitor *output_monitor = wl_container_of(listener, output_monitor, new_seat);
    struct seat *seat = data;

    struct cursor_output *cursor_output;
    wl_list_for_each(cursor_output, &output_monitor->outputs, link) {
        seat_set_cursor_image(seat, CURSOR_DEFAULT, cursor_output->ouput->state.scale, false);
        if (output_monitor->primary) {
            output_move_cursor_to_center(seat, output_monitor->primary);
        } else {
            seat_move_cursor(seat, 0, 0, true);
        }
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct output_monitor *output_monitor =
        wl_container_of(listener, output_monitor, server_destroy);

    wl_list_remove(&output_monitor->new_seat.link);
    wl_list_remove(&output_monitor->new_output.link);
    wl_list_remove(&output_monitor->primary_output.link);
    wl_list_remove(&output_monitor->server_destroy.link);

    struct cursor_output *cursor_output, *cursor_output_tmp;
    wl_list_for_each_safe(cursor_output, cursor_output_tmp, &output_monitor->outputs, link) {
        wl_list_remove(&cursor_output->link);
        wl_list_remove(&cursor_output->on.link);
        wl_list_remove(&cursor_output->off.link);
        wl_list_remove(&cursor_output->scale.link);
        wl_list_remove(&cursor_output->destroy.link);
        free(cursor_output);
    }

    free(output_monitor);
}

struct output_monitor *output_monitor_create(struct input_manager *input_manager)
{
    struct output_monitor *output_monitor = calloc(1, sizeof(struct output_monitor));
    if (!output_monitor) {
        return NULL;
    }

    output_monitor->input_manager = input_manager;

    wl_list_init(&output_monitor->outputs);

    output_monitor->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&output_monitor->new_output);
    output_monitor->primary_output.notify = handle_primary_output;
    kywc_output_add_primary_listener(&output_monitor->primary_output);

    output_monitor->new_seat.notify = handle_new_seat;
    wl_signal_add(&input_manager->events.new_seat, &output_monitor->new_seat);

    output_monitor->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &output_monitor->server_destroy);

    return output_monitor;
}
