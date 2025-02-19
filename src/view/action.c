// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdlib.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_seat.h>

#include <kywc/identifier.h>

#include "effect/capture.h"
#include "input/cursor.h"
#include "input/seat.h"
#include "nls.h"
#include "output.h"
#include "scene/thumbnail.h"
#include "util/dbus.h"
#include "util/file.h"
#include "util/string.h"
#include "view/action.h"
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
    { "win+up", "window tile up", WINDOW_ACTION_TILE_TOP },
    { "win+down", "window tile down", WINDOW_ACTION_TILE_BOTTOM },
    { "win+left", "window tile left", WINDOW_ACTION_TILE_LEFT },
    { "win+right", "window tile right", WINDOW_ACTION_TILE_RIGHT },
    { "win+alt+up", "window tiled to top half screen", WINDOW_ACTION_TILE_TOP_HALF_SCREEN },
    { "win+alt+down", "window tiled to bottom half screen", WINDOW_ACTION_TILE_BOTTOM_HALF_SCREEN },
    { "win+alt+left", "window tiled to left half screen", WINDOW_ACTION_TILE_LEFT_HALF_SCREEN },
    { "win+alt+right", "window tiled to right half screen", WINDOW_ACTION_TILE_RIGHT_HALF_SCREEN },
    { "win+shift+left", "window send to prev output", WINDOW_ACTION_SEND_LEFT_OUTPUT },
    { "win+shift+right", "window send to next output", WINDOW_ACTION_SEND_RIGHT_OUTPUT },
    { "ctrl+shift+left", "window send to prev output and maximize",
      WINDOW_ACTION_SEND_LEFT_OUTPUT_MAXIMIZE },
    { "ctrl+shift+right", "window send to next output and maximize",
      WINDOW_ACTION_SEND_RIGHT_OUTPUT_MAXIMIZE },
    { "ctrl+print", "window capture", WINDOW_ACTION_CAPTURE },
};

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
    char href[512];
    snprintf(href, 512, "<a href=\"file://%s\">%s</a>", path, path);
    dbus_notify(tr("Screenshot"), tr("Capture saved to"), href, "kylin-screenshot");
    free(data);
}

static void capture_handle_thumbnail_update(struct wl_listener *listener, void *data)
{
    struct view_capture *capture = wl_container_of(listener, capture, thumbnail_update);
    struct thumbnail_update_event *event = data;

    const char *dir = file_get_xdg_pictures_dir();
    const char *file = kywc_identifier_time_generate("", ".png");
    const char *path = string_create("%s/%s", dir, file);
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
    capture->thumbnail = thumbnail_create_from_view(view, THUMBNAIL_ENABLE_SECURITY, 1.0);
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

static struct output *target_output_by_view(struct view *view, enum window_action action)
{
    struct output *output = output_from_kywc_output(view->output);
    enum layout_edge edge = LAYOUT_EDGE_RIGHT;
    if (action == WINDOW_ACTION_SEND_LEFT_OUTPUT ||
        action == WINDOW_ACTION_SEND_LEFT_OUTPUT_MAXIMIZE) {
        edge = LAYOUT_EDGE_LEFT;
    }

    return output_find_specified_output(output, edge);
}

/* Move the view directly to the next output in equal proportions */
static void view_send_to_output(struct view *view, enum window_action action)
{
    if (view->base.fullscreen) {
        return;
    }

    struct output *output = target_output_by_view(view, action);
    if (output) {
        view_move_to_output(view, NULL, &output->base);
    }
}

/**
 * Move the view to the next output and maximize it,
 * but restore the original view size when the output state
 * of the original view is not the maximized state
 */
static void view_send_to_output_and_maximize(struct view *view, enum window_action action)
{
    if (view->base.fullscreen) {
        return;
    }

    struct output *output = target_output_by_view(view, action);
    if (!output) {
        return;
    }

    if (!view->saved.output || !view->base.maximized) {
        view->saved.tiled = view->base.maximized ? KYWC_TILE_ALL : view->base.tiled;
        view->saved.output = view->output;
    }

    view_move_to_output(view, NULL, &output->base);
    if (output_from_kywc_output(view->saved.output) != output) {
        kywc_view_set_maximized(&view->base, true, &output->base);
        return;
    }

    if (view->saved.tiled == KYWC_TILE_ALL || !view->saved.tiled) {
        kywc_view_set_maximized(&view->base, view->saved.tiled ? true : false, &output->base);
    } else {
        kywc_view_set_tiled(&view->base, view->saved.tiled, &output->base);
    }

    view->saved.output = NULL;
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
        if (kywc_view->ssd & KYWC_SSD_TITLE && !kywc_view->fullscreen) {
            lx = kywc_view->geometry.x - 8;
            ly = kywc_view->geometry.y;
            view_show_window_menu(view, seat, lx, ly);
        }
        break;
    case WINDOW_ACTION_TILE_TOP:
    case WINDOW_ACTION_TILE_BOTTOM:
    case WINDOW_ACTION_TILE_LEFT:
    case WINDOW_ACTION_TILE_RIGHT:
        window_begin_tile(view, action, seat);
        break;
    case WINDOW_ACTION_TILE_TOP_HALF_SCREEN:
    case WINDOW_ACTION_TILE_BOTTOM_HALF_SCREEN:
    case WINDOW_ACTION_TILE_LEFT_HALF_SCREEN:
    case WINDOW_ACTION_TILE_RIGHT_HALF_SCREEN:
        window_begin_tile_half_screen(view, action, seat);
        break;
    case WINDOW_ACTION_SEND_LEFT_OUTPUT:
    case WINDOW_ACTION_SEND_RIGHT_OUTPUT:
        view_send_to_output(view, action);
        break;
    case WINDOW_ACTION_SEND_LEFT_OUTPUT_MAXIMIZE:
    case WINDOW_ACTION_SEND_RIGHT_OUTPUT_MAXIMIZE:
        view_send_to_output_and_maximize(view, action);
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

        if (!kywc_key_binding_register(binding, KEY_BINDING_TYPE_WINDOW_ACTION, view_shortcuts,
                                       shortcut)) {
            kywc_key_binding_destroy(binding);
            continue;
        }
    }
    return true;
}

enum {
    TOGGLE_SHOW_DESKTOP = 0,
    SHOW_DESKTOP,
    RESTORE_DESKTOP,
    MINIMIZE_ALL_VIEWS,
    RESTORE_ALL_VIEWS,
    TOGGLE_SHOW_ACTIVE_ONLY,
};

static struct shortcut {
    char *keybind;
    char *desc;
    uint32_t action;
} shortcuts[] = {
    { "win+d:no", "toggle show desktop", TOGGLE_SHOW_DESKTOP },
    { "win+h", "show desktop", SHOW_DESKTOP },
    { "win+g", "restore desktop", RESTORE_DESKTOP },
    { "win+m", "minimize all views", MINIMIZE_ALL_VIEWS },
    { "win+shift+m", "restore all views", RESTORE_ALL_VIEWS },
    { "win+home", "toggle show active window only", TOGGLE_SHOW_ACTIVE_ONLY },
};

static struct gesture {
    enum gesture_type type;
    enum gesture_stage stage;
    uint8_t fingers;
    uint32_t devices;
    uint32_t directions;
    uint32_t edges;
    uint32_t follow_direction;
    double follow_threshold;
    char *desc;
    enum direction direction;
} gestures[] = {
    {
        GESTURE_TYPE_SWIPE,
        GESTURE_STAGE_TRIGGER,
        3,
        GESTURE_DEVICE_TOUCHPAD,
        GESTURE_DIRECTION_UP,
        GESTURE_EDGE_NONE,
        GESTURE_DIRECTION_NONE,
        0.0,
        "switch app or switcher",
        DIRECTION_UP,
    },
    {
        GESTURE_TYPE_SWIPE,
        GESTURE_STAGE_TRIGGER,
        3,
        GESTURE_DEVICE_TOUCHPAD,
        GESTURE_DIRECTION_DOWN,
        GESTURE_EDGE_NONE,
        GESTURE_DIRECTION_NONE,
        0.0,
        "hide switcher or show desktop",
        DIRECTION_DOWN,
    },
};

static void shortcuts_action(struct key_binding *binding, void *data)
{
    struct shortcut *shortcut = data;

    switch (shortcut->action) {
    case TOGGLE_SHOW_DESKTOP:
        view_manager_show_desktop(!view_manager_get_show_desktop(), true);
        break;
    case SHOW_DESKTOP:
    case MINIMIZE_ALL_VIEWS:
        view_manager_show_desktop(true, true);
        break;
    case RESTORE_DESKTOP:
    case RESTORE_ALL_VIEWS:
        view_manager_show_desktop(false, true);
        break;
    case TOGGLE_SHOW_ACTIVE_ONLY:
        view_manager_show_active_only(!view_manager_get_show_activte_only(), true);
        break;
    }
}

static void view_manager_show_switcher(bool show)
{
    if (!dbus_call_method("org.kylin.switch", "/MultitaskView", "org.kylin.switch.MultitaskView",
                          show ? "show" : "hide", NULL, NULL)) {
        kywc_log(KYWC_ERROR, "dbus call Multitaskview failed");
    }
}

static void gestures_action(struct gesture_binding *binding, void *data, double dx, double dy)
{
    struct gesture *gesture = data;
    switch (gesture->direction) {
    case DIRECTION_UP:
        if (!view_manager_get_show_desktop()) {
            view_manager_show_switcher(true);
        } else {
            view_manager_show_desktop(false, true);
        }
        break;
    case DIRECTION_DOWN:
        if (view_manager_get_show_switcher()) {
            view_manager_show_switcher(false);
        } else {
            view_manager_show_desktop(true, true);
        }
    default:
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

        if (!kywc_key_binding_register(binding, KEY_BINDING_TYPE_CTRL_VIEWS, shortcuts_action,
                                       shortcut)) {
            kywc_key_binding_destroy(binding);
            continue;
        }
    }

    for (size_t i = 0; i < sizeof(gestures) / sizeof(struct gesture); i++) {
        struct gesture *gesture = &gestures[i];
        struct gesture_binding *binding = kywc_gesture_binding_create(
            gesture->type, gesture->stage, gesture->devices, gesture->directions, gesture->edges,
            gesture->fingers, gesture->follow_direction, gesture->follow_threshold, gesture->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_gesture_binding_register(binding, gestures_action, gesture)) {
            kywc_gesture_binding_destroy(binding);
            continue;
        }
    }

    return true;
}
