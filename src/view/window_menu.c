// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdio.h>
#include <stdlib.h>

#include <linux/input-event-codes.h>

#include "input/seat.h"
#include "nls.h"
#include "view/action.h"
#include "view/workspace.h"
#include "view_p.h"
#include "widget/menu.h"

struct desktop_item {
    struct window_menu *window_menu;
    struct menu_item *item;
    uint32_t pos;
};

/* window menu per seat */
struct window_menu {
    struct wl_list link;
    struct menu *root;
    struct menu *more;
    struct menu *output;

    struct menu *desktop;
    struct desktop_item add_items[MAX_WORKSPACES];
    struct desktop_item move_items[MAX_WORKSPACES];
    struct menu_item *add_to;
    struct menu_item *move_to;

    struct seat *seat;
    struct wl_listener seat_destroy;

    struct view *view;
    struct wl_listener view_destroy;

    int x, y;
    bool enabled;
};

struct window_menu_manager {
    struct ky_scene_tree *tree;
    struct wl_list menus;
    struct wl_listener window_menu;
    struct wl_listener server_destroy;
};

static struct window_menu_manager *manager = NULL;

static bool window_menu_action(struct menu_item *item, uint32_t key, void *data)
{
    struct window_menu *window_menu = data;
    struct menu *menu = item->menu;
    enum window_action action = WINDOW_ACTION_NONE;

    if (menu == window_menu->root) {
        if (key == KEY_N) {
            action = WINDOW_ACTION_MINIMIZE;
        } else if (key == KEY_X) {
            action = WINDOW_ACTION_MAXIMIZE;
        } else if (key == KEY_C) {
            action = WINDOW_ACTION_CLOSE;
        }
    } else if (menu == window_menu->more) {
        if (key == KEY_M) {
            action = WINDOW_ACTION_MOVE;
        } else if (key == KEY_R) {
            action = WINDOW_ACTION_RESIZE;
        } else if (key == KEY_A) {
            action = WINDOW_ACTION_KEEP_ABOVE;
        } else if (key == KEY_B) {
            action = WINDOW_ACTION_KEEP_BELOW;
        } else if (key == KEY_F) {
            action = WINDOW_ACTION_FULLSCREEN;
        }
    } else if (menu == window_menu->desktop) {
        if (key == KEY_A) {
            view_add_all_workspace(window_menu->view);
        } else if (key == KEY_N) {
            struct workspace *workspace = workspace_create(NULL, workspace_manager_get_count());
            if (workspace) {
                view_add_workspace(window_menu->view, workspace);
            }
        } else if (key == KEY_M) {
            struct workspace *workspace = workspace_create(NULL, workspace_manager_get_count());
            if (workspace) {
                view_set_workspace(window_menu->view, workspace);
            }
        }
        return true;
    }

    if (action != WINDOW_ACTION_NONE) {
        window_action(window_menu->view, window_menu->seat, action);
        return true;
    }

    return false;
}

static bool add_desktop_action(struct menu_item *item, uint32_t key, void *data)
{
    struct desktop_item *desktop = data;
    struct workspace *workspace = workspace_by_position(desktop->pos);
    struct view *view = desktop->window_menu->view;

    if (item->checked) {
        view_remove_workspace(view, workspace);
    } else {
        view_add_workspace(view, workspace);
    }

    return true;
}

static bool move_desktop_action(struct menu_item *item, uint32_t key, void *data)
{
    struct desktop_item *desktop = data;
    struct workspace *workspace = workspace_by_position(desktop->pos);

    view_set_workspace(desktop->window_menu->view, workspace);
    return true;
}

static void window_menu_update_desktop(struct window_menu *window_menu)
{
    uint32_t count = workspace_manager_get_count();
    struct desktop_item *desktop;
    char name[64] = { 0 };

    for (uint32_t i = 0; i < count; i++) {
        desktop = &window_menu->add_items[i];
        snprintf(name, 64, "%s %d", tr("Desktop"), i + 1);
        if (!desktop->item) {
            desktop->item =
                menu_add_item(window_menu->desktop, name, 0, add_desktop_action, desktop);
        } else {
            menu_item_update_text(desktop->item, name);
        }
        menu_item_set_checked(desktop->item, false);
        menu_item_set_separator(desktop->item, i == 0);
        menu_item_lower_to_bottom(desktop->item);
        desktop->window_menu = window_menu;
        desktop->pos = i;
    }

    struct view_proxy *view_proxy;
    wl_list_for_each(view_proxy, &window_menu->view->view_proxies, view_link) {
        desktop = &window_menu->add_items[view_proxy->workspace->position];
        menu_item_set_checked(desktop->item, true);
    }

    for (uint32_t i = 0; i < count; i++) {
        desktop = &window_menu->move_items[i];
        snprintf(name, 64, "%s %d", tr("Move To Desktop"), i + 1);
        if (!desktop->item) {
            desktop->item =
                menu_add_item(window_menu->desktop, name, 0, move_desktop_action, desktop);
        } else {
            menu_item_update_text(desktop->item, name);
        }
        menu_item_set_separator(desktop->item, i == 0);
        menu_item_lower_to_bottom(desktop->item);
        desktop->window_menu = window_menu;
        desktop->pos = i;
    }

    menu_item_lower_to_bottom(window_menu->add_to);
    menu_item_lower_to_bottom(window_menu->move_to);
}

static void window_menu_set_enabled(struct window_menu *window_menu, bool enabled)
{
    if (window_menu->enabled == enabled) {
        return;
    }
    window_menu->enabled = enabled;

    if (!enabled) {
        wl_list_remove(&window_menu->view_destroy.link);
        window_menu->view = NULL;
        menu_hide_root(window_menu->root);
        return;
    }

    ky_scene_node_raise_to_top(ky_scene_node_from_tree(manager->tree));
    wl_signal_add(&window_menu->view->base.events.destroy, &window_menu->view_destroy);

    window_menu_update_desktop(window_menu);
    menu_show_root(window_menu->root, window_menu->seat, window_menu->x, window_menu->y);
}

static void window_menu_handle_view_destroy(struct wl_listener *listener, void *data)
{
    struct window_menu *window_menu = wl_container_of(listener, window_menu, view_destroy);
    window_menu_set_enabled(window_menu, false);
}

static void window_menu_handle_seat_destroy(struct wl_listener *listener, void *data)
{
    struct window_menu *window_menu = wl_container_of(listener, window_menu, seat_destroy);
    /* don't destroy the window menu, reuse it */
    window_menu->seat = NULL;
    wl_list_remove(&window_menu->seat_destroy.link);
    window_menu_set_enabled(window_menu, false);
}

static struct window_menu *window_menu_create(struct seat *seat)
{
    struct window_menu *window_menu = calloc(1, sizeof(struct window_menu));
    if (!window_menu) {
        return NULL;
    }

    wl_list_insert(&manager->menus, &window_menu->link);

    window_menu->seat = seat;
    window_menu->view_destroy.notify = window_menu_handle_view_destroy;
    window_menu->seat_destroy.notify = window_menu_handle_seat_destroy;
    wl_signal_add(&seat->events.destroy, &window_menu->seat_destroy);

    /* create the root menu: items and submenus */
    window_menu->root = menu_create(manager->tree, NULL);

    struct menu_item *desktop =
        menu_add_item(window_menu->root, tr("Desktop(D)"), KEY_D, NULL, NULL);
    window_menu->desktop = menu_create(NULL, desktop);
    menu_add_item(window_menu->desktop, tr("All Desktop(A)"), KEY_A, window_menu_action,
                  window_menu);
    window_menu->add_to = menu_add_item(window_menu->desktop, tr("Add To New Desktop(N)"), KEY_N,
                                        window_menu_action, window_menu);
    menu_item_set_separator(window_menu->add_to, true);
    window_menu->move_to = menu_add_item(window_menu->desktop, tr("Move To New Desktop(M)"), KEY_M,
                                         window_menu_action, window_menu);

    menu_add_item(window_menu->root, tr("Maximize(X)"), KEY_X, window_menu_action, window_menu);
    menu_add_item(window_menu->root, tr("Minimize(N)"), KEY_N, window_menu_action, window_menu);

    /* create the more action submenu */
    struct menu_item *more = menu_add_item(window_menu->root, tr("More(M)"), KEY_M, NULL, NULL);
    window_menu->more = menu_create(NULL, more);
    menu_add_item(window_menu->more, tr("Move(M)"), KEY_M, window_menu_action, window_menu);
    menu_add_item(window_menu->more, tr("Resize(R)"), KEY_R, window_menu_action, window_menu);
    menu_add_item(window_menu->more, tr("Keep-Above(A)"), KEY_A, window_menu_action, window_menu);
    menu_add_item(window_menu->more, tr("Keep-Below(B)"), KEY_B, window_menu_action, window_menu);
    menu_add_item(window_menu->more, tr("Fullscreen(F)"), KEY_F, window_menu_action, window_menu);

    menu_add_item(window_menu->root, tr("Close(C)"), KEY_C, window_menu_action, window_menu);

    return window_menu;
}

static struct window_menu *window_menu_by_seat(struct seat *seat)
{
    struct window_menu *window_menu, *empty_menu = NULL;
    wl_list_for_each(window_menu, &manager->menus, link) {
        if (window_menu->seat == seat) {
            return window_menu;
        }
        if (!empty_menu && !window_menu->seat) {
            empty_menu = window_menu;
        }
    }

    if (empty_menu) {
        empty_menu->seat = seat;
        return empty_menu;
    }

    return window_menu_create(seat);
}

static void handle_window_menu(struct wl_listener *listener, void *data)
{
    struct view_show_window_menu_event *event = data;
    /* create or find a window menu for this seat */
    struct window_menu *window_menu = window_menu_by_seat(event->seat);
    if (!window_menu) {
        return;
    }

    if (window_menu->enabled) {
        window_menu_set_enabled(window_menu, false);
    }

    window_menu->view = event->view;
    window_menu->x = event->x;
    window_menu->y = event->y;
    window_menu_set_enabled(window_menu, true);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->window_menu.link);

    struct window_menu *menu, *tmp;
    wl_list_for_each_safe(menu, tmp, &manager->menus, link) {
        wl_list_remove(&menu->link);
        free(menu);
    }

    /* free all menus by tree node destroy */
    ky_scene_node_destroy(ky_scene_node_from_tree(manager->tree));

    free(manager);
    manager = NULL;
}

bool window_menu_manager_create(struct view_manager *view_manager)
{
    manager = calloc(1, sizeof(struct window_menu_manager));
    if (!manager) {
        return false;
    }

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &manager->server_destroy);
    manager->window_menu.notify = handle_window_menu;
    wl_signal_add(&view_manager->events.window_menu, &manager->window_menu);

    wl_list_init(&manager->menus);
    manager->tree = ky_scene_tree_create(view_manager->layers[LAYER_POPUP].tree);

    /* create the default seat one */
    window_menu_create(input_manager_get_default_seat());

    return true;
}
