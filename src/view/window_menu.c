// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "input/seat.h"
#include "nls.h"
#include "output.h"
#include "view/action.h"
#include "view/workspace.h"
#include "view_p.h"
#include "widget/menu.h"

/* window menu per seat */
struct window_menu {
    struct wl_list link;
    struct menu *root;

    struct menu_item *minimize;
    struct menu_item *maximize;
    struct menu_item *move;
    struct menu_item *resize;
    struct menu_item *keep_above;
    struct menu_item *all_workspace;
    struct menu_item *move_left;
    struct menu_item *move_right;
    struct menu_item *close;

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
    struct wl_listener output_configured;
    struct wl_listener server_destroy;
};

static struct window_menu_manager *manager = NULL;

/* the workspace the view is currently shown in (prefer the current workspace) */
static struct workspace *view_current_workspace(struct view *view)
{
    struct view_proxy *view_proxy = view_proxy_by_workspace(view, workspace_manager_get_current());
    if (!view_proxy && !wl_list_empty(&view->view_proxies)) {
        view_proxy = wl_container_of(view->view_proxies.next, view_proxy, view_link);
    }
    return view_proxy ? view_proxy->workspace : NULL;
}

static bool window_menu_action(struct menu_item *item, uint32_t key, void *data)
{
    struct window_menu *window_menu = data;
    struct view *view = window_menu->view;
    struct kywc_view *kywc_view = &view->base;
    struct workspace *workspace;

    if (item == window_menu->minimize) {
        window_action(view, window_menu->seat, WINDOW_ACTION_MINIMIZE);
    } else if (item == window_menu->maximize) {
        window_action(view, window_menu->seat, WINDOW_ACTION_MAXIMIZE);
    } else if (item == window_menu->move) {
        window_action(view, window_menu->seat, WINDOW_ACTION_MOVE);
    } else if (item == window_menu->resize) {
        window_action(view, window_menu->seat, WINDOW_ACTION_RESIZE);
    } else if (item == window_menu->keep_above) {
        /* the checkable item is not toggled by the menu, toggle and apply here */
        bool above = !item->checked;
        menu_item_set_checked(item, above);
        kywc_view_set_kept_above(kywc_view, above);
    } else if (item == window_menu->all_workspace) {
        bool all = !item->checked;
        menu_item_set_checked(item, all);
        if (all) {
            view_add_all_workspace(view);
        } else {
            view_set_workspace(view, workspace_manager_get_current());
        }
    } else if (item == window_menu->move_left) {
        workspace = view_current_workspace(view);
        if (workspace && workspace->position > 0) {
            view_set_workspace(view, workspace_by_position(workspace->position - 1));
        }
    } else if (item == window_menu->move_right) {
        workspace = view_current_workspace(view);
        if (workspace && workspace->position + 1 < workspace_manager_get_count()) {
            view_set_workspace(view, workspace_by_position(workspace->position + 1));
        }
    } else if (item == window_menu->close) {
        window_action(view, window_menu->seat, WINDOW_ACTION_CLOSE);
    } else {
        return false;
    }

    return true;
}

static void window_menu_update(struct window_menu *window_menu)
{
    struct view *view = window_menu->view;
    struct kywc_view *kywc_view = &view->base;

    menu_item_update_text(window_menu->maximize,
                          kywc_view->maximized ? tr("Restore") : tr("Maximize"));
    menu_item_set_activated(window_menu->minimize, view_is_minimizable(view));
    menu_item_set_activated(window_menu->maximize, view_is_maximizable(view));
    menu_item_set_activated(window_menu->move, view_is_movable(view));
    menu_item_set_activated(window_menu->resize, view_is_resizable(view));
    menu_item_set_checked(window_menu->keep_above, kywc_view->kept_above);
    menu_item_set_checked(window_menu->all_workspace, view->base.sticky);

    struct workspace *workspace = view_current_workspace(view);
    menu_item_set_activated(window_menu->move_left, workspace && workspace->position > 0);
    menu_item_set_activated(window_menu->move_right,
                            workspace &&
                                workspace->position + 1 < workspace_manager_get_count());
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

    ky_scene_node_raise_to_top(&manager->tree->node);
    wl_signal_add(&window_menu->view->base.events.destroy, &window_menu->view_destroy);

    window_menu_update(window_menu);
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

    /* create the root menu: flat action list, in GXDE KWin order */
    window_menu->root = menu_create(manager->tree, NULL);
    menu_set_fade_enabled(window_menu->root, true);

    window_menu->minimize =
        menu_add_item(window_menu->root, tr("Minimize"), 0, window_menu_action, window_menu);
    window_menu->maximize =
        menu_add_item(window_menu->root, tr("Maximize"), 0, window_menu_action, window_menu);
    window_menu->move =
        menu_add_item(window_menu->root, tr("Move"), 0, window_menu_action, window_menu);
    window_menu->resize =
        menu_add_item(window_menu->root, tr("Resize"), 0, window_menu_action, window_menu);
    window_menu->keep_above =
        menu_add_item(window_menu->root, tr("Always on Top"), 0, window_menu_action, window_menu);
    window_menu->all_workspace = menu_add_item(window_menu->root, tr("Always on Visible Workspace"),
                                               0, window_menu_action, window_menu);
    window_menu->move_left = menu_add_item(window_menu->root, tr("Move to Workspace Left"), 0,
                                           window_menu_action, window_menu);
    window_menu->move_right = menu_add_item(window_menu->root, tr("Move to Workspace Right"), 0,
                                            window_menu_action, window_menu);
    window_menu->close =
        menu_add_item(window_menu->root, tr("Close"), 0, window_menu_action, window_menu);

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

void window_menu_show(struct view *view, struct seat *seat, int x, int y)
{
    if (!manager) {
        return;
    }

    /* create or find a window menu for this seat */
    struct window_menu *window_menu = window_menu_by_seat(seat);
    if (!window_menu) {
        return;
    }

    if (window_menu->enabled) {
        window_menu_set_enabled(window_menu, false);
    }

    window_menu->view = view;
    window_menu->x = x;
    window_menu->y = y;
    window_menu_set_enabled(window_menu, true);
}

static void handle_output_configured(struct wl_listener *listener, void *data)
{
    /* disable all window menus when output configured */
    struct window_menu *window_menu;
    wl_list_for_each(window_menu, &manager->menus, link) {
        window_menu_set_enabled(window_menu, false);
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&manager->server_destroy.link);
    wl_list_remove(&manager->output_configured.link);

    struct window_menu *menu, *tmp;
    wl_list_for_each_safe(menu, tmp, &manager->menus, link) {
        wl_list_remove(&menu->link);
        free(menu);
    }

    /* free all menus by tree node destroy */
    ky_scene_node_destroy(&manager->tree->node);

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

    wl_list_init(&manager->menus);
    manager->tree = ky_scene_tree_create(view_manager->layers[LAYER_POPUP].tree);

    manager->output_configured.notify = handle_output_configured;
    output_manager_add_configured_listener(&manager->output_configured);

    /* create the default seat one */
    window_menu_create(input_manager_get_default_seat());

    return true;
}
