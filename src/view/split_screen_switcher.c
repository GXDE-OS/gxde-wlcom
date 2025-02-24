// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wlr/types/wlr_seat.h>

#include "input/cursor.h"
#include "output.h"
#include "scene/decoration.h"
#include "theme.h"
#include "view_p.h"

enum split_screen_mode {
    LAYOUT_STATE_HALF_SCREEN = 0,
    LAYOUT_STATE_QUARTER_SCREEN,
    LAYOUT_STATE_COUNT,
};

struct split_screen_item {
    struct split_screen_box *box;
    struct kywc_view *kywc_view;

    enum kywc_tile item_type;
    struct ky_scene_rect *rect;

    struct wl_list link;
};

struct split_screen_box {
    struct split_screen_switcher *parent;

    enum split_screen_mode mode;
    struct ky_scene_tree *tree;
    struct ky_scene_decoration *deco;
    struct ky_scene_rect *rect;

    struct wl_list items;
    struct wl_list link;
};

struct split_screen_switcher {
    struct wl_list link;

    struct view *view;
    struct seat *seat;
    struct wl_listener seat_destroy;
    struct wl_listener hovered_view_unmap;

    struct ky_scene_tree *tree;
    struct ky_scene_decoration *deco;
    struct ky_scene_rect *rect;
    int width, height;
    struct wl_list split_screen_boxs;

    struct wl_event_source *timer;
    bool timer_triggered, timer_for_hidden;
};

static struct split_screen_switcher_manager {
    struct wl_list split_screen_switchers;
    struct wl_listener server_destroy;
} *manager = NULL;

static struct ky_scene_rect *split_screen_box_create_rect(struct ky_scene_tree *parent, int w,
                                                          int h, int radius,
                                                          const float color[static 4])
{
    struct ky_scene_rect *preview_area = ky_scene_rect_create(parent, w, h, color);
    ky_scene_node_set_radius(&preview_area->node, (int[4]){ radius, radius, radius, radius });

    return preview_area;
}

static struct ky_scene_decoration *split_screen_box_create_deco(struct ky_scene_tree *parent, int w,
                                                                int h, int border, int radius)
{
    struct ky_scene_decoration *frame = ky_scene_decoration_create(parent);
    ky_scene_decoration_set_margin_color(frame, (float[4]){ 0.f, 0.f, 0.f, 0.f },
                                         (float[4]){ 0, 0, 0, 0.1 },
                                         (float[4]){ 0.f, 0.f, 0.f, 0.f });
    ky_scene_decoration_set_surface_size(frame, w, h);
    ky_scene_decoration_set_margin(frame, 0, border, 0, 0, 0);
    ky_scene_decoration_set_shadow_mask(frame, SHADOW_MASK_ALL);
    ky_scene_decoration_set_round_corner_radius(frame, (int[4]){ radius, radius, radius, radius });
    ky_scene_decoration_set_surface_blurred(frame, true);
    return frame;
}

static bool split_screen_item_hover(struct seat *seat, struct ky_scene_node *node, double x,
                                    double y, uint32_t time, bool first, bool hold, void *data)
{
    struct split_screen_item *item = data;

    /* set split_screen_switcher background */
    float bg[4] = { 255, 255, 255, 1 };
    ky_scene_rect_set_color(item->box->parent->rect, bg);

    float color[4] = {
        (float)((0xDCDCDC >> 16) & 0XFF) / 255,
        (float)((0xDCDCDC >> 8) & 0XFF) / 255,
        (float)(0xDCDCDC & 0XFF) / 255,
        1.0,
    };
    ky_scene_rect_set_color(item->rect, color);

    ky_scene_node_set_enabled(&item->box->parent->tree->node, true);

    return false;
}

static void split_screen_item_leave(struct seat *seat, struct ky_scene_node *node, bool last,
                                    void *data)
{
    struct split_screen_item *item = data;
    float color[4] = {
        (float)((0xE6E6E6 >> 16) & 0XFF) / 255,
        (float)((0xE6E6E6 >> 8) & 0XFF) / 255,
        (float)(0xE6E6E6 & 0XFF) / 255,
        1.0,
    };
    ky_scene_rect_set_color(item->rect, color);
}

static void split_screen_item_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                                    bool pressed, uint32_t time, enum click_state state, void *data)
{
    struct split_screen_item *item = data;

    float color[4] = {
        (float)((0xB9B9B9 >> 16) & 0XFF) / 255,
        (float)((0xB9B9B9 >> 8) & 0XFF) / 255,
        (float)(0xB9B9B9 & 0XFF) / 255,
        1.0,
    };
    ky_scene_rect_set_color(item->rect, color);

    if (!pressed) {
        struct view *view = view_from_kywc_view(item->kywc_view);
        struct output *output = output_from_kywc_output(view->output);
        view_begin_tile_preview(view, seat, output, item->item_type);
    }

    /* restore to initial color */
    float color_normal[4] = {
        (float)((0xE6E6E6 >> 16) & 0XFF) / 255,
        (float)((0xE6E6E6 >> 8) & 0XFF) / 255,
        (float)(0xE6E6E6 & 0XFF) / 255,
        1.0,
    };
    ky_scene_rect_set_color(item->rect, color_normal);
    ky_scene_node_set_enabled(&item->box->parent->tree->node, false);
}

static struct ky_scene_node *get_split_screen_item_root(void *data)
{
    struct split_screen_item *item = data;
    return &item->box->parent->tree->node;
}

static const struct input_event_node_impl split_screen_item_impl = {
    .hover = split_screen_item_hover,
    .leave = split_screen_item_leave,
    .click = split_screen_item_click,
};

static void split_screen_item_set_position(struct split_screen_item *item)
{
    switch (item->item_type) {
    case KYWC_TILE_LEFT:
        ky_scene_node_set_position(&item->rect->node, 5, 5);
        break;
    case KYWC_TILE_RIGHT:
        ky_scene_node_set_position(&item->rect->node, 47, 5);
        break;
    case KYWC_TILE_TOP_LEFT:
        ky_scene_node_set_position(&item->rect->node, 5, 5);
        break;
    case KYWC_TILE_BOTTOM_LEFT:
        ky_scene_node_set_position(&item->rect->node, 5, 34);
        break;
    case KYWC_TILE_TOP_RIGHT:
        ky_scene_node_set_position(&item->rect->node, 47, 5);
        break;
    case KYWC_TILE_BOTTOM_RIGHT:
        ky_scene_node_set_position(&item->rect->node, 47, 34);
        break;
    default:
        break;
    }
}

static struct split_screen_item *split_screen_item_create(struct split_screen_box *box,
                                                          struct view *view, int item_type)
{
    struct split_screen_item *item = calloc(1, sizeof(*item));
    if (!item) {
        return NULL;
    }

    item->kywc_view = &view->base;
    item->item_type = item_type;
    item->box = box;

    float color[4] = {
        (float)((0xE6E6E6 >> 16) & 0XFF) / 255,
        (float)((0xE6E6E6 >> 8) & 0XFF) / 255,
        (float)(0xE6E6E6 & 0XFF) / 255,
        1.0,
    };
    if (item_type >= KYWC_TILE_TOP_LEFT) {
        item->rect = split_screen_box_create_rect(box->tree, 40, 27, 4, color);
    } else {
        item->rect = split_screen_box_create_rect(box->tree, 40, 56, 4, color);
    }

    split_screen_item_set_position(item);
    input_event_node_create(&item->rect->node, &split_screen_item_impl, &get_split_screen_item_root,
                            NULL, item);
    return item;
}

static struct split_screen_box *split_screen_box_create(struct split_screen_switcher *parent,
                                                        struct view *view, int mode)
{
    struct split_screen_box *box = calloc(1, sizeof(*box));
    if (!box) {
        return NULL;
    }

    float color[4] = {
        (float)((0xF6F6F6 >> 16) & 0XFF) / 255,
        (float)((0xF6F6F6 >> 8) & 0XFF) / 255,
        (float)(0xF6F6F6 & 0XFF) / 255,
        1.0,
    };
    box->mode = mode;
    box->parent = parent;
    box->tree = ky_scene_tree_create(parent->tree);
    box->deco = split_screen_box_create_deco(box->tree, 90, 64, 1, 6);
    box->rect = split_screen_box_create_rect(box->tree, 90, 64, 6, color);

    int count = 0;
    if (mode == LAYOUT_STATE_HALF_SCREEN) {
        count = 2;
    } else if (mode == LAYOUT_STATE_QUARTER_SCREEN) {
        count = 4;
    }

    wl_list_init(&box->items);
    for (int i = 0; i < count; i++) {
        ky_scene_node_set_position(ky_scene_node_from_decoration(box->deco), 0, 0);
        ky_scene_node_set_position(&box->rect->node, 1, 1);
        if (mode == LAYOUT_STATE_HALF_SCREEN) {
            struct split_screen_item *item =
                split_screen_item_create(box, view, i + KYWC_TILE_LEFT);
            wl_list_insert(&box->items, &item->link);
        } else if (mode == LAYOUT_STATE_QUARTER_SCREEN) {
            struct split_screen_item *item =
                split_screen_item_create(box, view, i + KYWC_TILE_TOP_LEFT);
            wl_list_insert(&box->items, &item->link);
        }
    }
    return box;
}

static void split_screen_switcher_handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct split_screen_switcher *split_screen_switcher =
        wl_container_of(listener, split_screen_switcher, seat_destroy);
    wl_list_remove(&split_screen_switcher->seat_destroy.link);
    wl_list_remove(&split_screen_switcher->hovered_view_unmap.link);
    wl_list_remove(&split_screen_switcher->link);

    struct split_screen_box *box, *box_tmp;
    wl_list_for_each_safe(box, box_tmp, &split_screen_switcher->split_screen_boxs, link) {
        struct split_screen_item *item, *item_tmp;
        wl_list_for_each_safe(item, item_tmp, &box->items, link) {
            ky_scene_node_destroy(&item->rect->node);
            free(item);
        }
        ky_scene_node_destroy(&box->tree->node);
        free(box);
    }

    ky_scene_node_destroy(&split_screen_switcher->tree->node);

    wl_event_source_remove(split_screen_switcher->timer);
    free(split_screen_switcher);
}

static bool split_screen_box_hover(struct seat *seat, struct ky_scene_node *node, double x,
                                   double y, uint32_t time, bool first, bool hold, void *data)
{
    struct split_screen_switcher *box = data;

    float bg[4] = { 255, 255, 255, 1 };
    ky_scene_rect_set_color(box->rect, bg);
    ky_scene_node_set_enabled(&box->tree->node, true);
    return false;
}

static void split_screen_box_leave(struct seat *seat, struct ky_scene_node *node, bool last,
                                   void *data)
{
    struct split_screen_switcher *box = data;

    float bg[4] = { 255, 255, 255, 0 };
    ky_scene_rect_set_color(box->rect, bg);
    ky_scene_node_set_enabled(&box->tree->node, false);
}

static void split_screen_box_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                                   bool pressed, uint32_t time, enum click_state state, void *data)
{
}

static struct ky_scene_node *get_split_screen_box_root(void *data)
{
    struct split_screen_switcher *box = data;
    return &box->tree->node;
}

static const struct input_event_node_impl split_screen_box_impl = {
    .hover = split_screen_box_hover,
    .leave = split_screen_box_leave,
    .click = split_screen_box_click,
};

static void split_screen_switcher_handle_hoverd_view_unmap(struct wl_listener *listener, void *data)
{
    struct split_screen_switcher *box = wl_container_of(listener, box, hovered_view_unmap);

    wl_list_remove(&box->hovered_view_unmap.link);
    wl_list_init(&box->hovered_view_unmap.link);

    split_screen_switcher_show(box->view, box->seat, false);
}

static int handle_split_screen_box(void *data)
{
    struct split_screen_switcher *box = data;
    box->timer_triggered = true;
    split_screen_switcher_show(box->view, box->seat, !box->timer_for_hidden);
    return 0;
}

static struct split_screen_switcher *split_screen_switcher_create(struct seat *seat,
                                                                  struct view *view)
{
    struct split_screen_switcher *box = calloc(1, sizeof(*box));
    if (!box) {
        return NULL;
    }

    box->view = view;
    box->seat = seat;
    box->seat_destroy.notify = split_screen_switcher_handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &box->seat_destroy);
    box->hovered_view_unmap.notify = split_screen_switcher_handle_hoverd_view_unmap;
    wl_list_init(&box->hovered_view_unmap.link);

    struct view_layer *layer = view_manager_get_layer(LAYER_POPUP, false);
    box->tree = ky_scene_tree_create(layer->tree);
    box->deco = split_screen_box_create_deco(box->tree, 204, 80, 1, 10);
    box->rect =
        split_screen_box_create_rect(box->tree, 204, 80, 10, (float[4]){ 255, 255, 255, 0 });
    box->width = 204;
    box->height = 80;

    ky_scene_node_set_position(ky_scene_node_from_decoration(box->deco), 0, 0);
    ky_scene_node_set_position(&box->rect->node, 1, 1);

    wl_list_init(&box->split_screen_boxs);
    for (int i = 0; i < LAYOUT_STATE_COUNT; i++) {
        struct split_screen_box *split_screen_box =
            split_screen_box_create(box, view, i + LAYOUT_STATE_HALF_SCREEN);
        ky_scene_node_set_position(&split_screen_box->tree->node, 8 + i * 98, 8);
        wl_list_insert(&box->split_screen_boxs, &split_screen_box->link);
    }

    input_event_node_create(&box->tree->node, &split_screen_box_impl, &get_split_screen_box_root,
                            NULL, box);
    ky_scene_node_set_enabled(&box->tree->node, false);
    wl_list_insert(&manager->split_screen_switchers, &box->link);

    struct wl_event_loop *loop = wl_display_get_event_loop(seat->wlr_seat->display);
    box->timer = wl_event_loop_add_timer(loop, handle_split_screen_box, box);

    return box;
}

static struct split_screen_switcher *split_screen_switcher_from_seat(struct seat *seat,
                                                                     struct view *view)
{
    struct split_screen_switcher *box;
    wl_list_for_each(box, &manager->split_screen_switchers, link) {
        if (box->seat == seat) {
            return box;
        }
    }
    return split_screen_switcher_create(seat, view);
}

void split_screen_switcher_show(struct view *view, struct seat *seat, bool enabled)
{
    if (!manager) {
        return;
    }

    struct split_screen_switcher *box = split_screen_switcher_from_seat(seat, view);
    if (!box) {
        return;
    }

    box->view = view;
    struct split_screen_box *split_screen_box;
    wl_list_for_each(split_screen_box, &box->split_screen_boxs, link) {
        struct split_screen_item *item;
        wl_list_for_each(item, &split_screen_box->items, link) {
            item->kywc_view = &view->base;
        }
    }

    struct theme *theme = theme_manager_get_theme();

    if (!enabled) {
        wl_event_source_timer_update(box->timer, 0);
        box->timer_triggered = false;
        box->timer_for_hidden = false;

        wl_list_remove(&box->hovered_view_unmap.link);
        wl_list_init(&box->hovered_view_unmap.link);
        ky_scene_node_set_enabled(&box->tree->node, false);
        return;
    }

    if (!box->timer_triggered) {
        wl_event_source_timer_update(box->timer, 500);
        box->timer_for_hidden = false;

        wl_list_remove(&box->hovered_view_unmap.link);
        wl_signal_add(&view->base.events.unmap, &box->hovered_view_unmap);
        return;
    }

    int x = seat->cursor->lx;
    if (x > (2 * theme->icon_size))
        x = x - 2 * theme->icon_size;

    struct output *output = input_current_output(seat);
    int max_x = output->geometry.x + output->geometry.width;
    if (x + box->width > max_x) {
        x = max_x - box->width;
    }

    int y = view->base.ssd & KYWC_SSD_TITLE ? view->base.geometry.y - 6
                                            : view->base.geometry.y + theme->button_width + 2;
    ky_scene_node_set_position(&box->tree->node, x, y);
    ky_scene_node_set_enabled(&box->tree->node, true);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

bool split_screen_switcher_manager_create(struct view_manager *view_manager)
{
    manager = calloc(1, sizeof(struct split_screen_switcher_manager));
    if (!manager) {
        return false;
    }

    wl_list_init(&manager->split_screen_switchers);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &manager->server_destroy);

    return true;
}