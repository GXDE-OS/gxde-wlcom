#include <stdlib.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "view_p.h"

#define VIEW_MIN_WIDTH 200
#define VIEW_MIN_HEIGHT 100

enum interactive_mode {
    INTERACTIVE_MODE_NONE = 0,
    INTERACTIVE_MODE_MOVE,
    INTERACTIVE_MODE_RESIZE,
};

struct interactive_grab {
    struct seat_pointer_grab pointer_grab;

    struct seat *seat;
    // struct wl_listener *seat_destroy;

    /* view being moved or resized */
    struct view *view;
    struct wl_listener view_unmap;

    /* move or resize */
    enum interactive_mode mode;
    /* moved or resized actually */
    bool ongoing;
    /* cursor position before move or resize */
    double cursor_x, cursor_y;
    /* view position and size before move or resize */
    struct kywc_box geo;
    /* resize edges */
    uint32_t resize_edges;
};

static void interactive_grab_destroy(struct interactive_grab *grab)
{
    wl_list_remove(&grab->view_unmap.link);
    seat_set_pointer_grab(grab->seat, NULL);
    free(grab);
}

static void interactive_done(struct interactive_grab *grab)
{
    cursor_set_image(grab->seat->cursor, CURSOR_DEFAULT);
    seat_set_pointer_grab(grab->seat, NULL);

    interactive_grab_destroy(grab);
}

static void interactive_process_move(struct interactive_grab *grab, double x, double y)
{
    struct kywc_view *kywc_view = &grab->view->base;
    struct kywc_box *geometry = &kywc_view->geometry;
    struct kywc_box *saved = &grab->view->saved.geometry;

    if (kywc_view->maximized || kywc_view->tiled) {
        double frac = (x - geometry->x) / geometry->width;
        saved->x = x - frac * saved->width;
        if (saved->x < geometry->x) {
            saved->x = geometry->x;
        }
        saved->y = geometry->y;
        grab->geo.x = saved->x;

        if (kywc_view->maximized) {
            kywc_view_set_maximized(kywc_view, false);
        } else {
            kywc_view_set_tiled(kywc_view, KYWC_TILE_NONE);
        }
    }

    int nx = grab->geo.x + x - grab->cursor_x;
    int ny = grab->geo.y + y - grab->cursor_y;
    // seat_move_constraints(seat, &nx, &ny);
    kywc_view_move(kywc_view, nx, ny);
}

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
static void interactive_process_resize(struct interactive_grab *grab, double x, double y)
{
    struct kywc_view *kywc_view = &grab->view->base;

    int min_width = MAX(kywc_view->min_width, VIEW_MIN_WIDTH);
    int min_height = MAX(kywc_view->min_height, VIEW_MIN_HEIGHT);
    double dx = x - grab->cursor_x;
    double dy = y - grab->cursor_y;

    struct kywc_box pending = kywc_view->geometry;

    if (grab->resize_edges & KYWC_EDGE_TOP) {
        pending.height = grab->geo.height - dy;
    } else if (grab->resize_edges & KYWC_EDGE_BOTTOM) {
        pending.height = grab->geo.height + dy;
    }
    if (pending.height < min_height) {
        pending.height = min_height;
    }

    if (grab->resize_edges & KYWC_EDGE_LEFT) {
        pending.width = grab->geo.width - dx;
    } else if (grab->resize_edges & KYWC_EDGE_RIGHT) {
        pending.width = grab->geo.width + dx;
    }
    if (pending.width < min_width) {
        pending.width = min_width;
    }

    if (grab->resize_edges & KYWC_EDGE_TOP) {
        /* anchor bottom edge */
        pending.y = grab->geo.y + grab->geo.height - pending.height;
    }
    if (grab->resize_edges & KYWC_EDGE_LEFT) {
        /* anchor right edge */
        pending.x = grab->geo.x + grab->geo.width - pending.width;
    }

    // seat_resize_constraints(seat, &pending, state->resize_edges);
    kywc_view_resize(kywc_view, &pending);
}

static void pointer_grab_motion(struct seat_pointer_grab *pointer_grab, uint32_t time, double lx,
                                double ly)
{
    struct interactive_grab *grab = pointer_grab->data;

    if (grab->mode == INTERACTIVE_MODE_MOVE) {
        /* set moving cursor image in server side, may replaced by client set_cursor later */
        if (!grab->ongoing) {
            cursor_set_image(grab->seat->cursor, CURSOR_MOVE);
        }
        interactive_process_move(grab, lx, ly);
    } else if (grab->mode == INTERACTIVE_MODE_RESIZE) {
        interactive_process_resize(grab, lx, ly);
    }

    grab->ongoing = true;
}

static void pointer_grab_button(struct seat_pointer_grab *pointer_grab, uint32_t time,
                                uint32_t button, bool pressed)
{
    kywc_log(KYWC_DEBUG, "grab %p button %d %s", pointer_grab, button,
             pressed ? "pressed" : "released");

    struct interactive_grab *grab = pointer_grab->data;

    if (!pressed) {
        interactive_done(grab);
    }
}

static void pointer_grab_axis(struct seat_pointer_grab *pointer_grab, uint32_t time, bool vertical,
                              double value)
{
    kywc_log(KYWC_DEBUG, "grab %p axis(%d) %f", pointer_grab, vertical, value);
}

static void pointer_grab_cancel(struct seat_pointer_grab *pointer_grab)
{
    struct interactive_grab *grab = pointer_grab->data;
    interactive_grab_destroy(grab);
}

static const struct seat_pointer_grab_interface pointer_grab_impl = {
    .motion = pointer_grab_motion,
    .button = pointer_grab_button,
    .axis = pointer_grab_axis,
    .cancel = pointer_grab_cancel,
};

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct interactive_grab *grab = wl_container_of(listener, grab, view_unmap);
    interactive_grab_destroy(grab);
}

static void interactive_grab_add(struct view *view, enum interactive_mode mode, uint32_t edges,
                                 struct seat *seat)
{
    struct interactive_grab *grab = calloc(1, sizeof(struct interactive_grab));
    if (!grab) {
        return;
    }

    grab->pointer_grab.interface = &pointer_grab_impl;
    grab->pointer_grab.seat = seat;
    grab->pointer_grab.data = grab;
    if (!seat_set_pointer_grab(seat, &grab->pointer_grab)) {
        free(grab);
        return;
    }

    grab->seat = seat;
    grab->mode = mode;
    grab->cursor_x = seat->cursor->lx;
    grab->cursor_y = seat->cursor->ly;
    grab->geo = view->base.geometry;
    grab->resize_edges = edges;
    grab->ongoing = false;

    grab->view = view;
    grab->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&view->base.events.unmap, &grab->view_unmap);
}

void interactive_begin_move(struct view *view, struct seat *seat)
{
    if (!view_is_moveable(view)) {
        return;
    }
    interactive_grab_add(view, INTERACTIVE_MODE_MOVE, 0, seat);
}

void interactive_begin_resize(struct view *view, uint32_t edges, struct seat *seat)
{
    if (!view_is_resizable(view)) {
        return;
    }
    interactive_grab_add(view, INTERACTIVE_MODE_RESIZE, edges, seat);
}
