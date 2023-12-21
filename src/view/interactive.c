// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <linux/input-event-codes.h>
#include <wlr/types/wlr_seat.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "scene/animation.h"
#include "theme.h"
#include "view/action.h"

#define VIEW_EDGE_GAP 20
#define VIEW_TOP_GAP 5
#define VIEW_BOTTOM_GAP 100
#define VIEW_MIN_WIDTH 200
#define VIEW_MIN_HEIGHT 100
#define VIEW_MOVE_STEP 10
#define VIEW_RESIZE_STEP 10
#define SNAP_BOX_FILTER 200
#define SNAP_BORDER_CORNER_RATIO 0.25

enum interactive_mode {
    INTERACTIVE_MODE_NONE = 0,
    INTERACTIVE_MODE_MOVE,
    INTERACTIVE_MODE_RESIZE,
};

struct interactive_grab {
    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;
    struct seat_touch_grab touch_grab;

    struct seat *seat;
    // struct wl_listener *seat_destroy;
    struct output *output;

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

    /* snap_box */
    struct ky_scene_node *snap_node;
    struct ky_scene_rect *snap_rect;
    struct output *snap_output;
    enum kywc_tile snap_mode;
    struct wl_event_source *filter;
    bool filter_enabled;
};

static enum kywc_tile get_kywc_tile_mode(struct interactive_grab *grab)
{
    double cur_x = grab->seat->cursor->lx;
    double cur_y = grab->seat->cursor->ly;
    enum kywc_tile mode = KYWC_TILE_NONE;
    /* current output usable area */
    struct kywc_box *usable = &grab->output->usable_area;
    int32_t y1 = cur_y - usable->y;

    /* left */
    if (cur_x - usable->x < VIEW_EDGE_GAP) {
        /* top left*/
        if (y1 < usable->height * SNAP_BORDER_CORNER_RATIO) {
            mode = KYWC_TILE_TOP_LEFT;
            /* bottom left */
        } else if (y1 > usable->height - usable->height * SNAP_BORDER_CORNER_RATIO) {
            mode = KYWC_TILE_BOTTOM_LEFT;
            /* left */
        } else {
            mode = KYWC_TILE_LEFT;
        }
        /* right */
    } else if (usable->x + usable->width - cur_x < VIEW_EDGE_GAP) {
        /* top right*/
        if (y1 < usable->height * SNAP_BORDER_CORNER_RATIO) {
            mode = KYWC_TILE_TOP_RIGHT;
            /* bottom right */
        } else if (y1 > usable->height - usable->height * SNAP_BORDER_CORNER_RATIO) {
            mode = KYWC_TILE_BOTTOM_RIGHT;
            /* right */
        } else {
            mode = KYWC_TILE_RIGHT;
        }
        /* all */
    } else if (y1 < VIEW_TOP_GAP) {
        mode = KYWC_TILE_ALL;
    }

    return mode;
}

static void snap_box_update(struct interactive_grab *grab, enum kywc_tile mode)
{
    if (grab->snap_mode == mode && grab->snap_output == grab->output) {
        return;
    }

    struct view *view = grab->view;
    bool need_source_box = grab->snap_mode == KYWC_TILE_NONE;

    if (!grab->snap_rect) {
        struct theme *theme = theme_manager_get_current();
        struct ky_scene_node *sibling = ky_scene_node_from_tree(view->tree);
        struct ky_scene_tree *parent = ky_scene_node_get_parent(sibling);
        grab->snap_rect = ky_scene_rect_create(parent, 0, 0, theme->selected_color);
        grab->snap_node = ky_scene_node_from_rect(grab->snap_rect);
        ky_scene_node_place_below(grab->snap_node, sibling);
        need_source_box = true;
    }

    grab->snap_mode = mode;
    grab->snap_output = grab->output;
    ky_scene_node_set_enabled(grab->snap_node, mode != KYWC_TILE_NONE);
    if (mode == KYWC_TILE_NONE) {
        return;
    }

    struct kywc_box geo = { 0 };
    view_get_tiled_geometry(view, &geo, &grab->output->base, mode);
    geo.x -= view->base.margin.off_x;
    geo.y -= view->base.margin.off_y;
    geo.width += view->base.margin.off_width;
    geo.height += view->base.margin.off_height;

    if (need_source_box) {
        struct wlr_box output_box = { geo.x, geo.y, geo.width, geo.height };
        struct wlr_box view_box = { view->base.geometry.x - view->base.margin.off_x,
                                    view->base.geometry.y - view->base.margin.off_y,
                                    view->base.geometry.width + view->base.margin.off_x,
                                    view->base.geometry.height + view->base.margin.off_y };
        struct wlr_box rect_box;
        wlr_box_intersection(&rect_box, &output_box, &view_box);
        ky_scene_rect_set_size(grab->snap_rect, rect_box.width, rect_box.height);
        ky_scene_node_set_position(grab->snap_node, rect_box.x, rect_box.y);
    }

    struct animation *animation = animation_manager_get_default(ANIMATION_TYPE_EASE_OUT);
    ky_scene_rect_set_size_with_animation(grab->snap_rect, geo.width, geo.height, animation, 200);
    ky_scene_node_set_position_with_animation(grab->snap_node, geo.x, geo.y, animation, 200);
}

static void snap_box_enable_filter(struct interactive_grab *grab, bool enabled)
{
    if (grab->filter_enabled == enabled) {
        return;
    }
    grab->filter_enabled = enabled;
    wl_event_source_timer_update(grab->filter, enabled ? SNAP_BOX_FILTER : 0);
}

static int handle_snap_box(void *data)
{
    struct interactive_grab *grab = data;
    grab->output = input_current_output(grab->seat);
    enum kywc_tile mode = get_kywc_tile_mode(grab);

    grab->filter_enabled = false;
    snap_box_update(grab, mode);
    return 0;
}

static void interactive_move_show_snap_box(struct interactive_grab *grab, int cur_x, int cur_y)
{
    if (!grab->view->base.resizable) {
        return;
    }

    struct kywc_box *usable = &grab->output->usable_area;

    /* left */
    if (cur_x - usable->x < VIEW_EDGE_GAP) {
        /* trigger timer to show snap box if not the leftmost output */
        if (!output_at_layout_edge(grab->output, LAYOUT_EDGE_LEFT) &&
            grab->snap_mode != KYWC_TILE_TOP_LEFT && grab->snap_mode != KYWC_TILE_LEFT &&
            grab->snap_mode != KYWC_TILE_BOTTOM_LEFT) {
            snap_box_update(grab, KYWC_TILE_NONE);
            snap_box_enable_filter(grab, true);
            return;
        }
        /* right */
    } else if (usable->x + usable->width - cur_x < VIEW_EDGE_GAP) {
        /* trigger timer to show snap box if not the rightmost output */
        if (!output_at_layout_edge(grab->output, LAYOUT_EDGE_RIGHT) &&
            grab->snap_mode != KYWC_TILE_RIGHT && grab->snap_mode != KYWC_TILE_TOP_RIGHT &&
            grab->snap_mode != KYWC_TILE_BOTTOM_RIGHT) {
            snap_box_update(grab, KYWC_TILE_NONE);
            snap_box_enable_filter(grab, true);
            return;
        }
        /* all */
    } else if (cur_y - usable->y < VIEW_TOP_GAP) {
        /* trigger timer to show snap box if not the rightmost output */
        if (!output_at_layout_edge(grab->output, LAYOUT_EDGE_TOP) &&
            grab->snap_mode != KYWC_TILE_ALL) {
            snap_box_update(grab, KYWC_TILE_NONE);
            snap_box_enable_filter(grab, true);
            return;
        }
    }

    enum kywc_tile mode = get_kywc_tile_mode(grab);
    snap_box_enable_filter(grab, false);
    snap_box_update(grab, mode);
}

static void interactive_grab_destroy(struct interactive_grab *grab)
{
    grab->view->current_resize_edges = KYWC_EDGE_NONE;
    wl_list_remove(&grab->view_unmap.link);
    seat_end_pointer_grab(grab->seat, &grab->pointer_grab);
    seat_end_keyboard_grab(grab->seat, &grab->keyboard_grab);
    seat_end_touch_grab(grab->seat, &grab->touch_grab);
    ky_scene_node_destroy(grab->snap_node);
    wl_event_source_remove(grab->filter);
    free(grab);
}

static void interactivate_done_move(struct interactive_grab *grab)
{
    grab->output = input_current_output(grab->seat);
    snap_box_update(grab, KYWC_TILE_NONE);
    enum kywc_tile mode = get_kywc_tile_mode(grab);
    if (mode == KYWC_TILE_NONE) {
        return;
    }

    if (mode == KYWC_TILE_ALL) {
        /* current cursor focused output, not the view most at output */
        kywc_view_set_maximized(&grab->view->base, true, &grab->output->base);
        return;
    }

    kywc_view_set_tiled(&grab->view->base, mode, &grab->output->base);
}

static void interactive_done(struct interactive_grab *grab)
{
    /* for snap to edge */
    if (grab->view->base.mapped && grab->ongoing && grab->mode == INTERACTIVE_MODE_MOVE) {
        interactivate_done_move(grab);
    }

    cursor_set_image(grab->seat->cursor, CURSOR_DEFAULT);
    interactive_grab_destroy(grab);
}

static void interactive_move_constraints(struct interactive_grab *grab, int *x, int *y)
{
    /* get current seat constraints output */
    struct kywc_box *usable = &grab->output->usable_area;
    struct kywc_view *kywc_view = &grab->view->base;
    struct kywc_box *current = &kywc_view->geometry;

    /* actual view coord */
    int x1 = *x - kywc_view->margin.off_x;
    int y1 = *y - kywc_view->margin.off_y;
    int x2 = x1 + current->width + kywc_view->margin.off_width;
    int y2 = y1 + current->height + kywc_view->margin.off_height;

    int ux2 = usable->x + usable->width;
    int uy2 = usable->y + usable->height;

    /* window edge adsorption in left, right */
    if (abs(usable->x - x1) < VIEW_EDGE_GAP) {
        *x = usable->x + kywc_view->margin.off_x;
    } else if (abs(x2 - ux2) < VIEW_EDGE_GAP) {
        *x = ux2 - current->width - kywc_view->margin.off_width + kywc_view->margin.off_x;
    }
    /* top and bottom */
    if (abs(usable->y - y1) < VIEW_EDGE_GAP) {
        *y = usable->y + kywc_view->margin.off_y;
    } else if (abs(y2 - uy2) < VIEW_EDGE_GAP) {
        *y = uy2 - current->height - kywc_view->margin.off_height + kywc_view->margin.off_y;
    }

    /* constraints when moving to top and bottom */
    if (output_at_layout_edge(grab->output, LAYOUT_EDGE_TOP) && y1 < usable->y) {
        *y = usable->y + kywc_view->margin.off_y;
    } else if (output_at_layout_edge(grab->output, LAYOUT_EDGE_BOTTOM) &&
               uy2 - y1 < VIEW_BOTTOM_GAP) {
        *y = uy2 - VIEW_BOTTOM_GAP + kywc_view->margin.off_y;
    }
}

static void interactive_process_move(struct interactive_grab *grab, double x, double y)
{
    struct kywc_view *kywc_view = &grab->view->base;
    struct kywc_box *geometry = &kywc_view->geometry;

    if (kywc_view->maximized || kywc_view->tiled) {
        struct kywc_box *saved = &grab->view->saved.geometry;
        double frac = (x - geometry->x) / geometry->width;
        saved->x = x - frac * saved->width;
        if (saved->x < geometry->x) {
            saved->x = geometry->x;
        }
        saved->y = geometry->y;
        grab->geo.x = saved->x;

        if (kywc_view->maximized) {
            kywc_view_set_maximized(kywc_view, false, NULL);
        } else {
            kywc_view_set_tiled(kywc_view, KYWC_TILE_NONE, NULL);
        }
    }

    int nx = grab->geo.x + x - grab->cursor_x;
    int ny = grab->geo.y + y - grab->cursor_y;
    interactive_move_constraints(grab, &nx, &ny);
    kywc_view_move(kywc_view, nx, ny);

    interactive_move_show_snap_box(grab, x, y);
}

static void interactive_resize_constraints(struct interactive_grab *grab, struct kywc_box *box)
{
    /* get current seat constraints output */
    struct kywc_box *usable = &grab->output->usable_area;
    struct kywc_view *kywc_view = &grab->view->base;
    struct kywc_box *current = &kywc_view->geometry;

    /* pending view coord */
    int x1 = box->x - kywc_view->margin.off_x;
    int y1 = box->y - kywc_view->margin.off_y;
    int x2 = x1 + box->width + kywc_view->margin.off_width;
    int y2 = y1 + box->height + kywc_view->margin.off_height;

    if (x1 > usable->x + usable->width || x2 < usable->x || y1 > usable->y + usable->height ||
        y2 < usable->y) {
        usable = &output_from_kywc_output(grab->view->output)->usable_area;
    }

    int ux2 = usable->x + usable->width;
    int uy2 = usable->y + usable->height;

    /* constraints when resize to top and bottom */
    if (grab->resize_edges & KYWC_EDGE_TOP) {
        /* top */
        if (output_at_layout_edge(grab->output, LAYOUT_EDGE_TOP) && y1 < usable->y) {
            box->y = usable->y + kywc_view->margin.off_y;
            box->height = current->height + current->y - box->y;
            /* bottom */
        } else if (output_at_layout_edge(grab->output, LAYOUT_EDGE_BOTTOM) &&
                   y1 > uy2 - kywc_view->margin.off_y - VIEW_EDGE_GAP) {
            box->y = uy2 - VIEW_EDGE_GAP;
            box->height = y2 - box->y - (kywc_view->margin.off_height - kywc_view->margin.off_y);
        }
    } else if (grab->resize_edges & KYWC_EDGE_BOTTOM) {
        /* top */
        if (output_at_layout_edge(grab->output, LAYOUT_EDGE_TOP) &&
            y2 < usable->y + VIEW_EDGE_GAP) {
            box->height = usable->y + VIEW_EDGE_GAP - y1 - kywc_view->margin.off_height;
            /* bottom */
        } else if (output_at_layout_edge(grab->output, LAYOUT_EDGE_BOTTOM) && y2 > uy2) {
            box->height = uy2 - y1 - kywc_view->margin.off_height;
        }
    }

    /* constraints when resize to left and right */
    if (grab->resize_edges & KYWC_EDGE_LEFT) {
        /* left */
        if (output_at_layout_edge(grab->output, LAYOUT_EDGE_LEFT) && x1 < usable->x) {
            box->x = usable->x + kywc_view->margin.off_x;
            box->width = current->width + current->x - box->x;
            /* right */
        } else if (output_at_layout_edge(grab->output, LAYOUT_EDGE_RIGHT) &&
                   x1 > ux2 - VIEW_EDGE_GAP) {
            box->x = ux2 - VIEW_EDGE_GAP + kywc_view->margin.off_x;
            box->width = x2 - box->x - (kywc_view->margin.off_width - kywc_view->margin.off_x);
        }
    } else if (grab->resize_edges & KYWC_EDGE_RIGHT) {
        /* left */
        if (output_at_layout_edge(grab->output, LAYOUT_EDGE_LEFT) &&
            x2 < usable->x + VIEW_EDGE_GAP) {
            box->width = usable->x + VIEW_EDGE_GAP - x1 - kywc_view->margin.off_width;
            /* right */
        } else if (output_at_layout_edge(grab->output, LAYOUT_EDGE_RIGHT) && x2 > ux2) {
            box->width = ux2 - x1 - kywc_view->margin.off_width;
        }
    }
}

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
static void interactive_process_resize(struct interactive_grab *grab, double x, double y)
{
    struct kywc_view *kywc_view = &grab->view->base;

    int min_width = MAX(kywc_view->min_width, VIEW_MIN_WIDTH);
    int min_height = MAX(kywc_view->min_height, VIEW_MIN_HEIGHT);
    int max_width = kywc_view->max_width;
    int max_height = kywc_view->max_height;
    struct wlr_box box = { 0 };

    wlr_output_layout_get_box(grab->seat->layout, NULL, &box);

    if (!max_width || max_width > box.width - kywc_view->margin.off_width) {
        max_width = box.width - kywc_view->margin.off_width;
    }
    if (!max_height || max_height > box.height - kywc_view->margin.off_height) {
        max_height = box.height - kywc_view->margin.off_height;
    }

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
    } else if (pending.height > max_height) {
        pending.height = max_height;
    }

    if (grab->resize_edges & KYWC_EDGE_LEFT) {
        pending.width = grab->geo.width - dx;
    } else if (grab->resize_edges & KYWC_EDGE_RIGHT) {
        pending.width = grab->geo.width + dx;
    }
    if (pending.width < min_width) {
        pending.width = min_width;
    } else if (pending.width > max_width) {
        pending.width = max_width;
    }

    if (grab->resize_edges & KYWC_EDGE_TOP) {
        /* anchor bottom edge */
        pending.y = grab->geo.y + grab->geo.height - pending.height;
    }
    if (grab->resize_edges & KYWC_EDGE_LEFT) {
        /* anchor right edge */
        pending.x = grab->geo.x + grab->geo.width - pending.width;
    }

    interactive_resize_constraints(grab, &pending);
    grab->view->current_resize_edges = grab->resize_edges;
    kywc_view_resize(kywc_view, &pending);
}

static bool pointer_grab_motion(struct seat_pointer_grab *pointer_grab, uint32_t time, double lx,
                                double ly)
{
    struct interactive_grab *grab = pointer_grab->data;
    grab->output = input_current_output(grab->seat);

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
    return true;
}

static bool pointer_grab_button(struct seat_pointer_grab *pointer_grab, uint32_t time,
                                uint32_t button, bool pressed)
{
    struct interactive_grab *grab = pointer_grab->data;

    if (!pressed) {
        interactive_done(grab);
        return false;
    }
    return true;
}

static bool pointer_grab_axis(struct seat_pointer_grab *pointer_grab, uint32_t time, bool vertical,
                              double value)
{
    return true;
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

static bool keyboard_grab_key(struct seat_keyboard_grab *keyboard_grab, uint32_t time, uint32_t key,
                              bool pressed, uint32_t modifiers)
{
    if (!pressed) {
        return true;
    }

    struct interactive_grab *grab = keyboard_grab->data;
    int step = grab->mode == INTERACTIVE_MODE_MOVE ? VIEW_MOVE_STEP : VIEW_RESIZE_STEP;
    int dx = key == KEY_RIGHT ? step : (key == KEY_LEFT ? -step : 0);
    int dy = key == KEY_DOWN ? step : (key == KEY_UP ? -step : 0);

    /* restore to the orig geometry */
    if (key == KEY_ESC) {
        kywc_view_resize(&grab->view->base, &grab->geo);
        interactive_done(grab);
        return false;
    }
    if (key == KEY_ENTER) {
        interactive_done(grab);
        return false;
    }

    struct cursor *cursor = grab->seat->cursor;
    cursor_move(cursor, NULL, dx, dy, true, false);
    pointer_grab_motion(&grab->pointer_grab, time, cursor->lx, cursor->ly);
    return true;
}

static void keyboard_grab_cancel(struct seat_keyboard_grab *keyboard_grab)
{
    struct interactive_grab *grab = keyboard_grab->data;
    interactive_grab_destroy(grab);
}

static const struct seat_keyboard_grab_interface keyboard_grab_impl = {
    .key = keyboard_grab_key,
    .cancel = keyboard_grab_cancel,
};

static bool touch_grab_touch(struct seat_touch_grab *touch_grab, uint32_t time, bool down)
{
    if (down) {
        return true;
    }

    struct interactive_grab *grab = touch_grab->data;
    interactive_done(grab);
    return false;
}

static bool touch_grab_motion(struct seat_touch_grab *touch_grab, uint32_t time, double lx,
                              double ly)
{
    struct interactive_grab *grab = touch_grab->data;
    pointer_grab_motion(&grab->pointer_grab, time, lx, ly);
    return true;
}

static void touch_grab_cancel(struct seat_touch_grab *touch_grab)
{
    struct interactive_grab *grab = touch_grab->data;
    interactive_grab_destroy(grab);
}

static const struct seat_touch_grab_interface touch_grab_impl = {
    .touch = touch_grab_touch,
    .motion = touch_grab_motion,
    .cancel = touch_grab_cancel,
};

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct interactive_grab *grab = wl_container_of(listener, grab, view_unmap);
    interactive_done(grab);
}

static void interactive_grab_add(struct view *view, enum interactive_mode mode, uint32_t edges,
                                 struct seat *seat)
{
    struct interactive_grab *grab = calloc(1, sizeof(struct interactive_grab));
    if (!grab) {
        return;
    }

    grab->pointer_grab = (struct seat_pointer_grab){ &pointer_grab_impl, seat, grab };
    seat_start_pointer_grab(seat, &grab->pointer_grab);
    grab->keyboard_grab = (struct seat_keyboard_grab){ &keyboard_grab_impl, seat, grab };
    seat_start_keyboard_grab(seat, &grab->keyboard_grab);
    grab->touch_grab = (struct seat_touch_grab){ &touch_grab_impl, seat, grab };
    seat_start_touch_grab(seat, &grab->touch_grab);

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

    struct wl_event_loop *loop = wl_display_get_event_loop(seat->wlr_seat->display);
    grab->filter = wl_event_loop_add_timer(loop, handle_snap_box, grab);
}

void window_begin_move(struct view *view, struct seat *seat)
{
    if (!view_is_moveable(view)) {
        return;
    }
    interactive_grab_add(view, INTERACTIVE_MODE_MOVE, 0, seat);
}

void window_begin_resize(struct view *view, uint32_t edges, struct seat *seat)
{
    if (!view_is_resizable(view)) {
        return;
    }
    interactive_grab_add(view, INTERACTIVE_MODE_RESIZE, edges, seat);
}
