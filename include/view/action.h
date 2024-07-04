// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _ACTION_H_
#define _ACTION_H_

#include "view.h"

struct seat;
struct output;

enum window_action {
    WINDOW_ACTION_NONE = 0,
    WINDOW_ACTION_MINIMIZE,
    WINDOW_ACTION_MAXIMIZE,
    WINDOW_ACTION_FULLSCREEN,
    WINDOW_ACTION_CLOSE,
    WINDOW_ACTION_MOVE,
    WINDOW_ACTION_RESIZE,
    WINDOW_ACTION_KEEP_ABOVE,
    WINDOW_ACTION_KEEP_BELOW,
    WINDOW_ACTION_MENU,
    WINDOW_ACTION_TILE_TOP,
    WINDOW_ACTION_TILE_BOTTOM,
    WINDOW_ACTION_TILE_LEFT,
    WINDOW_ACTION_TILE_RIGHT,
    WINDOW_ACTION_TILE_TOP_HALF_SCREEN,
    WINDOW_ACTION_TILE_BOTTOM_HALF_SCREEN,
    WINDOW_ACTION_TILE_LEFT_HALF_SCREEN,
    WINDOW_ACTION_TILE_RIGHT_HALF_SCREEN,
    WINDOW_ACTION_CAPTURE,
};

void window_action(struct view *view, struct seat *seat, enum window_action action);

/* for interactive move and resize */
void window_begin_move(struct view *view, struct seat *seat);

void window_begin_resize(struct view *view, uint32_t edges, struct seat *seat);

void window_begin_tile(struct view *view, uint32_t key, struct seat *seat);

void window_begin_tile_half_screen(struct view *view, uint32_t key, struct seat *seat);

void window_move_constraints(struct kywc_view *kywc_view, struct output *output, int *x, int *y);

#endif
