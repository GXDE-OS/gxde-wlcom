// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "input/cursor.h"
#include "input/seat.h"
#include "output.h"
#include "view/action.h"
#include "view_p.h"

static struct shortcut {
    char *keybind;
    char *desc;
    enum window_action action;
} shortcuts[] = {
    { "Alt+F10", "window maximized", WINDOW_ACTION_MAXIMIZE },
    { "Win+h", "window minimized", WINDOW_ACTION_MINIMIZE },
    { "Alt+F4", "window closed", WINDOW_ACTION_CLOSE },
    { "Alt+F7", "window move", WINDOW_ACTION_MOVE },
    { "Alt+F8", "window resize", WINDOW_ACTION_RESIZE },
    { "Alt+space", "window menu", WINDOW_ACTION_MENU },
    { "win+up", "window snap edge up", WINDOW_ACTION_SNAP_TOP },
    { "win+down", "window snap edge down", WINDOW_ACTION_SNAP_BOTTOM },
    { "win+left", "window snap edge left", WINDOW_ACTION_SNAP_LEFT },
    { "win+right", "window snap edge right", WINDOW_ACTION_SNAP_RIGHT },
};

static enum kywc_tile view_tile_invert(enum kywc_tile current, enum kywc_tile dir,
                                       bool has_extend_output)
{
    enum kywc_tile tiled = dir;
    switch (current) {
    case KYWC_TILE_LEFT:
        if (dir == KYWC_TILE_LEFT) {
            tiled = has_extend_output ? KYWC_TILE_RIGHT : KYWC_TILE_NONE;
        }
        break;
    case KYWC_TILE_RIGHT:
        if (dir == KYWC_TILE_RIGHT) {
            tiled = has_extend_output ? KYWC_TILE_LEFT : KYWC_TILE_NONE;
        }
        break;
    case KYWC_TILE_TOP:
        if (dir == KYWC_TILE_TOP) {
            tiled = has_extend_output ? KYWC_TILE_BOTTOM : KYWC_TILE_NONE;
        } else if (dir == KYWC_TILE_LEFT) {
            tiled = KYWC_TILE_TOP_LEFT;
        } else if (dir == KYWC_TILE_RIGHT) {
            tiled = KYWC_TILE_TOP_RIGHT;
        }
        break;
    case KYWC_TILE_BOTTOM:
        if (dir == KYWC_TILE_BOTTOM) {
            tiled = has_extend_output ? KYWC_TILE_TOP : KYWC_TILE_NONE;
        } else if (dir == KYWC_TILE_LEFT) {
            tiled = KYWC_TILE_BOTTOM_LEFT;
        } else if (dir == KYWC_TILE_RIGHT) {
            tiled = KYWC_TILE_BOTTOM_RIGHT;
        }
        break;
    default:
        break;
    }
    return tiled;
}

static void window_snap(struct view *view, enum kywc_tile dir)
{
    struct output *output = output_from_kywc_output(view->output);
    struct output *new_output = NULL;
    bool has_extend_output = false;
    enum kywc_tile tiled;

    if (view->base.tiled == dir) {
        switch (dir) {
        case KYWC_TILE_LEFT:
            new_output = output_adjacent_output(output, LAYOUT_EDGE_LEFT);
            break;
        case KYWC_TILE_RIGHT:
            new_output = output_adjacent_output(output, LAYOUT_EDGE_RIGHT);
            break;
        case KYWC_TILE_TOP:
            new_output = output_adjacent_output(output, LAYOUT_EDGE_TOP);
            break;
        case KYWC_TILE_BOTTOM:
            new_output = output_adjacent_output(output, LAYOUT_EDGE_BOTTOM);
            break;
        default:
            // cannot get here
            break;
        }
    }

    if (new_output && new_output != output) {
        output = new_output;
        has_extend_output = true;
    }

    tiled = view_tile_invert(view->base.tiled, dir, has_extend_output);

    kywc_view_set_tiled(&view->base, tiled, &output->base);
}

void window_action(struct view *view, struct seat *seat, enum window_action action)
{
    struct kywc_view *kywc_view = &view->base;
    int lx, ly;

    switch (action) {
    case WINDOW_ACTION_NONE:
        break;
    case WINDOW_ACTION_MINIMIZE:
        kywc_view_set_minimized(kywc_view, true);
        break;
    case WINDOW_ACTION_MAXIMIZE:
        kywc_view_toggle_maximized(kywc_view);
        break;
    case WINDOW_ACTION_FULLSCREEN:
        kywc_view_toggle_fullscreen(kywc_view);
        break;
    case WINDOW_ACTION_CLOSE:
        kywc_view_close(kywc_view);
        break;
    case WINDOW_ACTION_MOVE:
        /* seat cursor to middle of the view */
        lx = kywc_view->geometry.x + kywc_view->geometry.width / 2;
        ly = kywc_view->geometry.y + kywc_view->geometry.height / 2;
        cursor_move(seat->cursor, NULL, lx, ly, false, false);
        cursor_set_image(seat->cursor, CURSOR_DEFAULT);
        window_begin_move(view, seat);
        break;
    case WINDOW_ACTION_RESIZE:
        lx = kywc_view->geometry.x + kywc_view->geometry.width;
        ly = kywc_view->geometry.y + kywc_view->geometry.height;
        cursor_move(seat->cursor, NULL, lx + 8, ly + 8, false, false);
        cursor_set_image(seat->cursor, CURSOR_RESIZE_BOTTOM_RIGHT);
        window_begin_resize(view, KYWC_EDGE_RIGHT | KYWC_EDGE_BOTTOM, seat);
        break;
    case WINDOW_ACTION_KEEP_ABOVE:
        kywc_view_toggle_kept_above(kywc_view);
        break;
    case WINDOW_ACTION_KEEP_BELOW:
        kywc_view_toggle_kept_below(kywc_view);
        break;
    case WINDOW_ACTION_MENU:
        lx = kywc_view->geometry.x - kywc_view->margin.off_x / 2;
        ly = kywc_view->geometry.y - kywc_view->margin.off_y / 2;
        view_show_window_menu(view, seat, lx, ly);
        break;
    case WINDOW_ACTION_SNAP_TOP:
    case WINDOW_ACTION_SNAP_BOTTOM:
    case WINDOW_ACTION_SNAP_LEFT:
    case WINDOW_ACTION_SNAP_RIGHT:
        window_snap(view, action - WINDOW_ACTION_SNAP_TOP + 1);
        break;
    }
}

static void view_shortcuts(struct key_binding *binding, void *data)
{
    struct view *view = view_manager_get_activated();
    if (!view) {
        return;
    }

    struct shortcut *shortcut = data;
    window_action(view, input_manager_get_default_seat(), shortcut->action);
}

bool window_actions_create(struct view_manager *view_manager)
{
    for (size_t i = 0; i < sizeof(shortcuts) / sizeof(struct shortcut); i++) {
        struct shortcut *shortcut = &shortcuts[i];
        struct key_binding *binding = kywc_key_binding_create(shortcut->keybind, shortcut->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_key_binding_register(binding, view_shortcuts, shortcut)) {
            kywc_key_binding_destroy(binding);
            continue;
        }
    }
    return true;
}
