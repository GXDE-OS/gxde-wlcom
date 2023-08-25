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

static enum kywc_tile view_tile_invert(enum kywc_tile edge)
{
    switch (edge) {
    case KYWC_TILE_LEFT:
        return KYWC_TILE_RIGHT;
    case KYWC_TILE_RIGHT:
        return KYWC_TILE_LEFT;
    case KYWC_TILE_UP:
        return KYWC_TILE_DOWN;
    case KYWC_TILE_DOWN:
        return KYWC_TILE_UP;
    default:
        return KYWC_TILE_NONE;
    }
}

static void window_snap(struct view *view, enum kywc_tile tile)
{
    struct output *output = output_from_kywc_output(view->output);
    struct output *new_output = NULL;

    if (view->base.tiled == tile) {
        switch (tile) {
        case KYWC_TILE_LEFT:
            new_output = output_adjacent_output(output, LAYOUT_EDGE_LEFT);
            break;
        case KYWC_TILE_RIGHT:
            new_output = output_adjacent_output(output, LAYOUT_EDGE_RIGHT);
            break;
        case KYWC_TILE_UP:
            new_output = output_adjacent_output(output, LAYOUT_EDGE_TOP);
            break;
        case KYWC_TILE_DOWN:
            new_output = output_adjacent_output(output, LAYOUT_EDGE_BOTTOM);
            break;
        default:
            // cannot get here
            break;
        }
        if (new_output && new_output != output) {
            tile = view_tile_invert(tile);
            output = new_output;
        } else {
            /* restore to normal */
            tile = KYWC_TILE_NONE;
        }
    }

    kywc_view_set_tiled(&view->base, tile, &output->base);
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
        interactive_begin_move(view, seat);
        break;
    case WINDOW_ACTION_RESIZE:
        lx = kywc_view->geometry.x + kywc_view->geometry.width;
        ly = kywc_view->geometry.y + kywc_view->geometry.height;
        cursor_move(seat->cursor, NULL, lx + 8, ly + 8, false, false);
        cursor_set_image(seat->cursor, CURSOR_RESIZE_BOTTOM_RIGHT);
        interactive_begin_resize(view, KYWC_EDGE_RIGHT | KYWC_EDGE_BOTTOM, seat);
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
