// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>

#include <kywc/binding.h>

#include "input/seat.h"
#include "output.h"
#include "painter.h"
#include "theme.h"
#include "view/workspace.h"
#include "view_p.h"
#include "widget/scaled_buffer.h"
#include "widget/widget.h"

#define DEFAULT_VIEWS (25)
#define ITEM_HEIGHT (36)
#define DEFAULT_ICON_AREA_WIDTH (30)
#define DEFAULT_ICON_WIDTH (24)

enum set_dir {
    NONE = 0,
    BOTTOM,
    TOP,
};

struct item_view {
    struct kywc_view *kywc_view;
    struct widget *title_text;

    struct ky_scene_tree *tree;
    struct ky_scene_rect *background;
    struct ky_scene_node *icon_node;
    struct ky_scene_node *text_node;

    int text_witdh, text_height;
    float scale;

    struct wl_listener view_destroy;
};

static struct maximize_switcher {
    struct ky_scene_tree *tree;
    struct ky_scene_rect *background;

    struct item_view *active;

    /* select box */
    struct {
        struct ky_scene_rect *left;
        struct ky_scene_rect *top;
        struct ky_scene_rect *right;
        struct ky_scene_rect *bottom;
    } select;

    struct item_view **item_views;

    struct seat_keyboard_grab keyboard_grab;

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
    int dir;
    int last_position;

    bool enable;

    struct wlr_output *output;
    struct wl_listener output_frame;
    struct wl_listener server_destroy;
    struct wl_listener theme_update;
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
        switcher->dir = TOP;
        switcher->pending--;
        break;
    case KEY_M:
    case KEY_DOWN:
        switcher->dir = BOTTOM;
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
    struct kywc_view *view = item_view->kywc_view;

    int max_width = switcher->max_width - DEFAULT_ICON_AREA_WIDTH;
    widget_set_text(item_view->title_text, view->title, JUSTIFY_CENTER, false);
    widget_set_font(item_view->title_text, theme->font_name, theme->font_size);

    widget_set_front_color(item_view->title_text, theme->active_text_color);

    widget_set_max_size(item_view->title_text, max_width, ITEM_HEIGHT);
    widget_set_auto_resize(item_view->title_text, true);

    widget_set_enabled(item_view->title_text, true);
    widget_update(item_view->title_text, true);

    /* get actual size when auto-sized */
    int text_width, text_height;
    widget_get_size(item_view->title_text, &text_width, &text_height);

    if (text_width > max_width) {
        switcher->width = max_width + DEFAULT_ICON_AREA_WIDTH;
    } else if (text_width > switcher->width - DEFAULT_ICON_AREA_WIDTH) {
        switcher->width = text_width + DEFAULT_ICON_AREA_WIDTH;
    }

    item_view->text_witdh = text_width;
    item_view->text_height = text_height;
}

static void set_icon_buffer(struct item_view *item_view)
{
    struct kywc_view *kywc_view = item_view->kywc_view;
    struct wlr_buffer *buf = theme_icon_load(kywc_view->app_id, item_view->scale);
    if (!buf) {
        return;
    }

    struct ky_scene_buffer *buffer = ky_scene_buffer_from_node(item_view->icon_node);
    if (ky_scene_buffer_get_buffer(buffer) != buf) {
        ky_scene_buffer_set_buffer(buffer, buf);
    }
    if (ky_scene_buffer_get_buffer(buffer) != buf) {
        return;
    }

    int width, height;
    painter_buffer_unscaled_size(buf, &width, &height);
    ky_scene_buffer_set_dest_size(buffer, width, height);
}

static void update_buffer(struct ky_scene_buffer *buffer, float scale, void *data)
{
    struct item_view *item_view = data;
    item_view->scale = scale;
    /* update scene_buffer with new buffer */
    set_icon_buffer(item_view);
}

static void destroy_buffer(struct ky_scene_buffer *buffer, void *data)
{
    /* buffers are destroyed in theme */
}

static void handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct item_view *item_view = wl_container_of(listener, item_view, view_destroy);
    maximize_switcher_set_enable(false);
}

static void get_maximize_views(int *num_views)
{
    struct workspace *workspace = workspace_manager_get_current();
    struct kywc_output *kywc_output = output_manager_get_primary();
    float color[4] = { 0 };
    struct view *view;
    wl_list_for_each(view, &workspace->views, link) {
        if (!view->base.mapped) {
            continue;
        }
        if (!view->base.maximized) {
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
        /* create background */
        item_view->background = ky_scene_rect_create(item_view->tree, 0, 0, color);
        /* create icon */
        struct ky_scene_buffer *buf = scaled_buffer_create(
            item_view->tree, view->output->state.scale, update_buffer, destroy_buffer, item_view);
        item_view->icon_node = ky_scene_node_from_buffer(buf);
        /* create title */
        item_view->title_text = widget_create(item_view->tree);
        item_view->text_node = ky_scene_node_from_widget(item_view->title_text);
        /* update */
        set_icon_buffer(item_view);
        update_title_text(item_view);
        /* add item_views */
        ensure_thumbnails_size(*num_views + 1);
        switcher->item_views[*num_views] = item_view;
        *num_views += 1;
    }
}

static void draw_select_box(int index, struct item_view *current, struct item_view *new)
{
    float color[4] = { 0 };
    if (current) {
        ky_scene_rect_set_color(current->background, color);
    }

    struct theme *theme = theme_manager_get_current();
    color[0] = theme->accent_color[0];
    color[1] = theme->accent_color[1];
    color[2] = theme->accent_color[2];
    color[3] = 0.8;
    ky_scene_rect_set_color(new->background, color);

    /* update select left*/
    ky_scene_rect_set_size(switcher->select.left, 1, ITEM_HEIGHT - 2);
    ky_scene_node_set_position(ky_scene_node_from_rect(switcher->select.left), 0,
                               index * ITEM_HEIGHT + 1);
    /* update select top*/
    ky_scene_rect_set_size(switcher->select.top, switcher->width, 1);
    ky_scene_node_set_position(ky_scene_node_from_rect(switcher->select.top), 0,
                               index * ITEM_HEIGHT);
    /* update select right*/
    ky_scene_rect_set_size(switcher->select.right, 1, ITEM_HEIGHT - 2);
    ky_scene_node_set_position(ky_scene_node_from_rect(switcher->select.right), switcher->width - 1,
                               index * ITEM_HEIGHT + 1);
    /* update select box bottom*/
    ky_scene_rect_set_size(switcher->select.bottom, switcher->width, 1);
    ky_scene_node_set_position(ky_scene_node_from_rect(switcher->select.bottom), 0,
                               index * ITEM_HEIGHT + ITEM_HEIGHT - 1);
}

static void hide_all_items(void)
{
    struct item_view *item_view;
    for (int i = 0; i < switcher->num_view; i++) {
        item_view = switcher->item_views[i];
        ky_scene_node_set_enabled(ky_scene_node_from_tree(item_view->tree), false);
    }
}

static void show_current_page_items(int index)
{
    int icon_x = (DEFAULT_ICON_AREA_WIDTH - DEFAULT_ICON_WIDTH) / 2;
    int icon_y = (ITEM_HEIGHT - DEFAULT_ICON_WIDTH) / 2;
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
        struct ky_scene_node *node = ky_scene_node_from_tree(item_view->tree);
        ky_scene_node_set_enabled(node, true);
        ky_scene_node_set_position(node, 0, start_i * ITEM_HEIGHT);

        /* set icon position */
        ky_scene_node_set_position(item_view->icon_node, icon_x, icon_y);
        /* set text position */
        int text_x = DEFAULT_ICON_AREA_WIDTH +
                     (switcher->width - DEFAULT_ICON_AREA_WIDTH - item_view->text_witdh) / 2;
        int text_y = (ITEM_HEIGHT - item_view->text_height) / 2;
        ky_scene_node_set_position(item_view->text_node, text_x, text_y);

        ky_scene_rect_set_size(item_view->background, switcher->width - 2, ITEM_HEIGHT - 2);
        ky_scene_node_set_position(ky_scene_node_from_rect(item_view->background), 1, 1);
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
        if (switcher->dir == BOTTOM) {
            i_index = pending < switcher->last_position + switcher->views_control
                          ? switcher->last_position
                          : pending + 1 - switcher->views_control;
        } else if (switcher->dir == TOP) {
            i_index = pending < switcher->last_position ? pending : switcher->last_position;
        } else {
            i_index = switcher->last_position;
        }
    }

    switcher->last_position = i_index;

    hide_all_items();
    show_current_page_items(i_index);

    /* select box */
    struct item_view *current =
        switcher->current >= 0 ? switcher->item_views[switcher->current] : NULL;
    draw_select_box(pending - i_index, current, switcher->item_views[pending]);

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
        ky_scene_node_destroy(ky_scene_node_from_tree(item_view->tree));
        free(item_view);
    }

    free(switcher->item_views);
    switcher->item_views = NULL;
    switcher->num_windows = 0;
    wl_list_remove(&switcher->output_frame.link);
    ky_scene_node_set_enabled(ky_scene_node_from_tree(switcher->tree), false);
}

static bool show_maximize_switcher(void)
{
    struct kywc_output *kywc_output = output_manager_get_primary();
    struct output *output = output_from_kywc_output(kywc_output);
    struct kywc_box *usable_area = &output->usable_area;

    switcher->max_width = usable_area->width / 3 * 2;
    switcher->max_height = usable_area->height / 3 * 2;
    switcher->width = usable_area->width / 3;

    int num_view = 0;
    get_maximize_views(&num_view);
    if (num_view == 0) {
        return false;
    }

    if (num_view >= DEFAULT_VIEWS) {
        switcher->height = ITEM_HEIGHT * DEFAULT_VIEWS;
    } else if (num_view <= 4) {
        switcher->height = ITEM_HEIGHT * 4;
    } else {
        switcher->height = ITEM_HEIGHT * num_view;
    }

    if (switcher->height > switcher->max_height) {
        int max_num = switcher->max_height / ITEM_HEIGHT;
        switcher->views_control = max_num;
        switcher->height = max_num * ITEM_HEIGHT;
    } else {
        switcher->views_control = DEFAULT_VIEWS;
    }

    switcher->num_view = num_view;
    int width = switcher->width;
    int height = switcher->height;

    struct ky_scene_node *node = ky_scene_node_from_tree(switcher->tree);

    int x = usable_area->x + usable_area->width / 2 - width / 2;
    int y = usable_area->y + usable_area->height / 2 - height / 2;
    ky_scene_rect_set_size(switcher->background, width, height);
    ky_scene_node_set_position(node, x, y);
    ky_scene_node_set_enabled(ky_scene_node_from_tree(switcher->tree), true);
    switcher->output_frame.notify = handle_output_frame;
    wl_signal_add(&kywc_output->events.frame, &switcher->output_frame);

    switcher->pending = 1;
    switcher->current = -1;
    switcher->dir = BOTTOM;
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
        seat_end_keyboard_grab(seat, &switcher->keyboard_grab);
        return;
    }

    if (!show_maximize_switcher()) {
        switcher->enable = false;
        return;
    }

    seat_start_keyboard_grab(seat, &switcher->keyboard_grab);
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
    struct maximize_switcher *switcher = wl_container_of(listener, switcher, theme_update);
    struct theme *theme = theme_manager_get_current();

    float color[4] = { theme->active_bg_color[0] * 0.8f, theme->active_bg_color[1] * 0.8f,
                       theme->active_bg_color[2] * 0.8f, theme->active_bg_color[3] * 0.8f };
    ky_scene_rect_set_color(switcher->background, color);

    color[0] = theme->accent_color[0];
    color[1] = theme->accent_color[1];
    color[2] = theme->accent_color[2];
    color[3] = 1.0;
    ky_scene_rect_set_color(switcher->select.left, color);
    ky_scene_rect_set_color(switcher->select.top, color);
    ky_scene_rect_set_color(switcher->select.right, color);
    ky_scene_rect_set_color(switcher->select.bottom, color);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&switcher->server_destroy.link);
    ky_scene_node_destroy(ky_scene_node_from_tree(switcher->tree));

    free(switcher);
    switcher = NULL;
}

bool maximize_switcher_create(struct view_manager *view_manager)
{
    struct maximize_switcher *manager = calloc(1, sizeof(struct maximize_switcher));
    if (!manager) {
        return false;
    }

    switcher = manager;
    struct view_layer *layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
    manager->tree = ky_scene_tree_create(layer->tree);
    ky_scene_node_set_enabled(ky_scene_node_from_tree(manager->tree), false);

    struct theme *theme = theme_manager_get_current();
    float color[4] = { theme->active_bg_color[0] * 0.8f, theme->active_bg_color[1] * 0.8f,
                       theme->active_bg_color[2] * 0.8f, theme->active_bg_color[3] * 0.8f };
    manager->background = ky_scene_rect_create(manager->tree, 0, 0, color);

    color[0] = theme->accent_color[0];
    color[1] = theme->accent_color[1];
    color[2] = theme->accent_color[2];
    color[3] = 1.0;
    manager->select.left = ky_scene_rect_create(manager->tree, 0, 0, color);
    manager->select.top = ky_scene_rect_create(manager->tree, 0, 0, color);
    manager->select.right = ky_scene_rect_create(manager->tree, 0, 0, color);
    manager->select.bottom = ky_scene_rect_create(manager->tree, 0, 0, color);

    manager->keyboard_grab.data = manager;
    manager->keyboard_grab.interface = &keyboard_grab_impl;

    manager->theme_update.notify = handle_theme_update;
    theme_manager_add_update_listener(&manager->theme_update);
    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &manager->server_destroy);

    switcher_register_shortcuts();

    return true;
}
