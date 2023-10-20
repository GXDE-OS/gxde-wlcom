// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>

#include <kywc/log.h>

#include "input/seat.h"
#include "input_p.h"
#include "scene/surface.h"
#include "server.h"
#include "view/view.h"
#include "xwayland.h"

#define TOUCH_HOLD_TIMEOUT (100)
#define TOUCH_FILTER_TIMEOUT (200)

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

struct touch_manager {
    struct wl_listener new_input;
    struct wl_listener server_destroy;
};

struct touch {
    struct wlr_touch *wlr_touch;
    struct input *input;
    struct wl_listener input_destroy;

    struct wl_list points;
    uint32_t points_count;

    /* timer for gesture detect filter */
    struct wl_event_source *filter;
    bool filter_enabled;

    /* current gesture state per touch */
    struct gesture_state gestures;
    uint32_t hold_points;

    /* pinch angle */
    double angle;
};

struct touch_point {
    struct touch *touch;
    struct wl_list link;
    struct wlr_surface *surface;
    struct wl_listener surface_destroy;

    /* timer for hold gesture */
    struct wl_event_source *timer;
    bool hold, moved;

    double abs_x, abs_y;
    double last_x, last_y;
    double dx, dy;
    int32_t touch_id;
    uint32_t directions;
};

static struct touch *touch_from_wlr_touch(struct wlr_touch *wlr_touch)
{
    return wlr_touch->data;
}

static void touch_handle_input_destroy(struct wl_listener *listener, void *data)
{
    struct touch *touch = wl_container_of(listener, touch, input_destroy);
    wl_list_remove(&touch->input_destroy.link);
    gesture_state_finish(&touch->gestures);
    wl_event_source_remove(touch->filter);

    struct touch_point *point, *tmp;
    wl_list_for_each_safe(point, tmp, &touch->points, link) {
        wl_list_remove(&point->link);
        if (point->surface) {
            wl_list_remove(&point->surface_destroy.link);
        }
        if (point->timer) {
            wl_event_source_remove(point->timer);
        }
        free(point);
    }

    free(touch);
}

static enum gesture_edge touch_calc_edge(struct touch *touch)
{
    double abs_avg_x = 0.0;
    double abs_avg_y = 0.0;

    struct touch_point *point;
    wl_list_for_each(point, &touch->points, link) {
        /* meet the first free point */
        if (point->touch_id < 0) {
            break;
        }
        abs_avg_x += point->abs_x;
        abs_avg_y += point->abs_y;
    }

    abs_avg_x /= touch->points_count;
    abs_avg_y /= touch->points_count;
    // TODO: edge normalize by physical size of touch

    enum gesture_edge edge = GESTURE_EDGE_NONE;
    if (abs_avg_y <= 0.05) {
        edge = GESTURE_EDGE_TOP;
    }

    if (abs_avg_x <= 0.05) {
        edge = GESTURE_EDGE_LEFT;
    }

    if (abs_avg_x >= 0.95) {
        edge = GESTURE_EDGE_RIGHT;
    }

    if (abs_avg_y >= 0.95) {
        edge = GESTURE_EDGE_BOTTOM;
    }

    return edge;
}

static void touch_gesture_begin(struct touch *touch, enum gesture_type gesture, uint8_t fingers)
{
    struct gesture_state *state = &touch->gestures;
    if (state->type == gesture && state->fingers == fingers) {
        return;
    }
    /* cancel current gesture and enter the new hold */
    if (state->type != GESTURE_TYPE_NONE) {
        gesture_state_end(state, state->type, state->device, true);
    }
    if (gesture == GESTURE_TYPE_NONE) {
        return;
    }

    enum gesture_edge edge = touch_calc_edge(touch);
    gesture_state_begin(state, gesture, GESTURE_DEVICE_TOUCHSCREEN, edge, fingers);
}

static void touch_gesture_detect(struct touch *touch)
{
    if (touch->points_count == 0) {
        return;
    }

    bool all_points_moved = true;
    struct touch_point *point;
    wl_list_for_each(point, &touch->points, link) {
        /* meet the first free point */
        if (point->touch_id < 0) {
            break;
        }
        if (!point->moved) {
            all_points_moved = false;
            break;
        }
    }

    if (!all_points_moved) {
        /* hold geture if all hold points not moved */
        if (touch->hold_points) {
            touch_gesture_begin(touch, GESTURE_TYPE_HOLD, touch->hold_points);
        }
        return;
    }

    /* moved when one touch point swipe from edge */
    if (touch->points_count == 1) {
        touch_gesture_begin(touch, GESTURE_TYPE_SWIPE, 1);
        return;
    }

    /* swipre or pinch check */
    wl_list_for_each(point, &touch->points, link) {
        /* meet the first free point */
        if (point->touch_id < 0) {
            break;
        }
        if (!point->moved) {
            all_points_moved = false;
            break;
        }
    }

    bool one_direction = true;
    uint32_t directions = GESTURE_DIRECTION_NONE;
    wl_list_for_each(point, &touch->points, link) {
        /* meet the first free point */
        if (point->touch_id < 0) {
            break;
        }
        if (directions == GESTURE_DIRECTION_NONE) {
            directions = point->directions;
        } else if (directions != point->directions) {
            one_direction = false;
            break;
        }
    }

    touch_gesture_begin(touch, one_direction ? GESTURE_TYPE_SWIPE : GESTURE_TYPE_PINCH,
                        touch->points_count);
}

static void touch_filter_enable(struct touch *touch, bool enabled)
{
    wl_event_source_timer_update(touch->filter, enabled ? TOUCH_FILTER_TIMEOUT : 0);
    touch->filter_enabled = enabled;
}

static int touch_handle_timer(void *data)
{
    struct touch *touch = data;

    touch->filter_enabled = false;
    touch_gesture_detect(touch);
    return 0;
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
    struct touch_manager *manager = wl_container_of(listener, manager, new_input);
    struct input *input = data;

    /* input has been configured, only care about touch */
    if (input->prop.type != WLR_INPUT_DEVICE_TOUCH) {
        return;
    }

    struct touch *touch = calloc(1, sizeof(struct touch));
    if (!touch) {
        return;
    }

    struct wl_display *display = input->seat->wlr_seat->display;
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    touch->filter = wl_event_loop_add_timer(loop, touch_handle_timer, touch);
    if (!touch->filter) {
        free(touch);
        return;
    }

    touch->input = input;
    touch->input_destroy.notify = touch_handle_input_destroy;
    wl_signal_add(&input->events.destroy, &touch->input_destroy);

    touch->wlr_touch = wlr_touch_from_input_device(input->wlr_input);
    touch->wlr_touch->data = touch;
    wl_list_init(&touch->points);

    gesture_state_init(&touch->gestures, display);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct touch_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

bool touch_manager_create(struct input_manager *input_manager)
{
    struct touch_manager *manager = calloc(1, sizeof(struct touch_manager));
    if (!manager) {
        return false;
    }

    manager->new_input.notify = handle_new_input;
    input_add_new_listener(&manager->new_input);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(input_manager->server, &manager->server_destroy);

    return true;
}

static struct wlr_surface *touch_get_surface(struct touch *touch, double *sx, double *sy,
                                             struct wlr_surface **toplevel)
{
    struct seat *seat = touch->input->seat;
    struct cursor *cursor = seat->cursor;

    struct ky_scene_node *node =
        ky_scene_node_at(ky_scene_node_from_scene(seat->scene), cursor->lx, cursor->ly, sx, sy);
    if (!node) {
        return NULL;
    }

    if (toplevel) {
        *toplevel = input_event_node_toplevel(input_event_node_from_node(node));
    }
    return wlr_surface_try_from_node(node);
}

static int touch_point_handle_timer(void *data)
{
    struct touch_point *point = data;
    point->hold = true;
    point->touch->hold_points++;

    if (point->touch->filter_enabled) {
        return 0;
    }

    if (point->touch->gestures.type == GESTURE_TYPE_NONE) {
        touch_filter_enable(point->touch, true);
        return 0;
    }

    /* keep hold gesture */
    if (point->touch->gestures.type == GESTURE_TYPE_HOLD) {
        touch_gesture_begin(point->touch, GESTURE_TYPE_HOLD, point->touch->hold_points);
    }

    return 0;
}

static void touch_point_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct touch_point *point = wl_container_of(listener, point, surface_destroy);
    wl_list_remove(&point->surface_destroy.link);
    point->surface = NULL;
}

static struct touch_point *touch_point_create(struct touch *touch, int32_t touch_id)
{
    struct touch_point *point, *free_point = NULL;
    wl_list_for_each(point, &touch->points, link) {
        if (point->touch_id == touch_id) {
            return point;
        }
        if (!free_point && point->touch_id < 0) {
            free_point = point;
        }
    }

    /* not found, reuse the first free one */
    if (free_point) {
        free_point->touch_id = touch_id;
        return free_point;
    }

    /* alloc one if all in used */
    point = calloc(1, sizeof(struct touch_point));
    if (!point) {
        return NULL;
    }

    point->touch_id = touch_id;
    point->touch = touch;
    point->surface_destroy.notify = touch_point_handle_surface_destroy;
    wl_list_insert(touch->points.prev, &point->link);

    struct wl_display *display = touch->input->seat->wlr_seat->display;
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    point->timer = wl_event_loop_add_timer(loop, touch_point_handle_timer, point);

    return point;
}

static struct touch_point *touch_point_from_id(struct touch *touch, int32_t touch_id)
{
    struct touch_point *point;
    wl_list_for_each(point, &touch->points, link) {
        if (point->touch_id == touch_id) {
            return point;
        }
    }
    return NULL;
}

static void touch_cancel_points(struct touch *touch)
{
    struct seat *seat = touch->input->seat;

    struct touch_point *point;
    wl_list_for_each(point, &touch->points, link) {
        if (point->touch_id < 0 || !point->surface) {
            continue;
        }
        point->abs_x = point->last_x;
        point->abs_y = point->last_y;
        wlr_seat_touch_notify_cancel(seat->wlr_seat, point->surface);
    }
}

static void touch_point_reset(struct touch_point *point, bool cancelled)
{
    struct gesture_state *state = &point->touch->gestures;
    if (state->type != GESTURE_TYPE_NONE &&
        gesture_state_end(state, state->type, state->device, cancelled)) {
        touch_cancel_points(point->touch);
    }

    point->touch_id = -1;
    point->touch->points_count--;

    if (point->surface) {
        point->surface = NULL;
        wl_list_remove(&point->surface_destroy.link);
    }

    if (point->timer) {
        wl_event_source_timer_update(point->timer, 0);
    }

    if (point->hold) {
        point->touch->hold_points--;
        point->hold = false;
    }

    /* reinsert to tail */
    wl_list_remove(&point->link);
    wl_list_insert(point->touch->points.prev, &point->link);
}

bool touch_handle_down(struct wlr_touch_down_event *event)
{
    struct touch *touch = touch_from_wlr_touch(event->touch);
    if (!touch) {
        return false;
    }

    struct seat *seat = touch->input->seat;
    if (seat->touch_grab && seat->touch_grab->interface->touch &&
        seat->touch_grab->interface->touch(seat->touch_grab, event->time_msec, true)) {
        return true;
    }

    struct touch_point *point = touch_point_create(touch, event->touch_id);
    if (point->timer) {
        wl_event_source_timer_update(point->timer, TOUCH_HOLD_TIMEOUT);
    }
    touch->points_count++;
    point->abs_x = point->last_x = event->x;
    point->abs_y = point->last_y = event->y;
    point->moved = false;
    touch->angle = 0.0;

    struct wlr_surface *toplevel = NULL;
    double sx, sy;
    struct wlr_surface *surface = touch_get_surface(touch, &sx, &sy, &toplevel);
    if (!surface || !wlr_surface_accepts_touch(seat->wlr_seat, surface)) {
        return false;
    }

    point->surface = surface;
    wl_signal_add(&surface->events.destroy, &point->surface_destroy);

    if (xwayland_check_client(wl_resource_get_client(point->surface->resource))) {
        sx = xwayland_scale(sx);
        sy = xwayland_scale(sy);
    }

    wlr_seat_touch_notify_down(seat->wlr_seat, surface, event->time_msec, event->touch_id, sx, sy);

    /* activate and focus the toplevel surface */
    if (toplevel) {
        seat_focus_surface(seat, toplevel);
        struct view *view = view_try_from_wlr_surface(toplevel);
        if (view) {
            kywc_view_activate(&view->base);
        }
    }

    return true;
}

static uint32_t touch_point_calc_directions(struct touch_point *point, double dx, double dy)
{
    uint32_t directions = GESTURE_DIRECTION_NONE;

    if (fabs(dx) > fabs(dy)) {
        if (dx > 0) {
            directions |= GESTURE_DIRECTION_RIGHT;
        } else {
            directions |= GESTURE_DIRECTION_LEFT;
        }
    } else {
        if (dy > 0) {
            directions |= GESTURE_DIRECTION_DOWN;
        } else {
            directions |= GESTURE_DIRECTION_UP;
        }
    }

    return directions;
}

static void touch_calc_average_delta(struct touch *touch, double *dx, double *dy)
{
    double total_dx = 0, total_dy = 0;

    struct touch_point *point;
    wl_list_for_each(point, &touch->points, link) {
        /* meet the first free point */
        if (point->touch_id < 0) {
            break;
        }
        total_dx += point->dx;
        total_dy += point->dy;
    }

    *dx = total_dx / touch->points_count;
    *dy = total_dy / touch->points_count;
}

static double touch_calc_average_scale(struct touch *touch)
{
    double start_min = 1.0, current_min = 1.0;
    double start_max = 0.0, current_max = 0.0;

    struct touch_point *point;
    wl_list_for_each(point, &touch->points, link) {
        /* meet the first free point */
        if (point->touch_id < 0) {
            break;
        }
        if (start_min == 1.0 && start_max == 0.0) {
            start_min = start_max = point->abs_x;
            current_min = current_max = point->last_x;
        } else {
            start_min = MIN(start_min, point->abs_x);
            start_max = MAX(start_max, point->abs_x);
            current_min = MIN(current_min, point->last_x);
            current_max = MAX(current_max, point->last_x);
        }
    }
    double start_width = start_max - start_min;
    double current_width = current_max - current_min;
    if (start_width == 0.0) {
        kywc_log(KYWC_WARN, "start(max=%f, min=%f), current(max=%f, min=%f)", start_max, start_min,
                 current_max, current_min);
        return 1.0;
    }

    return current_width / start_width;
}

static double touch_calc_average_angle_delta(struct touch *touch)
{
    double dx = 0.0;
    double dy = 0.0;

    struct touch_point *point;
    wl_list_for_each(point, &touch->points, link) {
        /* meet the first free point */
        if (point->touch_id < 0) {
            break;
        }
        dx = point->last_x - dx;
        dy = point->last_y - dy;
    }

    double tangle = atan2(dy, dx) * 180.0 / 3.14;
    if (touch->angle == 0.0) {
        touch->angle = tangle;
    }

    double angle_delta = tangle - touch->angle;
    if (angle_delta > 180.0) {
        angle_delta -= 360.0;
    } else if (angle_delta < -180.0) {
        angle_delta += 360.0;
    }

    touch->angle = tangle;

    return angle_delta;
}

void touch_handle_motion(struct wlr_touch_motion_event *event, bool handle)
{
    struct touch *touch = touch_from_wlr_touch(event->touch);
    if (!touch) {
        return;
    }

    struct seat *seat = touch->input->seat;
    if (seat->touch_grab && seat->touch_grab->interface->motion &&
        seat->touch_grab->interface->motion(seat->touch_grab, event->time_msec, seat->cursor->lx,
                                            seat->cursor->ly)) {
        return;
    }

    struct touch_point *point = touch_point_from_id(touch, event->touch_id);
    if (!point) {
        return;
    }

    point->dx = event->x - point->last_x;
    point->dy = event->y - point->last_y;
    point->last_x = event->x;
    point->last_y = event->y;

    double dx = event->x - point->abs_x;
    double dy = event->y - point->abs_y;
    point->moved = fabs(dx) > 0.01f || fabs(dy) > 0.01f;
    /* calc point motion directions */
    point->directions = touch_point_calc_directions(point, dx, dy);

    if (!touch->filter_enabled) {
        touch_gesture_detect(touch);
        if (touch->gestures.type == GESTURE_TYPE_SWIPE) {
            double avg_dx = 0, avg_dy = 0;
            touch_calc_average_delta(touch, &avg_dx, &avg_dy);
            gesture_state_update(&touch->gestures, GESTURE_TYPE_SWIPE, GESTURE_DEVICE_TOUCHSCREEN,
                                 avg_dx, avg_dy, NAN, NAN);
        } else if (touch->gestures.type == GESTURE_TYPE_PINCH) {
            double avg_dx = 0, avg_dy = 0;
            touch_calc_average_delta(touch, &avg_dx, &avg_dy);
            double scale = touch_calc_average_scale(touch);
            double angle_delta = touch_calc_average_angle_delta(touch);
            gesture_state_update(&touch->gestures, GESTURE_TYPE_PINCH, GESTURE_DEVICE_TOUCHSCREEN,
                                 avg_dx, avg_dy, scale, angle_delta);
        }
    }

    if (!handle || !point->surface) {
        return;
    }

    double sx, sy;
    struct wlr_surface *surface = touch_get_surface(touch, &sx, &sy, NULL);

    /* if motion out of point->surface */
    if (surface != point->surface) {
        int lx, ly;
        struct ky_scene_buffer *scene_buffer = ky_scene_buffer_try_from_surface(point->surface);
        ky_scene_node_coords(ky_scene_node_from_buffer(scene_buffer), &lx, &ly);
        sx = seat->cursor->lx - lx;
        sy = seat->cursor->ly - ly;
    }

    if (xwayland_check_client(wl_resource_get_client(point->surface->resource))) {
        sx = xwayland_scale(sx);
        sy = xwayland_scale(sy);
    }

    selection_handle_cursor_move(seat, seat->cursor->lx, seat->cursor->ly);
    wlr_seat_touch_notify_motion(seat->wlr_seat, event->time_msec, event->touch_id, sx, sy);
}

void touch_handle_up(struct wlr_touch_up_event *event, bool handle)
{
    struct touch *touch = touch_from_wlr_touch(event->touch);
    if (!touch) {
        return;
    }

    struct touch_point *point = touch_point_from_id(touch, event->touch_id);
    if (point) {
        touch_point_reset(point, false);
    }

    struct seat *seat = touch->input->seat;
    if (seat->touch_grab && seat->touch_grab->interface->touch &&
        seat->touch_grab->interface->touch(seat->touch_grab, event->time_msec, false)) {
        return;
    }

    if (!point) {
        return;
    }

    touch_filter_enable(touch, true);

    if (handle) {
        wlr_seat_touch_notify_up(seat->wlr_seat, event->time_msec, event->touch_id);
    }
}

void touch_handle_cancel(struct wlr_touch_cancel_event *event, bool handle)
{
    struct touch *touch = touch_from_wlr_touch(event->touch);
    if (!touch) {
        return;
    }

    struct touch_point *point = touch_point_from_id(touch, event->touch_id);
    if (point) {
        touch_point_reset(point, true);
    }

    struct seat *seat = touch->input->seat;
    if (seat->touch_grab && seat->touch_grab->interface->touch &&
        seat->touch_grab->interface->touch(seat->touch_grab, event->time_msec, false)) {
        return;
    }

    if (point && handle && point->surface) {
        struct seat *seat = touch->input->seat;
        wlr_seat_touch_notify_cancel(seat->wlr_seat, point->surface);
    }
}
