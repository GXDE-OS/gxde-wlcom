// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <kywc/binding.h>
#include <kywc/identifier.h>
#include <kywc/log.h>

#include "effect/translation.h"
#include "input/input.h"
#include "nls.h"
#include "server.h"
#include "view/workspace.h"
#include "view_p.h"

struct workspace_manager {
    struct view_manager *view_manager;

    /* current activated workspace */
    struct workspace *current;
    struct workspace **workspaces;

    struct {
        struct wl_signal new_workspace;
    } events;

    struct wl_listener display_destroy;
    struct wl_listener server_ready;
    struct wl_listener server_destroy;

    uint32_t count, rows, columns;
};

static struct workspace_manager *workspace_manager = NULL;

static struct shortcut {
    char *keybind;
    char *desc;
    int switch_workspace;
} shortcuts[] = {
    { "Ctrl+Alt+Left:no", "switch to left workspace", DIRECTION_LEFT },
    { "Ctrl+Alt+Right:no", "switch to right workspace", DIRECTION_RIGHT },
    { "Ctrl+Alt+Up:no", "switch to up workspace", DIRECTION_UP },
    { "Ctrl+Alt+Down:no", "switch to down workspace", DIRECTION_DOWN },
    { "Ctrl+F1", "switch to workspace 0", 4 },
    { "Ctrl+F2", "switch to workspace 1", 5 },
    { "Ctrl+F3", "switch to workspace 2", 6 },
    { "Ctrl+F4", "switch to workspace 3", 7 },
};

static struct gesture {
    enum gesture_type type;
    uint8_t fingers;
    uint32_t devices;
    uint32_t directions;
    uint32_t edges;
    char *desc;
    enum direction direction;
} gestures[] = {
    { GESTURE_TYPE_SWIPE, 3, GESTURE_DEVICE_TOUCHPAD | GESTURE_DEVICE_TOUCHSCREEN,
      GESTURE_DIRECTION_LEFT, GESTURE_EDGE_NONE, "switch to left workspace", DIRECTION_LEFT },
    { GESTURE_TYPE_SWIPE, 3, GESTURE_DEVICE_TOUCHPAD | GESTURE_DEVICE_TOUCHSCREEN,
      GESTURE_DIRECTION_RIGHT, GESTURE_EDGE_NONE, "switch to right workspace", DIRECTION_RIGHT },
};

static void workspace_switch_to(int switch_workspace)
{
    if (workspace_manager->count == 1) {
        kywc_log(KYWC_INFO, "only one workspace, no need to switch");
        return;
    }

    int32_t current = workspace_manager->current->position;
    int32_t column = workspace_manager->columns;
    int32_t row = workspace_manager->rows - 1;
    int32_t last = workspace_manager->count - 1;
    int32_t pending = current;

    if (switch_workspace == DIRECTION_LEFT) {
        pending = current - 1;
        pending = pending < 0 ? last : pending;
    } else if (switch_workspace == DIRECTION_RIGHT) {
        pending = current + 1;
        pending = pending > last ? 0 : pending;
    } else if (switch_workspace == DIRECTION_UP) {
        pending = current - column;
        pending = pending < 0 ? current + row * column : pending;
        pending = pending > last ? pending - column : pending;
    } else if (switch_workspace == DIRECTION_DOWN) {
        pending = current + column;
        pending = pending > last ? current - row * column : pending;
        pending = pending < 0 ? pending + column : pending;
        /* add ctrl+f1...f4 to workspace */
    } else {
        pending = switch_workspace - 4;
        if (pending == current || pending > last) {
            return;
        }
    }

    if (!workspace_add_translation_effect(workspace_manager->workspaces[current],
                                          workspace_manager->workspaces[pending],
                                          switch_workspace)) {
        workspace_activate(workspace_manager->workspaces[pending]);
    }
}

static void shortcut_action(struct key_binding *binding, void *data)
{
    struct shortcut *shortcut = data;
    workspace_switch_to(shortcut->switch_workspace);
}

static void gesture_action(struct gesture_binding *binding, void *data)
{
    struct gesture *gesture = data;
    workspace_switch_to(gesture->direction);
}

static void workspace_register_shortcut(void)
{
    for (size_t i = 0; i < sizeof(shortcuts) / sizeof(struct shortcut); i++) {
        struct shortcut *shortcut = &shortcuts[i];
        struct key_binding *binding = kywc_key_binding_create(shortcut->keybind, shortcut->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_key_binding_register(binding, shortcut_action, shortcut)) {
            kywc_key_binding_destroy(binding);
            continue;
        }
    }

    for (size_t i = 0; i < sizeof(gestures) / sizeof(struct gesture); i++) {
        struct gesture *gesture = &gestures[i];
        struct gesture_binding *binding =
            kywc_gesture_binding_create(gesture->type, gesture->devices, gesture->directions,
                                        gesture->edges, gesture->fingers, gesture->desc);
        if (!binding) {
            continue;
        }

        if (!kywc_gesture_binding_register(binding, gesture_action, gesture)) {
            kywc_gesture_binding_destroy(binding);
            continue;
        }
    }
}

static void workspace_manager_update_count(uint32_t count)
{
    uint32_t column = count / workspace_manager->rows;
    if (count % workspace_manager->rows > 0) {
        column++;
    }

    workspace_manager->count = count;
    workspace_manager->columns = column;
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&workspace_manager->server_destroy.link);

    free(workspace_manager->workspaces);
    free(workspace_manager);
    workspace_manager = NULL;
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    wl_list_remove(&workspace_manager->display_destroy.link);

    workspace_manager->current = NULL;
    /* workspace_destroy will update count */
    while (workspace_manager->count) {
        workspace_destroy(workspace_manager->workspaces[0]);
    }
}

static void handle_server_ready(struct wl_listener *listener, void *data)
{
    /* create the default workspace and activate it */
    workspace_activate(workspace_create(NULL, 0));
    /* create workspaces according to configuration */
    for (uint32_t i = 1; i < workspace_manager->view_manager->state.num_workspaces; i++) {
        workspace_create(NULL, i);
    }
}

bool workspace_manager_create(struct view_manager *view_manager)
{
    workspace_manager = calloc(1, sizeof(struct workspace_manager));
    if (!workspace_manager) {
        return false;
    }

    workspace_manager->view_manager = view_manager;
    workspace_manager->workspaces = calloc(MAX_WORKSPACES, sizeof(struct workspace *));
    workspace_manager->rows = 2;

    wl_signal_init(&workspace_manager->events.new_workspace);

    workspace_manager->server_ready.notify = handle_server_ready;
    wl_signal_add(&view_manager->server->events.ready, &workspace_manager->server_ready);
    workspace_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &workspace_manager->server_destroy);
    workspace_manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(view_manager->server->display,
                                    &workspace_manager->display_destroy);

    ky_workspace_manager_create(view_manager->server);
    /* kde-plasma-virtual-desktop support */
    kde_virtual_desktop_management_create(view_manager->server);

    workspace_register_shortcut();

    return true;
}

void workspace_manager_add_new_listener(struct wl_listener *listener)
{
    wl_signal_add(&workspace_manager->events.new_workspace, listener);
}

void workspace_manager_set_rows(uint32_t rows)
{
    if (rows == workspace_manager->rows) {
        return;
    }

    workspace_manager->rows = rows;
    /* update columns */
    workspace_manager_update_count(workspace_manager->count);
}

uint32_t workspace_manager_get_rows(void)
{
    return workspace_manager->rows;
}

struct workspace *workspace_manager_get_current(void)
{
    return workspace_manager->current;
}

uint32_t workspace_manager_get_count(void)
{
    return workspace_manager->count;
}

static void workspace_set_enabled(struct workspace *workspace, bool enabled)
{
    for (int i = 0; i < 3; i++) {
        ky_scene_node_set_enabled(&workspace->layers[i].tree->node, enabled);
    }
}

static void workspace_update_name(struct workspace *workspace, const char *name)
{
    if (workspace->has_custom_name && !name) {
        return;
    }

    free((void *)workspace->name);
    workspace->name =
        name ? strdup(name)
             : kywc_identifier_utf8_generate("%s %d", tr("Desktop"), workspace->position + 1);
    workspace->has_custom_name = !!name;

    wl_signal_emit_mutable(&workspace->events.name, NULL);
}

/* auto add views in all workspaces */
static void workspace_auto_add_views(struct workspace *workspace)
{
    if (!workspace_manager->view_manager->server->ready) {
        return;
    }
    struct workspace *first_workspace = workspace_manager->workspaces[0];
    struct view_proxy *view_proxy;
    wl_list_for_each(view_proxy, &first_workspace->view_proxies, workspace_link) {
        struct view *view = view_proxy->view;
        if (view->show_in_all_workspaces) {
            view_add_workspace(view, workspace);
        }
    }
}

struct workspace *workspace_create(const char *name, uint32_t position)
{
    /* too many workspaces, reject it */
    if (workspace_manager->count == MAX_WORKSPACES) {
        return NULL;
    }

    struct workspace *workspace = calloc(1, sizeof(struct workspace));
    if (!workspace) {
        return NULL;
    };

    if (position > workspace_manager->count) {
        position = workspace_manager->count;
    }

    wl_list_init(&workspace->view_proxies);
    wl_signal_init(&workspace->events.name);
    wl_signal_init(&workspace->events.position);
    wl_signal_init(&workspace->events.activate);
    wl_signal_init(&workspace->events.destroy);
    wl_signal_init(&workspace->events.view_enter);
    wl_signal_init(&workspace->events.view_leave);

    /* create 3 tree per workspace and disabled default */
    struct view_layer *layers = workspace_manager->view_manager->layers;
    workspace->layers[0].layer = LAYER_BELOW;
    workspace->layers[0].tree = ky_scene_tree_create(layers[LAYER_BELOW].tree);
    workspace->layers[1].layer = LAYER_NORMAL;
    workspace->layers[1].tree = ky_scene_tree_create(layers[LAYER_NORMAL].tree);
    workspace->layers[2].layer = LAYER_ABOVE;
    workspace->layers[2].tree = ky_scene_tree_create(layers[LAYER_ABOVE].tree);
    workspace_set_enabled(workspace, false);

    /* insert to workspace manager workspaces */
    for (uint32_t i = workspace_manager->count; i > position; i--) {
        workspace_manager->workspaces[i] = workspace_manager->workspaces[i - 1];
        workspace_manager->workspaces[i]->position = i;
    }
    workspace->position = position;
    workspace_manager->workspaces[position] = workspace;
    workspace_manager_update_count(workspace_manager->count + 1);

    workspace->uuid = kywc_identifier_uuid_generate();
    workspace_update_name(workspace, name);
    wl_signal_emit_mutable(&workspace_manager->events.new_workspace, workspace);
    /* after new_workspace */
    workspace_auto_add_views(workspace);

    return workspace;
}

static void fix_workspace(struct workspace *workspace)
{
    /* fixup workspace position */
    for (uint32_t i = workspace->position; i < workspace_manager->count - 1; i++) {
        workspace_manager->workspaces[i] = workspace_manager->workspaces[i + 1];
        workspace_manager->workspaces[i]->position = i;
        wl_signal_emit_mutable(&workspace_manager->workspaces[i]->events.position, NULL);
        workspace_update_name(workspace_manager->workspaces[i], NULL);
    }
    workspace_manager_update_count(workspace_manager->count - 1);

    if (workspace_manager->count == 0) {
        return;
    }

    /* fixup activated workspace */
    if (workspace_manager->current == workspace) {
        workspace_activate(workspace_manager->workspaces[0]);
    }

    /* move all views to current activated workspace */
    struct workspace *current = workspace_manager_get_current();
    struct view_proxy *view_proxy, *tmp;
    wl_list_for_each_safe(view_proxy, tmp, &workspace->view_proxies, workspace_link) {
        struct view_proxy *new_proxy = view_add_workspace(view_proxy->view, current);
        if (new_proxy) {
            view_set_current_proxy(view_proxy->view, new_proxy);
        }
        view_proxy_destroy(view_proxy);
    }
}

void workspace_destroy(struct workspace *workspace)
{
    kywc_log(KYWC_INFO, "workspace %s destroy", workspace->name);

    fix_workspace(workspace);

    wl_signal_emit_mutable(&workspace->events.destroy, NULL);

    /* destroy trees, trees must be empty */
    for (int i = 0; i < 3; i++) {
        ky_scene_node_destroy(&workspace->layers[i].tree->node);
    }

    free((void *)workspace->uuid);
    free((void *)workspace->name);
    free(workspace);
}

static void workspace_set_activated(struct workspace *workspace, bool activated)
{
    if (workspace->activated == activated) {
        return;
    }

    workspace_set_enabled(workspace, activated);

    /* reparent view_tree which in multi workspace */
    struct view_proxy *view_proxy;
    wl_list_for_each(view_proxy, &workspace->view_proxies, workspace_link) {
        if (view_proxy->view->current_proxy != view_proxy) {
            view_set_current_proxy(view_proxy->view, view_proxy);
        }
    }

    workspace->activated = activated;
    wl_signal_emit_mutable(&workspace->events.activate, NULL);
}

void workspace_activate(struct workspace *workspace)
{
    struct workspace *old = workspace_manager->current;
    if (old == workspace) {
        return;
    }

    if (old) {
        workspace_set_activated(old, false);
    }

    /* disable show desktop when switching between workspaces */
    view_manager_show_desktop(false, true);

    workspace_manager->current = workspace;
    workspace_set_activated(workspace, true);

    /* auto activate topmost enabled view */
    view_topmost_activate(workspace);

    input_rebase_all_cursor();
    kywc_log(KYWC_INFO, "workspace %s(%d) is activated", workspace->name, workspace->position);
}

struct view_layer *workspace_layer(struct workspace *workspace, enum layer layer)
{
    switch (layer) {
    case LAYER_BELOW:
        return &workspace->layers[0];
    case LAYER_NORMAL:
        return &workspace->layers[1];
    case LAYER_ABOVE:
        return &workspace->layers[2];
    default:
        return NULL;
    }
}

struct workspace *workspace_by_position(uint32_t position)
{
    if (position >= workspace_manager->count) {
        return NULL;
    }

    return workspace_manager->workspaces[position];
}

struct workspace *workspace_by_uuid(const char *uuid)
{
    struct workspace *workspace;
    for (uint32_t i = 0; i < workspace_manager->count; i++) {
        workspace = workspace_manager->workspaces[i];
        if (strcmp(workspace->uuid, uuid) == 0) {
            return workspace;
        }
    }
    return NULL;
}

void workspace_set_position(struct workspace *workspace, uint32_t position)
{
    /* insert to last if position is too bigger */
    if (position >= workspace_manager->count) {
        position = workspace_manager->count - 1;
    }
    if (position == workspace->position) {
        return;
    }

    /* move workspaces */
    for (uint32_t i = workspace->position; i > position; i--) {
        workspace_manager->workspaces[i] = workspace_manager->workspaces[i - 1];
        workspace_manager->workspaces[i]->position = i;
        wl_signal_emit_mutable(&workspace_manager->workspaces[i]->events.position, NULL);
        workspace_update_name(workspace_manager->workspaces[i], NULL);
    }
    for (uint32_t i = workspace->position; i < position; i++) {
        workspace_manager->workspaces[i] = workspace_manager->workspaces[i + 1];
        workspace_manager->workspaces[i]->position = i;
        wl_signal_emit_mutable(&workspace_manager->workspaces[i]->events.position, NULL);
        workspace_update_name(workspace_manager->workspaces[i], NULL);
    }

    workspace->position = position;
    workspace_manager->workspaces[position] = workspace;
    wl_signal_emit_mutable(&workspace->events.position, NULL);
    workspace_update_name(workspace, NULL);
}
