// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#ifndef _WIDGET_MENU_H_
#define _WIDGET_MENU_H_

#include "input/seat.h"
#include "scene/decoration.h"
#include "widget.h"

struct menu_item {
    struct ky_scene_tree *tree;
    struct wl_listener destroy; // tree node destroy
    struct widget *content;

    struct wl_list link; // menu::items
    struct menu *menu;
    struct menu *submenu; // may be NULL

    char *text;
    bool first, last, checked, separator;
    bool enabled, redraw, actived;

    uint32_t key; // shortcut key

    bool (*action)(struct menu_item *item, uint32_t key, void *data);
    void *data;
};

struct menu {
    struct ky_scene_tree *tree;
    struct wl_listener destroy;

    /* for shadow and blur */
    struct ky_scene_decoration *deco;

    struct wl_list items;
    struct menu_item *parent; // NULL if root-menu
    struct menu_item *hovered;
    struct menu *root; // root menu

    /* redraw menu and items */
    struct wl_listener theme_update;

    int width, height, item_height;
    bool enabled, redraw;

    void *data;

    // only used when root menu
    struct menu *current; // current menu in operation

    struct seat *seat;
    struct seat_pointer_grab pointer_grab;
    struct seat_keyboard_grab keyboard_grab;
    struct seat_touch_grab touch_grab;
};

/**
 *  root-menu when parent_item is NULL and parent is not NULL
 *  submenu when parent_item is not NULL, parent is not used
 */
struct menu *menu_create(struct ky_scene_tree *parent, struct menu_item *parent_item);

void menu_destroy(struct menu *menu);

struct menu_item *menu_add_item(struct menu *menu, const char *text, uint32_t key,
                                bool (*action)(struct menu_item *item, uint32_t key, void *data),
                                void *data);

void menu_item_set_enabled(struct menu_item *item, bool enabled);

void menu_item_set_checked(struct menu_item *item, bool checked);

void menu_item_set_separator(struct menu_item *item, bool separator);

void menu_item_set_actived(struct menu_item *item, bool actived);

void menu_item_update_text(struct menu_item *item, const char *text);

void menu_item_raise_to_top(struct menu_item *item);

void menu_item_lower_to_bottom(struct menu_item *item);

void menu_item_place_above(struct menu_item *item, struct menu_item *sibling);

void menu_item_place_below(struct menu_item *item, struct menu_item *sibling);

/**
 * show a root-menu at (x, y), support input with specific seat
 */
void menu_show_root(struct menu *menu, struct seat *seat, int x, int y);

/*
 * hide a root-menu
 */
void menu_hide_root(struct menu *menu);

#endif /* _WIDGET_MENU_H_ */
