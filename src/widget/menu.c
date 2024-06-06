// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include <kywc/log.h>

#include "input/cursor.h"
#include "input/event.h"
#include "output.h"
#include "painter.h"
#include "theme.h"
#include "widget/menu.h"

#define SUB_MENU_GAP (2)

static void menu_draw_item(struct menu_item *item, bool force)
{
    if (!force && !item->redraw) {
        return;
    }
    item->redraw = false;

    struct theme *theme = theme_manager_get_current();
    uint32_t border_mask = BORDER_MASK_LEFT | BORDER_MASK_RIGHT;
    uint32_t corner_mask = CORNER_MASK_NONE;

    if (item->first) {
        border_mask |= BORDER_MASK_TOP;
        corner_mask |= CORNER_MASK_TOP_LEFT | CORNER_MASK_TOP_RIGHT;
    }
    if (item->last) {
        border_mask |= BORDER_MASK_BOTTOM;
        corner_mask |= CORNER_MASK_BOTTOM_LEFT | CORNER_MASK_BOTTOM_RIGHT;
    }
    if (item->separator) {
        border_mask |= BORDER_MASK_TOP;
    }

    uint32_t text_attr = item->checked ? TEXT_ATTR_CHECKED : TEXT_ATTR_NONE;
    text_attr |= item->submenu ? TEXT_ATTR_SUBMENU : TEXT_ATTR_NONE;
    text_attr |= item->key ? TEXT_ATTR_ACCEL : TEXT_ATTR_NONE;
    text_attr |= item->shortcut ? TEXT_ATTR_SHORTCUT : TEXT_ATTR_NONE;

    widget_set_text(item->content, item->text, TEXT_ALIGN_LEFT, text_attr);
    widget_set_shortcut(item->content, item->shortcut);
    widget_set_font(item->content, theme->font_name, theme->font_size);
    widget_set_size(item->content, item->menu->width, item->menu->item_height);

    float *backgrond_color = item->actived ? theme->active_bg_color : theme->inactive_bg_color;
    float *front_color = item->actived ? theme->active_text_color : theme->inactive_text_color;
    widget_set_backgrond_color(item->content,
                               (float[4]){ backgrond_color[0], backgrond_color[1],
                                           backgrond_color[2], theme->opacity / 100.0 });
    widget_set_front_color(item->content, front_color);
    widget_set_hovered_color(item->content, theme->accent_color);

    widget_set_border(item->content, theme->inactive_bg_color, border_mask, theme->border_width);
    widget_set_round_corner(item->content, corner_mask, theme->corner_radius);

    widget_update(item->content, true);
}

static void menu_render_items(struct menu *menu, bool force)
{
    if (wl_list_empty(&menu->items) || (!force && !menu->redraw)) {
        return;
    }
    menu->redraw = false;

    struct theme *theme = theme_manager_get_current();
    int max_width = 0, max_height = 0;
    int width = 0, height = 0;
    int item_count = 0;

    struct menu_item *item;
    wl_list_for_each_reverse(item, &menu->items, link) {
        painter_text_size(item->text, theme->font_name, theme->font_size, &width, &height);
        if (width > max_width) {
            max_width = width;
        }
        if (height > max_height) {
            max_height = height;
        }
        if (item->enabled) {
            item_count++;
        }
    }

    width = max_width * 2;
    height = max_height * 1.75;

    if (width != menu->width) {
        menu->width = width;
        force = true;
    }
    if (height != menu->item_height) {
        menu->item_height = height;
        force = true;
    }

    menu->height = menu->item_height * item_count;
    ky_scene_decoration_set_window_size(menu->deco, menu->width, menu->height);

    int index = 0;
    wl_list_for_each_reverse(item, &menu->items, link) {
        if (!item->enabled) {
            continue;
        }

        ky_scene_node_set_position(&item->tree->node, 0, index * menu->item_height);

        item->first = index == 0;
        item->last = ++index == item_count;
        menu_draw_item(item, force);
    }
}

static void menu_set_enabled(struct menu *menu, bool enabled)
{
    if (menu->enabled == enabled) {
        return;
    }

    menu->current = NULL;
    menu->hovered = NULL;
    menu->enabled = enabled;
    ky_scene_node_set_enabled(&menu->tree->node, enabled);

    if (enabled) {
        ky_scene_node_raise_to_top(menu->parent ? &menu->parent->tree->node : &menu->tree->node);
        menu_render_items(menu, false);
    }

    struct menu_item *item;
    wl_list_for_each_reverse(item, &menu->items, link) {
        if (!item->enabled) {
            continue;
        }
        ky_scene_node_set_enabled(&item->tree->node, enabled);
        if (!enabled && item->submenu) {
            menu_set_enabled(item->submenu, false);
        }
        widget_set_hovered(item->content, false);
        widget_set_enabled(item->content, enabled);
        widget_update(item->content, true);
    }

    if (menu->parent) {
        return;
    }

    /* clear grab when disable a root-menu */
    if (!enabled) {
        seat_end_pointer_grab(menu->seat, &menu->pointer_grab);
        seat_end_keyboard_grab(menu->seat, &menu->keyboard_grab);
        seat_end_touch_grab(menu->seat, &menu->touch_grab);
    } else {
        seat_start_pointer_grab(menu->seat, &menu->pointer_grab);
        seat_start_keyboard_grab(menu->seat, &menu->keyboard_grab);
        seat_start_touch_grab(menu->seat, &menu->touch_grab);
    }
}

static void menu_set_position(struct menu *menu, int x, int y)
{
    /* use (x, y) when root-menu, otherwise use parent item pos */
    int lx = x, ly = y;
    if (menu->parent) {
        ky_scene_node_coords(&menu->parent->tree->node, &lx, &ly);
    }

    struct kywc_output *kywc_output = kywc_output_at_point(lx, ly);
    struct output *output = output_from_kywc_output(kywc_output);
    struct kywc_box *geo = &output->geometry;

    /* keep menu visible in the output */
    int max_x = geo->x + geo->width;
    int max_y = geo->y + geo->height;

    if (!menu->parent) {
        if (lx + menu->width > max_x) {
            x = max_x - menu->width;
        }
        if (ly + menu->height > max_y) {
            y -= menu->height;
        }
    } else {
        /* default menu position */
        struct menu_item *parent = menu->parent;
        x = parent->menu->width + SUB_MENU_GAP;
        y = 0;

        if (lx + parent->menu->width + menu->width > max_x) {
            x = -menu->width + 4;
        }
        int off_y = ly + menu->height - max_y;
        if (off_y > 0) {
            y -= off_y;
        }
    }

    ky_scene_node_set_position(&menu->tree->node, x, y);
}

static bool menu_item_action(struct menu_item *item)
{
    if (item->action) {
        return item->action(item, item->key, item->data);
    }
    return false;
}

static void menu_item_set_hovered(struct menu_item *item)
{
    struct menu_item *hovered = item->menu->hovered;
    if (hovered == item) {
        return;
    }

    if (hovered) {
        widget_set_hovered(hovered->content, false);
        widget_update(hovered->content, true);
        if (hovered->submenu) {
            menu_set_enabled(hovered->submenu, false);
        }
    }

    widget_set_hovered(item->content, true);
    widget_update(item->content, true);
    item->menu->hovered = item;
}

static struct menu_item *menu_first_item(struct menu *menu)
{
    struct menu_item *item;
    wl_list_for_each_reverse(item, &menu->items, link) {
        if (item->enabled) {
            return item;
        }
    }
    return NULL;
}

static struct menu_item *menu_prev_or_next_item(struct menu *menu, struct wl_list *link, bool next)
{
    struct wl_list *node = next ? link->prev : link->next;
    /* skip list head */
    if (node == &menu->items) {
        node = next ? menu->items.prev : menu->items.next;
    }

    struct menu_item *item = wl_container_of(node, item, link);
    return item->enabled ? item : menu_prev_or_next_item(menu, node, next);
}

static void menu_hover_prev_or_next(struct menu *menu, bool next)
{
    if (wl_list_empty(&menu->items)) {
        return;
    }

    struct menu_item *item = menu->hovered
                                 ? menu_prev_or_next_item(menu, &menu->hovered->link, next)
                                 : menu_first_item(menu);
    if (item) {
        menu_item_set_hovered(item);
    }
}

static void submenu_show(struct menu *menu, bool hovered)
{
    menu_set_enabled(menu, true);
    menu_set_position(menu, 0, 0);
    menu->root->current = menu;
    if (hovered) {
        menu_hover_prev_or_next(menu, true);
    }
}

static bool menu_item_hover(struct seat *seat, struct ky_scene_node *node, double x, double y,
                            uint32_t time, bool first, bool hold, void *data)
{
    struct menu_item *item = data;

    if (!item->actived) {
        return false;
    }

    if (first) {
        cursor_set_image(seat->cursor, CURSOR_DEFAULT);
    } else if (item->menu->hovered == item) {
        return false;
    }

    menu_item_set_hovered(item);
    item->menu->root->current = item->menu;

    if (item->submenu) {
        submenu_show(item->submenu, false);
    }

    /* make sure parent item is hovered */
    if (item->menu->parent) {
        menu_item_set_hovered(item->menu->parent);
    }

    return false;
}

static void menu_item_leave(struct seat *seat, struct ky_scene_node *node, bool last, void *data)
{
    struct menu_item *item = data;
    /* don't if submenu is enabled */
    if (!item->action || (item->submenu && item->submenu->enabled)) {
        return;
    }

    if (item->menu->hovered == item) {
        widget_set_hovered(item->content, false);
        widget_update(item->content, true);
        item->menu->hovered = NULL;
    }
}

static void menu_item_click(struct seat *seat, struct ky_scene_node *node, uint32_t button,
                            bool pressed, uint32_t time, enum click_state state, void *data)
{
    struct menu_item *item = data;
    /* do actions when released */
    if (!item->actived || pressed) {
        return;
    }

    if (menu_item_action(item)) {
        menu_hide_root(item->menu->root);
    }
}

static const struct input_event_node_impl menu_item_impl = {
    .hover = menu_item_hover,
    .leave = menu_item_leave,
    .click = menu_item_click,
};

static struct ky_scene_node *menu_item_get_root(void *data)
{
    struct menu_item *item = data;
    struct menu *menu = item->menu;
    while (menu->parent) {
        menu = menu->parent->menu;
    }
    return &menu->tree->node;
}

static bool menu_shortcut(struct menu *menu, uint32_t key)
{
    struct menu_item *item;
    wl_list_for_each(item, &menu->items, link) {
        if (!item->enabled || item->key != key) {
            continue;
        }
        if (menu_item_action(item)) {
            return true;
        } else if (item->submenu) {
            submenu_show(item->submenu, true);
            menu_item_set_hovered(item);
        }
        break;
    }
    return false;
}

static bool keyboard_grab_key(struct seat_keyboard_grab *keyboard_grab, uint32_t time, uint32_t key,
                              bool pressed, uint32_t modifiers)
{
    if (!pressed) {
        return true;
    }

    struct menu *root = keyboard_grab->data;
    if (!root->current) {
        root->current = root;
    }
    struct menu *menu = root->current;

    switch (key) {
    case KEY_UP:
        menu_hover_prev_or_next(menu, false);
        break;
    case KEY_DOWN:
        menu_hover_prev_or_next(menu, true);
        break;
    case KEY_ESC:
        if (!menu->parent) {
            menu_hide_root(root);
            break;
        }
        // fallthrought to left key
    case KEY_LEFT:
        if (menu->parent) {
            menu_set_enabled(menu, false);
            root->current = menu->parent->menu;
        }
        break;
    case KEY_ENTER:
        if (menu->hovered) {
            if (menu_item_action(menu->hovered)) {
                menu_hide_root(root);
                break;
            }
        }
        // fallthrought to right key
    case KEY_RIGHT:
        if (menu->hovered && menu->hovered->submenu) {
            submenu_show(menu->hovered->submenu, true);
        }
        break;
    default:
        if (menu_shortcut(menu, key)) {
            menu_hide_root(root);
        }
        break;
    }

    return true;
}

static void keyboard_grab_cancel(struct seat_keyboard_grab *keyboard_grab)
{
    struct menu *root = keyboard_grab->data;
    menu_hide_root(root);
}

static const struct seat_keyboard_grab_interface keyboard_grab_impl = {
    .key = keyboard_grab_key,
    .cancel = keyboard_grab_cancel,
};

static void pointer_grab_cancel(struct seat_pointer_grab *pointer_grab)
{
    struct menu *root = pointer_grab->data;
    menu_hide_root(root);
}

static bool pointer_grab_button(struct seat_pointer_grab *pointer_grab, uint32_t time,
                                uint32_t button, bool pressed)
{
    struct menu *root = pointer_grab->data;
    struct seat *seat = pointer_grab->seat;

    /* check current hover node in the window menu tree */
    struct input_event_node *inode = input_event_node_from_node(seat->cursor->hover.node);
    struct ky_scene_node *node = input_event_node_root(inode);
    if (node == &root->tree->node) {
        inode->impl->click(seat, seat->cursor->hover.node, button, pressed, time, CLICK_STATE_NONE,
                           inode->data);
    } else if (pressed) {
        menu_hide_root(root);
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
    struct menu *root = touch_grab->data;
    return pointer_grab_button(&root->pointer_grab, time, BTN_LEFT, down);
}

static bool touch_grab_motion(struct seat_touch_grab *touch_grab, uint32_t time, double lx,
                              double ly)
{
    return false;
}

static void touch_grab_cancel(struct seat_touch_grab *touch_grab)
{
    struct menu *root = touch_grab->data;
    menu_hide_root(root);
}

static const struct seat_touch_grab_interface touch_grab_impl = {
    .touch = touch_grab_touch,
    .motion = touch_grab_motion,
    .cancel = touch_grab_cancel,
};

void menu_item_set_enabled(struct menu_item *item, bool enabled)
{
    if (item->enabled == enabled) {
        return;
    }
    item->enabled = enabled;
    item->menu->redraw = true;
    // TODO: update when menu is enabled
}

// TODO: update when item is enabled
void menu_item_set_checked(struct menu_item *item, bool checked)
{
    if (item->checked == checked) {
        return;
    }
    item->checked = checked;
    item->redraw = true;
    item->menu->redraw = true;
}

void menu_item_set_separator(struct menu_item *item, bool separator)
{
    if (item->separator == separator) {
        return;
    }
    item->separator = separator;
    item->redraw = true;
    item->menu->redraw = true;
}

void menu_item_set_actived(struct menu_item *item, bool actived)
{
    if (item->actived == actived) {
        return;
    }
    item->actived = actived;
    item->redraw = true;
    item->menu->redraw = true;
}

void menu_item_update_text(struct menu_item *item, const char *text)
{
    if (strcmp(text, item->text) == 0) {
        return;
    }

    free(item->text);
    item->text = strdup(text);
    item->redraw = true;
    item->menu->redraw = true;
}

void menu_item_place_above(struct menu_item *item, struct menu_item *sibling)
{
    assert(item != sibling);
    assert(item->menu == sibling->menu);

    if (item->link.prev == &sibling->link) {
        return;
    }

    wl_list_remove(&item->link);
    wl_list_insert(&sibling->link, &item->link);
    item->menu->redraw = true;
}

void menu_item_place_below(struct menu_item *item, struct menu_item *sibling)
{
    assert(item != sibling);
    assert(item->menu == sibling->menu);

    if (item->link.next == &sibling->link) {
        return;
    }

    wl_list_remove(&item->link);
    wl_list_insert(sibling->link.prev, &item->link);
    item->menu->redraw = true;
}

void menu_item_raise_to_top(struct menu_item *item)
{
    struct menu_item *top = wl_container_of(item->menu->items.prev, top, link);
    if (item == top) {
        return;
    }
    menu_item_place_above(item, top);
}

void menu_item_lower_to_bottom(struct menu_item *item)
{
    struct menu_item *bottom = wl_container_of(item->menu->items.next, bottom, link);
    if (item == bottom) {
        return;
    }
    menu_item_place_below(item, bottom);
}

static void item_handle_destroy(struct wl_listener *listener, void *data)
{
    struct menu_item *item = wl_container_of(listener, item, destroy);
    wl_list_remove(&item->destroy.link);
    wl_list_remove(&item->link);
    free(item->text);
    free(item->shortcut);
    free(item);
}

void menu_item_add_shortcut(struct menu_item *item, const char *text)
{
    if ((!text && !item->shortcut) ||
        (item->shortcut && text && strcmp(item->shortcut, text) == 0)) {
        return;
    }
    free(item->shortcut);
    item->shortcut = strdup(text);
    item->redraw = true;
    item->menu->redraw = true;
}

struct menu_item *menu_add_item(struct menu *menu, const char *text, uint32_t key,
                                bool (*action)(struct menu_item *item, uint32_t key, void *data),
                                void *data)
{
    struct menu_item *item = calloc(1, sizeof(struct menu_item));
    if (!item) {
        return NULL;
    }

    item->menu = menu;
    wl_list_insert(&menu->items, &item->link);
    item->menu->redraw = true;
    item->redraw = true;
    item->enabled = true;
    item->actived = true;

    item->data = data;
    item->text = strdup(text);
    item->key = key;
    item->action = action;

    item->tree = ky_scene_tree_create(menu->tree);
    item->destroy.notify = item_handle_destroy;
    /* tree destroy event is before node destroy */
    wl_signal_add(&item->tree->node.events.destroy, &item->destroy);
    /* use widget to create a scene buffer */
    item->content = widget_create(item->tree);
    input_event_node_create(ky_scene_node_from_widget(item->content), &menu_item_impl,
                            menu_item_get_root, NULL, item);

    return item;
}

static void menu_handle_destroy(struct wl_listener *listener, void *data)
{
    struct menu *menu = wl_container_of(listener, menu, destroy);
    wl_list_remove(&menu->destroy.link);
    wl_list_remove(&menu->theme_update.link);

    struct menu_item *item, *tmp;
    wl_list_for_each_safe(item, tmp, &menu->items, link) {
        wl_list_remove(&item->link);
        wl_list_init(&item->link);
    }

    free(menu);
}

static void menu_update_decoration(struct menu *menu)
{
    struct theme *theme = theme_manager_get_current();
    int radius = theme->corner_radius;
    int shadow = theme->shadow_border;

    ky_scene_decoration_set_margin(menu->deco, 0, 0, shadow);
    ky_scene_decoration_set_round_corner_radius(menu->deco,
                                                (int[4]){ radius, radius, radius, radius });
    ky_scene_node_set_position(ky_scene_node_from_decoration(menu->deco), -shadow, -shadow);
    ky_scene_decoration_set_blurred(menu->deco, theme->opacity != 100);
}

static void menu_handle_theme_update(struct wl_listener *listener, void *data)
{
    struct menu *menu = wl_container_of(listener, menu, theme_update);
    struct theme_update_event *update_event = data;
    uint32_t allowed_mask = THEME_UPDATE_MASK_FONT | THEME_UPDATE_MASK_BACKGROUND_COLOR |
                            THEME_UPDATE_MASK_ACCENT_COLOR | THEME_UPDATE_MASK_CORNER_RADIUS |
                            THEME_UPDATE_MASK_OPACITY;
    if (update_event->update_mask & allowed_mask) {
        /* force update all items */
        menu_render_items(menu, true);
        menu_update_decoration(menu);
    }
}

struct menu *menu_create(struct ky_scene_tree *parent, struct menu_item *parent_item)
{
    struct menu *menu = calloc(1, sizeof(struct menu));
    if (!menu) {
        return NULL;
    }

    parent = parent_item ? parent_item->tree : parent;
    menu->tree = ky_scene_tree_create(parent);
    ky_scene_node_set_enabled(&menu->tree->node, false);
    menu->destroy.notify = menu_handle_destroy;
    wl_signal_add(&menu->tree->node.events.destroy, &menu->destroy);

    wl_list_init(&menu->items);
    menu->parent = parent_item;
    menu->redraw = true;

    if (parent_item) { // is a submenu
        parent_item->submenu = menu;
        menu->root = parent_item->menu->root;
    } else {
        menu->root = menu;
        menu->pointer_grab.data = menu;
        menu->pointer_grab.interface = &pointer_grab_impl;
        menu->keyboard_grab.data = menu;
        menu->keyboard_grab.interface = &keyboard_grab_impl;
        menu->touch_grab.data = menu;
        menu->touch_grab.interface = &touch_grab_impl;
    }

    /* create shadow and blur support */
    menu->deco = ky_scene_decoration_create(menu->tree);
    ky_scene_decoration_set_shadow_mask(menu->deco, SHADOW_MASK_ALL);
    menu_update_decoration(menu);

    menu->theme_update.notify = menu_handle_theme_update;
    theme_manager_add_update_listener(&menu->theme_update);

    return menu;
}

void menu_destroy(struct menu *menu)
{
    menu_set_enabled(menu, false);

    if (menu->parent) {
        menu->parent->submenu = NULL;
    }

    ky_scene_node_destroy(&menu->tree->node);
}

void menu_show_root(struct menu *menu, struct seat *seat, int x, int y)
{
    assert(menu->parent == NULL);
    /* update root menu with the new seat and position */
    if (menu->enabled) {
        menu_set_enabled(menu, false);
    }

    menu->seat = seat;
    menu_set_enabled(menu, true);
    menu_set_position(menu, x, y);
}

void menu_hide_root(struct menu *menu)
{
    assert(menu->parent == NULL);
    menu_set_enabled(menu, false);
    menu->seat = NULL;
}
