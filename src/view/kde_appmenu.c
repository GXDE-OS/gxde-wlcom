/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_compositor.h>

#include "appmenu-protocol.h"
#include "view_p.h"

#define KDE_KWIN_APPMENU_MANAGER_VERSION 2

struct kde_appmenu_manager {
    struct wl_global* global;
    struct wl_list appmenus;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct kde_appmenu {
    struct wl_list link;
    struct wl_list resources;

    struct wlr_surface* surface;
    struct wl_listener surface_map;
    struct wl_listener surface_destroy;

    char* service_name;
    char* object_path;
};

static struct kde_appmenu_manager *manager;

static struct kde_appmenu* kde_appmenu_from_surface(struct wlr_surface* surface) {
    struct kde_appmenu* appmenu;
    wl_list_for_each(appmenu, &manager->appmenus, link) {
        if (appmenu->surface == surface) {
            return appmenu;
        }
    }

    return NULL;
}

static void kde_appmenu_apply(struct kde_appmenu* appmenu) {
    struct view *view = view_try_from_wlr_surface(appmenu->surface);
    if (!view) {
        return;
    }

    view_set_application_menu(view, appmenu->service_name, appmenu->object_path);
}

static void kde_appmenu_destroy(struct kde_appmenu* appmenu) {
    struct view *view = view_try_from_wlr_surface(appmenu->surface);
    if (view) {
        view_set_application_menu(view, NULL, NULL);
    }

    struct wl_resource* resource, *tmp;
    wl_resource_for_each_safe(resource, tmp, &appmenu->resources) {
        wl_resource_set_user_data(resource, NULL);
        wl_resource_set_destructor(resource, NULL);
        wl_list_remove(wl_resource_get_link(resource));
        wl_list_init(wl_resource_get_link(resource));
    }

    wl_list_remove(&appmenu->link);
    wl_list_remove(&appmenu->surface_map.link);
    wl_list_remove(&appmenu->surface_destroy.link);
    free(appmenu->service_name);
    free(appmenu->object_path);
    free(appmenu);
}

static void appmenu_handle_resource_destroy(struct wl_resource* resource) {
    struct kde_appmenu* appmenu = wl_resource_get_user_data(resource);
    wl_list_remove(wl_resource_get_link(resource));
    if (appmenu && wl_list_empty(&appmenu->resources)) {
        kde_appmenu_destroy(appmenu);
    }
}

static void appmenu_handle_surface_map(struct wl_listener* listener, void* data) {
    struct kde_appmenu* appmenu = wl_container_of(listener, appmenu, surface_map);
    wl_list_remove(&appmenu->surface_map.link);
    wl_list_init(&appmenu->surface_map.link);
    kde_appmenu_apply(appmenu);
}

static void appmenu_handle_surface_destroy(struct wl_listener* listener, void* data) {
    struct kde_appmenu* appmenu = wl_container_of(listener, appmenu, surface_destroy);
    kde_appmenu_destroy(appmenu);
}

static void appmenu_handle_set_address(struct wl_client* client, struct wl_resource* resource,
        const char* service_name, const char* object_path) {
    struct kde_appmenu* appmenu = wl_resource_get_user_data(resource);
    if (!appmenu) {
        return;
    }

    char* new_service_name = strdup(service_name);
    char* new_object_path = strdup(object_path);
    if (!new_service_name || !new_object_path) {
        free(new_service_name);
        free(new_object_path);
        wl_client_post_no_memory(client);
        return;
    }

    free(appmenu->service_name);
    free(appmenu->object_path);
    appmenu->service_name = new_service_name;
    appmenu->object_path = new_object_path;
    kde_appmenu_apply(appmenu);
}

static void appmenu_handle_release(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static const struct org_kde_kwin_appmenu_interface kde_appmenu_impl = {
    .set_address = appmenu_handle_set_address,
    .release = appmenu_handle_release,
};

static void manager_handle_create(struct wl_client* client, struct wl_resource* manager_resource,
        uint32_t id, struct wl_resource* surface_resource) {
    struct wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    if (!surface) {
        return;
    }

    struct kde_appmenu* appmenu = kde_appmenu_from_surface(surface);
    bool created = false;
    if (!appmenu) {
        appmenu = calloc(1, sizeof(*appmenu));
        if (!appmenu) {
            wl_client_post_no_memory(client);
            return;
        }
        created = true;

        wl_list_init(&appmenu->resources);
        wl_list_insert(&manager->appmenus, &appmenu->link);

        appmenu->surface = surface;
        appmenu->surface_map.notify = appmenu_handle_surface_map;
        appmenu->surface_destroy.notify = appmenu_handle_surface_destroy;
        wl_signal_add(&surface->events.destroy, &appmenu->surface_destroy);

        if (surface->mapped) {
            wl_list_init(&appmenu->surface_map.link);
        } else {
            wl_signal_add(&surface->events.map, &appmenu->surface_map);
        }
    }

    uint32_t version = wl_resource_get_version(manager_resource);
    struct wl_resource *resource =
        wl_resource_create(client, &org_kde_kwin_appmenu_interface, version, id);

    if (!resource) {
        wl_client_post_no_memory(client);
        if (created) {
            kde_appmenu_destroy(appmenu);
        }
        return;
    }

    wl_list_insert(&appmenu->resources, wl_resource_get_link(resource));
    wl_resource_set_implementation(resource, &kde_appmenu_impl, appmenu,
        appmenu_handle_resource_destroy);
}

static void manager_handle_release(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static const struct org_kde_kwin_appmenu_manager_interface kde_appmenu_manager_impl = {
    .create = manager_handle_create,
    .release = manager_handle_release,
};

static void kde_appmenu_manager_bind(struct wl_client* client, void* data, uint32_t version,
        uint32_t id) {
    struct wl_resource* resource =
        wl_resource_create(client, &org_kde_kwin_appmenu_manager_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_appmenu_manager_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener* listener, void* data) {
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

static void handle_server_destroy(struct wl_listener* listener, void* data) {
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
    manager = NULL;
}

bool kde_appmenu_manager_create(struct server* server) {
    manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->global =
        wl_global_create(server->display, &org_kde_kwin_appmenu_manager_interface,
            KDE_KWIN_APPMENU_MANAGER_VERSION, manager, kde_appmenu_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "(KDE Shim) Menu MGR: KDE appmenu manager creation failed!!");
        free(manager);
        manager = NULL;
        return false;
    }

    wl_list_init(&manager->appmenus);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
