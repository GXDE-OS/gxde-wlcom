// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>

#include "view_p.h"

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

static void stack_mode_view_click(struct seat *seat, struct view *view, uint32_t button,
                                  bool pressed, enum click_state state)
{
    struct kywc_view *kywc_view = &view->base;

    /* active current view */
    kywc_view_activate(kywc_view);
    seat_focus_surface(seat, view->surface);
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

    .view_click = stack_mode_view_click,
    .view_hover = NULL,

    .view_mode_enter = NULL,
    .view_mode_leave = NULL,

    .mode_destroy = NULL,
};

void stack_mode_register(struct view_manager *view_manager)
{
    view_manager_mode_register(&stack_mode_impl);
}