#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>

#include <kywc/binding.h>
#include <kywc/identifier.h>
#include <kywc/log.h>

#include "server.h"
#include "view/workspace.h"
#include "view_p.h"

#define MAX_WORKSPACES 16

struct workspace_manager {
    struct view_manager *view_manager;

    /* current activated workspace */
    struct workspace *current;
    struct workspace **workspaces;

    struct {
        struct wl_signal new_workspace;
    } events;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;

    uint32_t count, rows, columns;
};

static struct workspace_manager *workspace_manager = NULL;

enum direction { DIRECTION_LEFT, DIRECTION_RIGHT, DIRECTION_UP, DIRECTION_DOWN };

static struct shortcut {
    char *keybind;
    char *desc;
    enum direction direction;
} shortcuts[] = {
    { "Ctrl+Alt+Left", "switch to left workspace", DIRECTION_LEFT },
    { "Ctrl+Alt+Right", "switch to right workspace", DIRECTION_RIGHT },
    { "Ctrl+Alt+Up", "switch to up workspace", DIRECTION_UP },
    { "Ctrl+Alt+Down", "switch to down workspace", DIRECTION_DOWN },
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

static void workspace_switch_to(enum direction direction)
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

    if (direction == DIRECTION_LEFT) {
        pending = current - 1;
        pending = pending < 0 ? last : pending;
    } else if (direction == DIRECTION_RIGHT) {
        pending = current + 1;
        pending = pending > last ? 0 : pending;
    } else if (direction == DIRECTION_UP) {
        pending = current - column;
        pending = pending < 0 ? current + row * column : pending;
        pending = pending > last ? pending - column : pending;
    } else {
        pending = current + column;
        pending = pending > last ? current - row * column : pending;
        pending = pending < 0 ? pending + column : pending;
    }

    workspace_activate(workspace_manager->workspaces[pending]);
}

static void shortcut_action(struct key_binding *binding, void *data)
{
    struct shortcut *shortcut = data;
    workspace_switch_to(shortcut->direction);
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

    workspace_manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(view_manager->server, &workspace_manager->server_destroy);
    workspace_manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(view_manager->server->display,
                                    &workspace_manager->display_destroy);

    /* kde-plasma-virtual-desktop support */
    kde_virtual_desktop_management_create(view_manager->server);

    /* create the default workspace and activate it */
    workspace_activate(workspace_create(NULL, 0));
    workspace_create(NULL, 1);
    workspace_create(NULL, 2);
    workspace_create(NULL, 3);

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

static void workspace_set_enabled(struct workspace *workspace, bool enabled)
{
    for (int i = 0; i < 3; i++) {
        ky_scene_node_set_enabled(ky_scene_node_from_tree(workspace->layers[i].tree), enabled);
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

    workspace->position = position;
    workspace->name = name ? strdup(name) : kywc_identifier_generate("Desktop %d", position + 1);

    wl_list_init(&workspace->views);
    wl_signal_init(&workspace->events.activate);
    wl_signal_init(&workspace->events.destroy);

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
    workspace_manager->workspaces[position] = workspace;
    workspace_manager_update_count(workspace_manager->count + 1);

    wl_signal_emit_mutable(&workspace_manager->events.new_workspace, workspace);

    return workspace;
}

static void fix_workspace(struct workspace *workspace)
{
    /* fixup workspace position */
    for (uint32_t i = workspace->position; i < workspace_manager->count - 1; i++) {
        workspace_manager->workspaces[i] = workspace_manager->workspaces[i + 1];
        workspace_manager->workspaces[i]->position = i;
        // TODO: rename workspace ?
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

    struct view *view, *tmp;
    wl_list_for_each_safe(view, tmp, &workspace->views, link) {
        // TODO: fullscreen views
        view_set_workspace(view, current);
    }
}

void workspace_destroy(struct workspace *workspace)
{
    kywc_log(KYWC_INFO, "workspace %s destroy", workspace->name);

    fix_workspace(workspace);

    wl_signal_emit_mutable(&workspace->events.destroy, NULL);

    /* destroy trees, trees must be empty */
    for (int i = 0; i < 3; i++) {
        ky_scene_node_destroy(ky_scene_node_from_tree(workspace->layers[i].tree));
    }

    free((void *)workspace->name);
    free(workspace);
}

static void workspace_set_activated(struct workspace *workspace, bool activated)
{
    if (workspace->activated == activated) {
        return;
    }

    workspace_set_enabled(workspace, activated);

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

    workspace_manager->current = workspace;
    workspace_set_activated(workspace, true);

    /* auto activate topmost enabled view */
    view_topmost_activate(workspace);

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
