// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include "view_p.h"

struct stack_mode_view {
    struct wl_list link;

    char *uuid;
    struct {
        bool maximized, fullscreen;
        enum kywc_tile tiled;
        struct kywc_box geometry;
    } saved;
};

static struct stack_mode_manager {
    struct wl_list stack_views;

    struct view_manager *view_manager;
} *manager = NULL;

static struct stack_mode_view *stack_mode_view_from_uuid(const char *uuid)
{
    if (!uuid) {
        return NULL;
    }

    struct stack_mode_view *stack_view;
    wl_list_for_each(stack_view, &manager->stack_views, link) {
        if (strcmp(stack_view->uuid, uuid) == 0) {
            return stack_view;
        }
    }

    return NULL;
}

static void stack_mode_view_destroy(struct stack_mode_view *stack_view)
{
    wl_list_remove(&stack_view->link);
    free(stack_view->uuid);
    free(stack_view);
}

static void stack_mode_view_save(struct view *view)
{
    struct stack_mode_view *stack_view = calloc(1, sizeof(struct stack_mode_view));
    if (!stack_view) {
        return;
    }

    wl_list_insert(&manager->stack_views, &stack_view->link);
    stack_view->uuid = strdup(view->base.uuid);
    stack_view->saved.maximized = view->base.maximized;
    stack_view->saved.fullscreen = view->base.fullscreen;
    stack_view->saved.tiled = view->base.tiled;
    stack_view->saved.geometry = view->base.geometry;
}

static void stack_mode_view_restore(struct view *view)
{
    struct stack_mode_view *stack_view = stack_mode_view_from_uuid(view->base.uuid);
    if (!stack_view) {
        return;
    }

    if (stack_view->saved.fullscreen) {
        view_do_fullscreen(view, true, view->output);
    } else if (stack_view->saved.maximized) {
        if (view->base.fullscreen) {
            view_do_fullscreen(view, false, view->output);
        }
        view_do_maximized(view, true, view->output);
    } else if (stack_view->saved.tiled) {
        if (view->base.fullscreen) {
            view_do_fullscreen(view, false, view->output);
        }
        if (view->base.maximized) {
            view_do_maximized(view, false, view->output);
        }
        view_do_tiled(view, stack_view->saved.tiled, view->output);
    } else {
        if (view->base.fullscreen) {
            view_do_fullscreen(view, false, view->output);
        }
        if (view->base.maximized) {
            view_do_maximized(view, false, view->output);
        }
        view_do_resize(view, &stack_view->saved.geometry);
    }
    stack_mode_view_destroy(stack_view);
}

static void stack_mode_view_map(struct view *view)
{
    /* init view position */
    positioner_add_new_view(view);
}

static void stack_mode_view_move(struct view *view, int x, int y)
{
    if (!view_is_movable(view)) {
        return;
    }
    view_do_move(view, x, y);
}

static void stack_mode_view_resize(struct view *view, struct kywc_box *geometry)
{
    if (!view_is_resizable(view)) {
        return;
    }
    view_do_resize(view, geometry);
}

static void stack_mode_view_minimized(struct view *view, bool minimized)
{
    if (!view_is_minimizable(view)) {
        return;
    }
    view_do_minimized(view, minimized);
}

static void stack_mode_view_maximized(struct view *view, bool maximized,
                                      struct kywc_output *kywc_output)
{
    if (!view_is_maximizable(view)) {
        return;
    }
    view_do_maximized(view, maximized, kywc_output);
}

static void stack_mode_view_fullscreen(struct view *view, bool fullscreen,
                                       struct kywc_output *kywc_output)
{
    if (!view_is_fullscreenable(view)) {
        return;
    }
    view_do_fullscreen(view, fullscreen, kywc_output);
}

static void stack_mode_view_tiled(struct view *view, enum kywc_tile tile,
                                  struct kywc_output *kywc_output)
{
    if (!view_is_resizable(view)) {
        return;
    }
    view_do_tiled(view, tile, kywc_output);
}

static void stack_mode_view_activate(struct view *view)
{
    if (!view_is_activatable(view)) {
        return;
    }

    struct view *descendant = view_find_descendant_modal(view);
    view_do_activate(descendant ? descendant : view);
    view_raise_to_top(view, true);
}

static void stack_mode_view_show_menu(struct view *view, struct seat *seat, int x, int y)
{
    if (!view->current_proxy) {
        return;
    }
    window_menu_show(view, seat, x, y);
}

static void stack_mode_view_show_split_screen_switcher(struct view *view, struct seat *seat,
                                                       bool enabled)
{
    if (!view->current_proxy) {
        return;
    }
    split_screen_switcher_show(view, seat, enabled);
}

static void stack_mode_view_click(struct seat *seat, struct view *view, uint32_t button,
                                  bool pressed, enum click_state state)
{
    struct kywc_view *kywc_view = &view->base;

    /* active current view */
    kywc_view_activate(kywc_view);
    view_set_focus(view, seat);
}

static void stack_mode_enter(void)
{
    struct view *view;
    struct view_manager *view_manager = manager->view_manager;
    wl_list_for_each(view, &view_manager->views, link) {
        if (!view->base.mapped || view->base.minimized ||
            view->base.role != KYWC_VIEW_ROLE_NORMAL) {
            continue;
        }
        stack_mode_view_restore(view);
    }

    struct stack_mode_view *stack_view, *tmp;
    wl_list_for_each_safe(stack_view, tmp, &manager->stack_views, link) {
        stack_mode_view_destroy(stack_view);
    }
}

static void stack_mode_leave(void)
{
    struct view_manager *view_manager = manager->view_manager;

    struct view *view;
    wl_list_for_each(view, &view_manager->views, link) {
        if (!view->base.mapped || view->base.role != KYWC_VIEW_ROLE_NORMAL) {
            continue;
        }
        stack_mode_view_save(view);
    }
}

static void stack_mode_destroy(void)
{
    if (!manager) {
        return;
    }

    struct stack_mode_view *stack_view, *tmp;
    wl_list_for_each_safe(stack_view, tmp, &manager->stack_views, link) {
        stack_mode_view_destroy(stack_view);
    }

    free(manager);
    manager = NULL;
}

static const struct view_mode_interface stack_mode_impl = {
    .name = "stack_mode",

    .view_map = stack_mode_view_map,
    .view_unmap = NULL,

    .view_request_move = stack_mode_view_move,
    .view_request_resize = stack_mode_view_resize,
    .view_request_minimized = stack_mode_view_minimized,
    .view_request_maximized = stack_mode_view_maximized,
    .view_request_fullscreen = stack_mode_view_fullscreen,
    .view_request_tiled = stack_mode_view_tiled,
    .view_request_activate = stack_mode_view_activate,

    .view_request_show_menu = stack_mode_view_show_menu,
    .view_request_show_split_screen_switcher = stack_mode_view_show_split_screen_switcher,

    .view_click = stack_mode_view_click,
    .view_hover = NULL,

    .view_mode_enter = stack_mode_enter,
    .view_mode_leave = stack_mode_leave,

    .mode_destroy = stack_mode_destroy,
};

void stack_mode_register(struct view_manager *view_manager)
{
    struct view_mode *mode = view_manager_mode_register(&stack_mode_impl);
    if (!mode) {
        return;
    }

    manager = calloc(1, sizeof(struct stack_mode_manager));
    if (!manager) {
        view_manager_mode_unregister(mode);
        return;
    }
    wl_list_init(&manager->stack_views);
    manager->view_manager = view_manager;
}
