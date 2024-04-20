// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <kywc/identifier.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_seat.h>

#include "config.h"
#include "effect/capture.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "nls.h"
#include "output.h"
#include "scene/thumbnail.h"
#include "util/dir.h"
#include "view/action.h"
#include "view/workspace.h"
#include "view_p.h"

static struct window_shortcut {
    char *keybind;
    char *desc;
    enum window_action action;
} window_shortcuts[] = {
    { "Alt+F10", "window maximized", WINDOW_ACTION_MAXIMIZE },
    { "Alt+F9", "window minimized", WINDOW_ACTION_MINIMIZE },
    { "Alt+F4", "window closed", WINDOW_ACTION_CLOSE },
    { "Alt+F3", "window menu", WINDOW_ACTION_MENU },
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

#define MIRROR_BUFFER_DEBUG 0

struct view_capture {
    struct thumbnail *thumbnail;
    struct wl_listener thumbnail_update;
    struct wl_listener thumbnail_destroy;
#if MIRROR_BUFFER_DEBUG
    struct ky_scene_buffer *buffer;
    struct wl_event_source *timer;
#endif
};

static void view_capture_destroy(struct view_capture *capture)
{
    wl_list_remove(&capture->thumbnail_update.link);
    wl_list_remove(&capture->thumbnail_destroy.link);

#if MIRROR_BUFFER_DEBUG
    if (capture->buffer) {
        ky_scene_node_destroy(&capture->buffer->node);
    }
    wl_event_source_remove(capture->timer);
#endif
    if (capture->thumbnail) {
        thumbnail_destroy(capture->thumbnail);
    }

    free(capture);
}

static void capture_handle_thumbnail_destroy(struct wl_listener *listener, void *data)
{
    struct view_capture *capture = wl_container_of(listener, capture, thumbnail_destroy);
    capture->thumbnail = NULL;
    view_capture_destroy(capture);
}

#if MIRROR_BUFFER_DEBUG
static void capture_handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct view_capture *capture = wl_container_of(listener, capture, thumbnail_update);
    struct thumbnail_update_event *event = data;

    ky_scene_buffer_set_opacity(capture->buffer, 0.5);
    ky_scene_buffer_set_dest_size(capture->buffer, event->buffer->width, event->buffer->height);
    ky_scene_buffer_set_buffer(capture->buffer, event->buffer);

    thumbnail_mark_wants_update(capture->thumbnail, false);
    wl_event_source_timer_update(capture->timer, 250);
}

static int handle_capture(void *data)
{
    struct view_capture *capture = data;
    thumbnail_mark_wants_update(capture->thumbnail, true);
    return 0;
}
#else
static void capture_done(const char *path, void *data)
{
    config_notify(tr("Capture saved to"), path, "kylin-screenshot");
    free(data);
}

static void capture_handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct view_capture *capture = wl_container_of(listener, capture, thumbnail_update);
    struct thumbnail_update_event *event = data;

    const char *dir = dir_get_xdg_pictures();
    const char *file = kywc_identifier_time_generate("", ".png");
    const char *path =
        kywc_identifier_utf8_generate("%s/%s", dir_exists(dir) ? dir : getenv("HOME"), file);
    free((void *)dir);
    free((void *)file);

    capture_write_file(event->buffer, event->buffer->width, event->buffer->height, path,
                       capture_done, (void *)path);

    view_capture_destroy(capture);
}
#endif

static void window_capture_create(struct view *view, struct seat *seat)
{
    if (!view->base.mapped) {
        return;
    }

    struct view_capture *capture = calloc(1, sizeof(*capture));
    if (!capture) {
        return;
    }

    // struct ky_scene_buffer *buffer = ky_scene_buffer_try_from_surface(view->surface);
    capture->thumbnail = thumbnail_create_from_view(view, 0, 1.0);
    if (!capture->thumbnail) {
        free(capture);
        return;
    }

#if MIRROR_BUFFER_DEBUG
    struct view_layer *layer = view_manager_get_layer(LAYER_ON_SCREEN_DISPLAY, false);
    capture->buffer = ky_scene_buffer_create(layer->tree, NULL);
    ky_scene_node_set_input_bypassed(&capture->buffer->node, true);
    struct wl_event_loop *loop = wl_display_get_event_loop(seat->wlr_seat->display);
    capture->timer = wl_event_loop_add_timer(loop, handle_capture, capture);
#endif
    capture->thumbnail_update.notify = capture_handle_thumbnail_update;
    thumbnail_add_update_listener(capture->thumbnail, &capture->thumbnail_update);
    capture->thumbnail_destroy.notify = capture_handle_thumbnail_destroy;
    thumbnail_add_destroy_listener(capture->thumbnail, &capture->thumbnail_destroy);
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
        cursor_rebase(seat->cursor);
        window_begin_move(view, seat);
        break;
    case WINDOW_ACTION_RESIZE:
        lx = kywc_view->geometry.x + kywc_view->geometry.width;
        ly = kywc_view->geometry.y + kywc_view->geometry.height;
        cursor_move(seat->cursor, NULL, lx + 8, ly + 8, false, false);
        cursor_rebase(seat->cursor);
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
    case WINDOW_ACTION_CAPTURE:
        window_capture_create(view, seat);
        break;
    }
}

static void view_shortcuts(struct key_binding *binding, void *data)
{
    struct view *view = view_manager_get_activated();
    if (!view) {
        return;
    }

    struct window_shortcut *shortcut = data;
    window_action(view, input_manager_get_default_seat(), shortcut->action);
}

bool window_actions_create(struct view_manager *view_manager)
{
    for (size_t i = 0; i < sizeof(window_shortcuts) / sizeof(struct window_shortcut); i++) {
        struct window_shortcut *shortcut = &window_shortcuts[i];
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

enum {
    TOGGLE_SHOW_DESKTOP = 0,
    DO_SHOW_DESKTOP,
    DO_RESTORE_DESKTOP,
    MINIMIZE_ALL_VIEW,
};

static struct shortcut {
    char *keybind;
    char *desc;
    uint32_t action;
} shortcuts[] = {
    { "win+d:no", "toggle show desktop", TOGGLE_SHOW_DESKTOP },
    { "win+h", "do show desktop", DO_SHOW_DESKTOP },
    { "win+g", "do restore desktop", DO_RESTORE_DESKTOP },
    { "win+m", "minimize all view", MINIMIZE_ALL_VIEW },
};

static void view_manager_minimize_all_view(void)
{
    /* minimize all view in current workspace */
    struct workspace *workspace = workspace_manager_get_current();
    struct view *view;
    struct view_proxy *view_proxy;
    wl_list_for_each_reverse(view_proxy, &workspace->view_proxies, workspace_link) {
        view = view_proxy->view;
        /* skip views not mapped */
        if (!view->base.mapped) {
            continue;
        }

        kywc_view_set_minimized(&view->base, true);
    }
}

static void shortcuts_action(struct key_binding *binding, void *data)
{
    struct shortcut *shortcut = data;

    switch (shortcut->action) {
    case TOGGLE_SHOW_DESKTOP:
        view_manager_show_desktop(!view_manager_get_show_desktop(), true);
        break;
    case DO_SHOW_DESKTOP:
        view_manager_show_desktop(true, true);
        break;
    case DO_RESTORE_DESKTOP:
        view_manager_show_desktop(false, true);
        break;
    case MINIMIZE_ALL_VIEW:
        view_manager_minimize_all_view();
        break;
    }
}

bool view_manager_actions_create(struct view_manager *view_manager)
{
    for (size_t i = 0; i < sizeof(shortcuts) / sizeof(struct shortcut); i++) {
        struct shortcut *shortcut = &shortcuts[i];
        struct key_binding *binding = kywc_key_binding_create(shortcut->keybind, shortcut->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_key_binding_register(binding, shortcuts_action, shortcut)) {
            kywc_key_binding_destroy(binding);
            continue;
        }
    }
    return true;
}
