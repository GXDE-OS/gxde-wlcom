// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>
#include <string.h>

#include <kywc/identifier.h>

#include "plasma-virtual-desktop-protocol.h"
#include "view/workspace.h"
#include "view_p.h"

#define VIRTUAL_DESKTOP_VERSION 1
#define VIRTUAL_DESKTOP_MANAGEMENT_VERSION 2

struct kde_virtual_desktop_management {
    struct wl_global *global;
    struct wl_list resources;
    struct wl_list virtual_desktops;

    struct wl_listener new_workspace;
    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct kde_virtual_desktop {
    struct wl_list link;
    /* request get_virtual_desktop */
    struct wl_list resources;

    struct workspace *workspace;
    struct kde_virtual_desktop_management *management;

    struct wl_listener activate;
    struct wl_listener destroy;
};

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct kde_virtual_desktop_management *management =
        wl_container_of(listener, management, server_destroy);

    wl_list_remove(&management->server_destroy.link);
    free(management);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct kde_virtual_desktop_management *management =
        wl_container_of(listener, management, display_destroy);

    wl_list_remove(&management->new_workspace.link);
    wl_list_remove(&management->display_destroy.link);
    wl_global_destroy(management->global);
}

static void handle_workspace_destroy(struct wl_listener *listener, void *data)
{
    struct kde_virtual_desktop *virtual_desktop =
        wl_container_of(listener, virtual_desktop, destroy);

    wl_list_remove(&virtual_desktop->destroy.link);
    wl_list_remove(&virtual_desktop->link);

    struct kde_virtual_desktop_management *management = virtual_desktop->management;
    struct wl_resource *resource;
    wl_resource_for_each(resource, &virtual_desktop->resources) {
        org_kde_plasma_virtual_desktop_send_removed(resource);
    }

    wl_resource_for_each(resource, &management->resources) {
        org_kde_plasma_virtual_desktop_management_send_desktop_removed(
            resource, virtual_desktop->workspace->uuid);
    }

    free(virtual_desktop);
}

static void handle_workspace_activate(struct wl_listener *listener, void *data)
{
    struct kde_virtual_desktop *virtual_desktop =
        wl_container_of(listener, virtual_desktop, activate);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &virtual_desktop->resources) {
        if (virtual_desktop->workspace->activated) {
            org_kde_plasma_virtual_desktop_send_activated(resource);
        } else {
            org_kde_plasma_virtual_desktop_send_deactivated(resource);
        }
        org_kde_plasma_virtual_desktop_send_done(resource);
    }
}

static void handle_new_workspace(struct wl_listener *listener, void *data)
{
    struct kde_virtual_desktop *virtual_desktop = calloc(1, sizeof(struct kde_virtual_desktop));
    if (!virtual_desktop) {
        return;
    }

    struct workspace *workspace = data;
    virtual_desktop->workspace = workspace;

    virtual_desktop->activate.notify = handle_workspace_activate;
    wl_signal_add(&workspace->events.activate, &virtual_desktop->activate);
    virtual_desktop->destroy.notify = handle_workspace_destroy;
    wl_signal_add(&workspace->events.destroy, &virtual_desktop->destroy);

    struct kde_virtual_desktop_management *management =
        wl_container_of(listener, management, new_workspace);
    virtual_desktop->management = management;
    wl_list_init(&virtual_desktop->resources);
    wl_list_insert(&management->virtual_desktops, &virtual_desktop->link);

    struct wl_resource *resource;
    wl_resource_for_each(resource, &management->resources) {
        org_kde_plasma_virtual_desktop_management_send_desktop_created(
            resource, workspace->uuid, virtual_desktop->workspace->position);
    }
}

static struct kde_virtual_desktop *
get_virtual_desktop(struct kde_virtual_desktop_management *management, const char *uuid)
{
    struct kde_virtual_desktop *virtual_desktop;
    wl_list_for_each(virtual_desktop, &management->virtual_desktops, link) {
        if (!strcmp(uuid, virtual_desktop->workspace->uuid)) {
            return virtual_desktop;
        }
    }

    return NULL;
}

static void handle_request_activate(struct wl_client *client, struct wl_resource *resource)
{
    struct kde_virtual_desktop *virtual_desktop = wl_resource_get_user_data(resource);

    workspace_activate(virtual_desktop->workspace);
}

static const struct org_kde_plasma_virtual_desktop_interface kde_virtual_desktop_impl = {
    .request_activate = handle_request_activate,
};

static void kde_virtual_desktop_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void handle_get_virtual_desktop(struct wl_client *client,
                                       struct wl_resource *management_resource, uint32_t id,
                                       const char *desktop_id)
{
    struct kde_virtual_desktop_management *management =
        wl_resource_get_user_data(management_resource);

    struct kde_virtual_desktop *virtual_desktop = get_virtual_desktop(management, desktop_id);
    if (!virtual_desktop) {
        /* there is no error code defined in protocol */
        return;
    }

    struct wl_resource *resource = wl_resource_create(
        client, &org_kde_plasma_virtual_desktop_interface, VIRTUAL_DESKTOP_VERSION, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &kde_virtual_desktop_impl, virtual_desktop,
                                   kde_virtual_desktop_handle_resource_destroy);
    wl_list_insert(&virtual_desktop->resources, wl_resource_get_link(resource));

    /* send all desktop info to client */
    org_kde_plasma_virtual_desktop_send_desktop_id(resource, virtual_desktop->workspace->uuid);
    org_kde_plasma_virtual_desktop_send_name(resource, virtual_desktop->workspace->name);
    if (virtual_desktop->workspace->activated) {
        org_kde_plasma_virtual_desktop_send_activated(resource);
    } else {
        org_kde_plasma_virtual_desktop_send_deactivated(resource);
    }
    org_kde_plasma_virtual_desktop_send_done(resource);
}

static void handle_request_create_virtual_desktop(struct wl_client *client,
                                                  struct wl_resource *resource, const char *name,
                                                  uint32_t position)
{
    workspace_create(name, position);
}

static void handle_request_remove_virtual_desktop(struct wl_client *client,
                                                  struct wl_resource *resource,
                                                  const char *desktop_id)
{
    struct kde_virtual_desktop_management *management = wl_resource_get_user_data(resource);

    struct kde_virtual_desktop *virtual_desktop = get_virtual_desktop(management, desktop_id);
    if (!virtual_desktop) {
        /* there is no error code defined in protocol */
        return;
    }

    if (wl_list_length(&management->virtual_desktops) == 1) {
        kywc_log(KYWC_WARN, "reject to destroy the last virtual desktop");
        return;
    }

    workspace_destroy(virtual_desktop->workspace);
}

static const struct org_kde_plasma_virtual_desktop_management_interface
    kde_virtual_desktop_management_impl = {
        .get_virtual_desktop = handle_get_virtual_desktop,
        .request_create_virtual_desktop = handle_request_create_virtual_desktop,
        .request_remove_virtual_desktop = handle_request_remove_virtual_desktop,
    };

static void kde_virtual_desktop_management_handle_resource_destroy(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static struct kde_virtual_desktop *
get_virtual_desktop_by_workspace(struct kde_virtual_desktop_management *management,
                                 struct workspace *workspace)
{
    struct kde_virtual_desktop *virtual_desktop;
    wl_list_for_each(virtual_desktop, &management->virtual_desktops, link) {
        if (virtual_desktop->workspace == workspace) {
            return virtual_desktop;
        }
    }

    return NULL;
}

static void kde_virtual_desktop_management_bind(struct wl_client *client, void *data,
                                                uint32_t version, uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client, &org_kde_plasma_virtual_desktop_management_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct kde_virtual_desktop_management *management = data;
    wl_resource_set_implementation(resource, &kde_virtual_desktop_management_impl, management,
                                   kde_virtual_desktop_management_handle_resource_destroy);
    wl_list_insert(&management->resources, wl_resource_get_link(resource));

    /* send all desktops to client when bind */
    struct kde_virtual_desktop *virtual_desktop;
    for (uint32_t i = 0; i < workspace_manager_get_count(); i++) {
        virtual_desktop = get_virtual_desktop_by_workspace(management, workspace_by_position(i));
        org_kde_plasma_virtual_desktop_management_send_desktop_created(
            resource, virtual_desktop->workspace->uuid, virtual_desktop->workspace->position);
    }
    if (version >= ORG_KDE_PLASMA_VIRTUAL_DESKTOP_MANAGEMENT_ROWS_SINCE_VERSION) {
        org_kde_plasma_virtual_desktop_management_send_rows(resource, workspace_manager_get_rows());
    }
    org_kde_plasma_virtual_desktop_management_send_done(resource);
}

bool kde_virtual_desktop_management_create(struct server *server)
{
    struct kde_virtual_desktop_management *management =
        calloc(1, sizeof(struct kde_virtual_desktop_management));
    if (!management) {
        return false;
    }

    wl_list_init(&management->resources);
    wl_list_init(&management->virtual_desktops);

    management->global = wl_global_create(
        server->display, &org_kde_plasma_virtual_desktop_management_interface,
        VIRTUAL_DESKTOP_MANAGEMENT_VERSION, management, kde_virtual_desktop_management_bind);
    if (!management->global) {
        kywc_log(KYWC_WARN, "kde plasma virtual desktop create failed");
        free(management);
        return false;
    }

    management->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &management->server_destroy);
    management->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &management->display_destroy);

    management->new_workspace.notify = handle_new_workspace;
    workspace_manager_add_new_listener(&management->new_workspace);

    return true;
}
