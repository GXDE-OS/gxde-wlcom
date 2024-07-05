// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <linux/input-event-codes.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>

#include "effect/move.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "scene/animation.h"
#include "view/action.h"
#include "view/workspace.h"

#define VIEW_EDGE_GAP 20
#define VIEW_TOP_GAP 5
#define VIEW_BOTTOM_GAP 100
#define VIEW_LEFT_GAP 100
#define VIEW_RIGHT_GAP 100
#define VIEW_MIN_WIDTH 200
#define VIEW_MIN_HEIGHT 100
#define VIEW_MOVE_STEP 10
#define VIEW_RESIZE_STEP 10
#define SNAP_BOX_FILTER 200
#define SNAP_BORDER_CORNER_RATIO 0.25
#define EDGE_OFFSET 10

static float snap_background_color[4] = { 128.0 / 255, 128.0 / 255, 128.0 / 255, 128.0 / 255 };

enum interactive_mode {
    INTERACTIVE_MODE_NONE = 0,
    INTERACTIVE_MODE_MOVE,
    INTERACTIVE_MODE_RESIZE,
    INTERACTIVE_MODE_TILE,
    INTERACTIVE_MODE_TILE_HALF_SCREEN,
};

enum tile_state {
    TILE_NONE = 0,
    TILE_TOP,
    TILE_BOTTOM,
    TILE_LEFT,
    TILE_RIGHT,
    TILE_TOP_LEFT,
    TILE_BOTTOM_LEFT,
    TILE_TOP_RIGHT,
    TILE_BOTTOM_RIGHT,
    TILE_MINIMIZE,
    TILE_MAXIMIZE,
};

struct interactive_tile_state {
    int key_up;
    int key_down;
    int key_left;
    int key_right;
};

const struct interactive_tile_state tile_states[] = {
    { TILE_MAXIMIZE, TILE_MINIMIZE, TILE_LEFT, TILE_RIGHT },            // TILE_NONE
    { TILE_TOP, TILE_NONE, TILE_TOP_LEFT, TILE_TOP_RIGHT },             // TILE_TOP
    { TILE_NONE, TILE_MINIMIZE, TILE_BOTTOM_LEFT, TILE_BOTTOM_RIGHT },  // TILE_BOTTOM
    { TILE_TOP_LEFT, TILE_BOTTOM_LEFT, TILE_RIGHT, TILE_NONE },         // TILE_LEFT
    { TILE_TOP_RIGHT, TILE_BOTTOM_RIGHT, TILE_NONE, TILE_LEFT },        // TILE_RIGHT
    { TILE_MAXIMIZE, TILE_LEFT, TILE_TOP_RIGHT, TILE_TOP_RIGHT },       // TILE_TOP_LEFT
    { TILE_LEFT, TILE_MINIMIZE, TILE_BOTTOM_RIGHT, TILE_BOTTOM_RIGHT }, // TILE_BOTTOM_LEFT
    { TILE_MAXIMIZE, TILE_RIGHT, TILE_TOP_LEFT, TILE_TOP_LEFT },        // TILE_TOP_RIGHT
    { TILE_RIGHT, TILE_MINIMIZE, TILE_BOTTOM_LEFT, TILE_BOTTOM_LEFT },  // TILE_BOTTOM_RIGHT
    { TILE_NONE, TILE_MINIMIZE, TILE_MINIMIZE, TILE_MINIMIZE },         // TILE_MINIMIZE
    { TILE_TOP, TILE_NONE, TILE_LEFT, TILE_RIGHT },                     // TILE_MAXIMIZE
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

    /* move effect */
    struct move_proxy *proxy;
    struct wl_listener proxy_destroy;

    /* action tiled */
    enum tile_state current;
    float save_x_ratio;
    float save_y_ratio;
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
        struct ky_scene_node *sibling = &view->tree->node;
        grab->snap_rect = ky_scene_rect_create(sibling->parent, 0, 0, snap_background_color);
        grab->snap_node = &grab->snap_rect->node;
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

    struct animation *animation = animation_manager_get(ANIMATION_TYPE_EASE_OUT);
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
    struct view *view = grab->view;
    if (!view_is_resizable(view)) {
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
    if (grab->proxy) {
        wl_list_remove(&grab->proxy_destroy.link);
        move_proxy_destroy(grab->proxy);
    }

    /* for snap to edge */
    if (grab->view->base.mapped && grab->ongoing && grab->mode == INTERACTIVE_MODE_MOVE) {
        interactivate_done_move(grab);
    }

    cursor_set_image(grab->seat->cursor, CURSOR_DEFAULT);
    interactive_grab_destroy(grab);
}

static void window_adsorption_top_or_bottom(struct kywc_box *s_box, const struct kywc_box *l_box,
                                            int *offset, enum interactive_mode mode)
{
    int sx1 = s_box->x, sy1 = s_box->y, sx2 = s_box->x + s_box->width,
        sy2 = s_box->y + s_box->height;
    int lx1 = l_box->x, ly1 = l_box->y, lx2 = l_box->x + l_box->width,
        ly2 = l_box->y + l_box->height;

    if (sx1 > lx2 || sx2 < lx1) {
        return;
    }

    /* top adsorb bottom */
    int temp = abs(ly2 - sy1);
    if (temp < *offset) {
        *offset = temp;
        s_box->y = ly2;
        if (mode == INTERACTIVE_MODE_RESIZE) {
            s_box->height = sy2 - ly2;
        }
        return;
    }

    /* bottom adsorb top */
    temp = abs(ly1 - sy2);
    if (temp < *offset) {
        *offset = temp;
        if (mode == INTERACTIVE_MODE_MOVE) {
            s_box->y = ly1 - (sy2 - sy1);
        } else if (mode == INTERACTIVE_MODE_RESIZE) {
            s_box->height += temp;
        }
    }
}

static void window_adsorption_left_or_right(struct kywc_box *s_box, const struct kywc_box *l_box,
                                            int *offset, enum interactive_mode mode)
{
    int sx1 = s_box->x, sy1 = s_box->y, sx2 = s_box->x + s_box->width,
        sy2 = s_box->y + s_box->height;
    int lx1 = l_box->x, ly1 = l_box->y, lx2 = l_box->x + l_box->width,
        ly2 = l_box->y + l_box->height;

    if (sy1 > ly2 || sy2 < ly1) {
        return;
    }

    /* left adsorb right */
    int temp = abs(lx2 - sx1);
    if (temp < *offset) {
        *offset = temp;
        s_box->x = lx2;
        if (mode == INTERACTIVE_MODE_RESIZE) {
            s_box->width = sx2 - lx2;
        }
        return;
    }

    /* right adsorb left */
    temp = abs(lx1 - sx2);
    if (temp < *offset) {
        *offset = temp;
        if (mode == INTERACTIVE_MODE_MOVE) {
            s_box->x = lx1 - (sx2 - sx1);
        } else if (mode == INTERACTIVE_MODE_RESIZE) {
            s_box->width += temp;
        }
    }
}

static void window_adsorb_window_constraints(struct kywc_view *kywc_view, struct kywc_box *pending,
                                             int *gap_x, int *gap_y, uint32_t edges,
                                             enum interactive_mode mode)
{
    if ((view_manager_get_adsorption() & VIEW_ADSORPTION_WINDOW_EDGES) == 0) {
        return;
    }

    /* actual window box */
    struct kywc_box s_box = {
        .x = pending->x - kywc_view->margin.off_x,
        .y = pending->y - kywc_view->margin.off_y,
        .width = pending->width + kywc_view->margin.off_width,
        .height = pending->height + kywc_view->margin.off_height,
    };

    struct view_proxy *view_proxy;
    struct workspace *workspace = workspace_manager_get_current();
    wl_list_for_each(view_proxy, &workspace->view_proxies, workspace_link) {
        if (!view_proxy->view->base.mapped || view_proxy->view == view_from_kywc_view(kywc_view) ||
            view_proxy->view->base.minimized || view_proxy->view->base.maximized ||
            view_proxy->view->base.fullscreen) {
            continue;
        }

        /* be adsorbed window box */
        struct kywc_box l_box = {
            .x = view_proxy->view->base.geometry.x - view_proxy->view->base.margin.off_x,
            .y = view_proxy->view->base.geometry.y - view_proxy->view->base.margin.off_y,
            .width =
                view_proxy->view->base.geometry.width + view_proxy->view->base.margin.off_width,
            .height =
                view_proxy->view->base.geometry.height + view_proxy->view->base.margin.off_height,
        };

        if (edges & KYWC_EDGE_LEFT || edges & KYWC_EDGE_RIGHT) {
            window_adsorption_left_or_right(&s_box, &l_box, gap_x, mode);
        }
        if (edges & KYWC_EDGE_TOP || edges & KYWC_EDGE_BOTTOM) {
            window_adsorption_top_or_bottom(&s_box, &l_box, gap_y, mode);
        }
    }

    pending->x = s_box.x + kywc_view->margin.off_x;
    pending->y = s_box.y + kywc_view->margin.off_y;
    if (mode == INTERACTIVE_MODE_RESIZE) {
        pending->width = s_box.width - kywc_view->margin.off_width;
        pending->height = s_box.height - kywc_view->margin.off_height;
    }
}

static void window_adsorb_edges_constraints(struct kywc_view *kywc_view, struct kywc_box *pending,
                                            struct output *output, int *gap_x, int *gap_y)
{
    if ((view_manager_get_adsorption() & VIEW_ADSORPTION_SCREEN_EDGES) == 0) {
        return;
    }

    struct kywc_box *usable = &output->usable_area;
    struct kywc_box *current = &kywc_view->geometry;

    /* actual window box */
    struct kywc_box s_box = {
        .x = pending->x - kywc_view->margin.off_x,
        .y = pending->y - kywc_view->margin.off_y,
        .width = pending->width + kywc_view->margin.off_width,
        .height = pending->height + kywc_view->margin.off_height,
    };
    /* actual view coord */
    int x1 = s_box.x;
    int y1 = s_box.y;
    int x2 = s_box.x + s_box.width;
    int y2 = s_box.y + s_box.height;
    /* usable coord */
    int ux1 = usable->x;
    int uy1 = usable->y;
    int ux2 = usable->x + usable->width;
    int uy2 = usable->y + usable->height;

    /* window edge adsorption in left, right */
    if (abs(ux1 - x1) < *gap_x) {
        *gap_x = abs(ux1 - x1);
        pending->x = ux1 + kywc_view->margin.off_x;
    } else if (abs(x2 - ux2) < *gap_x) {
        *gap_x = abs(x2 - ux2);
        pending->x = ux2 - current->width - kywc_view->margin.off_width + kywc_view->margin.off_x;
    }
    /* top and bottom */
    if (abs(uy1 - y1) < *gap_y) {
        *gap_y = abs(uy1 - y1);
        pending->y = uy1 + kywc_view->margin.off_y;
    } else if (abs(y2 - uy2) < *gap_y) {
        pending->y = uy2 - current->height - kywc_view->margin.off_height + kywc_view->margin.off_y;
    }
}

void window_move_constraints(struct kywc_view *kywc_view, struct output *output, int *x, int *y)
{
    /* get current seat constraints output */
    struct kywc_box *usable = &output->usable_area;
    struct kywc_box *current = &kywc_view->geometry;

    /* actual view coord */
    int x1 = *x - kywc_view->margin.off_x;
    int y1 = *y - kywc_view->margin.off_y;
    int x2 = x1 + current->width + kywc_view->margin.off_width;

    struct kywc_box pending = {
        .x = *x,
        .y = *y,
        .width = kywc_view->geometry.width,
        .height = kywc_view->geometry.height,
    };
    int gap_x = EDGE_OFFSET, gap_y = EDGE_OFFSET;
    uint32_t edges = KYWC_EDGE_NONE;
    /* window edge adsorption in left, right */
    edges |= *x != kywc_view->geometry.x ? KYWC_EDGE_LEFT | KYWC_EDGE_RIGHT : KYWC_EDGE_NONE;
    /* top and bottom */
    edges |= *y != kywc_view->geometry.y ? KYWC_EDGE_TOP | KYWC_EDGE_BOTTOM : KYWC_EDGE_NONE;
    window_adsorb_window_constraints(kywc_view, &pending, &gap_x, &gap_y, edges,
                                     INTERACTIVE_MODE_MOVE);
    gap_x = gap_x < EDGE_OFFSET ? gap_x : VIEW_EDGE_GAP;
    gap_y = gap_y < EDGE_OFFSET ? gap_y : VIEW_EDGE_GAP;
    window_adsorb_edges_constraints(kywc_view, &pending, output, &gap_x, &gap_y);
    *x = pending.x;
    *y = pending.y;

    int ux2 = usable->x + usable->width;
    int uy2 = usable->y + usable->height;

    /* constraints when moving to top and bottom */
    if (output_at_layout_edge(output, LAYOUT_EDGE_TOP) && y1 < usable->y) {
        *y = usable->y + kywc_view->margin.off_y;
    } else if (output_at_layout_edge(output, LAYOUT_EDGE_BOTTOM) && uy2 - y1 < VIEW_BOTTOM_GAP) {
        *y = uy2 - VIEW_BOTTOM_GAP + kywc_view->margin.off_y;
    }

    /* constraints when moving to left and right */
    if (output_at_layout_edge(output, LAYOUT_EDGE_LEFT) && x2 < VIEW_LEFT_GAP) {
        *x = VIEW_LEFT_GAP - kywc_view->geometry.width -
             (kywc_view->margin.off_width - kywc_view->margin.off_x);
    } else if (output_at_layout_edge(output, LAYOUT_EDGE_RIGHT) && ux2 - x1 < VIEW_RIGHT_GAP) {
        *x = ux2 - VIEW_RIGHT_GAP + kywc_view->margin.off_y;
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

        if (grab->proxy) {
            int width = kywc_view->margin.off_width + saved->width;
            int height = kywc_view->margin.off_height + saved->height;
            move_proxy_resize(grab->proxy, width, height);
        }
    }

    int nx = grab->geo.x + x - grab->cursor_x;
    int ny = grab->geo.y + y - grab->cursor_y;
    window_move_constraints(&grab->view->base, grab->output, &nx, &ny);

    if (grab->proxy) {
        move_proxy_move(grab->proxy, nx, ny);
    } else {
        kywc_view_move(kywc_view, nx, ny);
    }

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

    int gap_x = EDGE_OFFSET, gap_y = EDGE_OFFSET;
    window_adsorb_window_constraints(kywc_view, box, &gap_x, &gap_y, grab->resize_edges,
                                     INTERACTIVE_MODE_RESIZE);

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

struct output *window_target_output_by_current_output(struct output *current, struct seat *seat,
                                                      enum layout_edge layout_edge)
{
    struct output *near_left = NULL, *far_left = NULL;
    struct output *near_right = NULL, *far_right = NULL;
    struct output *near_top = NULL, *far_top = NULL;
    struct output *near_bottom = NULL, *far_bottom = NULL;

    struct wlr_box box = { 0 };
    wlr_output_layout_get_box(seat->layout, NULL, &box);
    int min_left = box.width;
    int max_left = 0;
    int min_right = box.width;
    int max_right = 0;

    int min_top = box.height;
    int max_top = 0;
    int min_bottom = box.height;
    int max_bottom = 0;

    struct ky_scene_output *scene_output;
    wl_list_for_each(scene_output, &seat->scene->outputs, link) {
        struct output *tmp = output_from_wlr_output(scene_output->output);
        if (tmp == current) {
            continue;
        }
        int gap = tmp->geometry.x - current->geometry.x;
        /**
         * The nearest and farthest output on the left side of the X-axis is obtained.
         * If the distance is the same, the Y-axis is used to judge. The smaller the y,
         * the farther the output will be; the larger the y, the closer the output will be.
         */
        if (gap < 0) {
            gap = -gap;
            if (gap < min_left) {
                min_left = gap;
                near_left = tmp;
            }
            if (gap > max_left) {
                max_left = gap;
                far_left = tmp;
            }

            if (gap == min_left) {
                near_left = tmp->geometry.y > near_left->geometry.y ? tmp : near_left;
            }
            if (gap == max_left) {
                far_left = tmp->geometry.y < far_left->geometry.y ? tmp : far_left;
            }
            /**
             * The nearest and farthest output to the right of the X-axis is obtained.
             * If the distance is the same, the Y-axis is used to judge. The larger the y is,
             * the farther the output is; the smaller the y is, the closer the output is.
             */
        } else if (gap > 0) {
            if (gap < min_right) {
                min_right = gap;
                near_right = tmp;
            }
            if (gap > max_right) {
                max_right = gap;
                far_right = tmp;
            }

            if (gap == min_right) {
                near_right = tmp->geometry.y < near_right->geometry.y ? tmp : near_right;
            }
            if (gap == max_right) {
                far_right = tmp->geometry.y > far_right->geometry.y ? tmp : far_right;
            }
        } else if (gap == 0) {
            int gap_y = tmp->geometry.y - current->geometry.y;
            if (gap_y < 0) {
                gap_y = -gap_y;
                if (gap_y < min_top) {
                    min_top = gap_y;
                    near_top = tmp;
                }
                if (gap_y > max_top) {
                    max_top = gap_y;
                    far_top = tmp;
                }
            } else if (gap_y > 0) {
                if (min_bottom > gap_y) {
                    min_bottom = gap_y;
                    near_bottom = tmp;
                }
                if (max_bottom < gap_y) {
                    max_bottom = gap_y;
                    far_bottom = tmp;
                }
            }
        }
    }

    struct output *output = NULL;
    if (layout_edge == LAYOUT_EDGE_LEFT) {
        /* near_top > near_left > far_right > near_right > far_bottom > near_bottom */
        if (near_top) {
            output = near_top;
        } else if (near_left) {
            output = near_left;
        } else if (far_right) {
            output = far_right;
        } else if (near_right) {
            output = near_right;
        } else if (far_bottom) {
            output = far_bottom;
        } else if (near_bottom) {
            output = near_bottom;
        }
    } else if (layout_edge == LAYOUT_EDGE_RIGHT) {
        /* near_bottom > near_right > far_left > near_left > far_top > near_top */
        if (near_bottom) {
            output = near_bottom;
        } else if (near_right) {
            output = near_right;
        } else if (far_left) {
            output = far_left;
        } else if (near_left) {
            output = near_left;
        } else if (far_top) {
            output = far_top;
        } else if (near_top) {
            output = near_top;
        }
    }

    return output;
}

static void interactive_tile_output_update(struct interactive_grab *grab, int key)
{
    grab->output = output_from_kywc_output(grab->view->output);
    if (wl_list_length(&grab->seat->scene->outputs) == 1) {
        return;
    }

    enum layout_edge layout_edge = LAYOUT_EDGE_TOP;
    if (key == KEY_LEFT) {
        if (grab->current == TILE_LEFT || grab->current == TILE_TOP_LEFT ||
            grab->current == TILE_BOTTOM_LEFT) {
            layout_edge = LAYOUT_EDGE_LEFT;
        }
    } else if (key == KEY_RIGHT) {
        if (grab->current == TILE_RIGHT || grab->current == TILE_TOP_RIGHT ||
            grab->current == TILE_BOTTOM_RIGHT) {
            layout_edge = LAYOUT_EDGE_RIGHT;
        }
    }

    if (layout_edge == LAYOUT_EDGE_TOP) {
        return;
    }

    struct output *output =
        window_target_output_by_current_output(grab->output, grab->seat, layout_edge);
    grab->output = output ? output : grab->output;
}

static void interactive_tile_update(struct interactive_grab *grab, struct output *output, int state)
{
    if (state == TILE_MINIMIZE) {
        kywc_view_set_minimized(&grab->view->base, true);
        return;
    }

    if (state == TILE_MAXIMIZE) {
        kywc_view_set_maximized(&grab->view->base, true, NULL);
        return;
    }

    /**
     * Processing when the original position of the saved view is
     * no longer on the current screen when the shortcut keys are tiled.
     */
    struct kywc_box *saved_geo = &grab->view->saved.geometry;
    int x1 = saved_geo->x - grab->view->base.margin.off_x;
    int y1 = saved_geo->y - grab->view->base.margin.off_y;
    int x2 = x1 + saved_geo->width + grab->view->base.margin.off_width;
    int y2 = y1 + saved_geo->height + grab->view->base.margin.off_height;
    struct kywc_box *usable_area = &output->usable_area;
    /* The original size of the view is completely not on the current screen. */
    if (x2 < usable_area->x || y2 < usable_area->y || x1 > usable_area->x + usable_area->width ||
        y1 > usable_area->y + usable_area->height) {
        saved_geo->x = usable_area->x + grab->save_x_ratio * usable_area->width;
        saved_geo->y = usable_area->y + grab->save_y_ratio * usable_area->height;
    } else {
        /* Part of the original size of the view is on the current screen. */
        if (x2 > usable_area->x + usable_area->width || x1 < usable_area->x) {
            saved_geo->x = usable_area->x + grab->view->base.margin.off_x;
        }
        if (y2 > usable_area->y + usable_area->height || y1 < usable_area->y) {
            saved_geo->y = usable_area->y + grab->view->base.margin.off_y;
        }
    }

    if (saved_geo->width + grab->view->base.margin.off_width > usable_area->width) {
        saved_geo->width = usable_area->width - grab->view->base.margin.off_width;
    }
    if (saved_geo->height + grab->view->base.margin.off_height > usable_area->height) {
        saved_geo->height = usable_area->height - grab->view->base.margin.off_height;
    }

    if (grab->view->base.maximized && !state) {
        kywc_view_set_maximized(&grab->view->base, false, NULL);
        return;
    }

    if (grab->view->base.minimized) {
        /* kywc_view_activate will call the minimize restore interface. */
        kywc_view_activate(&grab->view->base);
        seat_focus_surface(grab->view->base.focused_seat, grab->view->surface);
    } else {
        kywc_view_set_tiled(&grab->view->base, state, &output->base);
    }
}

static void interactive_process_tile(struct interactive_grab *grab, int key)
{
    if (key != KEY_UP && key != KEY_DOWN && key != KEY_LEFT && key != KEY_RIGHT) {
        return;
    }

    enum tile_state pending_state = TILE_NONE;
    if (key == KEY_UP) {
        pending_state = tile_states[grab->current].key_up;
    } else if (key == KEY_DOWN) {
        pending_state = tile_states[grab->current].key_down;
    } else if (key == KEY_LEFT) {
        pending_state = tile_states[grab->current].key_left;
    } else if (key == KEY_RIGHT) {
        pending_state = tile_states[grab->current].key_right;
    }

    interactive_tile_output_update(grab, key);
    interactive_tile_update(grab, grab->output, pending_state);
    grab->current = pending_state;
}

static void interactive_process_tile_half_screen(struct interactive_grab *grab, int key)
{
    if (grab->current == TILE_MINIMIZE) {
        return;
    }

    grab->output = output_from_kywc_output(grab->view->output);
    enum kywc_tile tile = KYWC_TILE_NONE;
    if (key == KEY_UP) {
        tile = KYWC_TILE_TOP;
    } else if (key == KEY_DOWN) {
        tile = KYWC_TILE_BOTTOM;
    } else if (key == KEY_LEFT) {
        tile = KYWC_TILE_LEFT;
    } else if (key == KEY_RIGHT) {
        tile = KYWC_TILE_RIGHT;
    }

    if (tile == KYWC_TILE_NONE) {
        return;
    }

    if (grab->view->base.tiled != tile) {
        kywc_view_set_tiled(&grab->view->base, tile, grab->view->output);
        grab->current = (enum tile_state)grab->view->base.tiled;
        return;
    }

    if (wl_list_length(&grab->seat->scene->outputs) == 1 || tile == KYWC_TILE_TOP ||
        tile == KYWC_TILE_BOTTOM) {
        return;
    }

    struct output *output = window_target_output_by_current_output(
        grab->output, grab->seat, tile == KYWC_TILE_LEFT ? LAYOUT_EDGE_LEFT : LAYOUT_EDGE_RIGHT);
    if (!output) {
        return;
    }

    tile = tile == KYWC_TILE_LEFT ? KYWC_TILE_RIGHT : KYWC_TILE_LEFT;
    kywc_view_set_tiled(&grab->view->base, tile, &output->base);

    grab->output = output;
    grab->current = (enum tile_state)grab->view->base.tiled;
}

static bool pointer_grab_motion(struct seat_pointer_grab *pointer_grab, uint32_t time, double lx,
                                double ly)
{
    struct interactive_grab *grab = pointer_grab->data;
    grab->output = input_current_output(grab->seat);

    if (grab->mode == INTERACTIVE_MODE_MOVE) {
        /* set moving cursor image if moving */
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

    if (!pressed && button == BTN_LEFT) {
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
    struct interactive_grab *grab = keyboard_grab->data;
    if (!pressed) {
        if (key != KEY_LEFTMETA && key != KEY_RIGHTMETA) {
            return true;
        }

        if (grab->mode == INTERACTIVE_MODE_TILE ||
            grab->mode == INTERACTIVE_MODE_TILE_HALF_SCREEN) {
            interactive_done(grab);
            /* TODO: thumbnail */
            return true;
        }
    }

    if (grab->mode == INTERACTIVE_MODE_TILE || grab->mode == INTERACTIVE_MODE_TILE_HALF_SCREEN) {
        if (!((WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT) ^ modifiers)) {
            interactive_process_tile_half_screen(grab, key);
        } else if (!(WLR_MODIFIER_LOGO ^ modifiers)) {
            interactive_process_tile(grab, key);
        }
        return true;
    }

    int step = grab->mode == INTERACTIVE_MODE_MOVE ? VIEW_MOVE_STEP : VIEW_RESIZE_STEP;
    int dx = key == KEY_RIGHT ? step : (key == KEY_LEFT ? -step : 0);
    int dy = key == KEY_DOWN ? step : (key == KEY_UP ? -step : 0);

    /* restore to the orig geometry */
    if (key == KEY_ESC) {
        struct kywc_view *kywc_view = &grab->view->base;
        struct kywc_box geo = grab->geo;
        interactive_done(grab);
        kywc_view_resize(kywc_view, &geo);
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
    bool first = !grab->ongoing;
    pointer_grab_motion(&grab->pointer_grab, time, lx, ly);

    if (first && grab->ongoing) {
        seat_reset_input_gesture(grab->seat);
    }

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

static void handle_move_proxy_destroy(struct wl_listener *listener, void *data)
{
    struct interactive_grab *grab = wl_container_of(listener, grab, proxy_destroy);
    wl_list_remove(&grab->proxy_destroy.link);
    grab->proxy = NULL;
}

static int interactive_tiled_key_by_action(int action)
{
    int key = 0;
    if (action == WINDOW_ACTION_TILE_TOP || action == WINDOW_ACTION_TILE_TOP_HALF_SCREEN) {
        key = KEY_UP;
    } else if (action == WINDOW_ACTION_TILE_BOTTOM ||
               action == WINDOW_ACTION_TILE_BOTTOM_HALF_SCREEN) {
        key = KEY_DOWN;
    } else if (action == WINDOW_ACTION_TILE_LEFT || action == WINDOW_ACTION_TILE_LEFT_HALF_SCREEN) {
        key = KEY_LEFT;
    } else if (action == WINDOW_ACTION_TILE_RIGHT ||
               action == WINDOW_ACTION_TILE_RIGHT_HALF_SCREEN) {
        key = KEY_RIGHT;
    }

    return key;
}

static void interactive_tiled_record_view_ratio(struct interactive_grab *grab)
{
    struct kywc_box *usable_area = &grab->output->usable_area;
    grab->save_x_ratio = 1.0 * (grab->view->saved.geometry.x - usable_area->x) / usable_area->width;
    grab->save_y_ratio =
        1.0 * (grab->view->saved.geometry.y - usable_area->y) / usable_area->height;
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

    /* set the default cursor */
    if (mode == INTERACTIVE_MODE_MOVE) {
        cursor_set_image(seat->cursor, CURSOR_DEFAULT);
        /* create a move proxy for move effect */
        int width = view->base.geometry.width + view->base.margin.off_width;
        int height = view->base.geometry.height + view->base.margin.off_height;
        grab->proxy = move_proxy_create(view, width, height);
        if (grab->proxy) {
            grab->proxy_destroy.notify = handle_move_proxy_destroy;
            move_proxy_add_destroy_listener(grab->proxy, &grab->proxy_destroy);
        }
    } else if (mode == INTERACTIVE_MODE_RESIZE) {
        cursor_set_resize_image(seat->cursor, edges);
    } else if (mode == INTERACTIVE_MODE_TILE) {
        if (grab->view->base.maximized) {
            grab->current = TILE_MAXIMIZE;
        } else {
            grab->current = grab->view->base.tiled < KYWC_TILE_CENTER
                                ? (enum tile_state)grab->view->base.tiled
                                : TILE_NONE;
        }

        int key = interactive_tiled_key_by_action(edges);
        interactive_process_tile(grab, key);
        interactive_tiled_record_view_ratio(grab);
    } else if (mode == INTERACTIVE_MODE_TILE_HALF_SCREEN) {
        int key = interactive_tiled_key_by_action(edges);
        interactive_process_tile_half_screen(grab, key);
        interactive_tiled_record_view_ratio(grab);
    }

    struct wl_event_loop *loop = wl_display_get_event_loop(seat->wlr_seat->display);
    grab->filter = wl_event_loop_add_timer(loop, handle_snap_box, grab);
}

void window_begin_move(struct view *view, struct seat *seat)
{
    if (!view_is_movable(view)) {
        return;
    }
    interactive_grab_add(view, INTERACTIVE_MODE_MOVE, 0, seat);
}

void window_begin_resize(struct view *view, uint32_t edges, struct seat *seat)
{
    if (!view_is_resizable(view) || view->base.maximized) {
        return;
    }
    interactive_grab_add(view, INTERACTIVE_MODE_RESIZE, edges, seat);
}

void window_begin_tile(struct view *view, uint32_t key, struct seat *seat)
{
    if (view->base.fullscreen) {
        return;
    }

    interactive_grab_add(view, INTERACTIVE_MODE_TILE, key, seat);
}

void window_begin_tile_half_screen(struct view *view, uint32_t key, struct seat *seat)
{
    if (view->base.fullscreen) {
        return;
    }

    interactive_grab_add(view, INTERACTIVE_MODE_TILE_HALF_SCREEN, key, seat);
}