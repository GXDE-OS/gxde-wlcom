#include <stdlib.h>

#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <kywc/log.h>

#include "input/cursor.h"
#include "input/seat.h"

/* cursor images used in compositor */
static char *cursor_image[] = {
    "",
    "left_ptr",
    "grabbing",
    "top_left_corner",
    "top_side",
    "top_right_corner",
    "right_side",
    "bottom_right_corner",
    "bottom_side",
    "bottom_left_corner",
    "left_side",
};

static char *cursor_button[] = {
    "left", "right", "middle", "side", "extra", "forward", "back", "task",
};

static void cursor_feed_motion(struct cursor *cursor, double lx, double ly, uint32_t time)
{
    cursor->lx = lx;
    cursor->ly = ly;

    kywc_log(KYWC_DEBUG, "cursor move to (%f, %f)", cursor->lx, cursor->ly);
}

static void cursor_feed_button(struct cursor *cursor, uint32_t button, bool pressed, uint32_t time)
{
    kywc_log(KYWC_DEBUG, "cursor %s button %s", cursor_button[button - 0x110],
             pressed ? "pressed" : "released");
}

static void cursor_handle_motion(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, motion);
    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;
    struct wlr_pointer_motion_event *event = data;

    wlr_cursor_move(wlr_cursor, &event->pointer->base, event->delta_x, event->delta_y);
    cursor_feed_motion(cursor, wlr_cursor->x, wlr_cursor->y, event->time_msec);
}

static void cursor_handle_motion_absolute(struct wl_listener *listener, void *data)
{

    struct cursor *cursor = wl_container_of(listener, cursor, motion_absolute);
    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;
    struct wlr_pointer_motion_absolute_event *event = data;

    wlr_cursor_warp_absolute(wlr_cursor, &event->pointer->base, event->x, event->y);
    cursor_feed_motion(cursor, wlr_cursor->x, wlr_cursor->y, event->time_msec);
}

static void cursor_handle_button(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, button);
    struct wlr_pointer_button_event *event = data;

    cursor_feed_button(cursor, event->button, event->state == WLR_BUTTON_PRESSED, event->time_msec);
}

static void cursor_handle_axis(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, axis);
    struct wlr_pointer_axis_event *event = data;

    /* Notify the client with pointer focus of the axis event. */
    struct wlr_seat *wlr_seat = cursor->seat->wlr_seat;
    wlr_seat_pointer_notify_axis(wlr_seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source);
}

static void cursor_handle_frame(struct wl_listener *listener, void *data)
{
    struct cursor *cursor = wl_container_of(listener, cursor, frame);

    /* Notify the client with pointer focus of the frame event. */
    struct wlr_seat *wlr_seat = cursor->seat->wlr_seat;
    wlr_seat_pointer_notify_frame(wlr_seat);
}

#define CURSOR_ADD_SIGNAL(signal)                                                                  \
    cursor->signal.notify = cursor_handle_##signal;                                                \
    wl_signal_add(&wlr_cursor->events.signal, &cursor->signal);

struct cursor *cursor_create(struct seat *seat)
{
    struct cursor *cursor = calloc(1, sizeof(struct cursor));
    if (!cursor) {
        return false;
    }

    struct wlr_cursor *wlr_cursor = wlr_cursor_create();
    if (!wlr_cursor) {
        free(cursor);
        return false;
    }

    cursor->seat = seat;
    seat->cursor = cursor;
    cursor->wlr_cursor = wlr_cursor;

    // TODO: multi-layout for multi-seat
    wlr_cursor_attach_output_layout(wlr_cursor, seat->layout);

    const char *xcursor_theme = getenv("XCURSOR_THEME");
    const char *xcursor_size = getenv("XCURSOR_SIZE");
    uint32_t size = xcursor_size ? atoi(xcursor_size) : 24;

    /* xcursor manager per seat for cursor theme */
    cursor->xcursor_manager = wlr_xcursor_manager_create(xcursor_theme, size);
    wlr_xcursor_manager_load(cursor->xcursor_manager, 1.0);

    CURSOR_ADD_SIGNAL(motion);
    CURSOR_ADD_SIGNAL(motion_absolute);
    CURSOR_ADD_SIGNAL(button);
    CURSOR_ADD_SIGNAL(axis);
    CURSOR_ADD_SIGNAL(frame);

    return cursor;
}

#undef CURSOR_ADD_SIGNAL

void cursor_destroy(struct cursor *cursor)
{
    wl_list_remove(&cursor->motion.link);
    wl_list_remove(&cursor->motion_absolute.link);
    wl_list_remove(&cursor->button.link);
    wl_list_remove(&cursor->axis.link);
    wl_list_remove(&cursor->frame.link);

    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;
    struct wlr_xcursor_manager *xcursor_manager = cursor->xcursor_manager;
    wlr_xcursor_manager_destroy(xcursor_manager);

    wlr_cursor_destroy(wlr_cursor);

    cursor->seat->cursor = NULL;
    free(cursor);
}

void curosr_add_input(struct seat *seat, struct input *input)
{
    struct wlr_cursor *wlr_cursor = seat->cursor->wlr_cursor;
    wlr_cursor_attach_input_device(wlr_cursor, input->wlr_input);
}

void cursor_remove_input(struct input *input)
{
    struct wlr_cursor *wlr_cursor = input->seat->cursor->wlr_cursor;
    wlr_cursor_detach_input_device(wlr_cursor, input->wlr_input);
}

void cursor_set_image(struct cursor *cursor, enum cursor_name name, float scale, bool force)
{
    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;
    struct wlr_xcursor_manager *xcursor_manager = cursor->xcursor_manager;

    if (!force && cursor->name == name && cursor->scale == scale) {
        return;
    }

    if (name == CURSOR_NONE) {
        wlr_cursor_set_surface(wlr_cursor, NULL, 0, 0);
        return;
    }

    wlr_xcursor_manager_load(xcursor_manager, scale);

    const char *image = cursor_image[name];
    wlr_xcursor_manager_set_cursor_image(xcursor_manager, image, wlr_cursor);

    cursor->scale = scale;
    cursor->name = name;
}

void cursor_move(struct cursor *cursor, double x, double y, bool delta)
{
    struct wlr_cursor *wlr_cursor = cursor->wlr_cursor;

    if (delta) {
        wlr_cursor_move(wlr_cursor, NULL, x, y);
    } else {
        wlr_cursor_warp(wlr_cursor, NULL, x, y);
    }
}
