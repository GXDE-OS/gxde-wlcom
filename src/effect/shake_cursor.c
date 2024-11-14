// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>
#include <string.h>

#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include "effect/animator.h"
#include "effect_p.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "painter.h"
#include "render/pass.h"
#include "theme.h"
#include "util/time.h"

#define INTERVAL (500)
#define DURATION (150)
#define POINTS_SIZE (64)

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

enum cursor_stage {
    CURSOR_STAGE_HIDDEN = 0,
    CURSOR_STAGE_SHOWING,
    CURSOR_STAGE_SHOWN,
    CURSOR_STAGE_HIDING,
};

struct cursor_point {
    double lx, ly;
    uint32_t time_msec;
};

struct seat_cursor {
    struct wl_list link;
    struct shake_cursor_effect *effect;

    struct seat *seat;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_configure;
    struct wl_listener seat_destroy;

    struct cursor_point points[POINTS_SIZE];
    /* index of head and tail entry */
    size_t head, tail, count;

    /* buffer from cursor theme */
    struct wlr_buffer *buffer;
    struct wlr_texture *texture;
    uint32_t off_x, off_y;
    uint32_t orig_off_x, orig_off_y;

    double lx, ly;
    uint32_t last_shaked_time;
    uint32_t last_motion_time;
    uint32_t animation_start_time;
    enum cursor_stage stage;
};

struct shake_cursor_effect {
    struct effect *effect;
    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;

    struct wl_list cursors;
    struct wl_listener new_seat;

    struct server *server;
    struct animation *animation;
};

static bool in_same_sign(double a, double b)
{
    double tolerance = 1;
    // movements less than tolerance count as movements in any direction
    return (a >= -tolerance && b >= -tolerance) || (a <= tolerance && b <= tolerance);
}

/**
 * From KWin: Shake gestures are detected by comparing the length of the trail of the cursor
 * within past N milliseconds with the length of the diagonal of the bounding rectangle of the
 * trail. If the trail is longer than the diagonal by certain preconfigured factor, it's assumed
 * that the user shook the pointer
 */
static bool cursor_shake_detect(struct seat_cursor *cursor, double lx, double ly,
                                uint32_t time_msec)
{
    // remove old point in the history points
    size_t index = cursor->head, count = cursor->count;
    for (size_t i = 0; i < count; i++) {
        if (time_msec - cursor->points[index].time_msec < INTERVAL) {
            break;
        }
        index = (index + 1) % POINTS_SIZE;
        cursor->head = index;
        cursor->count--;
    }

    // merge points in same sign
    if (cursor->count >= 2) {
        struct cursor_point *last = &cursor->points[(cursor->tail - 1) % POINTS_SIZE];
        struct cursor_point *prev = &cursor->points[(cursor->tail - 2) % POINTS_SIZE];
        if (in_same_sign(last->lx - prev->lx, lx - last->lx) &&
            in_same_sign(last->ly - prev->ly, ly - last->ly)) {
            *last = (struct cursor_point){ lx, ly, time_msec };
            return false;
        }
    }

    // move head if ring is full
    if (cursor->count == POINTS_SIZE) {
        cursor->head = (cursor->head + 1) % POINTS_SIZE;
        cursor->count--;
    }
    // insert to tail
    cursor->points[cursor->tail] = (struct cursor_point){ lx, ly, time_msec };
    cursor->tail = (cursor->tail + 1) % POINTS_SIZE;
    cursor->count++;

    if (cursor->count < 2) {
        return false;
    }

    struct cursor_point *point = &cursor->points[cursor->head];
    double left = point->lx, top = point->ly;
    double right = point->lx, bottom = point->ly;
    double distance = 0, dx, dy;

    struct cursor_point *current, *next;
    index = cursor->head;

    for (size_t i = 1; i < cursor->count; i++) {
        current = &cursor->points[index];
        index = (index + 1) % POINTS_SIZE;
        next = &cursor->points[index];

        dx = next->lx - current->lx;
        dy = next->ly - current->ly;
        distance += sqrt(dx * dx + dy * dy);

        left = MIN(left, next->lx);
        top = MIN(top, next->ly);
        right = MAX(right, next->lx);
        bottom = MAX(bottom, next->ly);
    }

    double width = right - left, height = bottom - top;
    double diagonal = sqrt(width * width + height * height);
    if (diagonal < 100) {
        return false;
    }

    if (distance / diagonal > 4) {
        // clear points history
        cursor->head = cursor->tail;
        cursor->count = 0;
        return true;
    }

    return false;
}

static bool cursor_get_or_create_buffer(struct seat_cursor *cursor)
{
    if (cursor->buffer) {
        return true;
    }

    struct wlr_xcursor_manager *manager = cursor->seat->cursor->xcursor_manager;
    wlr_xcursor_manager_load(manager, 4.0);
    struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(manager, "default", 4.0);
    struct wlr_xcursor_image *image = xcursor->images[0];

    struct draw_info info = {
        .width = image->width,
        .height = image->height,
        .pixel = { image->width, image->height, image->buffer },
    };
    cursor->buffer = painter_draw_buffer(&info);
    if (!cursor->buffer) {
        return false;
    }

    cursor->off_x = image->hotspot_x;
    cursor->off_y = image->hotspot_y;

    xcursor = wlr_xcursor_manager_get_xcursor(manager, "default", 1.0);
    cursor->orig_off_x = xcursor->images[0]->hotspot_x;
    cursor->orig_off_y = xcursor->images[0]->hotspot_y;

    struct server *server = cursor->effect->server;
    cursor->texture = wlr_texture_from_buffer(server->renderer, cursor->buffer);

    return true;
}

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
    struct seat_cursor *cursor = wl_container_of(listener, cursor, cursor_motion);
    struct seat_cursor_motion_event *event = data;

    if (event->device && event->device->prop.type != WLR_INPUT_DEVICE_POINTER) {
        return;
    }

    cursor->lx = event->lx;
    cursor->ly = event->ly;
    cursor->last_motion_time = event->time_msec;

    if (cursor_shake_detect(cursor, event->lx, event->ly, event->time_msec)) {
        cursor->last_shaked_time = event->time_msec;
    }
}

static void handle_cursor_configure(struct wl_listener *listener, void *data)
{
    struct seat_cursor *cursor = wl_container_of(listener, cursor, cursor_configure);
    wlr_buffer_drop(cursor->buffer);
    cursor->buffer = NULL;
}

static void seat_cursor_destroy(struct seat_cursor *cursor)
{
    wl_list_remove(&cursor->seat_destroy.link);
    wl_list_remove(&cursor->cursor_configure.link);
    wl_list_remove(&cursor->cursor_motion.link);
    wl_list_remove(&cursor->link);
    wlr_buffer_drop(cursor->buffer);
    free(cursor);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_cursor *cursor = wl_container_of(listener, cursor, seat_destroy);
    seat_cursor_destroy(cursor);
}

static void seat_cursor_create(struct shake_cursor_effect *effect, struct seat *seat)
{
    struct seat_cursor *cursor = calloc(1, sizeof(*cursor));
    if (!cursor) {
        return;
    }

    cursor->effect = effect;
    wl_list_insert(&effect->cursors, &cursor->link);

    cursor->seat = seat;
    cursor->seat_destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &cursor->seat_destroy);
    cursor->cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&cursor->seat->events.cursor_motion, &cursor->cursor_motion);
    cursor->cursor_configure.notify = handle_cursor_configure;
    wl_signal_add(&cursor->seat->events.cursor_configure, &cursor->cursor_configure);

    cursor->effect->animation = animation_manager_get(ANIMATION_TYPE_EASE_IN_OUT);
    cursor->stage = CURSOR_STAGE_HIDDEN;
}

static bool handle_seat(struct seat *seat, int index, void *data)
{
    struct shake_cursor_effect *effect = data;
    seat_cursor_create(effect, seat);
    return false;
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct shake_cursor_effect *effect = wl_container_of(listener, effect, new_seat);
    struct seat *seat = data;
    seat_cursor_create(effect, seat);
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    struct shake_cursor_effect *effect = wl_container_of(listener, effect, enable);
    input_manager_for_each_seat(handle_seat, effect);
    seat_add_new_listener(&effect->new_seat);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct shake_cursor_effect *effect = wl_container_of(listener, effect, disable);

    wl_list_remove(&effect->new_seat.link);
    wl_list_init(&effect->new_seat.link);

    struct seat_cursor *cursor, *tmp;
    wl_list_for_each_safe(cursor, tmp, &effect->cursors, link) {
        seat_cursor_destroy(cursor);
    }
}

static bool handle_frame_render_pre(struct effect_entity *entity, struct ky_scene_output *output)
{
    struct shake_cursor_effect *effect = entity->user_data;

    struct seat_cursor *cursor;
    wl_list_for_each(cursor, &effect->cursors, link) {
        uint32_t time = current_time_msec();
        enum cursor_stage old_stage = cursor->stage;

        if (time - cursor->last_shaked_time < INTERVAL) {
            if (cursor->stage == CURSOR_STAGE_HIDDEN || cursor->stage == CURSOR_STAGE_HIDING) {
                cursor->stage = CURSOR_STAGE_SHOWING;
                cursor->animation_start_time = time;
            } else if (cursor->stage == CURSOR_STAGE_SHOWING &&
                       time - cursor->animation_start_time > DURATION) {
                cursor->stage = CURSOR_STAGE_SHOWN;
            }
        } else if (time - cursor->last_motion_time > INTERVAL / 2) {
            if (cursor->stage == CURSOR_STAGE_SHOWING || cursor->stage == CURSOR_STAGE_SHOWN) {
                cursor->stage = CURSOR_STAGE_HIDING;
                cursor->animation_start_time = time;
            } else if (cursor->stage == CURSOR_STAGE_HIDING &&
                       time - cursor->animation_start_time > DURATION) {
                cursor->stage = CURSOR_STAGE_HIDDEN;
            }
        }

        if (cursor->stage != CURSOR_STAGE_HIDDEN && !cursor_get_or_create_buffer(cursor)) {
            cursor->stage = CURSOR_STAGE_HIDDEN;
        }

        if (old_stage == CURSOR_STAGE_HIDDEN && cursor->stage != CURSOR_STAGE_HIDDEN) {
            cursor_set_image(cursor->seat->cursor, CURSOR_NONE);
            cursor_lock_image(cursor->seat->cursor, true);
        } else if (old_stage != CURSOR_STAGE_HIDDEN && cursor->stage == CURSOR_STAGE_HIDDEN) {
            cursor_lock_image(cursor->seat->cursor, false);
            cursor_rebase(cursor->seat->cursor);
        }
    }

    return true;
}

static bool handle_frame_render_end(struct effect_entity *entity,
                                    struct ky_scene_render_target *target)
{
    struct shake_cursor_effect *effect = entity->user_data;

    struct seat_cursor *cursor;
    wl_list_for_each(cursor, &effect->cursors, link) {
        if (cursor->stage == CURSOR_STAGE_HIDDEN) {
            continue;
        }

        struct wlr_box dst_box = {
            .x = cursor->lx - cursor->off_x,
            .y = cursor->ly - cursor->off_y,
            .width = cursor->buffer->width,
            .height = cursor->buffer->height,
        };
        if (cursor->stage == CURSOR_STAGE_SHOWING || cursor->stage == CURSOR_STAGE_HIDING) {
            float percent = (float)(current_time_msec() - cursor->animation_start_time) / DURATION;
            float value = animation_value(cursor->effect->animation, percent);
            int size = cursor->seat->state.cursor_size;
            if (cursor->stage == CURSOR_STAGE_SHOWING) {
                dst_box.width = size + (dst_box.width - size) * value;
                dst_box.height = size + (dst_box.height - size) * value;
            } else {
                dst_box.x = dst_box.x + (cursor->off_x - cursor->orig_off_x) * value;
                dst_box.y = dst_box.y + (cursor->off_y - cursor->orig_off_y) * value;
                dst_box.width = dst_box.width - (dst_box.width - size) * value;
                dst_box.height = dst_box.height - (dst_box.height - size) * value;
            }
        }

        struct wlr_box box;
        if (!wlr_box_intersection(&box, &dst_box, &target->logical)) {
            continue;
        }

        dst_box.x -= target->logical.x;
        dst_box.y -= target->logical.y;
        ky_scene_render_box(&dst_box, target);

        struct wlr_render_texture_options options = {
            .texture = cursor->texture,
            .dst_box = dst_box,
            .transform = target->transform,
        };
        wlr_render_pass_add_texture(target->render_pass, &options);
    }

    return true;
}

static bool handle_frame_render_post(struct effect_entity *entity,
                                     struct ky_scene_render_target *target)
{
    struct shake_cursor_effect *effect = entity->user_data;

    struct seat_cursor *cursor;
    wl_list_for_each(cursor, &effect->cursors, link) {
        if (cursor->stage == CURSOR_STAGE_HIDDEN) {
            continue;
        }

        pixman_region32_t region;
        pixman_region32_init_rect(&region, cursor->lx - cursor->off_x, cursor->ly - cursor->off_y,
                                  cursor->buffer->width, cursor->buffer->height);
        ky_scene_add_damage(cursor->effect->server->scene, &region);
        pixman_region32_fini(&region);
    }

    return true;
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    struct shake_cursor_effect *effect = wl_container_of(listener, effect, destroy);
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

static const struct effect_interface shake_cursor_effect_impl = {
    .frame_render_pre = handle_frame_render_pre,
    .frame_render_end = handle_frame_render_end,
    .frame_render_post = handle_frame_render_post,
    .configure = handle_effect_configure,
};

bool shake_cursor_effect_create(struct effect_manager *manager)
{
    struct shake_cursor_effect *effect = calloc(1, sizeof(*effect));
    if (!effect) {
        return false;
    }

    effect->effect = effect_create("shake_cursor", 110, false, &shake_cursor_effect_impl, effect);
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
    wl_list_init(&effect->cursors);

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
