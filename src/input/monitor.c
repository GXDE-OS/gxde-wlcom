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
    struct wl_listener power;
    struct wl_listener scale;
    struct wl_listener destroy;
};

struct input_monitor {
    struct input_manager *input_manager;
    struct kywc_output *primary;

    struct wl_list outputs;
    struct wl_listener new_output;
    struct wl_listener primary_output;
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

    cursor_move(cursor, geo.x, geo.y, false);
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

static void output_rebase_cursor(struct input_monitor *input_monitor, float scale, bool move)
{
    struct seat *seat;
    wl_list_for_each(seat, &input_monitor->input_manager->seats, link) {
        if (scale != 0.0) {
            cursor_reload_image(seat->cursor, scale);
        }

        if (!move) {
            continue;
        }

        /* prefer to move cursor to mapped output */
        struct kywc_output *output = seat_pick_mapped_output(seat);
        output = output ? output : input_monitor->primary;
        if (output) {
            cursor_move_to_output_center(seat->cursor, output);
        }
    }
}

static void handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, destroy);

    wl_list_remove(&cursor_output->link);
    wl_list_remove(&cursor_output->on.link);
    wl_list_remove(&cursor_output->power.link);
    wl_list_remove(&cursor_output->scale.link);
    wl_list_remove(&cursor_output->destroy.link);

    /* if primary not update when primary output is being destroy */
    if (cursor_output->monitor->primary == cursor_output->ouput) {
        cursor_output->monitor->primary = NULL;
    }

    output_rebase_cursor(cursor_output->monitor, 0.0, true);

    free(cursor_output);
}

static void handle_output_on(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, on);
    struct kywc_output *kywc_output = cursor_output->ouput;

    output_rebase_cursor(cursor_output->monitor, kywc_output->state.scale, true);
    input_restore_mapped_output(cursor_output->monitor, kywc_output);
}

static void handle_output_power(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, power);
    struct kywc_output *kywc_output = cursor_output->ouput;

    if (kywc_output->state.power) {
        output_rebase_cursor(cursor_output->monitor, kywc_output->state.scale, false);
    }
}

static void handle_output_scale(struct wl_listener *listener, void *data)
{
    struct cursor_output *cursor_output = wl_container_of(listener, cursor_output, scale);
    struct kywc_output *kywc_output = cursor_output->ouput;

    output_rebase_cursor(cursor_output->monitor, kywc_output->state.scale, true);
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
    cursor_output->power.notify = handle_output_power;
    wl_signal_add(&kywc_output->events.power, &cursor_output->power);
    cursor_output->scale.notify = handle_output_scale;
    wl_signal_add(&kywc_output->events.scale, &cursor_output->scale);
    cursor_output->destroy.notify = handle_output_destroy;
    wl_signal_add(&kywc_output->events.destroy, &cursor_output->destroy);

    if (!kywc_output->state.enabled) {
        return;
    }

    output_rebase_cursor(cursor_output->monitor, kywc_output->state.scale, true);
    input_restore_mapped_output(cursor_output->monitor, kywc_output);
}

static void handle_primary_output(struct wl_listener *listener, void *data)
{
    struct input_monitor *input_monitor = wl_container_of(listener, input_monitor, primary_output);

    struct kywc_output *kywc_output = data;
    input_monitor->primary = kywc_output;
}

static void handle_configured(struct wl_listener *listener, void *data)
{
    struct input_monitor *input_monitor = wl_container_of(listener, input_monitor, configured);
    output_rebase_cursor(input_monitor, 0.0, true);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct input_monitor *input_monitor = wl_container_of(listener, input_monitor, new_seat);
    struct seat *seat = data;

    struct cursor_output *cursor_output;
    wl_list_for_each(cursor_output, &input_monitor->outputs, link) {
        cursor_reload_image(seat->cursor, cursor_output->ouput->state.scale);
        if (input_monitor->primary) {
            cursor_move_to_output_center(seat->cursor, input_monitor->primary);
        } else {
            cursor_move(seat->cursor, 0, 0, true);
        }
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct input_monitor *input_monitor = wl_container_of(listener, input_monitor, server_destroy);

    wl_list_remove(&input_monitor->new_seat.link);
    wl_list_remove(&input_monitor->new_output.link);
    wl_list_remove(&input_monitor->primary_output.link);
    wl_list_remove(&input_monitor->configured.link);
    wl_list_remove(&input_monitor->server_destroy.link);

    struct cursor_output *cursor_output, *cursor_output_tmp;
    wl_list_for_each_safe(cursor_output, cursor_output_tmp, &input_monitor->outputs, link) {
        wl_list_remove(&cursor_output->link);
        wl_list_remove(&cursor_output->on.link);
        wl_list_remove(&cursor_output->power.link);
        wl_list_remove(&cursor_output->scale.link);
        wl_list_remove(&cursor_output->destroy.link);
        free(cursor_output);
    }

    free(input_monitor);
}

struct input_monitor *input_monitor_create(struct input_manager *input_manager)
{
    struct input_monitor *input_monitor = calloc(1, sizeof(struct input_monitor));
    if (!input_monitor) {
        return NULL;
    }

    input_monitor->input_manager = input_manager;

    wl_list_init(&input_monitor->outputs);

    input_monitor->new_output.notify = handle_new_output;
    kywc_output_add_new_listener(&input_monitor->new_output);
    input_monitor->primary_output.notify = handle_primary_output;
    kywc_output_add_primary_listener(&input_monitor->primary_output);
    input_monitor->configured.notify = handle_configured;
    output_manager_add_configured_listener(&input_monitor->configured);

    input_monitor->new_seat.notify = handle_new_seat;
    wl_signal_add(&input_manager->events.new_seat, &input_monitor->new_seat);

    input_monitor->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &input_monitor->server_destroy);

    return input_monitor;
}
