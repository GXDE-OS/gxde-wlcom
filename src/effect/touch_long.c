// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdint.h>
#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_touch.h>

#include "effect/shape.h"
#include "effect_p.h"
#include "input/cursor.h"
#include "input/seat.h"

struct seat_touch {
    struct wl_list link;
    struct touch_long_effect *effect;
    struct seat *seat;
    struct wl_listener touch_up;
    struct wl_listener touch_down;
    struct wl_listener touch_motion;
    struct wl_listener destroy;
};

struct touch_long_effect {
    struct circle_progressbar_effect *base;
    struct wl_listener new_seat;
    struct wl_list seat_touchs;

    double touch_x, touch_y;
};

static void handle_touch_down(struct wl_listener *listener, void *data)
{
    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_down);
    struct wlr_touch_down_event *event = data;
    // only first finger touch
    if (event->touch_id != 0) {
        circle_progressbar_effect_end(seat_touch->effect->base);
        return;
    }

    seat_touch->effect->touch_x = event->x;
    seat_touch->effect->touch_y = event->y;
    int32_t lx = roundf(seat_touch->seat->cursor->lx);
    int32_t ly = roundf(seat_touch->seat->cursor->ly);
    circle_progressbar_effect_begin(seat_touch->effect->base, lx, ly);
}

static void handle_touch_up(struct wl_listener *listener, void *data)
{
    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_up);
    circle_progressbar_effect_end(seat_touch->effect->base);
}

static void handle_touch_motion(struct wl_listener *listener, void *data)
{
    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, touch_motion);
    struct wlr_touch_motion_event *event = data;
    if (event->touch_id != 0) {
        circle_progressbar_effect_end(seat_touch->effect->base);
        return;
    }

    const float anti_acciden = 0.01f; // move diff > this then remove
    if (fabs(seat_touch->effect->touch_x - event->x) > anti_acciden ||
        fabs(seat_touch->effect->touch_y - event->y) > anti_acciden) {
        circle_progressbar_effect_end(seat_touch->effect->base);
    }
}

static void seat_touch_destroy(struct seat_touch *seat_touch)
{
    wl_list_remove(&seat_touch->link);
    wl_list_remove(&seat_touch->touch_up.link);
    wl_list_remove(&seat_touch->touch_down.link);
    wl_list_remove(&seat_touch->touch_motion.link);
    wl_list_remove(&seat_touch->destroy.link);
    free(seat_touch);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_touch *seat_touch = wl_container_of(listener, seat_touch, destroy);
    seat_touch_destroy(seat_touch);
}

static void seat_touch_create(struct touch_long_effect *effect, struct seat *seat)
{
    struct seat_touch *seat_touch = calloc(1, sizeof(*seat_touch));
    if (!seat_touch) {
        return;
    }

    wl_list_insert(&effect->seat_touchs, &seat_touch->link);
    seat_touch->effect = effect;
    seat_touch->seat = seat;
    seat_touch->touch_up.notify = handle_touch_up;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_up, &seat_touch->touch_up);
    seat_touch->touch_down.notify = handle_touch_down;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_down, &seat_touch->touch_down);
    seat_touch->touch_motion.notify = handle_touch_motion;
    wl_signal_add(&seat->cursor->wlr_cursor->events.touch_motion, &seat_touch->touch_motion);
    seat_touch->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &seat_touch->destroy);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct touch_long_effect *effect = wl_container_of(listener, effect, new_seat);
    struct seat *seat = data;
    seat_touch_create(effect, seat);
}

static bool handle_seat(struct seat *seat, int index, void *data)
{
    struct touch_long_effect *effect = data;
    seat_touch_create(effect, seat);
    return false;
}

static void handle_effect_enable(struct circle_progressbar_effect *base_effect, void *user_data)
{
    struct touch_long_effect *effect = user_data;

    input_manager_for_each_seat(handle_seat, effect);

    effect->new_seat.notify = handle_new_seat;
    seat_add_new_listener(&effect->new_seat);
}

static void handle_effect_disable(struct circle_progressbar_effect *base_effect, void *user_data)
{
    struct touch_long_effect *effect = user_data;

    wl_list_remove(&effect->new_seat.link);
    wl_list_init(&effect->new_seat.link);

    struct seat_touch *seat_touch, *tmp0;
    wl_list_for_each_safe(seat_touch, tmp0, &effect->seat_touchs, link) {
        seat_touch_destroy(seat_touch);
    }
}

static void handle_effect_destroy(struct circle_progressbar_effect *base_effect, void *user_data)
{
    struct touch_long_effect *effect = user_data;
    free(effect);
}

static const struct circle_progressbar_effect_interface effect_impl = {
    .enable = handle_effect_enable,
    .disable = handle_effect_disable,
    .destroy = handle_effect_destroy,
};

bool touch_long_effect_create(struct effect_manager *manager)
{
    struct touch_long_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }
    wl_list_init(&effect->new_seat.link);
    wl_list_init(&effect->seat_touchs);

    struct circle_progressbar_effect_options options = {};
    options.size = 100;
    options.animate_delay = 200;
    options.animate_duration = 800;

    effect->base = circle_progressbar_effect_create(manager, &options, &effect_impl, "touch_long",
                                                    102, true, effect);
    if (!effect->base) {
        free(effect);
        return false;
    }

    return true;
}
