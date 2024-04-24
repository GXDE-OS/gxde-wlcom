// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#include <stdlib.h>

#include <wlr/types/wlr_xdg_shell.h>

#include "xdg-dialog-v1-protocol.h"

#include "view_p.h"

#define XDG_DIALOG_VERSION 1

struct xdg_dialog_manager {
    struct wl_global *global;
    struct wl_list xdg_dialogs;

    struct wl_listener display_destroy;
    struct wl_listener server_destroy;
};

struct xdg_dialog {
    struct wl_list link;
    bool modal;

    struct view *view;
    struct wlr_xdg_toplevel *toplevel;
    struct wl_listener toplevel_destroy;
    struct wl_listener surface_map;
};

static struct xdg_dialog *xdg_dialog_from_toplevel(struct xdg_dialog_manager *manager,
                                                   struct wlr_xdg_toplevel *toplevel)
{
    struct xdg_dialog *xdg_dialog;
    wl_list_for_each(xdg_dialog, &manager->xdg_dialogs, link) {
        if (xdg_dialog->toplevel == toplevel) {
            return xdg_dialog;
        }
    }
    return NULL;
}

static void handle_dialog_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void xdg_dialog_set_modal(struct xdg_dialog *xdg_dialog, bool modal, bool force)
{
    if (!force && xdg_dialog->modal == modal) {
        return;
    }
    xdg_dialog->modal = modal;

    /* inert or surface is not mapped */
    if (!xdg_dialog->toplevel || !xdg_dialog->view) {
        return;
    }

    xdg_dialog->view->base.modal = modal;

    if (modal) {
        modal_create(xdg_dialog->view);
    } else {
        wl_signal_emit_mutable(&xdg_dialog->view->base.events.unset_modal, NULL);
    }
}

static void handle_set_modal(struct wl_client *client, struct wl_resource *resource)
{
    struct xdg_dialog *xdg_dialog = wl_resource_get_user_data(resource);
    xdg_dialog_set_modal(xdg_dialog, true, false);
}

static void handle_unset_modal(struct wl_client *client, struct wl_resource *resource)
{
    struct xdg_dialog *xdg_dialog = wl_resource_get_user_data(resource);
    xdg_dialog_set_modal(xdg_dialog, false, false);
}

static const struct xdg_dialog_v1_interface xdg_dialog_v1_interface_impl = {
    .destroy = handle_dialog_destroy,
    .set_modal = handle_set_modal,
    .unset_modal = handle_unset_modal,
};

static void xdg_dialog_handle_resource_destroy(struct wl_resource *resource)
{
    struct xdg_dialog *xdg_dialog = wl_resource_get_user_data(resource);
    wl_list_remove(&xdg_dialog->link);
    wl_list_remove(&xdg_dialog->surface_map.link);
    wl_list_remove(&xdg_dialog->toplevel_destroy.link);
    /* unapply its effects if xdg_toplevel is not destroyed */
    xdg_dialog_set_modal(xdg_dialog, false, false);
    free(xdg_dialog);
}

static void handle_surface_map(struct wl_listener *listener, void *data)
{
    struct xdg_dialog *xdg_dialog = wl_container_of(listener, xdg_dialog, surface_map);
    xdg_dialog->view = view_try_from_wlr_surface(xdg_dialog->toplevel->base->surface);
    /* view may not be mapped, modal will be applied in view_map */
    if (xdg_dialog->view->base.mapped) {
        xdg_dialog_set_modal(xdg_dialog, xdg_dialog->modal, true);
    } else {
        xdg_dialog->view->base.modal = xdg_dialog->modal;
    }
}

static void handle_toplevel_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_dialog *xdg_dialog = wl_container_of(listener, xdg_dialog, toplevel_destroy);
    wl_list_remove(&xdg_dialog->surface_map.link);
    wl_list_remove(&xdg_dialog->toplevel_destroy.link);
    wl_list_init(&xdg_dialog->surface_map.link);
    wl_list_init(&xdg_dialog->toplevel_destroy.link);
    /* If the xdg_toplevel is destroyed, the xdg_dialog_v1 becomes inert */
    xdg_dialog->toplevel = NULL;
}

static void handle_get_xdg_dialog(struct wl_client *client, struct wl_resource *resource,
                                  uint32_t id, struct wl_resource *toplevel)
{
    struct wlr_xdg_toplevel *xdg_toplevel = wlr_xdg_toplevel_from_resource(toplevel);
    if (!xdg_toplevel) {
        return;
    }

    struct xdg_dialog_manager *manager = wl_resource_get_user_data(resource);
    /* the xdg_toplevel object has already been used to create a xdg_dialog_v1 */
    struct xdg_dialog *xdg_dialog = xdg_dialog_from_toplevel(manager, xdg_toplevel);
    if (xdg_dialog) {
        wl_resource_post_error(
            resource, XDG_WM_DIALOG_V1_ERROR_ALREADY_USED,
            "the xdg_toplevel object has already been used to create a xdg_dialog_v1");
        return;
    }

    xdg_dialog = calloc(1, sizeof(struct xdg_dialog));
    if (!xdg_dialog) {
        wl_client_post_no_memory(client);
        return;
    }

    struct wl_resource *xdg_dialog_resource =
        wl_resource_create(client, &xdg_dialog_v1_interface, wl_resource_get_version(resource), id);
    if (!xdg_dialog_resource) {
        free(xdg_dialog);
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(xdg_dialog_resource, &xdg_dialog_v1_interface_impl, xdg_dialog,
                                   xdg_dialog_handle_resource_destroy);

    xdg_dialog->toplevel = xdg_toplevel;
    wl_list_insert(&manager->xdg_dialogs, &xdg_dialog->link);

    xdg_dialog->surface_map.notify = handle_surface_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &xdg_dialog->surface_map);
    /* If this object is destroyed before the related xdg_toplevel */
    xdg_dialog->toplevel_destroy.notify = handle_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->base->events.destroy, &xdg_dialog->toplevel_destroy);

    if (xdg_toplevel->base->surface->mapped) {
        handle_surface_map(&xdg_dialog->surface_map, NULL);
    }
}

static void handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static const struct xdg_wm_dialog_v1_interface xdg_wm_dialog_v1_interface_impl = {
    .destroy = handle_destroy,
    .get_xdg_dialog = handle_get_xdg_dialog,
};

static void xdg_dialog_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource =
        wl_resource_create(client, &xdg_wm_dialog_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    struct xdg_dialog_manager *manager = data;
    wl_resource_set_implementation(resource, &xdg_wm_dialog_v1_interface_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_dialog_manager *manager = wl_container_of(listener, manager, display_destroy);
    wl_list_remove(&manager->display_destroy.link);
    wl_global_destroy(manager->global);
}

static void handle_server_destroy(struct wl_listener *listener, void *data)
{
    struct xdg_dialog_manager *manager = wl_container_of(listener, manager, server_destroy);
    wl_list_remove(&manager->server_destroy.link);
    free(manager);
}

bool xdg_dialog_create(struct server *server)
{
    struct xdg_dialog_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return false;
    }

    manager->global = wl_global_create(server->display, &xdg_wm_dialog_v1_interface,
                                       XDG_DIALOG_VERSION, manager, xdg_dialog_bind);
    if (!manager->global) {
        kywc_log(KYWC_WARN, "xdg dialog create failed");
        free(manager);
        return false;
    }

    wl_list_init(&manager->xdg_dialogs);

    manager->server_destroy.notify = handle_server_destroy;
    server_add_destroy_listener(server, &manager->server_destroy);
    manager->display_destroy.notify = handle_display_destroy;
    wl_display_add_destroy_listener(server->display, &manager->display_destroy);

    return true;
}
