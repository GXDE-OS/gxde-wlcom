// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>
#include <wlr/types/wlr_output.h>

#include <kywc/binding.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "painter.h"
#include "scene/box.h"
#include "theme.h"
#include "view/workspace.h"
#include "view_p.h"
#include "widget/scaled_buffer.h"
#include "widget/widget.h"

#define SELECT_WIDTH_GAP (5)
#define SELECT_HEIGHT_GAP (8)
#define ICON_OPACIPY (0.4)
#define ITEM_HEIGHT (48)
#define MAX_DISPLAY_VIEW (25)
#define MIN_DISPLAY_VIEW (4)

enum set_dir {
    NONE = 0,
    BOTTOM,
    TOP,
};

struct maximize_switcher_color {
    float background_color[4];
    float border_color[4];
    float select_color[4];
};

static struct maximize_switcher_color light = {
    .background_color = { 1.0, 1.0, 1.0, 1.0 },
    .border_color = { 25.0 / 255, 25.0 / 255, 25.0 / 255, 1.0 },
    .select_color = { 16.0 / 255, 43.0 / 255.0, 75.0 / 255, 0.3 },
};

static struct maximize_switcher_color dark = {
    .background_color = { 25.0 / 255, 25.0 / 255, 25.0 / 255, 1.0 },
    .border_color = { 1.0, 1.0, 1.0, 1.0 },
    .select_color = { 73.0 / 255, 10.0 / 255.0, 13.0 / 255, 0.3 },
};

struct item_view {
    struct kywc_view *kywc_view;
    struct widget *title_text;

    struct ky_scene_tree *tree;
    struct ky_scene_rect *background;
    struct ky_scene_node *icon_node;
    struct ky_scene_node *text_node;

    struct ky_scene_box *border_box;
    struct ky_scene_node *border_node;

    int text_width, text_height;
    float scale;
    char *text;

    struct wl_listener view_destroy;
};

static struct maximize_switcher {
    struct ky_scene_tree *tree;
    struct ky_scene_rect *background;

    struct item_view *active;
    struct maximize_switcher_color *color;

    /* border */
    struct {
        struct ky_scene_rect *left;
        struct ky_scene_rect *top;
        struct ky_scene_rect *right;
        struct ky_scene_rect *bottom;
    } border;

    struct item_view **item_views;

    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;
    struct seat_touch_grab touch_grab;

    int pending, current;
    int width, height;
    int max_width, max_height;
    /* *
     * restrict page display items when the height
     * exceeds the maximum height of the background.
     */
    int views_control;
    int num_windows;
    int num_view;
    int direction;
    int last_position;

    bool enable;
    float icon_opacity;

    struct wlr_output *output;
    struct wl_listener output_frame;
    struct wl_listener theme_update;
    struct wl_listener server_destroy;
} *switcher = NULL;

static struct shortcut {
    char *keybind;
    char *desc;
} shortcuts[] = {
    { "Alt+m", "traverse all maximized views" },
};

static void maximize_switcher_set_enable(bool enable);

static void ensure_thumbnails_size(int num)
{
    if (switcher->num_windows > num) {
        return;
    }

    int alloc = switcher->num_windows ? switcher->num_windows : 16;
    while (alloc < num) {
        alloc *= 2;
    }

    if (!switcher->item_views) {
        switcher->item_views = malloc(alloc * sizeof(struct item_view *));
    } else if (alloc > switcher->num_windows) {
        switcher->item_views = realloc(switcher->item_views, alloc * sizeof(struct item_view *));
    }

    switcher->num_windows = alloc;
}

static bool item_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                       uint32_t time, bool first, bool hold, void *data)
{
    return false;
}

static void item_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data) {}

static void item_click(struct seat *seat, struct ky_scene_node *node, uint32_t button, bool pressed,
                       uint32_t time, enum click_state state, void *data)
{
    /* do actions when released */
    if (pressed || button != BTN_LEFT) {
        return;
    }

    struct item_view *item = data;
    struct item_view *item_view;
    for (int i = 0; i < switcher->num_view; i++) {
        item_view = switcher->item_views[i];
        if (item == item_view) {
            switcher->pending = i;
            switcher->direction = NONE;
        }
    }
    wlr_output_schedule_frame(switcher->output);
}

static const struct input_event_node_impl item_impl = {
    .hover = item_hover,
    .leave = item_leave,
    .click = item_click,
};

static void pointer_grab_cancel(struct seat_pointer_grab *pointer_grab)
{
    maximize_switcher_set_enable(false);
}

static bool pointer_grab_button(struct seat_pointer_grab *pointer_grab, uint32_t time,
                                uint32_t button, bool pressed)
{
    struct maximize_switcher *window_menu = pointer_grab->data;
    struct seat *seat = pointer_grab->seat;

    /* check current hover node in the window menu tree */
    struct input_event_node *inode = input_event_node_from_node(seat->cursor->hover.node);
    struct ky_scene_node *node = input_event_node_root(inode);
    if (node == &window_menu->tree->node) {
        inode->impl->click(seat, seat->cursor->hover.node, button, pressed, time, CLICK_STATE_NONE,
                           inode->data);
    } else if (pressed) {
        maximize_switcher_set_enable(false);
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
    struct maximize_switcher *maximize_switcher = touch_grab->data;
    return pointer_grab_button(&maximize_switcher->pointer_grab, time, BTN_LEFT, down);
}

static bool touch_grab_motion(struct seat_touch_grab *touch_grab, uint32_t time, double lx,
                              double ly)
{
    return false;
}

static void touch_grab_cancel(struct seat_touch_grab *touch_grab)
{
    maximize_switcher_set_enable(false);
}

static const struct seat_touch_grab_interface touch_grab_impl = {
    .touch = touch_grab_touch,
    .motion = touch_grab_motion,
    .cancel = touch_grab_cancel,
};

static bool keyboard_grab_key(struct seat_keyboard_grab *keyboard_grab, uint32_t time, uint32_t key,
                              bool pressed, uint32_t modifiers)
{
    if (!pressed) {
        if (key != KEY_LEFTALT && key != KEY_RIGHTALT) {
            return true;
        }

        if (switcher->active->kywc_view) {
            kywc_view_activate(switcher->active->kywc_view);
            struct view *view = view_from_kywc_view(switcher->active->kywc_view);
            seat_focus_surface(keyboard_grab->seat, view->surface);
        }

        maximize_switcher_set_enable(false);
        return true;
    }

    switch (key) {
    case KEY_UP:
        switcher->direction = TOP;
        switcher->pending--;
        break;
    case KEY_M:
    case KEY_DOWN:
        switcher->direction = BOTTOM;
        switcher->pending++;
        break;
    case KEY_ESC:
        maximize_switcher_set_enable(false);
        break;
    }

    wlr_output_schedule_frame(switcher->output);
    return true;
}

static void keyboard_grab_cancel(struct seat_keyboard_grab *keyboard_grab)
{
    maximize_switcher_set_enable(false);
}

static const struct seat_keyboard_grab_interface keyboard_grab_impl = {
    .key = keyboard_grab_key,
    .cancel = keyboard_grab_cancel,
};

static void update_title_text(struct item_view *item_view)
{
    struct theme *theme = theme_manager_get_current();
    int select_width_gap = SELECT_WIDTH_GAP * 2;
    int max_width = switcher->max_width - theme->button_width - select_width_gap;
    widget_set_text(item_view->title_text, item_view->text, JUSTIFY_CENTER, false, false,
                    item_view->kywc_view->minimized);

    widget_set_font(item_view->title_text, theme->font_name, theme->font_size);
    widget_set_front_color(item_view->title_text, theme->active_text_color);

    widget_set_max_size(item_view->title_text, max_width, ITEM_HEIGHT);
    widget_set_auto_resize(item_view->title_text, AUTO_RESIZE_ONLY);

    widget_set_enabled(item_view->title_text, true);
    widget_update(item_view->title_text, true);

    int text_width, text_height;
    widget_get_size(item_view->title_text, &text_width, &text_height);

    if (text_width > max_width) {
        switcher->width = max_width + theme->button_width + select_width_gap;
    } else if (text_width > switcher->width - theme->button_width - select_width_gap) {
        switcher->width = text_width + theme->button_width + select_width_gap;
    }

    item_view->text_width = text_width;
    item_view->text_height = text_height;
}

static void set_icon_buffer(struct item_view *item_view, float opacity)
{
    struct kywc_view *kywc_view = item_view->kywc_view;
    struct view *view = view_from_kywc_view(kywc_view);
    struct wlr_buffer *buf = view_get_icon_buffer(view, item_view->scale);
    if (!buf) {
        return;
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_from_node(item_view->icon_node);
    if (buffer->buffer != buf) {
        ky_scene_buffer_set_buffer(buffer, buf);
    }
    if (buffer->buffer != buf) {
        return;
    }

    int width, height;
    ky_scene_buffer_set_opacity(buffer, opacity);
    painter_buffer_dest_size(buf, &width, &height);
    ky_scene_buffer_set_dest_size(buffer, width, height);
}

static void update_buffer(struct ky_scene_buffer *buffer, float scale, void *data)
{
    struct item_view *item_view = data;
    item_view->scale = scale;
    /* update scene_buffer with new buffer */
    set_icon_buffer(item_view, switcher->icon_opacity);
}

static void destroy_buffer(struct ky_scene_buffer *buffer, void *data)
{
    /* buffers are destroyed in theme */
}

static struct ky_scene_node *item_get_root(void *data)
{
    return &switcher->tree->node;
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct item_view *item_view = wl_container_of(listener, item_view, view_destroy);
    maximize_switcher_set_enable(false);
}

static void get_maximize_views(int *num_views)
{
    struct theme *theme = theme_manager_get_current();
    struct workspace *workspace = workspace_manager_get_current();
    struct kywc_output *kywc_output = kywc_output_get_primary();
    float color[4] = { 0 };
    struct view *view;
    struct view_proxy *view_proxy;
    wl_list_for_each(view_proxy, &workspace->view_proxies, workspace_link) {
        view = view_proxy->view;
        if (!view->base.mapped || !view->base.maximized) {
            continue;
        }

        struct item_view *item_view = calloc(1, sizeof(struct item_view));
        if (!item_view) {
            continue;
        }

        item_view->kywc_view = &view->base;
        item_view->scale = kywc_output->state.scale;

        item_view->view_destroy.notify = handle_view_destroy;
        wl_signal_add(&view->base.events.destroy, &item_view->view_destroy);
        item_view->tree = ky_scene_tree_create(switcher->tree);
        input_event_node_create(&item_view->tree->node, &item_impl, item_get_root, NULL, item_view);
        /* create background */
        item_view->background = ky_scene_rect_create(item_view->tree, 0, 0, color);
        /* create title */
        if (view->base.minimized) {
            char left_bracket[] = "(";
            char right_bracket[] = ")";
            int len = strlen(view->base.title) + strlen(left_bracket) + strlen(right_bracket) + 1;
            item_view->text = malloc(len * sizeof(char));
            sprintf(item_view->text, "%s%s%s", left_bracket, view->base.title, right_bracket);
        } else {
            item_view->text = strdup(view->base.title);
        }
        item_view->title_text = widget_create(item_view->tree);
        item_view->text_node = ky_scene_node_from_widget(item_view->title_text);
        /* create icon */
        struct ky_scene_buffer *buf = scaled_buffer_create(
            item_view->tree, view->output->state.scale, update_buffer, destroy_buffer, item_view);
        item_view->icon_node = &buf->node;
        /* update */
        set_icon_buffer(item_view, switcher->icon_opacity);
        update_title_text(item_view);
        /* draw border */
        item_view->border_box = ky_scene_box_create(item_view->tree, 0, 0, theme->accent_color, 1);
        item_view->border_node = ky_scene_node_from_box(item_view->border_box);
        ky_scene_node_set_enabled(item_view->border_node, false);
        /* add item_views */
        ensure_thumbnails_size(*num_views + 1);
        switcher->item_views[*num_views] = item_view;
        *num_views += 1;
    }
}

static void update_item_view(struct item_view *item_view)
{
    int select_width_gap = SELECT_WIDTH_GAP;
    int select_height_gap = SELECT_HEIGHT_GAP;
    int width = switcher->width - select_width_gap * 2 - 2;
    int height = ITEM_HEIGHT - select_height_gap * 2;
    ky_scene_rect_set_size(item_view->background, width, height);
    ky_scene_node_set_position(&item_view->background->node, select_width_gap + 1,
                               select_height_gap);

    ky_scene_node_set_position(item_view->border_node, select_width_gap, select_height_gap - 1);
    ky_scene_box_set_size(item_view->border_box, width, height);
}

static void set_select_item_view(int index, struct item_view *current, struct item_view *new)
{
    float color[4] = { 0 };
    if (current) {
        ky_scene_rect_set_color(current->background, color);
        ky_scene_node_set_enabled(current->border_node, false);
        set_icon_buffer(current, switcher->icon_opacity);
    }

    ky_scene_node_set_enabled(new->border_node, true);
    ky_scene_rect_set_color(new->background, switcher->color->select_color);
    set_icon_buffer(new, 1);
}

static void hide_all_items(void)
{
    struct item_view *item_view;
    for (int i = 0; i < switcher->num_view; i++) {
        item_view = switcher->item_views[i];
        ky_scene_node_set_enabled(&item_view->tree->node, false);
    }
}

static void show_current_page_items(int index)
{
    struct theme *theme = theme_manager_get_current();
    int icon_width = theme->button_width;
    int select_width_gap = SELECT_WIDTH_GAP;
    int icon_x = (icon_width - theme->icon_size) / 2 + select_width_gap;
    int icon_y = (ITEM_HEIGHT - theme->icon_size) / 2;

    int temp_x = icon_width + select_width_gap;

    struct item_view *item_view;
    for (int i = 0; i < switcher->num_view; i++) {
        /* skipped view */
        int start_i = i - index;
        if (start_i < 0) {
            continue;
        }

        if (i >= switcher->views_control + index) {
            break;
        }

        item_view = switcher->item_views[i];
        ky_scene_node_set_enabled(&item_view->tree->node, true);
        ky_scene_node_set_position(&item_view->tree->node, 0, start_i * ITEM_HEIGHT);

        /* set icon position */
        ky_scene_node_set_position(item_view->icon_node, icon_x, icon_y);
        /* set text position */
        int text_x =
            temp_x +
            (switcher->width - icon_width - item_view->text_width - select_width_gap * 2) / 2;
        int text_y = (ITEM_HEIGHT - item_view->text_height) / 2;
        ky_scene_node_set_position(item_view->text_node, text_x, text_y);

        update_item_view(item_view);
    }
}

static void handle_output_frame(struct wl_listener *listener, void *data)
{
    int num_view = switcher->num_view;
    int pending = switcher->pending;

    int i_index = 0;
    if (num_view == 1) {
        pending = 0;
    } else if (pending >= num_view) {
        pending = 0;
    } else if (pending < 0) {
        pending = num_view - 1;
        i_index =
            pending - switcher->views_control >= 0 ? pending + 1 - switcher->views_control : 0;
    } else {
        if (switcher->direction == BOTTOM) {
            i_index = pending < switcher->last_position + switcher->views_control
                          ? switcher->last_position
                          : pending + 1 - switcher->views_control;
        } else if (switcher->direction == TOP) {
            i_index = pending < switcher->last_position ? pending : switcher->last_position;
        } else {
            i_index = switcher->last_position;
        }
    }

    switcher->last_position = i_index;

    hide_all_items();
    show_current_page_items(i_index);

    /* select */
    struct item_view *current =
        switcher->current >= 0 ? switcher->item_views[switcher->current] : NULL;
    set_select_item_view(pending - i_index, current, switcher->item_views[pending]);

    switcher->pending = pending;
    switcher->current = pending;
    switcher->active = switcher->item_views[switcher->current];
}

static void hide_maximize_switcher(void)
{
    struct item_view *item_view;
    for (int i = 0; i < switcher->num_view; i++) {
        item_view = switcher->item_views[i];
        wl_list_remove(&item_view->view_destroy.link);
        ky_scene_node_destroy(&item_view->tree->node);
        free(item_view->text);
        free(item_view);
    }

    free(switcher->item_views);
    switcher->item_views = NULL;
    switcher->num_windows = 0;
    wl_list_remove(&switcher->output_frame.link);
    ky_scene_node_set_enabled(&switcher->tree->node, false);
}

static bool show_maximize_switcher(void)
{
    struct output *output = input_current_output(input_manager_get_default_seat());
    struct kywc_box *usable_area = &output->usable_area;

    switcher->max_width = usable_area->width / 3 * 2;
    switcher->max_height = usable_area->height / 3 * 2;
    switcher->width = usable_area->width / 3;

    int num_view = 0;
    get_maximize_views(&num_view);
    if (num_view == 0) {
        return false;
    }

    if (num_view >= MAX_DISPLAY_VIEW) {
        switcher->height = ITEM_HEIGHT * MAX_DISPLAY_VIEW;
    } else if (num_view <= MIN_DISPLAY_VIEW) {
        switcher->height = ITEM_HEIGHT * MIN_DISPLAY_VIEW;
    } else {
        switcher->height = ITEM_HEIGHT * num_view;
    }

    if (switcher->height > switcher->max_height) {
        int max_num = switcher->max_height / ITEM_HEIGHT;
        switcher->views_control = max_num;
        switcher->height = max_num * ITEM_HEIGHT;
    } else {
        switcher->views_control = MAX_DISPLAY_VIEW;
    }

    switcher->num_view = num_view;

    int x = usable_area->x + usable_area->width / 2 - switcher->width / 2;
    int y = usable_area->y + usable_area->height / 2 - switcher->height / 2;
    ky_scene_rect_set_size(switcher->background, switcher->width, switcher->height);
    ky_scene_node_set_position(&switcher->tree->node, x, y);
    ky_scene_node_set_enabled(&switcher->tree->node, true);

    /* draw border */
    /* left */
    ky_scene_rect_set_size(switcher->border.left, 1, switcher->height);
    ky_scene_node_set_position(&switcher->border.left->node, -1, 0);
    /* top */
    ky_scene_node_set_position(&switcher->border.top->node, -1, -1);
    ky_scene_rect_set_size(switcher->border.top, switcher->width + 2, 1);
    /* right */
    ky_scene_rect_set_size(switcher->border.right, 1, switcher->height);
    ky_scene_node_set_position(&switcher->border.right->node, switcher->width, 0);
    /* bottom */
    ky_scene_rect_set_size(switcher->border.bottom, switcher->width + 2, 1);
    ky_scene_node_set_position(&switcher->border.bottom->node, -1, switcher->height);

    switcher->output_frame.notify = handle_output_frame;
    wl_signal_add(&output->scene_output->events.frame, &switcher->output_frame);

    switcher->pending = 1;
    switcher->current = -1;
    switcher->direction = BOTTOM;
    switcher->output = output->wlr_output;
    wlr_output_schedule_frame(switcher->output);

    return true;
}

static void maximize_switcher_set_enable(bool enable)
{
    if (switcher->enable == enable) {
        return;
    }

    struct seat *seat = input_manager_get_default_seat();
    switcher->enable = enable;

    if (!enable) {
        hide_maximize_switcher();
        seat_end_pointer_grab(seat, &switcher->pointer_grab);
        seat_end_keyboard_grab(seat, &switcher->keyboard_grab);
        seat_end_touch_grab(seat, &switcher->touch_grab);
        return;
    }

    if (!show_maximize_switcher()) {
        switcher->enable = false;
        return;
    }

    seat_start_pointer_grab(seat, &switcher->pointer_grab);
    seat_start_keyboard_grab(seat, &switcher->keyboard_grab);
    seat_start_touch_grab(seat, &switcher->touch_grab);
}

static void shortcut_action(struct key_binding *binding, void *data)
{
    maximize_switcher_set_enable(true);
}

static void switcher_register_shortcuts(void)
{
    for (size_t i = 0; i < sizeof(shortcuts) / sizeof(struct shortcut); i++) {
        struct shortcut *shortcut = &shortcuts[i];
        struct key_binding *binding = kywc_key_binding_create(shortcut->keybind, shortcut->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_key_binding_register(binding, shortcut_action, shortcut)) {
            kywc_key_binding_destroy(binding);
            continue;
        }
    }
}

static void handle_theme_update(struct wl_listener *listener, void *data)
{
    struct theme_update_event *update_event = data;
    if (update_event->update_mask != THEME_UPDATE_MASK_ALL) {
        return;
    }
    switcher->color = update_event->theme_type == THEME_TYPE_LIGHT ? &light : &dark;
    ky_scene_rect_set_color(switcher->background, switcher->color->background_color);

    ky_scene_rect_set_color(switcher->border.left, switcher->color->border_color);
    ky_scene_rect_set_color(switcher->border.top, switcher->color->border_color);
    ky_scene_rect_set_color(switcher->border.right, switcher->color->border_color);
    ky_scene_rect_set_color(switcher->border.bottom, switcher->color->border_color);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&switcher->server_destroy.link);
    wl_list_remove(&switcher->theme_update.link);
    ky_scene_node_destroy(&switcher->tree->node);

    free(switcher);
    switcher = NULL;
}

bool maximize_switcher_create(struct view_manager *view_manager)
{
    switcher = calloc(1, sizeof(struct maximize_switcher));
    if (!switcher) {
        return false;
    }

    struct theme *theme = theme_manager_get_current();
    struct view_layer *layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
    switcher->tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(&switcher->tree->node, false);

    switch (theme->theme_type) {
    case THEME_TYPE_LIGHT:
        switcher->color = &light;
        break;
    case THEME_TYPE_DARK:
        switcher->color = &dark;
        break;
    }

    switcher->icon_opacity = ICON_OPACIPY;
    switcher->background =
        ky_scene_rect_create(switcher->tree, 0, 0, switcher->color->background_color);
    switcher->border.left =
        ky_scene_rect_create(switcher->tree, 0, 0, switcher->color->border_color);
    switcher->border.top =
        ky_scene_rect_create(switcher->tree, 0, 0, switcher->color->border_color);
    switcher->border.right =
        ky_scene_rect_create(switcher->tree, 0, 0, switcher->color->border_color);
    switcher->border.bottom =
        ky_scene_rect_create(switcher->tree, 0, 0, switcher->color->border_color);

    switcher->pointer_grab.data = switcher;
    switcher->pointer_grab.interface = &pointer_grab_impl;
    switcher->keyboard_grab.data = switcher;
    switcher->keyboard_grab.interface = &keyboard_grab_impl;
    switcher->touch_grab.data = switcher;
    switcher->touch_grab.interface = &touch_grab_impl;

    switcher->theme_update.notify = handle_theme_update;
    theme_manager_add_update_listener(&switcher->theme_update);
    switcher->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &switcher->server_destroy);

    switcher_register_shortcuts();

    return true;
}
