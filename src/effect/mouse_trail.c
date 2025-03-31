// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>

#include "effect/shape.h"
#include "effect_p.h"
#include "input/cursor.h"
#include "input/seat.h"

struct seat_mouse {
    struct wl_list link;
    struct mouse_trail_effect *effect;
    struct seat *seat;
    struct wl_listener mouse_motion;
    struct wl_listener destroy;
};

struct mouse_trail_effect {
    struct trail_effect *base;
    struct wl_listener new_seat;
    struct wl_list seat_mouses;
};

static void handle_mouse_motion(struct wl_listener *listener, void *data)
{
    struct seat_mouse *seat_mouse = wl_container_of(listener, seat_mouse, mouse_motion);
    struct mouse_trail_effect *effect = seat_mouse->effect;

    int32_t lx = roundf(seat_mouse->seat->cursor->lx);
    int32_t ly = roundf(roundf(seat_mouse->seat->cursor->ly));
    trail_effect_add_trail(effect->base, 0, lx, ly);
    trail_effect_trail_add_point(effect->base, 0, lx, ly);
}

static void seat_mouse_destroy(struct seat_mouse *seat_mouse)
{
    wl_list_remove(&seat_mouse->link);
    wl_list_remove(&seat_mouse->mouse_motion.link);
    wl_list_remove(&seat_mouse->destroy.link);
    free(seat_mouse);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_mouse *seat_mouse = wl_container_of(listener, seat_mouse, destroy);
    seat_mouse_destroy(seat_mouse);
}

static void seat_mouse_create(struct mouse_trail_effect *effect, struct seat *seat)
{
    struct seat_mouse *seat_mouse = calloc(1, sizeof(*seat_mouse));
    if (!seat_mouse) {
        return;
    }

    wl_list_insert(&effect->seat_mouses, &seat_mouse->link);
    seat_mouse->effect = effect;
    seat_mouse->seat = seat;
    seat_mouse->mouse_motion.notify = handle_mouse_motion;
    wl_signal_add(&seat->cursor->wlr_cursor->events.motion, &seat_mouse->mouse_motion);
    seat_mouse->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &seat_mouse->destroy);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct mouse_trail_effect *effect = wl_container_of(listener, effect, new_seat);
    struct seat *seat = data;
    seat_mouse_create(effect, seat);
}

static bool handle_seat(struct seat *seat, int index, void *data)
{
    struct mouse_trail_effect *effect = data;
    seat_mouse_create(effect, seat);
    return false;
}

static void handle_effect_enable(struct trail_effect *base_effect, void *user_data)
{
    struct mouse_trail_effect *effect = user_data;

    input_manager_for_each_seat(handle_seat, effect);

    effect->new_seat.notify = handle_new_seat;
    seat_add_new_listener(&effect->new_seat);
}

static void handle_effect_disable(struct trail_effect *base_effect, void *user_data)
{
    struct mouse_trail_effect *effect = user_data;

    wl_list_remove(&effect->new_seat.link);
    wl_list_init(&effect->new_seat.link);

    struct seat_mouse *seat_mouse, *tmp0;
    wl_list_for_each_safe(seat_mouse, tmp0, &effect->seat_mouses, link) {
        seat_mouse_destroy(seat_mouse);
    }
}

static void handle_effect_destroy(struct trail_effect *base_effect, void *user_data)
{
    struct mouse_trail_effect *effect = user_data;
    free(effect);
}

static const struct trail_effect_interface effect_impl = {
    .enable = handle_effect_enable,
    .disable = handle_effect_disable,
    .destroy = handle_effect_destroy,
};

bool mouse_trail_effect_create(struct effect_manager *manager)
{
    struct mouse_trail_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }
    wl_list_init(&effect->new_seat.link);
    wl_list_init(&effect->seat_mouses);

    struct trail_effect_options options = {};
    options.color[0] = 1.0f;
    options.color[1] = 0.2f;
    options.color[2] = 0.2f;
    options.color[3] = 0.6f;
    options.thickness = 8.0f;
    options.life_time = 200;

    effect->base =
        trail_effect_create(manager, &options, &effect_impl, "mouse_trail", 100, false, effect);
    if (!effect->base) {
        free(effect);
        return false;
    }

    return true;
}
