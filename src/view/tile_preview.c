// SPDX-FileCopyrightText: 2025 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include <linux/input-event-codes.h>
#include <pixman.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "painter.h"
#include "scene/decoration.h"
#include "scene/thumbnail.h"
#include "theme.h"
#include "view/workspace.h"
#include "view_p.h"
#include "widget/scaled_buffer.h"
#include "widget/widget.h"

#define GAP 16
#define ITEM_HEIGHT 40
#define SELECT_BORDER_WIDTH 2
#define MAX_SPLIT_SCREEN 4

enum split_state {
    HALF_SCREEN = 0,
    QUARTER_SCREEN,
};

enum button_state {
    BUTTON_STATE_NONE = 0,
    BUTTON_STATE_HOVER,
    BUTTON_STATE_CLICKED,
};

enum preview_part {
    PREVIEW_ROOT = 0,
    PREVIEW_FRAME_RECT,
    PREVIEW_BUTTON_CLOSE,
    PREVIEW_TITLE_ICON,
    PREVIEW_TITLE_TEXT,
    PREVIEW_PART_COUNT,
};

struct tile_preview_color {
    float background_color[4];
    float font_color[4];
    float select_color[4];
};

static struct tile_preview_color light = {
    .background_color = { 207.0 / 255.0, 207.0 / 255.0, 207.0 / 255.0, 0.25 },
    .font_color = { 0, 0, 0, 1.0 },
    .select_color = { 0, 0, 0, 1.0 },
};

static struct tile_preview_color dark = {
    .background_color = { 54.0 / 255, 54.0 / 255, 54.0 / 255, 0.25 },
    .font_color = { 1.0, 1.0, 1.0, 1.0 },
    .select_color = { 1.0, 1.0, 1.0, 1.0 },
};

struct item_part {
    struct item *item;
    struct ky_scene_node *node;
    enum preview_part part;
    float scale;
};

struct item {
    struct wl_list link;
    struct ky_scene_tree *tree;

    struct widget *title_text;
    struct item_part part[PREVIEW_PART_COUNT];

    struct view *view;
    struct wl_listener view_unmap;

    int32_t max_text_width;
    int32_t text_height;
};

struct preview {
    struct view *view;
    struct wl_listener view_unmap;

    struct ky_scene_tree *tree;

    struct kywc_box box;

    enum kywc_tile tiled;
    int32_t index_first, current;
    int32_t total, row, col;
};

static struct tile_preview_manager {
    struct wl_list items;
    struct ky_scene_tree *tree;

    struct seat *seat;
    struct output *output;
    struct tile_preview_color *color;

    struct preview preview[MAX_SPLIT_SCREEN];
    struct preview *item_page;
    struct view *active_view;

    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;
    struct seat_touch_grab touch_grab;

    struct wl_listener new_mapped_view;
    struct wl_listener configured_output;
    struct wl_listener seat_destroy;

    enum split_state state;

    int32_t total_items;
    int32_t fixed_width;  // (usable_width - 160) / 4
    int32_t fixed_height; // usable_height / 22 * 5
} *manager = NULL;

static void title_text_update(struct item *item)
{
    struct theme *theme = theme_manager_get_theme();
    widget_set_text(item->title_text, item->view->base.title, JUSTIFY_CENTER, TEXT_ATTR_NONE);

    widget_set_font(item->title_text, theme->font_name, theme->font_size);
    widget_set_front_color(item->title_text, theme->active_text_color);

    widget_set_max_size(item->title_text, item->max_text_width, ITEM_HEIGHT);
    widget_set_auto_resize(item->title_text, AUTO_RESIZE_ONLY);

    widget_set_enabled(item->title_text, true);
    widget_update(item->title_text, true);

    int32_t text_width, text_height;
    widget_get_size(item->title_text, &text_width, &text_height);

    item->text_height = text_height;
}

static void icon_buffer_update(struct item_part *part, float scale)
{
    struct wlr_buffer *buf = view_get_icon_buffer(part->item->view, scale);
    if (!buf) {
        return;
    }

    struct ky_scene_buffer *icon_buffer = ky_scene_buffer_from_node(part->node);
    if (icon_buffer->buffer != buf) {
        ky_scene_buffer_set_buffer(icon_buffer, buf);
    }
    if (icon_buffer->buffer != buf) {
        return;
    }

    int width, height;
    painter_buffer_get_dest_size(buf, &width, &height);
    ky_scene_buffer_set_dest_size(icon_buffer, width, height);
}

static void set_close_buffer(struct item_part *part, enum button_state state)
{
    enum theme_buffer_type type = THEME_BUFFER_TYPE_BUTTON_CLOSE;
    struct theme *theme = theme_manager_get_theme();
    /* get actual type by current state */
    type += state * 4;

    struct wlr_fbox src;
    struct wlr_buffer *buf = theme_buffer_load(theme, part->scale, type, &src);
    struct ky_scene_buffer *buffer = ky_scene_buffer_from_node(part->node);
    if (buffer->buffer != buf) {
        ky_scene_buffer_set_buffer(buffer, buf);
    }
    /* shortcut here if set_buffer triggered scaled buffer update */
    if (buffer->buffer != buf) {
        return;
    }

    ky_scene_buffer_set_dest_size(buffer, theme->button_width, theme->button_width);
    ky_scene_buffer_set_source_box(buffer, &src);
}

static void item_buffer_update(struct ky_scene_buffer *buffer, float scale, void *data)
{
    struct item_part *item_part = data;
    item_part->scale = scale;
    if (item_part->part == PREVIEW_BUTTON_CLOSE) {
        set_close_buffer(item_part, BUTTON_STATE_NONE);
    } else if (item_part->part == PREVIEW_TITLE_ICON) {
        icon_buffer_update(item_part, scale);
    }
}

static void item_buffer_destroy(struct ky_scene_buffer *buffer, void *data)
{
    /* buffers are destroyed in theme */
}

static void preview_item_show(void)
{
    struct item *item;
    wl_list_for_each(item, &manager->items, link) {
        if (!item->view->base.minimized) {
            ky_scene_node_set_enabled(&item->view->tree->node, false);
        }
        if (!item->tree) {
            continue;
        }

        ky_scene_node_set_enabled(&item->tree->node, false);
    }

    int32_t i = 0, col = 0;
    int32_t node_x = 0;
    struct preview *preview = manager->item_page;
    int32_t node_y = (preview->box.height % manager->fixed_height) / 2;

    wl_list_for_each(item, &manager->items, link) {
        if (!item->tree) {
            continue;
        }

        if (i < preview->index_first) {
            i++;
            continue;
        }

        if (i == preview->index_first + preview->total) {
            break;
        }

        if (col != 0 && col % preview->col == 0) {
            node_y += manager->fixed_height;
        }

        node_x = (preview->box.width % manager->fixed_width) / 2 +
                 (col % preview->col) * manager->fixed_width;
        col++;

        ky_scene_node_set_position(&item->tree->node, node_x, node_y);
        ky_scene_node_set_enabled(&item->tree->node, true);

        i++;
    }
}

static void item_destory(struct item *item)
{
    wl_list_remove(&item->link);
    wl_list_remove(&item->view_unmap.link);

    if (item->tree) {
        manager->total_items--;
        ky_scene_node_set_enabled(&item->tree->node, false);
        ky_scene_node_destroy(&item->tree->node);
    }

    free(item);
}

static void handle_view_unmap(struct wl_listener *listener, void *data)
{
    struct item *item = wl_container_of(listener, item, view_unmap);
    item_destory(item);
    preview_item_show();
}

static struct item *item_create(struct view *view, bool flip)
{
    struct item *item = calloc(1, sizeof(struct item));
    if (!item) {
        return NULL;
    }

    /* The newly opened view is inserted behind the linked list. */
    wl_list_insert(flip ? &manager->items : manager->items.prev, &item->link);

    item->view = view;
    item->tree = NULL;
    item->view_unmap.notify = handle_view_unmap;
    wl_signal_add(&item->view->base.events.unmap, &item->view_unmap);

    return item;
}

static void tile_preview_done(void)
{
    struct item *item, *item_tmp;
    wl_list_for_each_safe(item, item_tmp, &manager->items, link) {
        if (!item->view->base.minimized) {
            ky_scene_node_set_enabled(&item->view->tree->node, true);
        }
        item_destory(item);
    }

    for (int32_t i = 0; i < MAX_SPLIT_SCREEN; i++) {
        struct preview *preview = &manager->preview[i];
        if (preview->view) {
            wl_list_remove(&preview->view_unmap.link);
            struct kywc_view *view = &preview->view->base;
            struct kywc_box pending = { 0 };
            view_get_tiled_geometry(preview->view, &pending, preview->view->output, view->tiled);
            kywc_view_resize(view, &pending);
        }
        if (preview->tree) {
            ky_scene_node_destroy(&preview->tree->node);
            manager->preview[i].tree = NULL;
        }

        preview->view = NULL;
    }

    if (manager->active_view) {
        kywc_view_activate(&manager->active_view->base);
        seat_focus_surface(manager->active_view->base.focused_seat, manager->active_view->surface);
    }

    wl_list_remove(&manager->new_mapped_view.link);
    wl_list_remove(&manager->configured_output.link);
    wl_list_remove(&manager->seat_destroy.link);

    seat_end_pointer_grab(manager->seat, &manager->pointer_grab);
    seat_end_keyboard_grab(manager->seat, &manager->keyboard_grab);
    seat_end_touch_grab(manager->seat, &manager->touch_grab);

    ky_scene_node_destroy(&manager->tree->node);
    free(manager);
    manager = NULL;
}

static void set_area(struct kywc_box *result, int x, int y, int width, int height)
{
    result->x = x;
    result->y = y;
    result->width = width;
    result->height = height;
}

static void tile_size(struct view *view, struct kywc_box *pending, enum kywc_tile tiled)
{
    struct kywc_box *usable = &manager->output->usable_area;
    int32_t half_width = usable->width * 0.5;
    int32_t half_height = usable->height * 0.5;
    int32_t half_gap = GAP * 0.5;
    int32_t x, y;

    if (tiled == KYWC_TILE_LEFT || tiled == KYWC_TILE_RIGHT) {
        x = tiled == KYWC_TILE_LEFT ? 0 : half_width + half_gap;
        set_area(pending, x, 0, half_width - half_gap, usable->height);
    } else if (tiled == KYWC_TILE_TOP_LEFT || tiled == KYWC_TILE_TOP_RIGHT) {
        x = tiled == KYWC_TILE_TOP_LEFT ? 0 : half_width + half_gap;
        y = 0;
        set_area(pending, x, y, half_width - half_gap, half_height - half_gap);
    } else if (tiled == KYWC_TILE_BOTTOM_LEFT || tiled == KYWC_TILE_BOTTOM_RIGHT) {
        x = tiled == KYWC_TILE_BOTTOM_LEFT ? 0 : half_width + half_gap;
        y = half_height + half_gap;
        set_area(pending, x, y, half_width - half_gap, half_height - half_gap);
    }

    struct kywc_view *ky_view = &view->base;
    pending->x += usable->x + ky_view->margin.off_x;
    pending->y += usable->y + ky_view->margin.off_y;
    pending->width -= ky_view->margin.off_width;
    pending->height -= ky_view->margin.off_height;
}

static void item_page_preview_update(void)
{
    int split = manager->state == HALF_SCREEN ? 2 : 4;
    for (int32_t i = 0; i < split; i++) {
        if (!manager->preview[i].view) {
            manager->item_page = &manager->preview[i];
            return;
        }
    }

    manager->item_page = NULL;
}

static void item_page_update(void)
{
    item_page_preview_update();

    if (!manager->item_page) {
        tile_preview_done();
        return;
    }

    struct item *item;
    wl_list_for_each(item, &manager->items, link) {
        if (!item->tree) {
            continue;
        }
        ky_scene_node_reparent(&item->tree->node, manager->item_page->tree);
    }

    preview_item_show();
}

static void handle_tiled_view_unmap(struct wl_listener *listener, void *data)
{
    struct preview *preview = wl_container_of(listener, preview, view_unmap);
    if (preview->view == manager->active_view) {
        manager->active_view = NULL;
    }

    preview->view = NULL;
    wl_list_remove(&preview->view_unmap.link);

    ky_scene_node_set_enabled(&preview->tree->node, true);
    item_page_update();
}

static void preview_area_set_view(struct preview *preview, struct view *view)
{
    preview->view = view;
    preview->view_unmap.notify = handle_tiled_view_unmap;
    wl_signal_add(&preview->view->base.events.unmap, &preview->view_unmap);
    ky_scene_node_set_enabled(&preview->tree->node, false);
}

static void pointer_grab_cancel(struct seat_pointer_grab *pointer_grab) {}

static bool pointer_grab_button(struct seat_pointer_grab *pointer_grab, uint32_t time,
                                uint32_t button, bool pressed)
{
    if (pressed) {
        tile_preview_done();
    }

    return true;
}

static bool pointer_grab_motion(struct seat_pointer_grab *pointer_grab, uint32_t time, double lx,
                                double ly)
{
    return false;
}

static bool pointer_grab_axis(struct seat_pointer_grab *pointer_grab, uint32_t time, bool vertical,
                              double value)
{
    return true;
}

static const struct seat_pointer_grab_interface pointer_grab_impl = {
    .motion = pointer_grab_motion,
    .button = pointer_grab_button,
    .axis = pointer_grab_axis,
    .cancel = pointer_grab_cancel,
};

static bool touch_grab_touch(struct seat_touch_grab *touch_grab, uint32_t time, bool down)
{
    // FIXME: interactive grab end
    return pointer_grab_button(&manager->pointer_grab, time, BTN_LEFT, down);
}

static bool touch_grab_motion(struct seat_touch_grab *touch_grab, uint32_t time, double lx,
                              double ly)
{
    return false;
}

static void touch_grab_cancel(struct seat_touch_grab *touch_grab) {}

static const struct seat_touch_grab_interface touch_grab_impl = {
    .touch = touch_grab_touch,
    .motion = touch_grab_motion,
    .cancel = touch_grab_cancel,
};

static bool keyboard_grab_key(struct seat_keyboard_grab *keyboard_grab, struct keyboard *keyboard,
                              uint32_t time, uint32_t key, bool pressed, uint32_t modifiers)
{
    return true;
}

static void keyboard_grab_cancel(struct seat_keyboard_grab *keyboard_grab) {}

static const struct seat_keyboard_grab_interface keyboard_grab_impl = {
    .key = keyboard_grab_key,
    .cancel = keyboard_grab_cancel,
};

static bool item_init(struct item *item, int gap, int border)
{
    int32_t item_w = manager->fixed_width;
    int32_t item_h = manager->fixed_height;
    struct theme *theme = theme_manager_get_theme();
    item->tree = ky_scene_tree_create(manager->item_page->tree);
    manager->total_items++;

    for (int32_t i = PREVIEW_ROOT; i < PREVIEW_PART_COUNT; i++) {
        item->part[i].part = i;
        item->part[i].item = item;
        item->part[i].scale = 1.0;
        int32_t x = 0, y = 0;
        if (i == PREVIEW_ROOT) {
            item->part[i].node = &item->tree->node;
        } else if (i == PREVIEW_BUTTON_CLOSE || i == PREVIEW_TITLE_ICON) {
            struct ky_scene_buffer *buf =
                scaled_buffer_create(item->tree, item->part[i].scale, item_buffer_update,
                                     item_buffer_destroy, &item->part[i]);
            item->part[i].node = &buf->node;

            if (i == PREVIEW_BUTTON_CLOSE) {
                set_close_buffer(&item->part[i], BUTTON_STATE_NONE);
                x = item_w - border - gap - theme->button_width;
                y = border + (ITEM_HEIGHT - border - theme->button_width) * 0.5;
            } else {
                icon_buffer_update(&item->part[i], item->part[i].scale);
                x = border + gap;
                y = border + (ITEM_HEIGHT - border - theme->icon_size) * 0.5;
            }
        } else if (i == PREVIEW_TITLE_TEXT) {
            item->title_text = widget_create(item->tree);
            item->part[i].node = ky_scene_node_from_widget(item->title_text);
            item->max_text_width =
                item_w - 4 * gap - 2 * border - theme->icon_size - theme->button_width;
            title_text_update(item);
            x = border + theme->icon_size + 2 * gap;
            y = border + (ITEM_HEIGHT - item->text_height) * 0.5;
        } else if (i == PREVIEW_FRAME_RECT) {
            struct ky_scene_rect *background =
                ky_scene_rect_create(item->tree, item_w, item_h, (float[4]){ 0.f, 0.f, 0.f, 0.f });
            item->part[i].node = &background->node;
        }

        ky_scene_node_set_position(item->part[i].node, x, y);
    }

    return true;
}

static void preview_init(struct preview *preview, enum kywc_tile tile)
{
    preview->tiled = tile;
    preview->index_first = 0;
    preview->current = -1;
    preview->tree = ky_scene_tree_create(manager->tree);

    struct kywc_box *usable = &manager->output->usable_area;
    int32_t half_width = usable->width * 0.5;
    int32_t half_height = usable->height * 0.5;
    int32_t half_gap = GAP * 0.5;
    int32_t tmp_gap = GAP * 1.5;
    int32_t x, y;

    int32_t back_w = half_width + half_gap;
    int32_t back_h =
        tile == KYWC_TILE_LEFT || tile == KYWC_TILE_RIGHT ? usable->height : half_height + half_gap;
    struct ky_scene_rect *preview_area, *background;
    background =
        ky_scene_rect_create(preview->tree, back_w, back_h, (float[4]){ 0.f, 0.f, 0.f, 0.f });

    if (tile == KYWC_TILE_LEFT || tile == KYWC_TILE_RIGHT) {
        x = tile == KYWC_TILE_LEFT ? GAP : half_width + half_gap;
        set_area(&preview->box, x, GAP, half_width - tmp_gap, usable->height - 2 * GAP);
    } else if (tile == KYWC_TILE_TOP_LEFT || tile == KYWC_TILE_TOP_RIGHT) {
        x = tile == KYWC_TILE_TOP_LEFT ? GAP : half_width + half_gap;
        y = GAP;
        set_area(&preview->box, x, y, half_width - tmp_gap, half_height - tmp_gap);
    } else if (tile == KYWC_TILE_BOTTOM_LEFT || tile == KYWC_TILE_BOTTOM_RIGHT) {
        x = tile == KYWC_TILE_BOTTOM_LEFT ? GAP : half_width + half_gap;
        y = half_height + half_gap;
        set_area(&preview->box, x, y, half_width - tmp_gap, half_height - tmp_gap);
    }

    preview->box.x += usable->x;
    preview->box.y += usable->y;
    preview->row = preview->box.height / manager->fixed_height;
    preview->col = preview->box.width / manager->fixed_width;
    preview->total = preview->row * preview->col;

    preview_area = ky_scene_rect_create(preview->tree, preview->box.width, preview->box.height,
                                        manager->color->background_color);
    ky_scene_node_set_position(&preview->tree->node, preview->box.x, preview->box.y);
    ky_scene_node_set_position(&background->node, -GAP, -GAP);

    struct theme *theme = theme_manager_get_theme();
    int radius = theme->corner_radius;
    ky_scene_node_set_radius(&preview_area->node, (int[4]){ radius, radius, radius, radius });
    /* add blur */
    pixman_region32_t region;
    pixman_region32_init(&region);
    ky_scene_node_set_blur_region(&preview_area->node, theme->opacity != 100 ? &region : NULL);
}

static void create_preview_area(struct view *view, uint32_t tile)
{
    manager->state =
        tile == KYWC_TILE_LEFT || tile == KYWC_TILE_RIGHT ? HALF_SCREEN : QUARTER_SCREEN;
    if (manager->state == HALF_SCREEN) {
        preview_init(&manager->preview[0], KYWC_TILE_LEFT);
        preview_init(&manager->preview[1], KYWC_TILE_RIGHT);
    } else {
        preview_init(&manager->preview[0], KYWC_TILE_TOP_LEFT);
        preview_init(&manager->preview[1], KYWC_TILE_TOP_RIGHT);
        preview_init(&manager->preview[2], KYWC_TILE_BOTTOM_LEFT);
        preview_init(&manager->preview[3], KYWC_TILE_BOTTOM_RIGHT);
    }

    struct preview *preview = NULL;
    if (tile == KYWC_TILE_LEFT || tile == KYWC_TILE_RIGHT) {
        preview = tile == KYWC_TILE_LEFT ? &manager->preview[0] : &manager->preview[1];
    } else if (tile == KYWC_TILE_TOP_LEFT) {
        preview = &manager->preview[0];
    } else if (tile == KYWC_TILE_TOP_RIGHT) {
        preview = &manager->preview[1];
    } else if (tile == KYWC_TILE_BOTTOM_LEFT) {
        preview = &manager->preview[2];
    } else if (tile == KYWC_TILE_BOTTOM_RIGHT) {
        preview = &manager->preview[3];
    }

    preview_area_set_view(preview, view);
    manager->active_view = view;
}

static void handle_new_mapped_view(struct wl_listener *listener, void *data)
{
    struct kywc_view *kywc_view = data;
    if (kywc_view->role != KYWC_VIEW_ROLE_NORMAL) {
        return;
    }

    struct item *item = item_create(view_from_kywc_view(kywc_view), false);
    if (item && item_init(item, 8, SELECT_BORDER_WIDTH)) {
        preview_item_show();
    }
}

static void tiled_preview_show(struct view *view, uint32_t tile)
{
    create_preview_area(view, tile);
    item_page_preview_update();

    if (!manager->item_page) {
        tile_preview_done();
        return;
    }

    struct view_proxy *view_proxy;
    struct workspace *workspace = workspace_manager_get_current();
    wl_list_for_each_reverse(view_proxy, &workspace->view_proxies, workspace_link) {
        if (!view_proxy->view->base.mapped || view_proxy->view->base.skip_taskbar ||
            (view_proxy->view->output != &manager->output->base) || view == view_proxy->view) {
            continue;
        }

        struct item *item = item_create(view_proxy->view, true);
        if (item) {
            item_init(item, 8, SELECT_BORDER_WIDTH);
        }
    }

    struct kywc_box pending = { 0 };
    tile_size(view, &pending, view->base.tiled);
    kywc_view_resize(&view->base, &pending);
    ky_scene_node_set_enabled(&manager->tree->node, true);

    preview_item_show();
}

static void handle_configured_output(struct wl_listener *listener, void *data)
{
    tile_preview_done();
}

static void handle_seat_destroy(struct wl_listener *listener, void *data)
{
    tile_preview_done();
}

static void tiled_preview_add(struct view *view, struct seat *seat, struct output *output,
                              uint32_t tile)
{
    manager = calloc(1, sizeof(struct tile_preview_manager));
    if (!manager) {
        return;
    }

    wl_list_init(&manager->items);

    struct theme *theme = theme_manager_get_theme();
    switch (theme->type) {
    case THEME_TYPE_UNDEFINED:
    case THEME_TYPE_LIGHT:
        manager->color = &light;
        break;
    case THEME_TYPE_DARK:
        manager->color = &dark;
        break;
    }

    manager->seat = seat;
    manager->pointer_grab = (struct seat_pointer_grab){ &pointer_grab_impl, seat, manager };
    seat_start_pointer_grab(seat, &manager->pointer_grab);
    manager->keyboard_grab = (struct seat_keyboard_grab){ &keyboard_grab_impl, seat, manager };
    seat_start_keyboard_grab(seat, &manager->keyboard_grab);
    manager->touch_grab = (struct seat_touch_grab){ &touch_grab_impl, seat, manager };
    seat_start_touch_grab(seat, &manager->touch_grab);

    struct kywc_box *usable_area = &output->usable_area;
    manager->fixed_width = (usable_area->width - 160) / 4;
    manager->fixed_height = usable_area->height / 22 * 5;
    struct view_layer *layer = view_manager_get_layer(LAYER_SWITCHER, false);
    manager->tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(&manager->tree->node, false);

    manager->new_mapped_view.notify = handle_new_mapped_view;
    kywc_view_add_new_mapped_listener(&manager->new_mapped_view);
    manager->configured_output.notify = handle_configured_output;
    output_manager_add_configured_listener(&manager->configured_output);
    manager->seat_destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &manager->seat_destroy);

    manager->output = output;
    manager->item_page = NULL;
    manager->total_items = 0;
    tiled_preview_show(view, tile);
}

void view_begin_tile_preview(struct view *view, struct seat *seat, struct output *output,
                             uint32_t tile)
{
    if (manager) {
        return;
    }

    if (view->base.tiled != tile) {
        kywc_view_set_tiled(&view->base, tile, &output->base);
    }

    tiled_preview_add(view, seat, output, tile);
}
