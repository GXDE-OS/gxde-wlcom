// SPDX-FileCopyrightText: 2026 CharOfString <root@charofstring.cc>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>
#include <string.h>

#include <kywc/log.h>

#include "util/dbus.h"
#include "view_p.h"

#define APP_BRIDGE_SERVICE "marcus.panel.util.AppBridge"
#define APP_BRIDGE_PATH "/AppBridge"
#define APP_BRIDGE_INTERFACE "marcus.panel.util.AppBridge"
#define APP_BRIDGE_METHOD "UpdateActiveApp"
#define HUSKY_PANEL_APP_ID "HuskyPanel"

struct dbus_toplevel_manager {
    struct dbus_object* service;
    struct wl_listener activate_view;
    struct wl_listener server_destroy;
};

static void handle_activate_view(struct wl_listener* listener,
        void* data) {
    (void)listener;

    struct view* view = data;
    if (!view || !view->base.mapped) {
        return;
    }

    const char* name = view->base.app_id ? view->base.app_id : "Unknown";
    const char* appid = view->base.app_id ? view->base.app_id : "Unknown";
    const char* title = view->base.title ? view->base.title : "Unknown";
    if (strcmp(name, HUSKY_PANEL_APP_ID) == 0 ||
            strcmp(appid, HUSKY_PANEL_APP_ID) == 0) {
        return;
    }
}

static void handle_server_destroy(struct wl_listener* listener, void* data) {
    (void)data;

    struct dbus_toplevel_manager* manager =
        wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->activate_view.link);
    wl_list_remove(&manager->server_destroy.link);
    dbus_unregister_object(manager->service);
    free(manager);
}

bool dbus_toplevel_manager_create(struct server* server) {
    struct dbus_toplevel_manager* manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->service = dbus_register_object("org.kde.KWin", NULL,
        NULL, NULL, NULL);

    manager->activate_view.notify = handle_activate_view;
    view_manager_add_activate_view_listener(&manager->activate_view);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);

    return true;
}
