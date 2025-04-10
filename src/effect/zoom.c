// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_touch.h>

#include <kywc/log.h>

#include "effect_p.h"
#include "input/cursor.h"
#include "input/seat.h"

#define MAX_SCALE 16

struct seat_cursor {
    struct seat *seat;
    struct wl_list link;

    struct wl_listener cursor_motion;
    struct wl_listener seat_destroy;
};

struct zoom_effect {
    struct effect *effect;

    int scale;
    /* logic coord */
    double mouse_lx, mouse_ly;
    bool cursor_moved;
    /* record offset from the viewport box to the nearest point */
    int offset_x, offset_y;
    /* record the viewport box */
    pixman_region32_t viewport_region;

    struct ky_scene *scene;

    struct wl_listener new_seat;
    struct wl_list seat_cursors; // seat_cursor

    struct wl_listener enable;
    struct wl_listener disable;
    struct wl_listener destroy;
};

static struct zoom_effect *zoom = NULL;

static bool handle_effect_configure(struct effect *effect, const struct effect_option *option)
{
    if (effect_option_is_enabled_option(option)) {
        return true;
    }

    if (strcmp(option->key, "scale") || zoom->scale == option->value.num ||
        option->value.num > MAX_SCALE) {
        kywc_log(KYWC_WARN, "invalid value");
        return false;
    }

    zoom->scale = option->value.num;

    ky_scene_damage_whole(zoom->scene);

    return true;
}

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
    struct seat_cursor_motion_event *event = data;
    if (event->device && event->device->prop.type != WLR_INPUT_DEVICE_POINTER) {
        return;
    }

    zoom->mouse_lx = event->lx;
    zoom->mouse_ly = event->ly;
    zoom->cursor_moved = true;
}

static void seat_cursor_destroy(struct seat_cursor *cursor)
{
    wl_list_remove(&cursor->seat_destroy.link);
    wl_list_remove(&cursor->cursor_motion.link);
    wl_list_remove(&cursor->link);
    free(cursor);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct seat_cursor *cursor = wl_container_of(listener, cursor, seat_destroy);
    seat_cursor_destroy(cursor);
}

static void seat_cursor_create(struct zoom_effect *effect, struct seat *seat)
{
    struct seat_cursor *seat_cursor = calloc(1, sizeof(*seat_cursor));
    if (!seat_cursor) {
        return;
    }

    wl_list_insert(&effect->seat_cursors, &seat_cursor->link);

    seat_cursor->seat = seat;
    seat_cursor->seat_destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &seat_cursor->seat_destroy);
    seat_cursor->cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&seat->events.cursor_motion, &seat_cursor->cursor_motion);
}

static void handle_new_seat(struct wl_listener *listener, void *data)
{
    struct seat *seat = data;
    seat_cursor_create(zoom, seat);
}

static bool handle_seat(struct seat *seat, int index, void *data)
{
    seat_cursor_create(zoom, seat);
    return false;
}

static void handle_effect_enable(struct wl_listener *listener, void *data)
{
    input_manager_for_each_seat(handle_seat, zoom);
    seat_add_new_listener(&zoom->new_seat);
    ky_scene_damage_whole(zoom->scene);
}

static void handle_effect_disable(struct wl_listener *listener, void *data)
{
    struct ky_scene_output *scene_output;
    wl_list_for_each(scene_output, &zoom->scene->outputs, link) {
        struct wlr_output *wlr_output = scene_output->output;
        struct wlr_box src_box = { scene_output->x, scene_output->y,
                                   wlr_output->width / wlr_output->scale,
                                   wlr_output->height / wlr_output->scale };
        ky_scene_output_set_viewport_source_box(scene_output, &src_box);
        scene_output->viewport.has_src = false;
    }

    wl_list_remove(&zoom->new_seat.link);
    wl_list_init(&zoom->new_seat.link);
    pixman_region32_clear(&zoom->viewport_region);

    struct seat_cursor *cursor, *tmp;
    wl_list_for_each_safe(cursor, tmp, &zoom->seat_cursors, link) {
        seat_cursor_destroy(cursor);
    }

    ky_scene_damage_whole(zoom->scene);
}

static void handle_effect_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&zoom->destroy.link);
    wl_list_remove(&zoom->enable.link);
    wl_list_remove(&zoom->disable.link);
    pixman_region32_fini(&zoom->viewport_region);
    free(zoom);
    zoom = NULL;
}

static void process_cursor_outside_viewport_region(struct zoom_effect *effect,
                                                   struct ky_scene_render_target *target)
{
    struct seat_cursor *seat_cursor;
    struct cursor *cursor;
    wl_list_for_each(seat_cursor, &effect->seat_cursors, link) {
        cursor = seat_cursor->seat->cursor;
        if (!pixman_region32_contains_point(&effect->viewport_region, cursor->lx, cursor->ly,
                                            NULL)) {
            double lx = target->logical.x + target->logical.width * 0.5;
            double ly = target->logical.y + target->logical.height * 0.5;
            cursor_move(cursor, NULL, lx, ly, false, false);
        }
    }
}

static void calc_closest_point(struct zoom_effect *effect, double *dest_x, double *dest_y)
{
    double distance_x, distance_y, distance;
    double min_x = effect->mouse_lx, min_y = effect->mouse_ly, min_distance = DBL_MAX;
    int rects_len;
    pixman_box32_t *rects = pixman_region32_rectangles(&effect->viewport_region, &rects_len);
    for (int i = 0; i < rects_len; i++) {
        struct wlr_box box;
        pixman_box32_t *rect = &rects[i];
        box.x = rect->x1;
        box.y = rect->y1;
        box.width = rect->x2 - rect->x1;
        box.height = rect->y2 - rect->y1;
        wlr_box_closest_point(&box, effect->mouse_lx, effect->mouse_ly, &distance_x, &distance_y);

        // calculate squared distance suitable for comparison
        distance = (effect->mouse_lx - distance_x) * (effect->mouse_lx - distance_x) +
                   (effect->mouse_ly - distance_y) * (effect->mouse_ly - distance_y);

        if (!isfinite(distance)) {
            distance = DBL_MAX;
        }

        if (distance < min_distance) {
            min_x = distance_x;
            min_y = distance_y;
            min_distance = distance;
        }
    }

    *dest_x = min_x;
    *dest_y = min_y;
}

static void calc_viewport_region(struct zoom_effect *effect)
{
    pixman_region32_clear(&effect->viewport_region);

    struct kywc_box logic_box = { 0 };
    int trans_width, trans_height = 0;
    struct ky_scene_output *scene_output;
    wl_list_for_each(scene_output, &effect->scene->outputs, link) {
        wlr_output_transformed_resolution(scene_output->output, &trans_width, &trans_height);
        float scale = effect->scale * scene_output->output->scale;
        logic_box.x = scene_output->x / effect->scale + effect->offset_x;
        logic_box.y = scene_output->y / effect->scale + effect->offset_y;
        logic_box.width = trans_width / scale;
        logic_box.height = trans_height / scale;

        pixman_region32_union_rect(&effect->viewport_region, &effect->viewport_region, logic_box.x,
                                   logic_box.y, logic_box.width, logic_box.height);
    }
}

static bool handle_frame_render_pre(struct effect_entity *entity,
                                    struct ky_scene_render_target *target)
{
    struct zoom_effect *effect = entity->user_data;

    bool need_check = !pixman_region32_not_empty(&effect->viewport_region);
    /* check if the mouse is in the area */
    if (effect->cursor_moved || need_check) {
        if (!pixman_region32_contains_point(&effect->viewport_region, effect->mouse_lx,
                                            effect->mouse_ly, NULL)) {
            double closest_x = 0, closest_y = 0;
            calc_closest_point(effect, &closest_x, &closest_y);
            int distance_x = effect->mouse_lx - closest_x;
            int distance_y = effect->mouse_ly - closest_y;

            effect->offset_x += distance_x;
            effect->offset_y += distance_y;
            calc_viewport_region(effect);
        }
        ky_scene_damage_whole(zoom->scene);
        effect->cursor_moved = false;
    }

    /* cacl target logical */
    float output_scale = target->output->output->scale;
    target->scale = effect->scale * output_scale;
    target->logical.width = target->logical.width / effect->scale;
    target->logical.height = target->logical.height / effect->scale;
    target->logical.x = target->output->x / effect->scale + effect->offset_x;
    target->logical.y = target->output->y / effect->scale + effect->offset_y;

    ky_scene_output_set_viewport_source_box(target->output, &target->logical);

    /* force rendering cursor */
    target->options |= KY_SCENE_RENDER_ENABLE_CURSORS;

    if (need_check) {
        process_cursor_outside_viewport_region(effect, target);
    }

    return true;
}

static const struct effect_interface zoom_effect_impl = {
    .frame_render_pre = handle_frame_render_pre,
    .configure = handle_effect_configure,
};

bool zoom_effect_create(struct effect_manager *effect_manager)
{
    struct ky_scene_output *scene_output;
    wl_list_for_each(scene_output, &effect_manager->server->scene->outputs, link) {
        if (scene_output->output->hardware_cursor) {
            return false;
        }
    }

    zoom = calloc(1, sizeof(*zoom));
    if (!zoom) {
        return false;
    }

    zoom->effect = effect_create("zoom", 122, false, &zoom_effect_impl, NULL);
    if (!zoom->effect) {
        free(zoom);
        zoom = NULL;
        return false;
    }

    struct effect_entity *entity = ky_scene_add_effect(effect_manager->server->scene, zoom->effect);
    if (!entity) {
        effect_destroy(zoom->effect);
        free(zoom);
        zoom = NULL;
        return false;
    }

    entity->user_data = zoom;
    zoom->scene = effect_manager->server->scene;

    zoom->scale = effect_get_option_int(zoom->effect, "scale", 2);
    pixman_region32_init(&zoom->viewport_region);

    zoom->enable.notify = handle_effect_enable;
    wl_signal_add(&zoom->effect->events.enable, &zoom->enable);
    zoom->disable.notify = handle_effect_disable;
    wl_signal_add(&zoom->effect->events.disable, &zoom->disable);
    zoom->destroy.notify = handle_effect_destroy;
    wl_signal_add(&zoom->effect->events.destroy, &zoom->destroy);
    zoom->new_seat.notify = handle_new_seat;

    wl_list_init(&zoom->new_seat.link);
    wl_list_init(&zoom->seat_cursors);

    if (zoom->effect->enabled) {
        handle_effect_enable(&zoom->enable, NULL);
    }

    return true;
}
