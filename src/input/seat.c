#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <wayland-client-protocol.h>

#include "input.h"
#include "server.h"

struct seat *seat_create(struct input_manager *input_manager, const char *name)
{
    struct seat *seat = calloc(1, sizeof(struct seat));
    if (!seat) {
        return NULL;
    }

    seat->name = strdup(name);
    wl_list_init(&seat->inputs);
    wl_list_init(&seat->keyboards);

    wl_list_insert(&input_manager->seats, &seat->link);

    struct server *server = input_manager->server;
    if (server->impl) {
        server->impl->init_seat(server, seat);
    }

    return seat;
}

void seat_destroy(struct seat *seat)
{
    wl_list_remove(&seat->link);

    struct input *input;
    wl_list_for_each(input, &seat->inputs, seat_link) {
        seat_remove_input(input);
    }

    if (seat->impl) {
        seat->impl->destroy(seat);
    }

    free(seat->name);
    free(seat);
}

static void seat_update_capabilities(struct seat *seat)
{
    struct input *input;
    seat->caps = 0;
    wl_list_for_each(input, &seat->inputs, link) {
        switch (input->prop.type) {
        case KYWC_INPUT_DEVICE_KEYBOARD:
            seat->caps |= WL_SEAT_CAPABILITY_KEYBOARD;
            break;
        case KYWC_INPUT_DEVICE_POINTER:
            seat->caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
        case KYWC_INPUT_DEVICE_TOUCH:
            seat->caps |= WL_SEAT_CAPABILITY_TOUCH;
            break;
        case KYWC_INPUT_DEVICE_TABLET_TOOL:
            seat->caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
        case KYWC_INPUT_DEVICE_SWITCH:
        case KYWC_INPUT_DEVICE_TABLET_PAD:
            break;
        }
    }

    seat->impl->set_caps(seat, seat->caps);
}

void seat_add_input(struct seat *seat, struct input *input)
{
    input->seat = seat;
    wl_list_insert(&seat->inputs, &input->seat_link);

    seat->impl->add_input(seat, input);

    seat_update_capabilities(seat);
}

void seat_remove_input(struct input *input)
{
    struct seat *seat = input->seat;

    seat->impl->remove_input(seat, input);

    wl_list_remove(&input->seat_link);
    input->seat = NULL;

    seat_update_capabilities(seat);
}

void seat_set_cursor_image(struct seat *seat, enum cursor_name name, float scale, bool force)
{
    struct cursor *cursor = seat->cursor;
    if (!cursor) {
        return;
    }

    if (!force && cursor->name == name && cursor->scale == scale) {
        return;
    }

    seat->impl->set_cursor_image(seat, name, scale);

    cursor->scale = scale;
    cursor->name = name;
}
