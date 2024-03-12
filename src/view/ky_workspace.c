// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include "kywc-workspace-v1-protocol.h"

#include "view/workspace.h"
#include "view_p.h"

struct ky_workspace_manager {
    struct wl_event_loop *event_loop;
    struct wl_global *global;
    struct wl_list resources;
    struct wl_list workspaces;

    struct wl_event_source *idle_source;

    struct wl_listener new_workspace;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct ky_workspace {
    struct ky_workspace_manager *manager;
    struct wl_list link;

    struct wl_list resources;

    struct workspace *workspace;
    struct wl_listener name;
    struct wl_listener position;
    struct wl_listener activate;
    struct wl_listener destroy;
};

static void workspace_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void workspace_handle_set_position(struct wl_client *client, struct wl_resource *resource,
                                          uint32_t position)
{
    struct ky_workspace *ky_workspace = wl_resource_get_user_data(resource);
    if (!ky_workspace) {
        return;
    }

    workspace_set_position(ky_workspace->workspace, position);
}

static void workspace_handle_activate(struct wl_client *client, struct wl_resource *resource)
{
    struct ky_workspace *ky_workspace = wl_resource_get_user_data(resource);
    if (!ky_workspace) {
        return;
    }

    workspace_activate(ky_workspace->workspace);
}

static void workspace_handle_remove(struct wl_client *client, struct wl_resource *resource)
{
    struct ky_workspace *ky_workspace = wl_resource_get_user_data(resource);
    if (!ky_workspace) {
        return;
    }

    workspace_destroy(ky_workspace->workspace);
}

static const struct kywc_workspace_v1_interface ky_workspace_impl = {
    .destroy = workspace_handle_destroy,
    .set_position = workspace_handle_set_position,
    .activate = workspace_handle_activate,
    .remove = workspace_handle_remove,
};

static void ky_workspace_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static struct wl_resource *
create_workspace_resource_for_resource(struct ky_workspace *ky_workspace,
                                       struct wl_resource *manager_resource)
{
    struct wl_client *client = wl_resource_get_client(manager_resource);
    struct wl_resource *resource = wl_resource_create(client, &kywc_workspace_v1_interface,
                                                      wl_resource_get_version(manager_resource), 0);
    if (!resource) {
        wl_client_post_no_memory(client);
        return NULL;
    }

    wl_resource_set_implementation(resource, &ky_workspace_impl, ky_workspace,
                                   ky_workspace_resource_destroy);

    wl_list_insert(&ky_workspace->resources, wl_resource_get_link(resource));
    kywc_workspace_manager_v1_send_workspace(manager_resource, resource,
                                             ky_workspace->workspace->uuid);
    return resource;
}

static void workspace_send_details_to_workspace_resource(struct ky_workspace *ky_workspace,
                                                         struct wl_resource *resource)
{
    struct workspace *workspace = ky_workspace->workspace;

    kywc_workspace_v1_send_name(resource, workspace->name);
    kywc_workspace_v1_send_position(resource, workspace->position);
    if (workspace->activated) {
        kywc_workspace_v1_send_activated(resource);
    } else {
        kywc_workspace_v1_send_deactivated(resource);
    }
}

static void manager_handle_create_workspace(struct wl_client *client, struct wl_resource *resource,
                                            const char *name, uint32_t position)
{
    workspace_create(name, position);
}

static void manager_handle_stop(struct wl_client *client, struct wl_resource *resource)
{
    kywc_workspace_manager_v1_send_finished(resource);
    wl_resource_destroy(resource);
}

static const struct kywc_workspace_manager_v1_interface ky_workspace_manager_impl = {
    .create_workspace = manager_handle_create_workspace,
    .stop = manager_handle_stop,
};

static void ky_workspace_manager_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void ky_workspace_manager_bind(struct wl_client *client, void *data, uint32_t version,
                                      uint32_t id)
{
    struct ky_workspace_manager *manager = data;
    struct wl_resource *resource =
        wl_resource_create(client, &kywc_workspace_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &ky_workspace_manager_impl, manager,
                                   ky_workspace_manager_resource_destroy);
    wl_list_insert(&manager->resources, wl_resource_get_link(resource));

    /* send all workspaces and details */
    struct ky_workspace *ky_workspace, *tmp;
    wl_list_for_each_reverse_safe(ky_workspace, tmp, &manager->workspaces, link) {
        create_workspace_resource_for_resource(ky_workspace, resource);
    }
    wl_list_for_each_reverse_safe(ky_workspace, tmp, &manager->workspaces, link) {
        struct wl_resource *workspace_resource =
            wl_resource_find_for_client(&ky_workspace->resources, client);
        workspace_send_details_to_workspace_resource(ky_workspace, workspace_resource);
    }
    kywc_workspace_manager_v1_send_done(resource);
}

static void manager_idle_send_done(void *data)
{
    struct ky_workspace_manager *manager = data;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &manager->resources) {
        kywc_workspace_manager_v1_send_done(resource);
    }

    manager->idle_source = NULL;
}

static void manager_update_idle_source(struct ky_workspace_manager *manager)
{
    if (manager->idle_source) {
        return;
    }

    manager->idle_source =
        wl_event_loop_add_idle(manager->event_loop, manager_idle_send_done, manager);
}

static void handle_workspace_destroy(struct wl_listener *listener, void *data)
{
    struct ky_workspace *ky_workspace = wl_container_of(listener, ky_workspace, destroy);

    struct wl_resource *resource, *tmp;
    wl_resource_for_each_safe(resource, tmp, &ky_workspace->resources) {
        kywc_workspace_v1_send_removed(resource);
        // make the resource inert
        wl_list_remove(wl_resource_get_link(resource));
        wl_list_init(wl_resource_get_link(resource));
        wl_resource_set_user_data(resource, NULL);
    }

    wl_list_remove(&ky_workspace->name.link);
    wl_list_remove(&ky_workspace->position.link);
    wl_list_remove(&ky_workspace->activate.link);
    wl_list_remove(&ky_workspace->destroy.link);
    wl_list_remove(&ky_workspace->link);

    free(ky_workspace);
}

static void handle_workspace_activate(struct wl_listener *listener, void *data)
{
    struct ky_workspace *ky_workspace = wl_container_of(listener, ky_workspace, activate);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &ky_workspace->resources) {
        if (ky_workspace->workspace->activated) {
            kywc_workspace_v1_send_activated(resource);
        } else {
            kywc_workspace_v1_send_deactivated(resource);
        }
    }

    manager_update_idle_source(ky_workspace->manager);
}

static void handle_workspace_name(struct wl_listener *listener, void *data)
{
    struct ky_workspace *ky_workspace = wl_container_of(listener, ky_workspace, name);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &ky_workspace->resources) {
        kywc_workspace_v1_send_name(resource, ky_workspace->workspace->name);
    }

    manager_update_idle_source(ky_workspace->manager);
}

static void handle_workspace_position(struct wl_listener *listener, void *data)
{
    struct ky_workspace *ky_workspace = wl_container_of(listener, ky_workspace, position);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &ky_workspace->resources) {
        kywc_workspace_v1_send_position(resource, ky_workspace->workspace->position);
    }

    manager_update_idle_source(ky_workspace->manager);
}

static void handle_new_workspace(struct wl_listener *listener, void *data)
{
    struct ky_workspace *ky_workspace = calloc(1, sizeof(*ky_workspace));
    if (!ky_workspace) {
        return;
    }

    struct ky_workspace_manager *manager = wl_container_of(listener, manager, new_workspace);
    ky_workspace->manager = manager;
    wl_list_init(&ky_workspace->resources);
    wl_list_insert(&manager->workspaces, &ky_workspace->link);

    struct workspace *workspace = data;
    ky_workspace->workspace = workspace;

    ky_workspace->name.notify = handle_workspace_name;
    wl_signal_add(&workspace->events.name, &ky_workspace->name);
    ky_workspace->position.notify = handle_workspace_position;
    wl_signal_add(&workspace->events.position, &ky_workspace->position);
    ky_workspace->activate.notify = handle_workspace_activate;
    wl_signal_add(&workspace->events.activate, &ky_workspace->activate);
    ky_workspace->destroy.notify = handle_workspace_destroy;
    wl_signal_add(&workspace->events.destroy, &ky_workspace->destroy);

    /* send workspaces and detail */
    struct wl_resource *resource, *workspace_resource;
    wl_resource_for_each(resource, &manager->resources) {
        workspace_resource = create_workspace_resource_for_resource(ky_workspace, resource);
        workspace_send_details_to_workspace_resource(ky_workspace, workspace_resource);
        kywc_workspace_manager_v1_send_done(resource);
    }
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct ky_workspace_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct ky_workspace_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->new_workspace.link);
    wl_list_remove(&manager->display_destroy.link);
    if (manager->idle_source) {
        wl_event_source_remove(manager->idle_source);
    }
    wl_global_destroy(manager->global);
}

bool ky_workspace_manager_create(struct server *server)
{
    struct ky_workspace_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display, &kywc_workspace_manager_v1_interface, 1,
                                       manager, ky_workspace_manager_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "kywc workspace manager create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->resources);
    wl_list_init(&manager->workspaces);

    manager->event_loop = wl_display_get_event_loop(server->display);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    manager->new_workspace.notify = handle_new_workspace;
    workspace_manager_add_new_listener(&manager->new_workspace);

    return true;
}
