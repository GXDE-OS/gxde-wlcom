// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "effect_p.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "render/pass.h"
#include "theme.h"
#include "util/time.h"

#define INTERVAL (750)
#define RADIUS (50)
#define CIRCLES (4)

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

struct seat_pointer {
    struct wl_list link;
    struct locate_pointer_effect *effect;

    struct seat *seat;
    struct wl_listener keyboard_key;
    struct wl_listener seat_destroy;

    double lx, ly;
    uint32_t animation_start_time;
    bool shown;
};

struct locate_pointer_effect {
    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct wl_list pointers;
    struct wl_listener new_seat;

    struct server *server;
};

static void handle_keyboard_key(struct wl_listener *listener, void *data)
{
    struct seat_pointer *pointer = wl_container_of(listener, pointer, keyboard_key);
    struct seat_keyboard_key_event *event = data;

    if (event->device && event->device->prop.is_virtual) {
        return;
    }
    if (!event->pressed || (event->keycode != KEY_LEFTCTRL && event->keycode != KEY_RIGHTCTRL)) {
        return;
    }

    pointer->animation_start_time = event->time_msec;
    if (!pointer->shown) {
        pixman_region32_t region;
        // pointer position may be different when render, but we only need damage here
        pixman_region32_init_rect(&region, pointer->seat->cursor->lx - RADIUS,
                                  pointer->seat->cursor->ly - RADIUS, RADIUS * 2, RADIUS * 2);
        ky_scene_add_damage(pointer->effect->server->scene, &region);
        pixman_region32_fini(&region);
    }
}

static void seat_pointer_destroy(struct seat_pointer *pointer)
{
    wl_list_remove(&pointer->seat_destroy.link);
    wl_list_remove(&pointer->keyboard_key.link);
    wl_list_remove(&pointer->link);
    free(pointer);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_pointer *pointer = wl_container_of(listener, pointer, seat_destroy);
    seat_pointer_destroy(pointer);
}

static void seat_pointer_create(struct locate_pointer_effect *effect, struct seat *seat)
{
    struct seat_pointer *pointer = calloc(1, sizeof(*pointer));
    if (!pointer) {
        return;
    }

    pointer->effect = effect;
    wl_list_insert(&effect->pointers, &pointer->link);

    pointer->seat = seat;
    pointer->seat_destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &pointer->seat_destroy);
    pointer->keyboard_key.notify = handle_keyboard_key;
    wl_signal_add(&pointer->seat->events.keyboard_key, &pointer->keyboard_key);
}

static bool handle_seat(struct seat *seat, int index, void *data)
{
    struct locate_pointer_effect *effect = data;
    seat_pointer_create(effect, seat);
    return false;
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct locate_pointer_effect *effect = wl_container_of(listener, effect, new_seat);
    struct seat *seat = data;
    seat_pointer_create(effect, seat);
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct locate_pointer_effect *effect = wl_container_of(listener, effect, enable);
    input_manager_for_each_seat(handle_seat, effect);
    seat_add_new_listener(&effect->new_seat);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct locate_pointer_effect *effect = wl_container_of(listener, effect, disable);

    wl_list_remove(&effect->new_seat.link);
    wl_list_init(&effect->new_seat.link);

    struct seat_pointer *pointer, *tmp;
    wl_list_for_each_safe(pointer, tmp, &effect->pointers, link) {
        seat_pointer_destroy(pointer);
    }
}

static bool handle_frame_render_begin(struct effect_entity *entity,
                                      struct ky_scene_render_target *target)
{
    struct locate_pointer_effect *effect = entity->user_data;

    struct seat_pointer *pointer;
    wl_list_for_each(pointer, &effect->pointers, link) {
        pointer->shown = current_time_msec() - pointer->animation_start_time <= INTERVAL;
        if (pointer->shown) {
            pointer->lx = pointer->seat->cursor->lx;
            pointer->ly = pointer->seat->cursor->ly;
        }
    }

    return true;
}

static bool handle_frame_render_end(struct effect_entity *entity,
                                    struct ky_scene_render_target *target)
{
    struct locate_pointer_effect *effect = entity->user_data;

    struct seat_pointer *pointer;
    wl_list_for_each(pointer, &effect->pointers, link) {
        if (!pointer->shown) {
            continue;
        }

        struct wlr_box dst_box = { pointer->lx - RADIUS, pointer->ly - RADIUS, RADIUS * 2,
                                   RADIUS * 2 };
        struct wlr_box box;
        if (!wlr_box_intersection(&box, &dst_box, &target->logical)) {
            continue;
        }

        float progress = (float)(current_time_msec() - pointer->animation_start_time) / INTERVAL;
        float *color = theme_manager_get_current()->accent_color;
        float circle_progress, alpha, radius;

        for (int i = 0; i <= CIRCLES; i++) {
            if (progress <= 0.) {
                break;
            }

            circle_progress = MIN(1., (progress * 2));
            progress -= 0.5 / CIRCLES;
            if (circle_progress >= 1.) {
                continue;
            }

            alpha = 1 - circle_progress;
            radius = ceil(RADIUS * circle_progress);

            dst_box.x = pointer->lx - radius;
            dst_box.y = pointer->ly - radius;
            dst_box.height = dst_box.width = radius * 2;

            dst_box.x -= target->logical.x;
            dst_box.y -= target->logical.y;
            ky_scene_render_box(&dst_box, target);
            radius *= target->scale;

            struct ky_render_rect_options options = {
                .base = {
                    .box = dst_box,
                    .color = { color[0] * alpha, color[1] * alpha, color[2] * alpha, alpha },
                },
                .radius = { radius , radius, radius, radius },
            };
            ky_render_pass_add_rect(target->render_pass, &options);
        }
    }

    return true;
}

static bool handle_frame_render_post(struct effect_entity *entity,
                                     struct ky_scene_render_target *target)
{
    struct locate_pointer_effect *effect = entity->user_data;

    struct seat_pointer *pointer;
    wl_list_for_each(pointer, &effect->pointers, link) {
        if (!pointer->shown) {
            continue;
        }

        pixman_region32_t region;
        pixman_region32_init_rect(&region, pointer->lx - RADIUS, pointer->ly - RADIUS, RADIUS * 2,
                                  RADIUS * 2);
        ky_scene_add_damage(pointer->effect->server->scene, &region);
        pixman_region32_fini(&region);
    }

    return true;
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct locate_pointer_effect *effect = wl_container_of(listener, effect, destroy);
    wl_list_remove(&effect->destroy.link);
    wl_list_remove(&effect->enable.link);
    wl_list_remove(&effect->disable.link);
    wl_list_remove(&effect->new_seat.link);
    free(effect);
}

static bool handle_effect_configure(struct effect *effect, const struct effect_option *option)
{
    if (effect_option_is_enabled_option(option)) {
        return true;
    }

    return false;
}

static const struct effect_interface locate_pointer_effect_impl = {
    .frame_render_begin = handle_frame_render_begin,
    .frame_render_end = handle_frame_render_end,
    .frame_render_post = handle_frame_render_post,
    .configure = handle_effect_configure,
};

bool locate_pointer_effect_create(struct effect_manager *manager)
{
    struct locate_pointer_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect =
        effect_create("locate_pointer", 111, false, &locate_pointer_effect_impl, effect);
    if (!effect->effect) {
        free(effect);
        return false;
    }

    struct effect_entity *entity = ky_scene_add_effect(manager->server->scene, effect->effect);
    if (!entity) {
        effect_destroy(effect->effect);
        free(effect);
        return false;
    }

    entity->user_data = effect;
    effect->server = manager->server;
    wl_list_init(&effect->pointers);

    effect->enable.notify = handle_effect_enable;
    wl_signal_add(&effect->effect->events.enable, &effect->enable);
    effect->disable.notify = handle_effect_disable;
    wl_signal_add(&effect->effect->events.disable, &effect->disable);
    effect->destroy.notify = handle_effect_destroy;
    wl_signal_add(&effect->effect->events.destroy, &effect->destroy);
    effect->new_seat.notify = handle_new_seat;
    wl_list_init(&effect->new_seat.link);

    if (effect->effect->enabled) {
        handle_effect_enable(&effect->enable, NULL);
    }

    return true;
}
