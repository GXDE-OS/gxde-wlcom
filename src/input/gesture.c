// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>

#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "input/gesture.h"
#include "input_p.h"

#define GESTURE_DEFAULT_TIMEOUT (200)
#define GESTURE_TOUCHPAD_TIMEOUT (20)
#define GESTURE_TOUCHSCREEN_HOLD_TIMEOUT (800)

/**
 * touchpad:
 * Relative motion deltas are normalized to represent those of a device with 1000dpi resolution
 * https://wayland.freedesktop.org/libinput/doc/latest/api/group__event__gesture.html#ga3888052854155ad133fa837e4f28d771
 * we set 0.5 inch be triggered by default
 */
#define GESTURE_TOUCHPAD_TRIGGER_THRESHOLD (500)
/* touchscreen: chase kwin, full width height is 1 */
#define GESTURE_TOUCHSCREEN_TRIGGER_THRESHOLD (0.15)

static char *gestures[] = { "none", "pinch", "swipe", "hold" };

static void gesture_state_reset(struct gesture_state *state)
{
    state->type = GESTURE_TYPE_NONE;
    state->phase = GESTURE_PHASE_NONE;
    state->device = GESTURE_DEVICE_NONE;
    state->directions = GESTURE_DIRECTION_NONE;
    state->edge = GESTURE_EDGE_NONE;
    state->triggered = false;
    state->handled = false;
    if (state->timer) {
        wl_event_source_timer_update(state->timer, 0);
    }
}

static void gesture_state_trigger(struct gesture_state *state)
{
    // Ignore gesture under some threshold
    const double min_rotation = 5;
    const double min_scale_delta = 0.1;

    // Determine direction
    switch (state->type) {
    // Gestures with scale and rotation
    case GESTURE_TYPE_PINCH:
        if (state->rotation > min_rotation) {
            state->directions |= GESTURE_DIRECTION_CLOCKWISE;
        }
        if (state->rotation < -min_rotation) {
            state->directions |= GESTURE_DIRECTION_COUNTERCLOCKWISE;
        }

        if (state->scale > (1.0 + min_scale_delta)) {
            state->directions |= GESTURE_DIRECTION_OUTWARD;
        }
        if (state->scale < (1.0 - min_scale_delta)) {
            state->directions |= GESTURE_DIRECTION_INWARD;
        }
        // fallthrough
    // Gestures with dx and dy
    case GESTURE_TYPE_SWIPE:
        if (fabs(state->dx) > fabs(state->dy)) {
            if (state->dx > 0) {
                state->directions |= GESTURE_DIRECTION_RIGHT;
            } else {
                state->directions |= GESTURE_DIRECTION_LEFT;
            }
        } else {
            if (state->dy > 0) {
                state->directions |= GESTURE_DIRECTION_DOWN;
            } else {
                state->directions |= GESTURE_DIRECTION_UP;
            }
        }
    // Gesture without any direction
    case GESTURE_TYPE_HOLD:
        break;
    // Not tracking any gesture
    case GESTURE_TYPE_NONE:
        assert("Not tracking any gesture.");
        break;
    }

    kywc_log(KYWC_DEBUG, "gesture %s triggered: fingers: %u, directions: %d", gestures[state->type],
             state->fingers, state->directions);

    state->triggered = true;
    state->phase = GESTURE_PHASE_TRIGGER;
    state->handled = bindings_handle_gesture_binding(state);
}

static void gesture_state_follow(struct gesture_state *state, double dx, double dy,
                                 bool before_triggered)
{
    // only support swipe for now
    if (state->type != GESTURE_TYPE_SWIPE) {
        return;
    }

    state->phase = before_triggered ? GESTURE_PHASE_BEFORE : GESTURE_PHASE_AFTER;
    state->handled = bindings_handle_gesture_binding(state);
}

static void gesture_state_stop(struct gesture_state *state)
{
    // only support swipe for now
    if (state->type != GESTURE_TYPE_SWIPE) {
        return;
    }

    state->phase = GESTURE_PHASE_STOP;
    state->handled = bindings_handle_gesture_binding(state);
}

static int gesture_handle_timer(void *data)
{
    struct gesture_state *state = data;
    gesture_state_trigger(state);
    return 0;
}

static void gesture_swipe_state_update(struct gesture_state *state, double dx, double dy)
{
    state->dx += dx;
    state->dy += dy;

    if (!state->triggered) {
        if ((state->device == GESTURE_DEVICE_TOUCHSCREEN &&
             (state->dx < -GESTURE_TOUCHSCREEN_TRIGGER_THRESHOLD ||
              state->dx > GESTURE_TOUCHSCREEN_TRIGGER_THRESHOLD ||
              state->dy < -GESTURE_TOUCHSCREEN_TRIGGER_THRESHOLD ||
              state->dy > GESTURE_TOUCHSCREEN_TRIGGER_THRESHOLD)) ||
            (state->device == GESTURE_DEVICE_TOUCHPAD &&
             (state->dx < -GESTURE_TOUCHPAD_TRIGGER_THRESHOLD ||
              state->dx > GESTURE_TOUCHPAD_TRIGGER_THRESHOLD ||
              state->dy < -GESTURE_TOUCHPAD_TRIGGER_THRESHOLD ||
              state->dy > GESTURE_TOUCHPAD_TRIGGER_THRESHOLD))) {
            gesture_state_trigger(state);
        } else {
            gesture_state_follow(state, dx, dy, true);
        }
    } else {
        gesture_state_follow(state, dx, dy, false);
    }
}

void gesture_state_init(struct gesture_state *state, void *display)
{
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    state->timer = wl_event_loop_add_timer(loop, gesture_handle_timer, state);
    gesture_state_reset(state);
}

void gesture_state_finish(struct gesture_state *state)
{
    if (state->timer) {
        wl_event_source_remove(state->timer);
    }
}

void gesture_state_begin(struct gesture_state *state, enum gesture_type type,
                         enum gesture_device device, enum gesture_edge edge, uint8_t fingers)
{
    state->type = type;
    state->device = device;
    state->fingers = fingers;

    state->dx = 0.0;
    state->dy = 0.0;
    state->scale = 1.0;
    state->rotation = 0.0;
    state->edge = edge;

    if (state->timer && state->type != GESTURE_TYPE_SWIPE) {
        int timeout = GESTURE_DEFAULT_TIMEOUT;
        if (state->device == GESTURE_DEVICE_TOUCHPAD) {
            timeout = GESTURE_TOUCHPAD_TIMEOUT;
        } else if (state->type == GESTURE_TYPE_HOLD &&
                   state->device == GESTURE_DEVICE_TOUCHSCREEN) {
            timeout = GESTURE_TOUCHSCREEN_HOLD_TIMEOUT;
        }
        wl_event_source_timer_update(state->timer, timeout);
    }

    kywc_log(KYWC_DEBUG, "gesture %s state begin: fingers: %u", gestures[type], fingers);
}

void gesture_state_update(struct gesture_state *state, enum gesture_type type,
                          enum gesture_device device, double dx, double dy, double scale,
                          double rotation)
{
    if (state->type != type || state->device != device) {
        return;
    }

    if (state->type == GESTURE_TYPE_HOLD) {
        assert("hold does not update.");
        return;
    }

    if (state->type == GESTURE_TYPE_PINCH) {
        state->dx += dx;
        state->dy += dy;
        state->scale = scale;
        state->rotation += rotation;
    }

    if (state->type == GESTURE_TYPE_SWIPE) {
        gesture_swipe_state_update(state, dx, dy);
    }

    kywc_log(KYWC_DEBUG, "gesture %s state update: fingers: %u gesture: %f %f %f %f",
             gestures[state->type], state->fingers, state->dx, state->dy, state->scale,
             state->rotation);
}

bool gesture_state_end(struct gesture_state *state, enum gesture_type type,
                       enum gesture_device device, bool cancelled)
{
    if (state->type != type || state->device != device) {
        return false;
    }

    if (cancelled) {
        kywc_log(KYWC_DEBUG, "gesture %s state cancelled", gestures[state->type]);
        gesture_state_stop(state);
        gesture_state_reset(state);
        return false;
    }

    if (!state->triggered && state->type != GESTURE_TYPE_HOLD) {
        gesture_state_trigger(state);
    }

    bool handled = state->handled;
    gesture_state_stop(state);
    gesture_state_reset(state);
    return handled;
}
