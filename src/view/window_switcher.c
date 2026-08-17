/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * This is a imitation of Deepin KWin's window switcher...
 */

#include <stdlib.h>

#include <linux/input-event-codes.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>

#include <kywc/binding.h>

#include "effect/animator.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "scene/animation.h"
#include "server.h"
#include "theme.h"
#include "view/workspace.h"
#include "view_p.h"
#include "widget/scaled_buffer.h"

/* From Deepin KWin's theme */
#define ITEM_ICON_SIZE 128
#define ITEM_MARGIN 10
#define CELL_SIZE (ITEM_ICON_SIZE + ITEM_MARGIN * 2)
#define POPUP_MARGIN 32
#define POPUP_EXTRA 4
#define SCREEN_PADDING 70
#define MIN_ITEMS_EACH_ROW 7
#define MAX_ROWS 2
#define POPUP_RADIUS 6
#define HIGHLIGHT_RADIUS 4
#define HIGHLIGHT_DURATION 80

struct window_switcher_colors {
    float background[4];
    float border[4];
    float highlight[4];
};

static const struct window_switcher_colors light_colors = {
    .background = { 0.30f, 0.30f, 0.30f, 0.30f },
    .border = { 0.0f, 0.0f, 0.0f, 0.10f },
    .highlight = { 1.0f / 255.0f, 189.0f / 255.0f, 1.0f, 1.0f },
};

static const struct window_switcher_colors dark_colors = {
    .background = { 24.0f / 255.0f * 0.72f, 24.0f / 255.0f * 0.72f, 24.0f / 255.0f * 0.72f,
                    0.72f },
    .border = { 0.16f, 0.16f, 0.16f, 0.16f },
    .highlight = { 1.0f / 255.0f, 189.0f / 255.0f, 1.0f, 1.0f },
};

struct switcher_item {
    struct window_switcher* switcher;
    struct view* view;
    struct ky_scene_tree* tree;
    struct ky_scene_rect* hit_area;
    struct ky_scene_node* icon_node;
    float scale;
    int icon_size;
    int index;

    struct wl_listener view_unmap;
};

struct window_switcher {
    struct ky_scene_tree* tree;
    struct ky_scene_rect* background;
    struct ky_scene_rect* inner_background;
    struct ky_scene_rect* highlight;

    struct switcher_item** items;
    size_t item_count;
    size_t item_capacity;
    int current;
    int columns;
    int rows;
    int cell_size;
    int width;
    int height;
    bool enabled;

    const struct window_switcher_colors* colors;
    struct output* output;

    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;
    struct seat_touch_grab touch_grab;

    struct wl_listener theme_update;
    struct wl_listener output_destroy;
    struct wl_listener server_destroy;
};

static struct window_switcher* switcher;

static void window_switcher_set_enabled(bool enabled);

static bool ensure_item_capacity(size_t count) {
    if (count <= switcher->item_capacity) {
        return true;
    }

    size_t capacity = switcher->item_capacity ? switcher->item_capacity * 2 : 16;
    while (capacity < count) {
        capacity *= 2;
    }

    struct switcher_item** items = realloc(switcher->items, capacity * sizeof(*items));
    if (!items) {
        return false;
    }

    switcher->items = items;
    switcher->item_capacity = capacity;
    return true;
}

static struct ky_scene_node* item_get_root(void* data) {
    struct switcher_item* item = data;
    return &item->switcher->tree->node;
}

static struct ky_scene_node* switcher_get_root(void* data) {
    struct window_switcher* window_switcher = data;
    return &window_switcher->tree->node;
}

static bool item_hover(struct seat* seat, struct ky_scene_node* node, double x, double y,
                       uint32_t time, bool first, bool hold, void* data) {
    return false;
}

static void item_leave(struct seat* seat, struct ky_scene_node* node, bool last, void* data) {}

static void select_index(int index, bool animate) {
    if (!switcher->item_count) {
        return;
    }

    int count = switcher->item_count;
    index %= count;
    if (index < 0) {
        index += count;
    }
    switcher->current = index;

    int x = POPUP_MARGIN + index % switcher->columns * switcher->cell_size;
    int y = POPUP_MARGIN + index / switcher->columns * switcher->cell_size;
    struct animation* animation =
        animate ? animation_manager_get(ANIMATION_TYPE_EASE_IN_OUT) : NULL;
    ky_scene_node_set_position_with_animation(&switcher->highlight->node, x, y, animation,
                                              HIGHLIGHT_DURATION);
    output_schedule_frame(switcher->output->wlr_output);
}

static void activate_current(void) {
    if (switcher->current < 0 || switcher->current >= (int)switcher->item_count) {
        return;
    }

    struct view* view = switcher->items[switcher->current]->view;
    if (view->base.minimized) {
        kywc_view_set_minimized(&view->base, false);
    }
    kywc_view_activate(&view->base);
    view_set_focus(view, switcher->keyboard_grab.seat);
}

static void item_click(struct seat* seat, struct ky_scene_node* node, uint32_t button, bool pressed,
                       uint32_t time, enum click_state state, void* data) {
    if (button != BTN_LEFT || pressed) {
        return;
    }

    struct switcher_item* item = data;
    select_index(item->index, true);
    activate_current();
    window_switcher_set_enabled(false);
}

static const struct input_event_node_impl item_impl = {
    .hover = item_hover,
    .leave = item_leave,
    .click = item_click,
};

static const struct input_event_node_impl switcher_impl = {
    .hover = item_hover,
    .leave = item_leave,
    .click = NULL,
};

static void pointer_grab_cancel(struct seat_pointer_grab* grab) {
    window_switcher_set_enabled(false);
}

static bool pointer_grab_button(struct seat_pointer_grab* grab, uint32_t time, uint32_t button,
                                bool pressed) {
    struct input_event_node* inode = input_event_node_from_node(grab->seat->cursor->hover.node);
    if (inode && input_event_node_root(inode) == &switcher->tree->node) {
        if (inode->impl->click) {
            inode->impl->click(grab->seat, grab->seat->cursor->hover.node, button, pressed, time,
                               CLICK_STATE_NONE, inode->data);
        }
    } else if (pressed) {
        window_switcher_set_enabled(false);
    }
    return true;
}

static bool pointer_grab_motion(struct seat_pointer_grab* grab, uint32_t time, double lx,
                                double ly) {
    return false;
}

static bool pointer_grab_axis(struct seat_pointer_grab* grab, uint32_t time, bool vertical,
                              double value) {
    return true;
}

static const struct seat_pointer_grab_interface pointer_grab_impl = {
    .motion = pointer_grab_motion,
    .button = pointer_grab_button,
    .axis = pointer_grab_axis,
    .cancel = pointer_grab_cancel,
};

static bool touch_grab_touch(struct seat_touch_grab* grab, uint32_t time, bool down) {
    return pointer_grab_button(&switcher->pointer_grab, time, BTN_LEFT, down);
}

static bool touch_grab_motion(struct seat_touch_grab* grab, uint32_t time, double lx, double ly) {
    return false;
}

static void touch_grab_cancel(struct seat_touch_grab* grab) {
    window_switcher_set_enabled(false);
}

static const struct seat_touch_grab_interface touch_grab_impl = {
    .touch = touch_grab_touch,
    .motion = touch_grab_motion,
    .cancel = touch_grab_cancel,
};

static bool keyboard_grab_key(struct seat_keyboard_grab* grab, struct keyboard* keyboard,
                              uint32_t time, uint32_t key, bool pressed, uint32_t modifiers) {
    if (!pressed) {
        if (key == KEY_LEFTALT || key == KEY_RIGHTALT) {
            activate_current();
            window_switcher_set_enabled(false);
        }
        return true;
    }

    switch (key) {
    case KEY_TAB:
        select_index(switcher->current + ((modifiers & WLR_MODIFIER_SHIFT) ? -1 : 1), true);
        break;
    case KEY_LEFT:
        select_index(switcher->current - 1, true);
        break;
    case KEY_RIGHT:
        select_index(switcher->current + 1, true);
        break;
    case KEY_UP:
        select_index(switcher->current - switcher->columns, true);
        break;
    case KEY_DOWN:
        select_index(switcher->current + switcher->columns, true);
        break;
    case KEY_ESC:
        window_switcher_set_enabled(false);
        break;
    default:
        break;
    }

    return true;
}

static void keyboard_grab_cancel(struct seat_keyboard_grab* grab) {
    window_switcher_set_enabled(false);
}

static const struct seat_keyboard_grab_interface keyboard_grab_impl = {
    .key = keyboard_grab_key,
    .cancel = keyboard_grab_cancel,
};

static void set_item_icon(struct switcher_item* item, float scale) {
    item->scale = scale;
    struct wlr_buffer* buffer =
        view_get_icon_buffer_by_size(item->view, ITEM_ICON_SIZE, item->scale);
    if (!buffer) {
        return;
    }

    struct ky_scene_buffer* scene_buffer = ky_scene_buffer_from_node(item->icon_node);
    if (scene_buffer->buffer != buffer) {
        ky_scene_buffer_set_buffer(scene_buffer, buffer);
    }
    ky_scene_buffer_set_dest_size(scene_buffer, item->icon_size, item->icon_size);
}

static void update_item_icon(struct ky_scene_buffer* buffer, float scale, void* data) {
    set_item_icon(data, scale);
}

static void destroy_item_icon(struct ky_scene_buffer* buffer, void* data) {
    /* Icon buffers are owned by the theme manager or the view. */
}

static void handle_item_view_unmap(struct wl_listener* listener, void* data) {
    window_switcher_set_enabled(false);
}

static bool add_item(struct view* view) {
    if (!ensure_item_capacity(switcher->item_count + 1)) {
        return false;
    }

    struct switcher_item* item = calloc(1, sizeof(*item));
    if (!item) {
        return false;
    }

    item->switcher = switcher;
    item->view = view;
    item->index = switcher->item_count;
    item->scale = switcher->output->base.state.scale;
    item->icon_size = ITEM_ICON_SIZE;
    item->tree = ky_scene_tree_create(switcher->tree);
    item->hit_area = ky_scene_rect_create(item->tree, switcher->cell_size, switcher->cell_size,
                                          (float[4]){ 0.0f, 0.0f, 0.0f, 0.0f });
    input_event_node_create(&item->tree->node, &item_impl, item_get_root, NULL, item);

    struct ky_scene_buffer* icon =
        scaled_buffer_create(item->tree, item->scale, update_item_icon, destroy_item_icon, item);
    item->icon_node = &icon->node;
    ky_scene_node_set_position(item->icon_node, ITEM_MARGIN, ITEM_MARGIN);
    set_item_icon(item, item->scale);

    item->view_unmap.notify = handle_item_view_unmap;
    wl_signal_add(&view->base.events.unmap, &item->view_unmap);

    switcher->items[switcher->item_count++] = item;
    return true;
}

static bool collect_items(void) {
    struct workspace* workspace = workspace_manager_get_current();
    struct view_proxy* proxy;
    wl_list_for_each(proxy, &workspace->view_proxies, workspace_link) {
        struct view* view = proxy->view;
        if (!view->base.mapped || view->base.role != KYWC_VIEW_ROLE_NORMAL ||
            view->base.skip_switcher || !view_is_activatable(view)) {
            continue;
        }
        if (!add_item(view)) {
            return false;
        }
    }
    return switcher->item_count > 0;
}

static void calculate_layout(void) {
    const struct kywc_box* screen = &switcher->output->geometry;
    int max_width = screen->width - SCREEN_PADDING * 2;
    int popup_spacing = POPUP_MARGIN * 2 + POPUP_EXTRA;
    if (max_width < popup_spacing + 1) {
        max_width = popup_spacing + 1;
    }
    int max_columns = (max_width - popup_spacing) / CELL_SIZE;
    if (max_columns < 1) {
        max_columns = 1;
    }

    int columns = max_columns;
    int cell_size = CELL_SIZE;
    bool scale_items = false;
    if (columns < MIN_ITEMS_EACH_ROW && (int)switcher->item_count > columns) {
        scale_items = true;
        columns =
            switcher->item_count < MIN_ITEMS_EACH_ROW ? switcher->item_count : MIN_ITEMS_EACH_ROW;
    }
    if (columns * MAX_ROWS < (int)switcher->item_count) {
        columns = (switcher->item_count + MAX_ROWS - 1) / MAX_ROWS;
        scale_items = true;
    }
    if (columns > (int)switcher->item_count) {
        columns = switcher->item_count;
    }
    if (scale_items) {
        cell_size = (max_width - POPUP_MARGIN * 2) / columns;
    }
    if (cell_size < 1) {
        cell_size = 1;
    }

    switcher->columns = columns;
    switcher->rows = (switcher->item_count + columns - 1) / columns;
    int max_height = screen->height * 0.7f;
    int max_cell_height = (max_height - popup_spacing) / switcher->rows;
    if (max_cell_height > 0 && cell_size > max_cell_height) {
        cell_size = max_cell_height;
    }
    switcher->cell_size = cell_size;
    switcher->width = cell_size * columns + popup_spacing;
    if (switcher->width > max_width) {
        switcher->width = max_width;
    }
    switcher->height = cell_size * switcher->rows + popup_spacing;
}

static void apply_layout(void) {
    struct kywc_box* screen = &switcher->output->geometry;
    int x = screen->x + (screen->width - switcher->width) / 2;
    int y = screen->y + (screen->height - switcher->height) / 2;

    ky_scene_rect_set_size(switcher->background, switcher->width, switcher->height);
    ky_scene_rect_set_size(switcher->inner_background, switcher->width - 2, switcher->height - 2);
    ky_scene_rect_set_size(switcher->highlight, switcher->cell_size, switcher->cell_size);
    ky_scene_node_set_position(&switcher->inner_background->node, 1, 1);
    ky_scene_node_set_position(&switcher->tree->node, x, y);

    int icon_size = switcher->cell_size - ITEM_MARGIN * 2;
    if (icon_size > ITEM_ICON_SIZE) {
        icon_size = ITEM_ICON_SIZE;
    }
    if (icon_size < 1) {
        icon_size = 1;
    }

    for (size_t i = 0; i < switcher->item_count; i++) {
        struct switcher_item* item = switcher->items[i];
        int item_x = POPUP_MARGIN + i % switcher->columns * switcher->cell_size;
        int item_y = POPUP_MARGIN + i / switcher->columns * switcher->cell_size;
        int icon_offset = (switcher->cell_size - icon_size) / 2;

        item->icon_size = icon_size;
        ky_scene_node_set_position(&item->tree->node, item_x, item_y);
        ky_scene_rect_set_size(item->hit_area, switcher->cell_size, switcher->cell_size);
        ky_scene_node_set_position(item->icon_node, icon_offset, icon_offset);
        ky_scene_buffer_set_dest_size(ky_scene_buffer_from_node(item->icon_node), icon_size,
                                      icon_size);
    }
}

static void destroy_items(void) {
    for (size_t i = 0; i < switcher->item_count; i++) {
        struct switcher_item* item = switcher->items[i];
        wl_list_remove(&item->view_unmap.link);
        ky_scene_node_destroy(&item->tree->node);
        free(item);
    }
    switcher->item_count = 0;
}

static void handle_output_destroy(struct wl_listener* listener, void* data) {
    window_switcher_set_enabled(false);
}

static bool show_switcher(int direction) {
    switcher->output = input_current_output(input_manager_get_default_seat());
    switcher->cell_size = CELL_SIZE;
    if (!switcher->output || !collect_items()) {
        destroy_items();
        return false;
    }

    calculate_layout();
    apply_layout();
    switcher->output_destroy.notify = handle_output_destroy;
    wl_signal_add(&switcher->output->base.events.destroy, &switcher->output_destroy);
    ky_scene_node_set_enabled(&switcher->tree->node, true);
    select_index(direction < 0 ? (int)switcher->item_count - 1 : (switcher->item_count > 1 ? 1 : 0),
                 false);
    return true;
}

static void hide_switcher(void) {
    ky_scene_node_set_enabled(&switcher->tree->node, false);
    wl_list_remove(&switcher->output_destroy.link);
    wl_list_init(&switcher->output_destroy.link);
    destroy_items();
    switcher->current = -1;
    switcher->output = NULL;
}

static void window_switcher_set_enabled(bool enabled) {
    if (switcher->enabled == enabled) {
        return;
    }

    struct seat* seat = input_manager_get_default_seat();
    switcher->enabled = enabled;
    if (!enabled) {
        hide_switcher();
        seat_end_pointer_grab(seat, &switcher->pointer_grab);
        seat_end_keyboard_grab(seat, &switcher->keyboard_grab);
        seat_end_touch_grab(seat, &switcher->touch_grab);
        return;
    }

    seat_start_pointer_grab(seat, &switcher->pointer_grab);
    seat_start_keyboard_grab(seat, &switcher->keyboard_grab);
    seat_start_touch_grab(seat, &switcher->touch_grab);
}

static void shortcut_action(struct key_binding* binding, void* data) {
    int direction = *(int*)data;
    if (switcher->enabled) {
        select_index(switcher->current + direction, true);
        return;
    }

    if (!show_switcher(direction)) {
        return;
    }
    window_switcher_set_enabled(true);
}

static void register_shortcut(const char* keybind, int* direction) {
    struct key_binding* binding = kywc_key_binding_create(keybind, "switch windows");
    if (!binding) {
        return;
    }
    if (!kywc_key_binding_register(binding, KEY_BINDING_TYPE_WINDOW_SWITCHER, shortcut_action,
                                   direction)) {
        kywc_key_binding_destroy(binding);
    }
}

static void apply_colors(void) {
    ky_scene_rect_set_color(switcher->background, switcher->colors->border);
    ky_scene_rect_set_color(switcher->inner_background, switcher->colors->background);
    ky_scene_rect_set_color(switcher->highlight, switcher->colors->highlight);
}

static void handle_theme_update(struct wl_listener* listener, void* data) {
    struct theme_update_event* event = data;
    if (!(event->update_mask & THEME_UPDATE_MASK_TYPE)) {
        return;
    }
    switcher->colors = event->theme_type == THEME_TYPE_DARK ? &dark_colors : &light_colors;
    apply_colors();
}

static void handle_server_destroy(struct wl_listener* listener, void* data) {
    if (switcher->enabled) {
        window_switcher_set_enabled(false);
    }
    wl_list_remove(&switcher->server_destroy.link);
    wl_list_remove(&switcher->theme_update.link);
    ky_scene_node_destroy(&switcher->tree->node);
    free(switcher->items);
    free(switcher);
    switcher = NULL;
}

bool window_switcher_create(struct view_manager* view_manager) {
    static int forward = 1;
    static int backward = -1;

    switcher = calloc(1, sizeof(*switcher));
    if (!switcher) {
        return false;
    }

    struct view_layer* layer = view_manager_get_layer(LAYER_SWITCHER, false);
    switcher->tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(&switcher->tree->node, false);
    wl_list_init(&switcher->output_destroy.link);
    input_event_node_create(&switcher->tree->node, &switcher_impl, switcher_get_root, NULL,
                            switcher);
    switcher->current = -1;

    switcher->colors =
        theme_manager_get_theme()->type == THEME_TYPE_DARK ? &dark_colors : &light_colors;
    switcher->background = ky_scene_rect_create(switcher->tree, 0, 0, switcher->colors->border);
    switcher->inner_background =
        ky_scene_rect_create(switcher->tree, 0, 0, switcher->colors->background);
    switcher->highlight = ky_scene_rect_create(switcher->tree, 0, 0, switcher->colors->highlight);

    int popup_radius[4] = { POPUP_RADIUS, POPUP_RADIUS, POPUP_RADIUS, POPUP_RADIUS };
    int highlight_radius[4] = { HIGHLIGHT_RADIUS, HIGHLIGHT_RADIUS, HIGHLIGHT_RADIUS,
                                HIGHLIGHT_RADIUS };
    ky_scene_node_set_radius(&switcher->background->node, popup_radius);
    ky_scene_node_set_radius(&switcher->inner_background->node, popup_radius);
    ky_scene_node_set_radius(&switcher->highlight->node, highlight_radius);

    pixman_region32_t blur_region;
    pixman_region32_init(&blur_region);
    ky_scene_node_set_blur_region(&switcher->inner_background->node, &blur_region);
    ky_scene_node_set_blur_level(&switcher->inner_background->node, 3, 4.0f);
    pixman_region32_fini(&blur_region);

    switcher->pointer_grab = (struct seat_pointer_grab){
        .interface = &pointer_grab_impl,
        .data = switcher,
    };
    switcher->keyboard_grab = (struct seat_keyboard_grab){
        .interface = &keyboard_grab_impl,
        .data = switcher,
    };
    switcher->touch_grab = (struct seat_touch_grab){
        .interface = &touch_grab_impl,
        .data = switcher,
    };

    switcher->theme_update.notify = handle_theme_update;
    theme_manager_add_update_listener(&switcher->theme_update);
    switcher->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &switcher->server_destroy);

    register_shortcut("Alt+Tab", &forward);
    /* Shift+Tab is reported by xkbcommon as ISO_Left_Tab. */
    register_shortcut("Alt+Shift+ISO_Left_Tab", &backward);
    return true;
}
