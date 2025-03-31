// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer.h>

#include "effect/shape.h"
#include "effect_p.h"
#include "input/cursor.h"
#include "input/seat.h"

struct mouse_click_effect {
    struct tap_ripple_effect *base;
    struct wl_listener new_seat;
    struct wl_list seat_mouses;
};

struct seat_mouse {
    struct wl_list link;
    struct mouse_click_effect *effect;
    struct seat *seat;
    struct wl_listener mouse_button;
    struct wl_listener destroy;
};

static void handle_mouse_button(struct wl_listener *listener, void *data)
{
    struct wlr_pointer_button_event *event = data;
    if (event->state != WLR_BUTTON_PRESSED) {
        return;
    }
    struct seat_mouse *seat_mouse = wl_container_of(listener, seat_mouse, mouse_button);
    struct mouse_click_effect *effect = seat_mouse->effect;
    tap_ripple_effect_remove_all_points(effect->base);
    tap_ripple_effect_add_point(effect->base, 0, roundf(seat_mouse->seat->cursor->lx),
                                roundf(seat_mouse->seat->cursor->ly));
}

static void seat_mouse_destroy(struct seat_mouse *seat_mouse)
{
    wl_list_remove(&seat_mouse->link);
    wl_list_remove(&seat_mouse->mouse_button.link);
    wl_list_remove(&seat_mouse->destroy.link);
    free(seat_mouse);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_mouse *seat_mouse = wl_container_of(listener, seat_mouse, destroy);
    seat_mouse_destroy(seat_mouse);
}

static void seat_mouse_create(struct mouse_click_effect *effect, struct seat *seat)
{
    struct seat_mouse *seat_mouse = calloc(1, sizeof(*seat_mouse));
    if (!seat_mouse) {
        return;
    }
    seat_mouse->effect = effect;
    seat_mouse->seat = seat;
    seat_mouse->mouse_button.notify = handle_mouse_button;
    wl_signal_add(&seat->cursor->wlr_cursor->events.button, &seat_mouse->mouse_button);
    seat_mouse->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &seat_mouse->destroy);

    wl_list_insert(&effect->seat_mouses, &seat_mouse->link);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct mouse_click_effect *effect = wl_container_of(listener, effect, new_seat);
    struct seat *seat = data;
    seat_mouse_create(effect, seat);
}

static bool handle_seat(struct seat *seat, int index, void *data)
{
    struct mouse_click_effect *effect = data;
    seat_mouse_create(effect, seat);
    return false;
}

static void handle_effect_enable(struct tap_ripple_effect *base_effect, void *data)
{
    struct mouse_click_effect *effect = data;

    input_manager_for_each_seat(handle_seat, effect);

    effect->new_seat.notify = handle_new_seat;
    seat_add_new_listener(&effect->new_seat);
}

static void handle_effect_disable(struct tap_ripple_effect *base_effect, void *data)
{
    struct mouse_click_effect *effect = data;

    wl_list_remove(&effect->new_seat.link);
    wl_list_init(&effect->new_seat.link);

    struct seat_mouse *seat_mouse, *tmp0;
    wl_list_for_each_safe(seat_mouse, tmp0, &effect->seat_mouses, link) {
        seat_mouse_destroy(seat_mouse);
    }
}

static void handle_effect_destroy(struct tap_ripple_effect *base_effect, void *data)
{
    struct mouse_click_effect *effect = data;
    free(effect);
}

static const struct tap_ripple_effect_interface effect_impl = {
    .enable = handle_effect_enable,
    .disable = handle_effect_disable,
    .destroy = handle_effect_destroy,
};

bool mouse_click_effect_create(struct effect_manager *manager)
{
    struct mouse_click_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }
    wl_list_init(&effect->new_seat.link);
    wl_list_init(&effect->seat_mouses);

    struct tap_ripple_effect_options options = {};
    options.size = 50;
    options.color[0] = 1.0f;
    options.color[1] = 0.2f;
    options.color[2] = 0.2f;
    options.animate_duration = 600;
    options.start_radius = 0.5f;
    options.end_radius = 0.0f;
    options.start_attenuation = 0.4f;
    options.end_attenuation = 0.8f;

    effect->base = tap_ripple_effect_create(manager, &options, &effect_impl, "mouse_click", 100,
                                            false, effect);
    if (!effect->base) {
        free(effect);
        return false;
    }

    return true;
}
