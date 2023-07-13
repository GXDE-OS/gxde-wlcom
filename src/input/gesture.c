#include <assert.h>

#include <wlr/types/wlr_seat.h>

#include <kywc/log.h>

#include "input/gesture.h"
#include "input_p.h"

static char *gestures[] = { "none", "pinch", "swipe", "hold" };

static void gesture_state_reset(struct gesture_state *state)
{
    state->type = GESTURE_TYPE_NONE;
    state->device = GESTURE_DEVICE_NONE;
    state->directions = GESTURE_DIRECTION_NONE;
    if (state->timer) {
        wl_event_source_timer_update(state->timer, 0);
    }
}

static int gesture_handle_timer(void *data)
{
    struct gesture_state *state = data;
    if (state->type != GESTURE_TYPE_HOLD) {
        return 0;
    }

    kywc_log(KYWC_DEBUG, "hold gesture triggered");
    bindings_handle_gesture_binding(state);
    gesture_state_reset(state);
    return 0;
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
                         enum gesture_device device, uint8_t fingers)
{
    state->type = type;
    state->device = device;
    state->fingers = fingers;

    state->dx = 0.0;
    state->dy = 0.0;
    state->scale = 1.0;
    state->rotation = 0.0;

    if (state->timer && type == GESTURE_TYPE_HOLD) {
        wl_event_source_timer_update(state->timer, 800);
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

    state->dx += dx;
    state->dy += dy;

    if (state->type == GESTURE_TYPE_PINCH) {
        state->scale = scale;
        state->rotation += rotation;
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
        gesture_state_reset(state);
        return false;
    }

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

    kywc_log(KYWC_DEBUG, "gesture %s state end: fingers: %u, directions: %d", gestures[state->type],
             state->fingers, state->directions);

    bool handled = bindings_handle_gesture_binding(state);
    gesture_state_reset(state);
    return handled;
}
